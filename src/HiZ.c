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
#include <stdio.h>

/* Exact coverage of sixteen framebuffer pixel centres per tile. Partial
   occluders combine coverage with a conservative farthest-depth bound. */
struct Tile { float full, partial; cc_uint16 mask; float rays[6]; };
static struct Tile* tiles;
static int width,height,blocksX,blocksY,allocated;
static cc_bool active;
static cc_bool vc4Pixels;
static cc_bool flatDepth;
static cc_bool planeDepth;
static cc_bool rayBounds;
static cc_bool rayExit;
static cc_bool raysCached;
static int raysWidth,raysHeight;
static float cachedRight[3],cachedUp[3],cachedForward[3],cachedGuard;
static struct Matrix raysProjection;
static double rayPrepTotalUs;
static int rayPrepFrames;
static float originX,originY,originZ;
int HiZ_ProxyCount,HiZ_Scanned;
static struct Matrix mvp;
struct Projected { float x, y, z, ux, uy; };
struct ClipVertex { float x,y,z,w; };
struct Edge { float x, y, c, reciprocalX; };
static CC_INLINE int PixelFloor(float v) { int i=(int)v; return i>v?i-1:i; }
static CC_INLINE int PixelCeil(float v) { int i=(int)v; return i<v?i+1:i; }
static void PrepareRays(void) {
	static float columns[480],rows[270];
	const struct Matrix *v=&Gfx.View,*p=&Gfx.Projection;
	float right[3]={v->row1.x,v->row2.x,v->row3.x};
	float up[3]={v->row1.y,v->row2.y,v->row3.y};
	float forward[3]={-v->row1.z,-v->row2.z,-v->row3.z};
	float eye[3],radius[3],nx,ny,d,lo,hi,len,guard,error;
	cc_bool reuse;
	int axis,bx,by;
	rayBounds=false;
	if(!Options_GetBool("gfx-hizraybounds",true)) return;
	if(Math_AbsF(p->row3.w+1)>0.00001f || Math_AbsF(p->row4.w)>0.00001f ||
		Math_AbsF(p->row3.x)+Math_AbsF(p->row3.y)+Math_AbsF(p->row4.x)+Math_AbsF(p->row4.y)>0.00001f ||
		Math_AbsF(p->row1.x)<0.00001f || Math_AbsF(p->row2.y)<0.00001f) return;
	len=right[0]*right[0]+right[1]*right[1]+right[2]*right[2]; if(Math_AbsF(len-1)>0.0001f) return;
	len=up[0]*up[0]+up[1]*up[1]+up[2]*up[2]; if(Math_AbsF(len-1)>0.0001f) return;
	len=forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2]; if(Math_AbsF(len-1)>0.0001f) return;
	len=right[0]*up[0]+right[1]*up[1]+right[2]*up[2]; if(Math_AbsF(len)>0.0001f) return;
	len=right[0]*forward[0]+right[1]*forward[1]+right[2]*forward[2]; if(Math_AbsF(len)>0.0001f) return;
	len=up[0]*forward[0]+up[1]*forward[1]+up[2]*forward[2]; if(Math_AbsF(len)>0.0001f) return;
	guard=Options_GetFloat("gfx-hizraycache",0,0.1f,0);
	reuse=raysCached && raysWidth==width && raysHeight==height && cachedGuard==guard &&
		Mem_Equal(&raysProjection,p,sizeof(struct Matrix));
	for(axis=0;axis<3;axis++) {
		eye[axis]=-v->row4.x*right[axis]-v->row4.y*up[axis]+v->row4.z*forward[axis];
		error=Math_AbsF(forward[axis]-cachedForward[axis])+
			Math_AbsF(right[axis]-cachedRight[axis])*(1.0f+1.0f/width)/Math_AbsF(p->row1.x)+
			Math_AbsF(up[axis]-cachedUp[axis])*(1.0f+1.0f/height)/Math_AbsF(p->row2.y);
		if(error>guard) reuse=false;
		/* Include a half-pixel raster guard around the sixteen sample centres. */
		radius[axis]=Math_AbsF(right[axis])*4.0f/(width*Math_AbsF(p->row1.x))+
			Math_AbsF(up[axis])*4.0f/(height*Math_AbsF(p->row2.y))+guard;
	}
	originX=eye[0]; originY=eye[1]; originZ=eye[2];
	/* Cache a cone that encloses every ray for a bounded range of camera
	   rotations. Translation only changes the origin, never the directions. */
	if(reuse) { rayBounds=true; return; }
	for(bx=0;bx<blocksX;bx++) columns[bx]=((bx*4+2)*2.0f/width-1.0f)/p->row1.x;
	for(by=0;by<blocksY;by++) rows[by]=((by*4+2)*2.0f/height-1.0f)/p->row2.y;
	for(by=0;by<blocksY;by++) for(bx=0;bx<blocksX;bx++) {
		nx=columns[bx]; ny=rows[by];
		for(axis=0;axis<3;axis++) {
			d=forward[axis]+right[axis]*nx+up[axis]*ny;
			lo=d-radius[axis]; hi=d+radius[axis];
			tiles[by*blocksX+bx].rays[axis*2]=Math_AbsF(hi)>0.000000001f?1.0f/hi:0;
			tiles[by*blocksX+bx].rays[axis*2+1]=Math_AbsF(lo)>0.000000001f?1.0f/lo:0;
		}
	}
	for(axis=0;axis<3;axis++) {
		cachedRight[axis]=right[axis]; cachedUp[axis]=up[axis]; cachedForward[axis]=forward[axis];
	}
	raysProjection=*p; raysWidth=width; raysHeight=height; cachedGuard=guard; raysCached=true;
	rayBounds=true;
}
void HiZ_Begin(void) {
	int i; void* replacement;
	cc_uint64 rayStart;
	active=false; width=Game.Width; height=Game.Height;
	vc4Pixels=Options_GetBool("gfx-hizvc4",false);
	flatDepth=Options_GetBool("gfx-hizflatdepth",false);
	planeDepth=Options_GetBool("gfx-hizplanedepth",false);
	rayExit=Options_GetBool("gfx-hizrayexit",false);
	if(width<=0 || height<=0 || width>1920 || height>1080) return;
	blocksX=(width+3)/4; blocksY=(height+3)/4;
	if(allocated<blocksX*blocksY) {
		replacement=Mem_TryRealloc(tiles,blocksX*blocksY,sizeof(struct Tile));
		if(!replacement) return;
		tiles=replacement; allocated=blocksX*blocksY;
		raysCached=false;
	}
	Matrix_Mul(&mvp,&Gfx.View,&Gfx.Projection);
	for(i=0;i<blocksX*blocksY;i++) { tiles[i].full=1; tiles[i].partial=-1; tiles[i].mask=0; }
	rayStart=Stopwatch_Measure(); PrepareRays();
	rayPrepTotalUs+=Stopwatch_ElapsedMicroseconds(rayStart,Stopwatch_Measure()); rayPrepFrames++;
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
static int RasterPolygon(const struct Projected* const* v,int count,float margin,cc_bool forceFlat) {
	struct Edge e[16]; struct Tile* tile;
	float area,sign,zx,zy,zc,dx1,dx2,dy1,dy2;
	float minY=v[0]->y,maxY=minY,lo,hi,py,edge,far,globalFar;
	int i,j,k,y,y0,y1,by,bx,left[4],right[4],first,last,l,r,mask,changed=0,fullLeft,fullRight;
	for(k=2;k<count;k++) {
		dx1=v[k-1]->x-v[0]->x; dy1=v[k-1]->y-v[0]->y;
		dx2=v[k]->x-v[0]->x; dy2=v[k]->y-v[0]->y;
		area=dx1*dy2-dy1*dx2;
		if(Math_AbsF(area)>=0.00001f) break;
	}
	if(k==count) return 0;
	if(Math_AbsF(area)<0.00001f) return 0;
	if(vc4Pixels && area*((v[k-1]->ux-v[0]->ux)*(v[k]->uy-v[0]->uy)-(v[k]->ux-v[0]->ux)*(v[k-1]->uy-v[0]->uy))<=0) return 0;
	sign=area<0?-1.0f:1.0f;
	globalFar=v[0]->z;
	for(i=1;i<count;i++) globalFar=max(globalFar,v[i]->z);
	globalFar+=0.0000001f;
	if(planeDepth) {
		/* Fit the original projected plane. Snapping each vertex can change
		   interpolated depth by at most the coordinate error times its slope.
		   Bounding that error avoids cracks between separately rasterised
		   triangles without using one loose depth for the entire face. */
		dx1=v[k-1]->ux-v[0]->ux; dy1=v[k-1]->uy-v[0]->uy;
		dx2=v[k]->ux-v[0]->ux; dy2=v[k]->uy-v[0]->uy;
		area=dx1*dy2-dy1*dx2;
		if(Math_AbsF(area)<0.00001f) return 0;
	}
	zx=((v[k-1]->z-v[0]->z)*dy2-(v[k]->z-v[0]->z)*dy1)/area;
	zy=(dx1*(v[k]->z-v[0]->z)-dx2*(v[k-1]->z-v[0]->z))/area;
	zc=v[0]->z-zx*(planeDepth?v[0]->ux:v[0]->x)-zy*(planeDepth?v[0]->uy:v[0]->y);
	if(planeDepth) zc+=0.125f*(Math_AbsF(zx)+Math_AbsF(zy));
	for(i=0;i<count;i++) {
		j=(i+1)%count;
		e[i].x=(v[i]->y-v[j]->y)*sign; e[i].y=(v[j]->x-v[i]->x)*sign;
		e[i].c=(v[i]->x*v[j]->y-v[j]->x*v[i]->y)*sign;
		e[i].reciprocalX=Math_AbsF(e[i].x)>0.000001f?1.0f/e[i].x:0;
		/* Reserve the GPU's subpixel edge quantisation margin. */
		e[i].c-=margin*(Math_AbsF(e[i].x)+Math_AbsF(e[i].y));
		minY=min(minY,v[i]->y); maxY=max(maxY,v[i]->y);
	}
	y0=max(0,PixelCeil(minY-0.5f)); y1=min(height-1,PixelFloor(maxY-0.5f));
	for(by=y0/4;by<=y1/4;by++) {
		first=width; last=-1;
		for(j=0;j<4;j++) {
			y=by*4+j; left[j]=width; right[j]=-1;
			if(y<y0 || y>y1) continue;
			py=y+0.5f; lo=0.5f; hi=width-0.5f;
			for(i=0;i<count;i++) {
				edge=e[i].y*py+e[i].c;
				if(e[i].x>0.000001f) lo=max(lo,-edge*e[i].reciprocalX);
				else if(e[i].x < -0.000001f) hi=min(hi,-edge*e[i].reciprocalX);
				else if(edge<0) break;
			}
			if(i<count) continue;
			left[j]=max(0,PixelCeil(lo-0.5f)); right[j]=min(width-1,PixelFloor(hi-0.5f));
			first=min(first,left[j]); last=max(last,right[j]);
		}
		if(first>last) continue;
		fullLeft=max(max(left[0],left[1]),max(left[2],left[3]));
		fullRight=min(min(right[0],right[1]),min(right[2],right[3]));
		for(bx=first/4;bx<=last/4;bx++) {
			mask=65535;
			if(bx*4<fullLeft || bx*4+3>fullRight) {
			mask=0;
			for(j=0;j<4;j++) {
				l=max(0,left[j]-bx*4); r=min(3,right[j]-bx*4);
				if(l<=r) mask|=(((1<<(r-l+1))-1)<<l)<<(j*4);
			}
			}
			if(!mask) continue;
			far=(!planeDepth && (flatDepth || forceFlat))?globalFar:zx*(bx*4+(zx>0?3.5f:0.5f))+zy*(by*4+(zy>0?3.5f:0.5f))+zc+0.0000001f;
			tile=&tiles[by*blocksX+bx];
			if(mask==65535) {
				if(far<tile->full) {
					tile->full=far; changed++;
					if(tile->partial>=far) { tile->mask=0; tile->partial=-1; }
				}
			} else if(far<tile->full) {
				if((mask&tile->mask)==tile->mask) {
					if(mask==tile->mask && far>=tile->partial) continue;
					tile->mask=mask; tile->partial=far;
				} else {
					if(!(mask&~tile->mask)) continue;
					tile->mask|=mask; tile->partial=max(tile->partial,far);
				}
				changed++;
				if(tile->mask==65535) {
					tile->full=min(tile->full,tile->partial); tile->mask=0; tile->partial=-1;
				}
			}
		}
	}
	return changed;
}
static int RasterQuad(const struct Projected* box,int a,int b,int c,int d) {
	const struct Projected *v[4]={&box[a],&box[b],&box[c],&box[d]};
	return RasterPolygon(v,4,vc4Pixels?0:0.125f,false);
}
static float ClipDistance(const struct ClipVertex* v,int plane) {
	switch(plane) {
	case 0: return v->w-0.2f;
	case 1: return v->z+v->w-0.002f;
	case 2: return v->x+(1.0f-0.5f/width)*v->w;
	case 3: return (1.0f-0.5f/width)*v->w-v->x;
	case 4: return v->y+(1.0f-0.5f/height)*v->w;
	default: return (1.0f-0.5f/height)*v->w-v->y;
	}
}
static int AddClippedFace(float x,float y,float z,float sx,float sy,float sz) {
	struct ClipVertex buffers[2][16],*in=buffers[0],*out=buffers[1],*swap,*a,*b,*q;
	struct Projected projected[16]; const struct Projected* vertices[16];
	float wx,wy,wz,da,db,t; int i,plane,count=4,next;
	for(i=0;i<4;i++) {
		wx=x; wy=y; wz=z;
		if(sx==0) { if(i==1 || i==2) wy+=sy; if(i>=2) wz+=sz; }
		else if(sy==0) { if(i==1 || i==2) wx+=sx; if(i>=2) wz+=sz; }
		else { if(i==1 || i==2) wx+=sx; if(i>=2) wy+=sy; }
		in[i].x=wx*mvp.row1.x+wy*mvp.row2.x+wz*mvp.row3.x+mvp.row4.x;
		in[i].y=wx*mvp.row1.y+wy*mvp.row2.y+wz*mvp.row3.y+mvp.row4.y;
		in[i].z=wx*mvp.row1.z+wy*mvp.row2.z+wz*mvp.row3.z+mvp.row4.z;
		in[i].w=wx*mvp.row1.w+wy*mvp.row2.w+wz*mvp.row3.w+mvp.row4.w;
	}
	/* Clip only blockers, leaving a strict subset of the real opaque face.
	   The inset guards CPU/GPU clipping and subpixel rounding differences. */
	for(plane=0;plane<6;plane++) {
		next=0; a=&in[count-1]; da=ClipDistance(a,plane);
		for(i=0;i<count;i++) {
			b=&in[i]; db=ClipDistance(b,plane);
			if((da<0)!=(db<0)) {
				if(next>=16) return 0;
				t=da/(da-db); q=&out[next++];
				q->x=a->x+t*(b->x-a->x); q->y=a->y+t*(b->y-a->y);
				q->z=a->z+t*(b->z-a->z); q->w=a->w+t*(b->w-a->w);
			}
			if(db>=0) { if(next>=16) return 0; out[next++]=*b; }
			a=b; da=db;
		}
		if(next<3) return 0;
		count=next; swap=in; in=out; out=swap;
	}
	for(i=0;i<count;i++) {
		projected[i].x=(in[i].x/in[i].w*0.5f+0.5f)*width;
		projected[i].y=(in[i].y/in[i].w*0.5f+0.5f)*height;
		projected[i].ux=projected[i].x; projected[i].uy=projected[i].y;
		projected[i].z=-1.0f/in[i].w; vertices[i]=&projected[i];
	}
	return RasterPolygon(vertices,count,0.125f,true);
}
int HiZ_AddFace(float x,float y,float z,float sx,float sy,float sz,int diagonal) {
	struct Projected p[4];
	if(!active) return 0;
	if(sx==0) {
		if(!Project(x,y,z,&p[0]) || !Project(x,y+sy,z,&p[1]) || !Project(x,y+sy,z+sz,&p[2]) || !Project(x,y,z+sz,&p[3])) return AddClippedFace(x,y,z,sx,sy,sz);
	} else if(sy==0) {
		if(!Project(x,y,z,&p[0]) || !Project(x+sx,y,z,&p[1]) || !Project(x+sx,y,z+sz,&p[2]) || !Project(x,y,z+sz,&p[3])) return AddClippedFace(x,y,z,sx,sy,sz);
	} else {
		if(!Project(x,y,z,&p[0]) || !Project(x+sx,y,z,&p[1]) || !Project(x+sx,y+sy,z,&p[2]) || !Project(x,y+sy,z,&p[3])) return AddClippedFace(x,y,z,sx,sy,sz);
	}
	if(!vc4Pixels || flatDepth || planeDepth) return RasterQuad(p,0,1,2,3);
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
	struct Tile* tile;
	float minX,maxX,minY,maxY,near,cx,cy,cz,cw,rx,ry,rz,rw,iw0,iw1;
	float offX,offY,offZ,invX,invY,invZ,entry,exit,limit;
	float lowX,highX,lowY,highY,lowZ,highZ;
	int i,x0,x1,y0,y1,bx,by,l,r,b,t,mask,row;
	int axisX,axisY,axisZ;
	if(!active) return false;
	lowX=x-originX; highX=x+sx-originX; offX=lowX>0?lowX:highX<0?highX:0;
	lowY=y-originY; highY=y+sy-originY; offY=lowY>0?lowY:highY<0?highY:0;
	lowZ=z-originZ; highZ=z+sz-originZ; offZ=lowZ>0?lowZ:highZ<0?highZ:0;
	axisX=offX<0?1:0; axisY=offY<0?3:2; axisZ=offZ<0?5:4;
	/* Interval projection encloses the eight corners with two reciprocal
	   operations. Correlation is deliberately discarded, enlarging bounds. */
	sx*=0.5f; sy*=0.5f; sz*=0.5f; x+=sx; y+=sy; z+=sz;
	cx=x*mvp.row1.x+y*mvp.row2.x+z*mvp.row3.x+mvp.row4.x;
	cy=x*mvp.row1.y+y*mvp.row2.y+z*mvp.row3.y+mvp.row4.y;
	cz=x*mvp.row1.z+y*mvp.row2.z+z*mvp.row3.z+mvp.row4.z;
	cw=x*mvp.row1.w+y*mvp.row2.w+z*mvp.row3.w+mvp.row4.w;
	rx=sx*Math_AbsF(mvp.row1.x)+sy*Math_AbsF(mvp.row2.x)+sz*Math_AbsF(mvp.row3.x);
	ry=sx*Math_AbsF(mvp.row1.y)+sy*Math_AbsF(mvp.row2.y)+sz*Math_AbsF(mvp.row3.y);
	rz=sx*Math_AbsF(mvp.row1.z)+sy*Math_AbsF(mvp.row2.z)+sz*Math_AbsF(mvp.row3.z);
	rw=sx*Math_AbsF(mvp.row1.w)+sy*Math_AbsF(mvp.row2.w)+sz*Math_AbsF(mvp.row3.w);
	if(cw-rw<=0.001f || cz-rz+cw-rw<=0) return false;
	iw0=1.0f/(cw-rw); iw1=1.0f/(cw+rw);
	minX=((cx-rx)*((cx-rx)<0?iw0:iw1)*0.5f+0.5f)*width;
	maxX=((cx+rx)*((cx+rx)<0?iw1:iw0)*0.5f+0.5f)*width;
	minY=((cy-ry)*((cy-ry)<0?iw0:iw1)*0.5f+0.5f)*height;
	maxY=((cy+ry)*((cy+ry)<0?iw1:iw0)*0.5f+0.5f)*height;
	near=-iw0;
	/* The GPU snaps edges to its subpixel raster grid. Include a half-pixel
	   guard so a snapped edge cannot extend outside the software query. */
	minX-=0.5f; maxX+=0.5f; minY-=0.5f; maxY+=0.5f;
	if(minX>width || maxX<0 || minY>height || maxY<0) return false;
	minX=max(0,minX); maxX=min(width,maxX); minY=max(0,minY); maxY=min(height,maxY);
	x0=max(0,PixelCeil(minX-0.5f)); x1=min(width-1,PixelFloor(maxX-0.5f));
	y0=max(0,PixelCeil(minY-0.5f)); y1=min(height-1,PixelFloor(maxY-0.5f));
	if(x0>x1 || y0>y1) return false;
	near-=0.0000001f;
	for(by=y0/4;by<=y1/4;by++) for(bx=x0/4;bx<=x1/4;bx++) {
		tile=&tiles[by*blocksX+bx];
		if(tile->full<near) continue;
		if(rayBounds) {
			/* Slab entry bounds are conditioned on this tile's ray cone. The
			   maximum of three independent lower bounds remains conservative. */
			invX=tile->rays[axisX]; invY=tile->rays[axisY]; invZ=tile->rays[axisZ];
			if((offX>0 && invX<=0) || (offX<0 && invX>=0) ||
				(offY>0 && invY<=0) || (offY<0 && invY>=0) ||
				(offZ>0 && invZ<=0) || (offZ<0 && invZ>=0)) continue;
			entry=max(cw-rw,max(offX*invX,max(offY*invY,offZ*invZ)));
			/* Upper bounds on slab exit also let us ignore rectangle tiles
			   that cannot intersect the box at all (notably sky corners). */
			if(rayExit) {
				exit=1.0e30f;
				if(tile->rays[1]>0) exit=min(exit,highX*tile->rays[1]); else if(tile->rays[0]<0) exit=min(exit,lowX*tile->rays[0]);
				if(tile->rays[3]>0) exit=min(exit,highY*tile->rays[3]); else if(tile->rays[2]<0) exit=min(exit,lowY*tile->rays[2]);
				if(tile->rays[5]>0) exit=min(exit,highZ*tile->rays[5]); else if(tile->rays[4]<0) exit=min(exit,lowZ*tile->rays[4]);
				if(entry>exit+0.01f) continue;
			}
			limit=-1.0f-0.0000001f*entry;
			if(tile->full*entry<limit) continue;
			if(tile->partial*entry>=limit) return false;
		} else if(tile->partial>=near) return false;
		l=max(0,x0-bx*4); r=min(3,x1-bx*4); b=max(0,y0-by*4); t=min(3,y1-by*4);
		row=((1<<(r-l+1))-1)<<l; mask=0;
		for(i=b;i<=t;i++) mask|=row<<(i*4);
		if((tile->mask&mask)!=mask) return false;
	}
	return true;
}
cc_bool HiZ_HiddenBox(float x,float y,float z,float size) { return HiZ_HiddenBounds(x,y,z,size,size,size); }
void HiZ_LogStats(void) {
	int bx,by,x,y,covered=0,full=0;
	struct Tile* tile;
	if(!active) { printf("BENCH_HIZ active=0\n"); return; }
	for(by=0;by<blocksY;by++) for(bx=0;bx<blocksX;bx++) {
		tile=&tiles[by*blocksX+bx];
		if(tile->full<1) full++;
		for(y=0;y<4 && by*4+y<height;y++) for(x=0;x<4 && bx*4+x<width;x++) {
			if(tile->full<1 || (tile->mask&(1<<(y*4+x)))) covered++;
		}
	}
	printf("BENCH_HIZ active=1 rays=%d ray_us=%.1f covered_pixels=%d total_pixels=%d full_tiles=%d proxies=%d scanned=%d eye=%.3f,%.3f,%.3f\n",rayBounds,rayPrepFrames?rayPrepTotalUs/rayPrepFrames:0,covered,width*height,full,HiZ_ProxyCount,HiZ_Scanned,originX,originY,originZ);
}
#endif
