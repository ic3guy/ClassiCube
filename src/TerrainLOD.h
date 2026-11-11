#ifndef CC_TERRAIN_LOD_H
#define CC_TERRAIN_LOD_H
#include "Core.h"
struct ChunkInfo;
#if defined CC_BUILD_LINUX && CC_GFX_BACKEND == CC_GFX_BACKEND_GL2
void TerrainLOD_Init(void);
cc_uint16 TerrainLOD_Signature(const struct ChunkInfo* chunk);
void TerrainLOD_Prepare(BlockID* padded,int x,int y,int z,cc_uint16 signature,cc_bool* allAir,cc_bool* allSolid);
#else
#define TerrainLOD_Init()
#define TerrainLOD_Signature(chunk) 0
#define TerrainLOD_Prepare(padded,x,y,z,signature,allAir,allSolid)
#endif
#endif
