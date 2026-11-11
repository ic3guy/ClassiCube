#ifndef CC_OCCLUSION_H
#define CC_OCCLUSION_H
#include "Core.h"
struct ChunkInfo;
struct VertexTextured;
struct ChunkPartInfo;
struct OcclusionRange;
#ifdef CC_BUILD_LINUX
void Occlusion_Init(void);
void Occlusion_Reset(void);
void Occlusion_Invalidate(void);
void Occlusion_Build(int index, const BlockID* padded, cc_bool allAir, cc_bool allSolid);
void Occlusion_MeshBounds(int index, const struct VertexTextured* vertices, int count);
void Occlusion_BuildRanges(struct ChunkPartInfo* part, struct VertexTextured* vertices, int y);
void Occlusion_BuildSurfaces(int index, const struct VertexTextured* vertices);
cc_bool Occlusion_HiddenRange(const struct OcclusionRange* range);
int Occlusion_Cull(struct ChunkInfo* chunks, struct ChunkInfo** sorted, struct ChunkInfo** rendered, int count);
#else
#define Occlusion_Init()
#define Occlusion_Reset()
#define Occlusion_Invalidate()
#define Occlusion_Build(index, padded, allAir, allSolid)
#define Occlusion_MeshBounds(index, vertices, count)
#define Occlusion_BuildRanges(part, vertices, y)
#define Occlusion_BuildSurfaces(index, vertices)
#define Occlusion_Cull(chunks, sorted, rendered, count) (count)
#endif
#endif
