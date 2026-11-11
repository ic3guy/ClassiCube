#include "Occlusion.h"
#ifdef CC_BUILD_LINUX
#include "MapRenderer.h"
#include "World.h"
#include "Block.h"
#include "Camera.h"
#include "ExtMath.h"
#include "Graphics.h"
#include "Platform.h"
#include "Options.h"
#include "Funcs.h"
#include "HiZ.h"
#include "Benchmark.h"
#include "Game.h"
#include <stdlib.h>

/* A row describes which chunk faces share a non-opaque connected component. */
/* No ray-visible chunk can be discarded unless every such route is blocked. */
struct SolidBox { cc_uint8 x,y,z,sx,sy,sz; };
struct MeshFace { float x,y,z,sx,sy,sz; cc_uint8 side,diagonal; };
struct Portals {
	cc_uint8 exits[6], boxes; cc_bool known, boundsKnown;
	float minX,minY,minZ,maxX,maxY,maxZ;
	struct SolidBox solid[64];
	struct MeshFace* surfaces;
	int surfaceCount;
};
static struct Portals* portals;
static cc_uint8* state;
static cc_uint8 *entered, *allowed, *visible, *outward;
static int* queue;
static int capacity, head, tail;
static cc_bool enabled, failed;
static cc_bool portalEnabled, hizEnabled;
static cc_bool meshOccluders;
static int boxBudget;
static int meshBands;
static cc_bool boundsActive;
static unsigned serial, cachedSerial;
static cc_bool cached;
static int cachedWidth,cachedHeight;
static struct Matrix cachedView, cachedProjection;
/* Shared scratch storage: meshing is single threaded. */
static cc_uint8 visited[4096];
static cc_uint16 flood[4096];

void Occlusion_Init(void) {
	portalEnabled = Options_GetBool("gfx-portalcull", false);
	hizEnabled = Options_GetBool("gfx-hizcull", false);
	boxBudget = Options_GetInt("gfx-hizboxes",16,16384,128);
	meshOccluders=Options_GetBool("gfx-hizmesh",false);
	meshBands = Options_GetInt("gfx-meshbands",0,16,0);
	enabled = portalEnabled || hizEnabled;
}
void Occlusion_Invalidate(void) { serial++; }

void Occlusion_Reset(void) {
	int i;
	boundsActive=false;
	if(portals) for(i=0;i<capacity;i++) Mem_Free(portals[i].surfaces);
	Mem_Free(portals); Mem_Free(state); Mem_Free(queue);
	portals = NULL; state = NULL; queue = NULL;
	capacity = 0; failed = false;
	cached = false;
}

static cc_bool EnsureStorage(void) {
	if (!enabled || failed || !World.ChunksCount) return false;
	/* Partial boundary chunks need separate topology; conservatively bypass them. */
	if ((World.Width | World.Height | World.Length) & 15) return false;
	if (capacity == World.ChunksCount) return true;
	Occlusion_Reset();
	capacity = World.ChunksCount;
	portals = Mem_TryAllocCleared(capacity, sizeof(struct Portals));
	state   = Mem_TryAllocCleared(capacity, 4);
	queue   = Mem_TryAlloc(capacity * 6, sizeof(int));
	if (!portals || !state || !queue) {
		Occlusion_Reset(); failed = true; return false;
	}
	entered = state; allowed = state + capacity; visible = state + capacity * 2; outward = state + capacity * 3;
	return true;
}

static cc_bool Passable(const BlockID* padded, int pos) {
	int x = pos & 15, z = (pos >> 4) & 15, y = pos >> 8;
	return !Blocks.FullOpaque[padded[(y + 1) * 324 + (z + 1) * 18 + x + 1]];
}

static void BuildBoxes(struct Portals* p,const BlockID* data,cc_bool allAir,cc_bool allSolid) {
	cc_uint8 cells[512];
	struct SolidBox* box;
	int x,y,z,xx,yy,zz,dx,dy,dz,sx,sy,sz;
	cc_bool solid;
	p->boxes=0;
	if (allAir || !hizEnabled || meshOccluders) return;
	for (z=0;z<8;z++) for (y=0;y<8;y++) for (x=0;x<8;x++) {
		solid=true;
		if (!allSolid) for (yy=y*2;yy<y*2+2 && solid;yy++) for (zz=z*2;zz<z*2+2 && solid;zz++) for (xx=x*2;xx<x*2+2;xx++) {
			if (!Blocks.FullOpaque[data[(yy+1)*324+(zz+1)*18+xx+1]]) { solid=false; break; }
		}
		cells[x+y*8+z*64]=solid;
	}
	/* Merge guaranteed-solid cells into cuboids, avoiding artificial seams */
	/* between separately projected cells on the same flat terrain surface. */
	for (y=7;y>=0;y--) for (z=0;z<8;z++) for (x=0;x<8;x++) {
		if (!cells[x+y*8+z*64]) continue;
		sx=1; while (x+sx<8 && cells[x+sx+y*8+z*64]) sx++;
		sz=1;
		while (z+sz<8) {
			solid=true;
			for (dx=0;dx<sx;dx++) if (!cells[x+dx+y*8+(z+sz)*64]) solid=false;
			if (!solid) break; sz++;
		}
		sy=1;
		while (y-sy>=0) {
			solid=true;
			for (dz=0;dz<sz;dz++) for (dx=0;dx<sx;dx++) if (!cells[x+dx+(y-sy)*8+(z+dz)*64]) solid=false;
			if (!solid) break; sy++;
		}
		for (dy=0;dy<sy;dy++) for (dz=0;dz<sz;dz++) for (dx=0;dx<sx;dx++) cells[x+dx+(y-dy)*8+(z+dz)*64]=0;
		if (p->boxes>=64) continue;
		box=&p->solid[p->boxes++];
		box->x=x*2; box->y=(y-sy+1)*2; box->z=z*2;
		box->sx=sx*2; box->sy=sy*2; box->sz=sz*2;
	}
}

void Occlusion_Build(int index, const BlockID* padded, cc_bool allAir, cc_bool allSolid) {
	struct Portals* p;
	int start, first, last, pos, x, y, z, faces, i;
	if (!EnsureStorage()) return;
	Occlusion_Invalidate();
	p = &portals[index];
	Mem_Free(p->surfaces); p->surfaces=NULL; p->surfaceCount=0;
	Mem_Set(p->exits, allAir ? 63 : 0, 6);
	p->known = true;
	p->boundsKnown=false;
	BuildBoxes(p,padded,allAir,allSolid);
	if (!portalEnabled) return;
	if (allAir || allSolid) return;
	Mem_Set(visited, 0, sizeof(visited));
	for (start = 0; start < 4096; start++) {
		if (visited[start]) continue;
		visited[start] = 1;
		if (!Passable(padded, start)) continue;
		first = 0; last = 1; flood[0] = start; faces = 0;
		while (first < last) {
			pos = flood[first++];
			x = pos & 15; z = (pos >> 4) & 15; y = pos >> 8;
			if (!x)      faces |= 1 << FACE_XMIN;
			if (x == 15) faces |= 1 << FACE_XMAX;
			if (!z)      faces |= 1 << FACE_ZMIN;
			if (z == 15) faces |= 1 << FACE_ZMAX;
			if (!y)      faces |= 1 << FACE_YMIN;
			if (y == 15) faces |= 1 << FACE_YMAX;
#define FLOOD_NEXT(test, delta) \
			if (test) { \
				i = pos + (delta); \
				if (!visited[i]) { \
					visited[i] = 1; \
					if (Passable(padded, i)) flood[last++] = i; \
				} \
			}
			FLOOD_NEXT(x > 0, -1); FLOOD_NEXT(x < 15, 1);
			FLOOD_NEXT(z > 0, -16); FLOOD_NEXT(z < 15, 16);
			FLOOD_NEXT(y > 0, -256); FLOOD_NEXT(y < 15, 256);
#undef FLOOD_NEXT
		}
		for (i = 0; i < 6; i++) if (faces & (1 << i)) p->exits[i] |= faces;
	}
}

static void Enter(int index, int face) {
	int bit = 1 << face;
	if (!allowed[index] || (entered[index] & bit)) return;
	entered[index] |= bit;
	visible[index]  = true;
	queue[tail++]   = index * 6 + face;
}

void Occlusion_MeshBounds(int index,const struct VertexTextured* v,int count) {
	struct Portals* p; int i;
	if(!enabled || !portals || index<0 || index>=capacity || !count) return;
	p=&portals[index];
	p->minX=p->maxX=v[0].x; p->minY=p->maxY=v[0].y; p->minZ=p->maxZ=v[0].z;
	for(i=1;i<count;i++) {
		p->minX=min(p->minX,v[i].x); p->maxX=max(p->maxX,v[i].x);
		p->minY=min(p->minY,v[i].y); p->maxY=max(p->maxY,v[i].y);
		p->minZ=min(p->minZ,v[i].z); p->maxZ=max(p->maxZ,v[i].z);
	}
	p->boundsKnown=true;
}

static void RangeBounds(struct OcclusionRange* r,const struct VertexTextured* v) {
	int i;
	r->minX=r->maxX=v[0].x; r->minY=r->maxY=v[0].y; r->minZ=r->maxZ=v[0].z;
	for(i=1;i<r->count;i++) {
		r->minX=min(r->minX,v[i].x); r->maxX=max(r->maxX,v[i].x);
		r->minY=min(r->minY,v[i].y); r->maxY=max(r->maxY,v[i].y);
		r->minZ=min(r->minZ,v[i].z); r->maxZ=max(r->maxZ,v[i].z);
	}
}

static int FaceCompare(const void* aa,const void* bb) {
	const struct MeshFace *a=aa,*b=bb;
	float ka=a->y+a->sy,kb=b->y+b->sy;
	return ka<kb?1:ka>kb?-1:0;
}
void Occlusion_BuildSurfaces(int index,const struct VertexTextured* vertices) {
	struct Portals* p;
	struct ChunkPartInfo* part;
	struct MeshFace* f;
	struct OcclusionRange r;
	const struct VertexTextured* v;
	int batch,face,i,total=0,offset,used=0;
	if(!meshOccluders || !hizEnabled || !portals) return;
	p=&portals[index];
	for(batch=0;batch<MapRenderer_1DUsedCount;batch++) {
		part=&MapRenderer_PartsNormal[index+batch*World.ChunksCount];
		if(part->offset<0) continue;
		for(face=0;face<6;face++) total+=part->counts[face]/4;
	}
	if(!total) return;
	p->surfaces=Mem_TryAlloc(total,sizeof(struct MeshFace));
	if(!p->surfaces) return;
	for(batch=0;batch<MapRenderer_1DUsedCount;batch++) {
		part=&MapRenderer_PartsNormal[index+batch*World.ChunksCount];
		if(part->offset<0) continue;
		offset=part->offset+part->spriteCount;
		for(face=0;face<6;face++) {
			for(i=0;i<part->counts[face];i+=4) {
				r.count=4; RangeBounds(&r,vertices+offset+i); f=&p->surfaces[used++];
				f->x=r.minX; f->y=r.minY; f->z=r.minZ;
				f->sx=r.maxX-r.minX; f->sy=r.maxY-r.minY; f->sz=r.maxZ-r.minZ; f->side=face;
				v=&vertices[offset+i];
				if(f->sx==0) f->diagonal=(v->y==r.minY)!=(v->z==r.minZ);
				else if(f->sy==0) f->diagonal=(v->x==r.minX)!=(v->z==r.minZ);
				else f->diagonal=(v->x==r.minX)!=(v->y==r.minY);
			}
			offset+=part->counts[face];
		}
	}
	p->surfaceCount=used;
	qsort(p->surfaces,used,sizeof(struct MeshFace),FaceCompare);
}

static cc_bool Facing(const struct MeshFace* f) {
	switch(f->side) {
	case FACE_XMIN: return Camera.CurrentPos.x<f->x-0.001f;
	case FACE_XMAX: return Camera.CurrentPos.x>f->x+0.001f;
	case FACE_YMIN: return Camera.CurrentPos.y<f->y-0.001f;
	case FACE_YMAX: return Camera.CurrentPos.y>f->y+0.001f;
	case FACE_ZMIN: return Camera.CurrentPos.z<f->z-0.001f;
	case FACE_ZMAX: return Camera.CurrentPos.z>f->z+0.001f;
	}
	return false;
}
void Occlusion_BuildRanges(struct ChunkPartInfo* part,struct VertexTextured* vertices,int chunkY) {
	struct OcclusionRange* ranges;
	struct VertexTextured* scratch;
	int face,i,j,b,band,count,largest=0,offset,used=0;
	int counts[16],starts[16],next[16];
	float y;
	part->rangeCount=0; part->ranges=NULL;
	if(!meshBands || part->offset<0 || part->spriteCount) return;
	for(face=0;face<6;face++) largest=max(largest,part->counts[face]);
	if(!largest) return;
	ranges=Mem_TryAlloc(meshBands*6,sizeof(struct OcclusionRange));
	scratch=Mem_TryAlloc(largest,sizeof(struct VertexTextured));
	if(!ranges || !scratch) { Mem_Free(ranges); Mem_Free(scratch); return; }
	offset=part->offset;
	for(face=0;face<6;face++) {
		count=part->counts[face]; Mem_Set(counts,0,sizeof(counts));
		for(i=0;i<count;i+=4) {
			y=0; for(j=0;j<4;j++) y+=vertices[offset+i+j].y;
			band=Math_Floor((y*0.25f-chunkY)*(meshBands/16.0f));
			band=max(0,min(meshBands-1,band)); counts[band]+=4;
		}
		for(b=0,j=0;b<meshBands;b++) { starts[b]=next[b]=j; j+=counts[b]; }
		for(i=0;i<count;i+=4) {
			y=0; for(j=0;j<4;j++) y+=vertices[offset+i+j].y;
			band=Math_Floor((y*0.25f-chunkY)*(meshBands/16.0f)); band=max(0,min(meshBands-1,band));
			for(j=0;j<4;j++) scratch[next[band]++]=vertices[offset+i+j];
		}
		if(count) Mem_Copy(vertices+offset,scratch,count*sizeof(struct VertexTextured));
		for(b=0;b<meshBands;b++) if(counts[b]) {
			ranges[used].offset=offset+starts[b]; ranges[used].count=counts[b]; ranges[used].section=2<<face;
			RangeBounds(&ranges[used],vertices+ranges[used].offset); used++;
		}
		offset+=count;
	}
	Mem_Free(scratch); part->ranges=ranges; part->rangeCount=used;
}
cc_bool Occlusion_HiddenRange(const struct OcclusionRange* r) {
	return boundsActive && HiZ_HiddenBounds(r->minX-0.001f,r->minY-0.001f,r->minZ-0.001f,
		r->maxX-r->minX+0.002f,r->maxY-r->minY+0.002f,r->maxZ-r->minZ+0.002f);
}

int Occlusion_Cull(struct ChunkInfo* chunks, struct ChunkInfo** sorted, struct ChunkInfo** rendered, int count) {
	struct ChunkInfo* c;
	struct Portals* p;
	struct SolidBox* box;
	struct MeshFace* surface;
	int i, j, x, y, z, index, face, exits, entry, bit,boxes,scanned;
	int cx = Math_Floor(Camera.CurrentPos.x) >> 4;
	int cy = Math_Floor(Camera.CurrentPos.y) >> 4;
	int cz = Math_Floor(Camera.CurrentPos.z) >> 4;
	boundsActive=false;
	if(Benchmark_Mode && (Benchmark_DisableOcclusion || Benchmark_Preloading)) return count;
	if (!EnsureStorage()) return count;
	/* A noclipping camera inside an opaque block has no reliable air component. */
	x = Math_Floor(Camera.CurrentPos.x); y = Math_Floor(Camera.CurrentPos.y); z = Math_Floor(Camera.CurrentPos.z);
	if (World_Contains(x,y,z) && Blocks.FullOpaque[World_SafeGetBlock(x,y,z)]) return count;
	if (cached && cachedSerial == serial && cachedWidth==Game.Width && cachedHeight==Game.Height && Mem_Equal(&cachedView,&Gfx.View,sizeof(struct Matrix)) && Mem_Equal(&cachedProjection,&Gfx.Projection,sizeof(struct Matrix))) goto filter;
	Mem_Set(state, 0, capacity * 4);
	head = tail = 0;
	for (i = 0; i < capacity; i++) {
		c = &chunks[i];
		/* Padded sphere includes corners and floating-point boundary tolerance. */
		allowed[i] = Frustum_TestSphere(c->centreX, c->centreY, c->centreZ, 15) != FRUSTUM_OUTSIDE;
		/* A straight sight ray is monotone on each axis. Reject paths that */
		/* would have to turn back towards the camera to reach a hidden cave. */
		if (Camera.CurrentPos.x >= c->centreX-8) outward[i] |= 1 << FACE_XMIN;
		if (Camera.CurrentPos.x <= c->centreX+8) outward[i] |= 1 << FACE_XMAX;
		if (Camera.CurrentPos.z >= c->centreZ-8) outward[i] |= 1 << FACE_ZMIN;
		if (Camera.CurrentPos.z <= c->centreZ+8) outward[i] |= 1 << FACE_ZMAX;
		if (Camera.CurrentPos.y >= c->centreY-8) outward[i] |= 1 << FACE_YMIN;
		if (Camera.CurrentPos.y <= c->centreY+8) outward[i] |= 1 << FACE_YMAX;
	}
	if (!portalEnabled) {
		for (i=0;i<capacity;i++) visible[i]=allowed[i];
		goto spatial;
	}
	if (cx >= 0 && cy >= 0 && cz >= 0 && cx < World.ChunksX && cy < World.ChunksY && cz < World.ChunksZ) {
		index = World_ChunkPack(cx,cy,cz);
		/* Union all camera-chunk components: conservative if camera region is ambiguous. */
		for (face = 0; face < 6; face++) Enter(index, face);
	} else {
		/* An outside camera enters the convex world bounds through a facing plane. */
		for (z = 0; z < World.ChunksZ; z++) for (y = 0; y < World.ChunksY; y++) for (x = 0; x < World.ChunksX; x++) {
			index = World_ChunkPack(x,y,z);
			if (cx < 0 && !x) Enter(index, FACE_XMIN);
			if (cx >= World.ChunksX && x == World.ChunksX-1) Enter(index, FACE_XMAX);
			if (cy < 0 && !y) Enter(index, FACE_YMIN);
			if (cy >= World.ChunksY && y == World.ChunksY-1) Enter(index, FACE_YMAX);
			if (cz < 0 && !z) Enter(index, FACE_ZMIN);
			if (cz >= World.ChunksZ && z == World.ChunksZ-1) Enter(index, FACE_ZMAX);
		}
	}
	while (head < tail) {
		entry = queue[head++]; index = entry / 6; face = entry % 6;
		/* Stale or unbuilt topology is transparent until rebuilt, preventing holes. */
		exits = !portals[index].known || chunks[index].dirty ? 63 : portals[index].exits[face];
		exits &= outward[index];
		x = index % World.ChunksX;
		y = (index / World.ChunksX) % World.ChunksY;
		z = index / (World.ChunksX * World.ChunksY);
		if ((exits & (1 << FACE_XMIN)) && x > 0) Enter(index-1, FACE_XMAX);
		if ((exits & (1 << FACE_XMAX)) && x < World.ChunksX-1) Enter(index+1, FACE_XMIN);
		if ((exits & (1 << FACE_YMIN)) && y > 0) Enter(index-World.ChunksX, FACE_YMAX);
		if ((exits & (1 << FACE_YMAX)) && y < World.ChunksY-1) Enter(index+World.ChunksX, FACE_YMIN);
		if ((exits & (1 << FACE_ZMIN)) && z > 0) Enter(index-World.ChunksX*World.ChunksY, FACE_ZMAX);
		if ((exits & (1 << FACE_ZMAX)) && z < World.ChunksZ-1) Enter(index+World.ChunksX*World.ChunksY, FACE_ZMIN);
	}
spatial:
	if (hizEnabled) {
		HiZ_Begin(); boxes=scanned=0;
		for (i=0;i<capacity && boxes<boxBudget && scanned<boxBudget*4;i++) {
			c=sorted[i]; index=(int)(c-chunks);
			if (!allowed[index] || c->dirty || !portals[index].known) continue;
			if(meshOccluders) {
				p=&portals[index];
				for(bit=0;bit<p->surfaceCount && boxes<boxBudget && scanned<boxBudget*4;bit++) {
					surface=&p->surfaces[bit]; if(!Facing(surface)) continue;
					scanned++;
					if(HiZ_HiddenBounds(surface->x-0.001f,surface->y-0.001f,surface->z-0.001f,
						surface->sx+0.002f,surface->sy+0.002f,surface->sz+0.002f)) continue;
					if(HiZ_AddFace(surface->x,surface->y,surface->z,surface->sx,surface->sy,surface->sz,surface->diagonal)) boxes++;
				}
				continue;
			}
			for (bit=0;bit<portals[index].boxes && boxes<boxBudget && scanned<boxBudget*4;bit++) {
				box=&portals[index].solid[bit];
				x=c->centreX-8+box->x; y=c->centreY-8+box->y; z=c->centreZ-8+box->z;
				scanned++;
				if (HiZ_AddBox(x,y,z,box->sx,box->sy,box->sz)) boxes++;
			}
		}
		HiZ_End();
		for (i=0;i<count;i++) {
			c=rendered[i]; index=(int)(c-chunks);
			p=&portals[index];
			if(!visible[index]) continue;
			if(p->boundsKnown && !c->dirty) {
				if(HiZ_HiddenBounds(p->minX-0.001f,p->minY-0.001f,p->minZ-0.001f,
					p->maxX-p->minX+0.002f,p->maxY-p->minY+0.002f,p->maxZ-p->minZ+0.002f)) visible[index]=false;
			} else if(HiZ_HiddenBox(c->centreX-9,c->centreY-9,c->centreZ-9,18)) visible[index]=false;
		}
	}
	cachedView = Gfx.View; cachedProjection = Gfx.Projection;
	cachedWidth=Game.Width; cachedHeight=Game.Height;
	cachedSerial = serial; cached = true;
filter:
	boundsActive=hizEnabled;
	for (i = 0, j = 0; i < count; i++) {
		index = (int)(rendered[i] - chunks);
		if (visible[index]) rendered[j++] = rendered[i];
	}
	return j;
}
#endif
