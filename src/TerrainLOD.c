#include "TerrainLOD.h"
#if defined CC_BUILD_LINUX && CC_GFX_BACKEND == CC_GFX_BACKEND_GL2
#include "MapRenderer.h"
#include "Options.h"
#include "World.h"
#include "Block.h"
#include "Camera.h"
#include "Graphics.h"
#include "Greedy.h"
#include "TexturePack.h"
#include "PackedCol.h"

static cc_bool enabled;
static int nearDistance,farStep;
void TerrainLOD_Init(void) {
	enabled=Options_GetBool("gfx-farlod",false) && Options_GetBool("gfx-greedy2d",false);
	nearDistance=Options_GetInt("gfx-lodnear",16,256,48);
	farStep=Options_GetInt("gfx-lodstep",0,3,0);
}
static float DistanceSquared(int x,int y,int z) {
	float dx=x-Camera.CurrentPos.x,dy=y-Camera.CurrentPos.y,dz=z-Camera.CurrentPos.z;
	return dx*dx+dy*dy+dz*dz;
}
static int Level(float d) {
	int radius=nearDistance;
	if(d<=radius*radius) return 0;
	if(farStep) return farStep;
	radius*=2; if(d<=radius*radius) return 1;
	radius*=2; if(d<=radius*radius) return 2;
	return 3;
}
cc_uint16 TerrainLOD_Signature(const struct ChunkInfo* c) {
	float d; int level;
	if(!enabled || !Greedy_Supported || (Gfx.Mipmaps && !Greedy_ExplicitGrad) || Atlas1D.TilesPerAtlas>256) return 0;
	d=DistanceSquared(c->centreX,c->centreY,c->centreZ); level=Level(d);
	/* Keep every chunk volume closed while neighbouring detail levels
	   update independently. This also remains safe after a teleport. */
	return 0xC000|level;
}
static BlockID SampleCell(int x,int y,int z,int step) {
	int xx,yy,zz,solid=0,liquid=0,votes=0,volume=step*step*step;
	BlockID id,candidate=BLOCK_STONE,water=BLOCK_WATER,surface=BLOCK_AIR;
	for(yy=0;yy<step;yy++) for(zz=0;zz<step;zz++) for(xx=0;xx<step;xx++) {
		id=World_SafeGetBlock(x+xx,y+yy,z+zz);
		if(Blocks.FullOpaque[id]) {
			if(id==BLOCK_GRASS || id==BLOCK_SAND || id==BLOCK_GRAVEL || id==BLOCK_SNOW) surface=id;
			solid++; if(!votes) { candidate=id; votes=1; }
			else votes+=id==candidate?1:-1;
		} else if(Blocks.IsLiquid[id]) { liquid++; water=id; }
	}
	if(solid*2>=volume) return surface!=BLOCK_AIR?surface:candidate;
	if(liquid*2>=volume) return water;
	return BLOCK_AIR;
}
void TerrainLOD_Prepare(BlockID* padded,int x,int y,int z,cc_uint16 signature,cc_bool* allAir,cc_bool* allSolid) {
	BlockID samples[1000],id;
	int level=signature&3,step=1<<level,side=16/step+2;
	int xx,yy,zz,wx,wy,wz,index,sx,sy,sz;
	if(!signature) return;
	if(level) for(yy=0;yy<side;yy++) for(zz=0;zz<side;zz++) for(xx=0;xx<side;xx++) {
		wx=x+(xx-1)*step; wy=y+(yy-1)*step; wz=z+(zz-1)*step;
		samples[(yy*side+zz)*side+xx]=SampleCell(wx,wy,wz,step);
	}
	*allAir=true; *allSolid=true;
	for(yy=-1;yy<=16;yy++) for(zz=-1;zz<=16;zz++) for(xx=-1;xx<=16;xx++) {
		index=((yy+1)*18+zz+1)*18+xx+1; id=padded[index];
		wx=x+xx; wy=y+yy; wz=z+zz;
		if(level && World_Contains(wx,wy,wz)) {
			sx=(xx+step)>>level; sy=(yy+step)>>level; sz=(zz+step)>>level;
			id=samples[(sy*side+sz)*side+sx];
			/* Grass belongs on the exposed top layer, rather than every row
			   of an enlarged cell's vertical sides. Use the unclipped neighbour
			   sample so chunk-boundary closure does not add grass stripes. */
			if(id==BLOCK_GRASS && Blocks.FullOpaque[samples[((((yy+1+step)>>level)*side+sz)*side+sx)]]) id=BLOCK_DIRT;
		}
		/* Close chunk volumes independently, including during asynchronous
		   detail transitions. Neighbours can retain their previous meshes
		   until the frame budget allows rebuilding without opening cracks. */
		if((signature&0x4000) && (xx==-1 || xx==16 || zz==-1 || zz==16 || yy==-1 || yy==16)) id=BLOCK_AIR;
		padded[index]=id;
		*allAir=*allAir && Blocks.Draw[id]==DRAW_GAS;
		*allSolid=*allSolid && Blocks.FullOpaque[id];
	}
}
void TerrainLOD_ColorFaces(int index,struct VertexTextured* vertices) {
	struct ChunkPartInfo* part; int batch,face,i,offset;
	PackedCol colors[6]={Env.SunXSide,Env.SunXSide,Env.SunZSide,Env.SunZSide,Env.SunYMin,Env.SunCol};
	/* The coarse surface does not share the original fine heightmap. Use
	   directional environment lighting so a single enlarged face is not
	   fragmented by shadows belonging to the replaced fine geometry. */
	for(batch=0;batch<MapRenderer_1DUsedCount;batch++) {
		part=&MapRenderer_PartsNormal[index+batch*World.ChunksCount];
		if(part->offset<0) continue;
		offset=part->offset+part->spriteCount;
		for(face=0;face<6;face++) {
			for(i=0;i<part->counts[face];i++) vertices[offset+i].Col=colors[face];
			offset+=part->counts[face];
		}
	}
}
#endif
