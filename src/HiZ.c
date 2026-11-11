#include "HiZ.h"
#ifdef CC_BUILD_LINUX
#include "Vectors.h"
#include "Graphics.h"
#include "Camera.h"
#include "ExtMath.h"
#include "Funcs.h"
#include "Game.h"
#include "Platform.h"
#include "Options.h"

/* Exact coverage of sixteen framebuffer pixel centres per tile. Partial
   occluders combine coverage with a conservative farthest-depth bound. */
struct Tile { float full, partial; cc_uint16 mask; };
static struct Tile* tiles;
static int width,height,blocksX,blocksY,allocated;
static cc_bool active;
static cc_bool vc4Pixels;
static struct Matrix mvp;
struct Projected { float x, y, z, ux, uy; };
struct Edge { float x, y, c; };
void HiZ_Begin(void) {
	int i; void* replacement;
	active=false; width=Game.Width; height=Game.Height;
	vc4Pixels=Options_GetBool("gfx-hizvc4",false);
	if(width<=0 || height<=0 || width>1920 || height>1080) return;
	blocksX=(width+3)/4; blocksY=(height+3)/4;
	if(allocated<blocksX*blocksY) {
		replacement=Mem_TryRealloc(tiles,blocksX*blocksY,sizeof(struct Tile));
		if(!replacement) return;
		tiles=replacement; allocated=blocksX*blocksY;
	}
	Matrix_Mul(&mvp,&Gfx.View,&Gfx.Projection);
	for(i=0;i<blocksX*blocksY;i++) { tiles[i].full=1; tiles[i].partial=-1; tiles[i].mask=0; }
	active=true;
}
void HiZ_End(void) { }
static cc_bool Project(float x,float y,float z,struct Projected* p) {
	float w=x*mvp.row1.w+y*mvp.row2.w+z*mvp.row3.w+mvp.row4.w;
	float nz=x*mvp.row1.z+y*mvp.row2.z+z*mvp.row3.z+mvp.row4.z;
	if(w<=0.001f || nz<=-w) return false;
	p->x=((x*mvp.row1.x+y*mvp.row2.x+z*mvp.row3.x+mvp.row4.x)/w*0.5f+0.5f)*width;
	p->y=((x*mvp.row1.y+y*mvp.row2.y+z*mvp.row3.y+mvp.row4.y)/w*0.5f+0.5f)*height;
	p->ux=p->x; p->uy=p->y;
	if(vc4Pixels) {
		/* VC4 emits signed 12.4 offsets from the viewport centre, truncating
		   towards zero. Preserve the GPU's shared edges rather than eroding
		   every thin terrain strip into disconnected coverage. */
		p->x=(int)((p->x-width*0.5f)*16.0f)*(1.0f/16.0f)+width*0.5f;
		p->y=(int)((p->y-height*0.5f)*16.0f)*(1.0f/16.0f)+height*0.5f;
	}
	/* Reciprocal camera depth is linear across a projected plane. Unlike NDC
	   depth near 1, it retains useful precision for distant cave surfaces. */
	p->z=-1.0f/w; return true;
}
static cc_bool ProjectBox(float x,float y,float z,float sx,float sy,float sz,struct Projected* p) {
	int i;
	for(i=0;i<8;i++) if(!Project(x+((i&1)?sx:0),y+((i&2)?sy:0),z+((i&4)?sz:0),&p[i])) return false;
	return true;
}
static int RasterQuad(const struct Projected* box,int a,int b,int c,int d) {
	const struct Projected *v[4]={&box[a],&box[b],&box[c],&box[d]};
	struct Edge e[4]; struct Tile* tile;
	float area,sign,zx,zy,zc,dx1,dx2,dy1,dy2;
	float minY=v[0]->y,maxY=minY,lo,hi,py,edge,far;
	int i,j,y,y0,y1,by,bx,left[4],right[4],first,last,l,r,mask,changed=0;
	dx1=v[1]->x-v[0]->x; dy1=v[1]->y-v[0]->y;
	dx2=v[2]->x-v[0]->x; dy2=v[2]->y-v[0]->y;
	area=dx1*dy2-dy1*dx2;
	if(Math_AbsF(area)<0.00001f) return 0;
	if(vc4Pixels && area*((v[1]->ux-v[0]->ux)*(v[2]->uy-v[0]->uy)-(v[2]->ux-v[0]->ux)*(v[1]->uy-v[0]->uy))<=0) return 0;
	sign=area<0?-1.0f:1.0f;
	zx=((v[1]->z-v[0]->z)*dy2-(v[2]->z-v[0]->z)*dy1)/area;
	zy=(dx1*(v[2]->z-v[0]->z)-dx2*(v[1]->z-v[0]->z))/area;
	zc=v[0]->z-zx*v[0]->x-zy*v[0]->y;
	for(i=0;i<4;i++) {
		j=(i+1)&3;
		e[i].x=(v[i]->y-v[j]->y)*sign; e[i].y=(v[j]->x-v[i]->x)*sign;
		e[i].c=(v[i]->x*v[j]->y-v[j]->x*v[i]->y)*sign;
		/* Reserve the GPU's subpixel edge quantisation margin. */
		if(!vc4Pixels) e[i].c-=0.125f*(Math_AbsF(e[i].x)+Math_AbsF(e[i].y));
		minY=min(minY,v[i]->y); maxY=max(maxY,v[i]->y);
	}
	y0=max(0,Math_Ceil(minY-0.5f)); y1=min(height-1,Math_Floor(maxY-0.5f));
	for(by=y0/4;by<=y1/4;by++) {
		first=width; last=-1;
		for(j=0;j<4;j++) {
			y=by*4+j; left[j]=width; right[j]=-1;
			if(y<y0 || y>y1) continue;
			py=y+0.5f; lo=0.5f; hi=width-0.5f;
			for(i=0;i<4;i++) {
				edge=e[i].y*py+e[i].c;
				if(e[i].x>0.000001f) lo=max(lo,-edge/e[i].x);
				else if(e[i].x < -0.000001f) hi=min(hi,-edge/e[i].x);
				else if(edge<0) break;
			}
			if(i<4) continue;
			left[j]=max(0,Math_Ceil(lo-0.5f)); right[j]=min(width-1,Math_Floor(hi-0.5f));
			first=min(first,left[j]); last=max(last,right[j]);
		}
		if(first>last) continue;
		for(bx=first/4;bx<=last/4;bx++) {
			mask=0;
			for(j=0;j<4;j++) {
				l=max(0,left[j]-bx*4); r=min(3,right[j]-bx*4);
				if(l<=r) mask|=(((1<<(r-l+1))-1)<<l)<<(j*4);
			}
			if(!mask) continue;
			far=zx*(bx*4+(zx>0?3.5f:0.5f))+zy*(by*4+(zy>0?3.5f:0.5f))+zc+0.0000001f;
			tile=&tiles[by*blocksX+bx];
			if(mask==65535) {
				if(far<tile->full) { tile->full=far; changed++; }
			} else if(far<tile->full) {
				tile->mask|=mask; tile->partial=max(tile->partial,far); changed++;
				if(tile->mask==65535) {
					tile->full=min(tile->full,tile->partial); tile->mask=0; tile->partial=-1;
				}
			}
		}
	}
	return changed;
}
int HiZ_AddFace(float x,float y,float z,float sx,float sy,float sz,int diagonal) {
	struct Projected p[4];
	if(!active) return 0;
	if(sx==0) {
		if(!Project(x,y,z,&p[0]) || !Project(x,y+sy,z,&p[1]) || !Project(x,y+sy,z+sz,&p[2]) || !Project(x,y,z+sz,&p[3])) return 0;
	} else if(sy==0) {
		if(!Project(x,y,z,&p[0]) || !Project(x+sx,y,z,&p[1]) || !Project(x+sx,y,z+sz,&p[2]) || !Project(x,y,z+sz,&p[3])) return 0;
	} else {
		if(!Project(x,y,z,&p[0]) || !Project(x+sx,y,z,&p[1]) || !Project(x+sx,y+sy,z,&p[2]) || !Project(x,y+sy,z,&p[3])) return 0;
	}
	if(!vc4Pixels) return RasterQuad(p,0,1,2,3);
	if(diagonal) return RasterQuad(p,0,1,3,3)+RasterQuad(p,1,2,3,3);
	return RasterQuad(p,0,1,2,2)+RasterQuad(p,0,2,3,3);
}
int HiZ_AddBox(float x,float y,float z,float sx,float sy,float sz) {
	struct Projected p[8]; int marked=0;
	if(!active) return 0;
	if(HiZ_HiddenBounds(x,y,z,sx,sy,sz)) return 0;
	x+=0.001f; y+=0.001f; z+=0.001f; sx-=0.002f; sy-=0.002f; sz-=0.002f;
	if(!ProjectBox(x,y,z,sx,sy,sz,p)) return 0;
	if(Camera.CurrentPos.x<=x) marked+=RasterQuad(p,0,2,6,4);
	if(Camera.CurrentPos.x>=x+sx) marked+=RasterQuad(p,1,5,7,3);
	if(Camera.CurrentPos.y<=y) marked+=RasterQuad(p,0,4,5,1);
	if(Camera.CurrentPos.y>=y+sy) marked+=RasterQuad(p,2,3,7,6);
	if(Camera.CurrentPos.z<=z) marked+=RasterQuad(p,0,1,3,2);
	if(Camera.CurrentPos.z>=z+sz) marked+=RasterQuad(p,4,6,7,5);
	return marked;
}
cc_bool HiZ_HiddenBounds(float x,float y,float z,float sx,float sy,float sz) {
	struct Projected p[8]; struct Tile* tile;
	float minX,maxX,minY,maxY,near;
	int i,x0,x1,y0,y1,bx,by,l,r,b,t,mask,row;
	if(!active || !ProjectBox(x,y,z,sx,sy,sz,p)) return false;
	minX=maxX=p[0].x; minY=maxY=p[0].y; near=p[0].z;
	for(i=1;i<8;i++) {
		minX=min(minX,p[i].x); maxX=max(maxX,p[i].x); minY=min(minY,p[i].y); maxY=max(maxY,p[i].y); near=min(near,p[i].z);
	}
	/* The GPU snaps edges to its subpixel raster grid. Include a half-pixel
	   guard so a snapped edge cannot extend outside the software query. */
	minX-=0.5f; maxX+=0.5f; minY-=0.5f; maxY+=0.5f;
	x0=max(0,Math_Ceil(minX-0.5f)); x1=min(width-1,Math_Floor(maxX-0.5f));
	y0=max(0,Math_Ceil(minY-0.5f)); y1=min(height-1,Math_Floor(maxY-0.5f));
	if(x0>x1 || y0>y1) return false;
	near-=0.0000001f;
	for(by=y0/4;by<=y1/4;by++) for(bx=x0/4;bx<=x1/4;bx++) {
		tile=&tiles[by*blocksX+bx];
		if(tile->full<near) continue;
		if(tile->partial>=near) return false;
		l=max(0,x0-bx*4); r=min(3,x1-bx*4); b=max(0,y0-by*4); t=min(3,y1-by*4);
		row=((1<<(r-l+1))-1)<<l; mask=0;
		for(i=b;i<=t;i++) mask|=row<<(i*4);
		if((tile->mask&mask)!=mask) return false;
	}
	return true;
}
cc_bool HiZ_HiddenBox(float x,float y,float z,float size) { return HiZ_HiddenBounds(x,y,z,size,size,size); }
#endif
