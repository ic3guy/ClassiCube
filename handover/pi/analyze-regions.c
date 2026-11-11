#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
static uint8_t* data;
static uint32_t *queue,w,h,l,oneY,volume,shiftW,shiftL;
static uint64_t faces;
static uint32_t head,tail;
static void enter(uint32_t i,uint8_t mark) { if (!data[i]) { data[i]=mark; queue[tail++]=i; } }
static uint32_t flood(uint8_t mark) {
    uint32_t i,x,y,z;
    while (head<tail) {
        i=queue[head++]; x=i&(w-1); z=(i>>shiftW)&(l-1); y=i>>(shiftW+shiftL);
#define VISIT(condition,delta) if(condition) { uint32_t n=i+(delta); if(data[n]==1) faces++; else enter(n,mark); }
        VISIT(x>0,-1); VISIT(x+1<w,1);
        VISIT(z>0,-w); VISIT(z+1<l,w);
        VISIT(y>0,-oneY); VISIT(y+1<h,oneY);
#undef VISIT
    }
    return tail;
}
int main(int argc,char** argv) {
    FILE* f;
    uint32_t dims[3],i,x,y,z,outside,closed=0,components=0,largest=0,n;
    uint64_t outsideFaces,closedFaces=0;
    struct timespec a,b;
    if(argc!=2) return 2;
    f=fopen(argv[1],"rb"); if(!f) return 3;
    if(fread(dims,4,3,f)!=3) return 4;
    w=dims[0];h=dims[1];l=dims[2]; oneY=w*l; volume=oneY*h;
    if((w&(w-1)) || (l&(l-1))) return 5;
    while((1U<<shiftW)<w) shiftW++; while((1U<<shiftL)<l) shiftL++;
    data=malloc(volume); queue=malloc((size_t)volume*4);
    if(!data || !queue) return 6;
    if(fread(data,1,volume,f)!=volume) return 7; fclose(f);
    clock_gettime(CLOCK_MONOTONIC,&a);
    for(y=0;y<h;y++) for(z=0;z<l;z++) { enter((y*l+z)*w,2); enter((y*l+z)*w+w-1,2); }
    for(y=0;y<h;y++) for(x=0;x<w;x++) { enter(y*oneY+x,2); enter(y*oneY+(l-1)*w+x,2); }
    for(z=0;z<l;z++) for(x=0;x<w;x++) { enter(z*w+x,2); enter((h-1)*oneY+z*w+x,2); }
    outside=flood(2); outsideFaces=faces;
    for(i=0;i<volume;i++) if(!data[i]) {
        head=tail=0; faces=0; enter(i,3); n=flood(3); closed+=n; components++;
        closedFaces+=faces; if(n>largest) largest=n;
    }
    clock_gettime(CLOCK_MONOTONIC,&b);
    printf("{\"width\":%u,\"height\":%u,\"length\":%u,\"outside_cells\":%u,\"outside_faces\":%llu,\"closed_cells\":%u,\"closed_faces\":%llu,\"closed_components\":%u,\"largest_closed\":%u,\"analysis_seconds\":%.3f}\n",w,h,l,outside,(unsigned long long)outsideFaces,closed,(unsigned long long)closedFaces,components,largest,(b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9);
    free(data);free(queue);return 0;
}
