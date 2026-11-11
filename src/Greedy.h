#ifndef CC_GREEDY_H
#define CC_GREEDY_H
#include "Core.h"
struct VertexTextured;
#if defined CC_BUILD_LINUX && CC_GFX_BACKEND == CC_GFX_BACKEND_GL2
extern cc_bool Greedy_Supported;
extern cc_bool Greedy_ExplicitGrad;
cc_bool Greedy_Enabled(void);
int Greedy_Compact(int index,struct VertexTextured* vertices,int count,int x,int y,int z);
cc_bool Gfx_DrawTerrainRanges(int count,const int* lengths,const int* starts);
#else
#define Greedy_ExplicitGrad false
#define Greedy_Enabled() false
#define Greedy_Compact(index,vertices,count,x,y,z) (count)
#endif
#if CC_GFX_BACKEND == CC_GFX_BACKEND_GL2
void Gfx_SetTerrainRepeat(cc_bool enabled);
void Gfx_TrimTerrainVb(int count);
#else
#define Gfx_SetTerrainRepeat(enabled)
#define Gfx_TrimTerrainVb(count)
#endif
#endif
