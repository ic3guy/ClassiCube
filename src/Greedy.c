#include "Greedy.h"
#if defined CC_BUILD_LINUX && CC_GFX_BACKEND == CC_GFX_BACKEND_GL2
#include "MapRenderer.h"
#include "Graphics.h"
#include "TexturePack.h"
#include "Platform.h"
#include "Options.h"
#include "World.h"
#include "ExtMath.h"
#include "Funcs.h"
#include "PackedCol.h"
#include "Benchmark.h"

cc_bool Greedy_Supported;
cc_bool Greedy_ExplicitGrad;
static struct VertexTextured* scratch;
static int scratchCapacity;
/* One 16x16 face grid at each of the seventeen possible block planes. */
static PackedCol keys[17*256];
static cc_uint8 occupied[17*256];
static const cc_uint8 cornerA[6][4]={{1,0,0,1},{0,1,1,0},{1,0,0,1},{1,0,0,1},{1,0,0,1},{1,0,0,1}};
static const cc_uint8 cornerB[6][4]={{1,1,0,0},{1,1,0,0},{0,0,1,1},{1,1,0,0},{1,1,0,0},{0,0,1,1}};

cc_bool Greedy_Enabled(void) {
	return Greedy_Supported && (!Gfx.Mipmaps || Greedy_ExplicitGrad) && Atlas1D.TilesPerAtlas<=256 && Options_GetBool("gfx-greedy2d",false);
}
static int Row(const struct VertexTextured* v) {
	float low=v[0].V; int i;
	for(i=1;i<4;i++) low=min(low,v[i].V);
	return Math_Floor(low/Atlas1D.InvTileSize+0.00001f);
}
static void Convert(struct VertexTextured* dst,const struct VertexTextured* src) {
	int row=Row(src),i;
	float origin=row*Atlas1D.InvTileSize;
	for(i=0;i<4;i++) {
		dst[i]=src[i]; dst[i].V=(src[i].V-origin)/Atlas1D.InvTileSize;
		dst[i].Col=(src[i].Col&PACKEDCOL_RGB_MASK)|PackedCol_A_Bits(row);
	}
}
/* Only complete block faces with uniform light can be merged. Partial blocks
   and lighting gradients retain their original geometry and texture mapping. */
static cc_bool Insert(const struct VertexTextured* v,int face,int ox,int oy,int oz) {
	float xx[2],yy[2],zz[2],u0=v[0].U,u1=u0,v0=v[0].V,v1=v0;
	int i,p,a0,a1,b0,b1,a,b,row=Row(v);
	PackedCol key=(v[0].Col&PACKEDCOL_RGB_MASK)|PackedCol_A_Bits(row);
	xx[0]=xx[1]=v[0].x; yy[0]=yy[1]=v[0].y; zz[0]=zz[1]=v[0].z;
	for(i=0;i<4;i++) {
		if((v[i].Col&PACKEDCOL_RGB_MASK)!=(v[0].Col&PACKEDCOL_RGB_MASK)) return false;
		if(v[i].x!=(int)v[i].x || v[i].y!=(int)v[i].y || v[i].z!=(int)v[i].z) return false;
		xx[0]=min(xx[0],v[i].x); xx[1]=max(xx[1],v[i].x);
		yy[0]=min(yy[0],v[i].y); yy[1]=max(yy[1],v[i].y);
		zz[0]=min(zz[0],v[i].z); zz[1]=max(zz[1],v[i].z);
		u0=min(u0,v[i].U); u1=max(u1,v[i].U); v0=min(v0,v[i].V); v1=max(v1,v[i].V);
	}
	if(face<2) { p=(int)xx[0]-ox; a0=(int)zz[0]-oz; a1=(int)zz[1]-oz; b0=(int)yy[0]-oy; b1=(int)yy[1]-oy; }
	else if(face<4) { p=(int)zz[0]-oz; a0=(int)xx[0]-ox; a1=(int)xx[1]-ox; b0=(int)yy[0]-oy; b1=(int)yy[1]-oy; }
	else { p=(int)yy[0]-oy; a0=(int)xx[0]-ox; a1=(int)xx[1]-ox; b0=(int)zz[0]-oz; b1=(int)zz[1]-oz; }
	if(p<0 || p>16 || a0<0 || b0<0 || a1>16 || b1>16 || a0>=a1 || b0>=b1) return false;
	if(Math_AbsF(u0)>0.001f || Math_AbsF(u1-(a1-a0))>0.001f) return false;
	if(Math_AbsF(v0-row*Atlas1D.InvTileSize)>0.000001f || Math_AbsF((v1-v0)/Atlas1D.InvTileSize-1)>0.001f || b1-b0!=1) return false;
	for(b=b0;b<b1;b++) for(a=a0;a<a1;a++) {
		i=p*256+b*16+a; if(occupied[i]) return false;
	}
	for(b=b0;b<b1;b++) for(a=a0;a<a1;a++) { i=p*256+b*16+a; keys[i]=key; occupied[i]=1; }
	return true;
}
static void Emit(struct VertexTextured* dst,int face,int p,int a,int b,int w,int h,PackedCol key,int ox,int oy,int oz) {
	int i; float av,bv;
	for(i=0;i<4;i++) {
		av=a+(cornerA[face][i]?w:0); bv=b+(cornerB[face][i]?h:0);
		if(face<2) { dst[i].x=ox+p; dst[i].y=oy+bv; dst[i].z=oz+av; }
		else if(face<4) { dst[i].x=ox+av; dst[i].y=oy+bv; dst[i].z=oz+p; }
		else { dst[i].x=ox+av; dst[i].y=oy+p; dst[i].z=oz+bv; }
		dst[i].Col=key;
		if(face==FACE_XMAX || face==FACE_ZMIN) dst[i].U=cornerA[face][i]?0:w;
		else dst[i].U=cornerA[face][i]?w-(1.0f-UV2_Scale):0;
		if(face<4) dst[i].V=cornerB[face][i]?0:h-(1.0f-UV2_Scale);
		else dst[i].V=cornerB[face][i]?h-(1.0f-UV2_Scale):0;
	}
}
static int CompactFace(struct VertexTextured* vertices,int src,int dst,int count,int face,int x,int y,int z) {
	int i,p,a,b,aa,bb,w,h,used=0,cell;
	PackedCol key;
	cc_bool matches,overflow=false;
	if(!count) return 0;
	if(scratchCapacity<count) {
		void* replacement=Mem_TryRealloc(scratch,count,sizeof(struct VertexTextured));
		if(!replacement) { for(i=0;i<count;i+=4) Convert(vertices+dst+i,vertices+src+i); return count; }
		scratch=replacement; scratchCapacity=count;
	}
	Mem_Set(occupied,0,sizeof(occupied));
	for(i=0;i<count;i+=4) {
		if(!Insert(vertices+src+i,face,x,y,z)) { Convert(scratch+used,vertices+src+i); used+=4; }
	}
	for(p=0;p<=16 && !overflow;p++) for(b=0;b<16 && !overflow;b++) for(a=0;a<16;a++) {
		cell=p*256+b*16+a; if(!occupied[cell]) continue;
		key=keys[cell]; w=1;
		while(a+w<16 && occupied[cell+w] && keys[cell+w]==key) w++;
		h=1;
		while(b+h<16) {
			matches=true;
			for(aa=0;aa<w;aa++) if(!occupied[cell+h*16+aa] || keys[cell+h*16+aa]!=key) { matches=false; break; }
			if(!matches) break; h++;
		}
		if(used+4>count) { overflow=true; break; }
		Emit(scratch+used,face,p,a,b,w,h,key,x,y,z); used+=4;
		for(bb=0;bb<h;bb++) for(aa=0;aa<w;aa++) occupied[cell+bb*16+aa]=0;
	}
	if(overflow || used>=count) {
		for(i=0;i<count;i+=4) Convert(vertices+dst+i,vertices+src+i); return count;
	}
	for(i=0;i<used;i++) vertices[dst+i]=scratch[i];
	return used;
}
int Greedy_Compact(int index,struct VertexTextured* v,int count,int x,int y,int z) {
	struct ChunkPartInfo *part,*groups[3]={MapRenderer_PartsNormal,MapRenderer_PartsTranslucent,MapRenderer_PartsCutout};
	int batch,g,face,src,dst=0,i,n,old;
	if(Benchmark_Mode) Benchmark_MeshBefore+=count;
	if(!Greedy_Enabled()) { if(Benchmark_Mode) Benchmark_MeshAfter+=count; return count; }
	for(batch=0;batch<MapRenderer_1DUsedCount;batch++) for(g=0;g<3;g++) {
		part=&groups[g][index+batch*World.ChunksCount]; if(part->offset<0) continue;
		src=part->offset; part->offset=dst;
		for(i=0;i<part->spriteCount;i++) v[dst++]=v[src++];
		for(face=0;face<6;face++) {
			old=part->counts[face]; n=old;
			if(g==0) n=CompactFace(v,src,dst,old,face,x,y,z);
			else for(i=0;i<old;i++) v[dst+i]=v[src+i];
			part->counts[face]=n; src+=old; dst+=n;
		}
	}
	if(dst) v[dst]=v[dst-1];
	Gfx_TrimTerrainVb(dst+1);
	if(Benchmark_Mode) Benchmark_MeshAfter+=dst;
	return dst;
}
#endif
