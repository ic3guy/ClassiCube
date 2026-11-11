#include "MapRenderer.h"
#include "Block.h"
#include "Builder.h"
#include "Camera.h"
#include "Entity.h"
#include "EnvRenderer.h"
#include "Event.h"
#include "ExtMath.h"
#include "Funcs.h"
#include "Game.h"
#include "Graphics.h"
#include "Platform.h"
#include "TexturePack.h"
#include "Utils.h"
#include "World.h"
#include "Options.h"
#include "Benchmark.h"
#include "Adaptive.h"

int MapRenderer_1DUsedCount;
struct ChunkPartInfo* MapRenderer_PartsNormal;
struct ChunkPartInfo* MapRenderer_PartsTranslucent;
struct ChunkPartInfo* MapRenderer_PartsCutout;

static cc_bool inTranslucent;
static IVec3 chunkPos;

/* Whether fully opaque blocks are drawn without alpha testing */
/* (alpha testing is expensive on some GPUs, as it prevents early depth testing) */
static cc_bool opaqueNoAlphaTest;
/* Maximum number of unwanted vertices that can be drawn, in order to combine two draw calls into one */
static int mergeGap;

/* The number of non-empty Normal/Translucent/Cutout ChunkPartInfos (across entire world) for each 1D atlas batch. */
/* 1D atlas batches that do not have any ChunkPartInfos can be entirely skipped. */
static int normPartsCount[ATLAS1D_MAX_ATLASES], tranPartsCount[ATLAS1D_MAX_ATLASES], cutPartsCount[ATLAS1D_MAX_ATLASES];
/* Whether there are any visible Normal/Translucent/Cutout ChunkPartInfos for each 1D atlas batch. */
/* 1D atlas batches that do not have any visible ChunkPartInfos can be skipped. */
static cc_bool hasNormParts[ATLAS1D_MAX_ATLASES], hasTranParts[ATLAS1D_MAX_ATLASES], hasCutParts[ATLAS1D_MAX_ATLASES];
/* Whether renderer should check if there are any visible Normal/Translucent/Cutout ChunkPartInfos for each 1D atlas batch. */
static cc_bool checkNormParts[ATLAS1D_MAX_ATLASES], checkTranParts[ATLAS1D_MAX_ATLASES], checkCutParts[ATLAS1D_MAX_ATLASES];

/* Render info for all chunks in the world. Unsorted. */
static struct ChunkInfo* mapChunks;
/* Pointers to render info for all chunks in the world, sorted by distance from the camera. */
static struct ChunkInfo** sortedChunks;
/* Pointers to render info for all chunks in the world, sorted by distance from the camera. */
/* Only chunks that can be rendered (i.e. not empty and are visible) are included in this.  */
static struct ChunkInfo** renderChunks;
/* Number of actually used pointers in the renderChunks array. Entries past this are ignored and skipped. */
static int renderChunksCount;
/* Distance of each chunk from the camera. */
static cc_uint32* distances;
/* Maximum number of chunk updates that can be performed in one frame. */
static int maxChunkUpdates;
/* Cached number of chunks in the world */
static int chunksCount;

static void ChunkInfo_Init(struct ChunkInfo* chunk, int x, int y, int z) {
	chunk->centreX = x + HALF_CHUNK_SIZE; chunk->centreY = y + HALF_CHUNK_SIZE; 
	chunk->centreZ = z + HALF_CHUNK_SIZE;
#if CC_GFX_BACKEND != CC_GFX_BACKEND_GL11
	chunk->vb = 0;
#endif

	chunk->visible  = true;  
	chunk->empty    = false;
	chunk->allAir   = false;
	chunk->noData   = true;
	chunk->dirty    = true;
	chunk->skipClip = false;

	chunk->drawXMin = false; chunk->drawXMax = false; chunk->drawZMin = false;
	chunk->drawZMax = false; chunk->drawYMin = false; chunk->drawYMax = false;

	chunk->normalParts      = NULL;
	chunk->translucentParts = NULL;
	chunk->cutoutParts      = NULL;
}

static CC_INLINE void ChunkInfo_Refresh(struct ChunkInfo* chunk) {
	if (chunk->allAir) return; /* do not recreate chunks completely air */

	chunk->empty = false;
	chunk->dirty = true;
}

/* Index of maximum used 1D atlas + 1 */
CC_NOINLINE static int MapRenderer_UsedAtlases(void) {
	TextureLoc maxLoc = 0;
	int i;

	for (i = 0; i < Array_Elems(Blocks.Textures); i++) {
		maxLoc = max(maxLoc, Blocks.Textures[i]);
	}
	return Atlas1D_Index(maxLoc) + 1;
}


/*########################################################################################################################*
*-------------------------------------------------------Map rendering-----------------------------------------------------*
*#########################################################################################################################*/
static void CheckWeather(float delta) {
	IVec3 pos;
	BlockID block;
	cc_bool outside;
	IVec3_Floor(&pos, &Camera.CurrentPos);

	block   = World_SafeGetBlock(pos.x, pos.y, pos.z);
	outside = pos.y < 0 || !World_ContainsXZ(pos.x, pos.z);
	inTranslucent = Blocks.Draw[block] == DRAW_TRANSLUCENT || (pos.y < Env.EdgeHeight && outside);

	/* If we are under water, render weather before to blend properly */
	if (!inTranslucent || Env.Weather == WEATHER_SUNNY) return;
	Gfx_SetAlphaBlending(true);
	EnvRenderer_RenderWeather(delta);
	Gfx_SetAlphaBlending(false);
}

#ifdef CC_CLIPPING_FLAGS
	#define DrawBatch(count, start) Gfx_DrawIndexedTris_T2fC4b(count, start, info->skipClip ? DRAW_HINT_NOCLIP : 0)
#else
	#define DrawBatch(count, start) Gfx_DrawIndexedTris_T2fC4b(count, start, 0)
#endif

#if CC_GFX_BACKEND == CC_GFX_BACKEND_GL11
	#define DrawFace(face, ign)    Gfx_BindVb(part.vbs[face]); DrawBatch(0, 0);
	#define DrawFaces(f1, f2, ign) DrawFace(f1, ign); DrawFace(f2, ign);

#define DrawNormalFaces(minFace, maxFace) \
if (drawMin && drawMax) { \
	Gfx_SetFaceCulling(true); \
	DrawFaces(minFace, maxFace, offset); \
	Gfx_SetFaceCulling(false); \
	Game_Vertices += (part.counts[minFace] + part.counts[maxFace]); \
} else if (drawMin) { \
	DrawFace(minFace, offset); \
	Game_Vertices += part.counts[minFace]; \
} else if (drawMax) { \
	DrawFace(maxFace, offset + part.counts[minFace]); \
	Game_Vertices += part.counts[maxFace]; \
}

#define DrawTranslucentFaces(minFace, maxFace) \
if (drawMin && drawMax) { \
	DrawFaces(minFace, maxFace, offset); \
	Game_Vertices += (part.counts[minFace] + part.counts[maxFace]); \
} else if (drawMin) { \
	DrawFace(minFace, offset); \
	Game_Vertices += part.counts[minFace]; \
} else if (drawMax) { \
	DrawFace(maxFace, offset + part.counts[minFace]); \
	Game_Vertices += part.counts[maxFace]; \
}
#else
/* Bit in the 'wanted' mask of DrawPartSections for the sprites of a chunk part */
#define SECTION_SPRITES 1
/* Bit in the 'wanted' mask of DrawPartSections for the given face of a chunk part */
#define SECTION_FACE(face) (2 << (face))

/* Draws the wanted sections of the given chunk part, using as few draw calls as possible. */
/* The vertices of a chunk part are stored as sprites first, then each of the six faces in order. */
/*  Therefore wanted sections that are next to each other can be drawn using just one draw call. */
/* Two wanted sections that are separated by at most 'maxGap' unwanted vertices are also drawn using */
/*  one draw call. Unwanted sections always face away from the camera, so when maxGap is more */
/*  than 0 then face culling MUST be enabled, so the GPU skips over the unwanted sections. */
static void DrawPartSections(struct ChunkInfo* info, const struct ChunkPartInfo* part, int wanted, int maxGap) {
	int offset = part->offset;
	int runBeg = 0, runLen = 0, gap = 0;
	int i, count;

	for (i = 0; i <= FACE_COUNT; i++, offset += count)
	{
		count = i == 0 ? part->spriteCount : part->counts[i - 1];
		if (!count) continue;
		if (!(wanted & (1 << i))) { gap += count; continue; }

		if (runLen && gap <= maxGap && (runLen + gap + count) <= GFX_MAX_VERTICES) {
			/* Close enough to the previous wanted section to share its draw call */
			runLen += gap + count;
		} else {
			if (runLen) {
				DrawBatch(runLen, runBeg); Game_Vertices += runLen;
#ifdef CC_BUILD_LINUX
				if (Benchmark_Mode) Benchmark_Draws++;
#endif
			}
			runBeg = offset;
			runLen = count;
		}
		gap = 0;
	}
	if (runLen) {
		DrawBatch(runLen, runBeg); Game_Vertices += runLen;
#ifdef CC_BUILD_LINUX
		if (Benchmark_Mode) Benchmark_Draws++;
#endif
	}
}
#endif

/* Renders either the opaque parts or the cutout parts of all visible chunks in the given 1D atlas batch */
static void RenderNormalBatch(int batch, cc_bool cutout) {
	int batchOffset = chunksCount * batch;
	struct ChunkInfo* info;
	struct ChunkPartInfo* parts;
	struct ChunkPartInfo part;
	cc_bool* hasParts = cutout ? hasCutParts : hasNormParts;
	int i;
#if CC_GFX_BACKEND == CC_GFX_BACKEND_GL11
	cc_bool drawMin, drawMax;
	int offset, count;
#else
	int wanted;
#endif

	for (i = 0; i < renderChunksCount; i++) {
		info  = renderChunks[i];
		parts = cutout ? info->cutoutParts : info->normalParts;
		if (!parts) continue;

		part = parts[batchOffset];
		if (part.offset < 0) continue;
		hasParts[batch] = true;

#if CC_GFX_BACKEND != CC_GFX_BACKEND_GL11
		Gfx_BindVb_Textured(info->vb);

		/* NOTE: Face culling is on, so drawing all the sprites at once is fine */
		wanted = SECTION_SPRITES;
		if (info->drawXMin) wanted |= SECTION_FACE(FACE_XMIN);
		if (info->drawXMax) wanted |= SECTION_FACE(FACE_XMAX);
		if (info->drawZMin) wanted |= SECTION_FACE(FACE_ZMIN);
		if (info->drawZMax) wanted |= SECTION_FACE(FACE_ZMAX);
		if (info->drawYMin) wanted |= SECTION_FACE(FACE_YMIN);
		if (info->drawYMax) wanted |= SECTION_FACE(FACE_YMAX);

		DrawPartSections(info, &part, wanted, mergeGap);
#else
		offset  = part.offset + part.spriteCount;
		drawMin = info->drawXMin && part.counts[FACE_XMIN];
		drawMax = info->drawXMax && part.counts[FACE_XMAX];
		DrawNormalFaces(FACE_XMIN, FACE_XMAX);

		offset  += part.counts[FACE_XMIN] + part.counts[FACE_XMAX];
		drawMin = info->drawZMin && part.counts[FACE_ZMIN];
		drawMax = info->drawZMax && part.counts[FACE_ZMAX];
		DrawNormalFaces(FACE_ZMIN, FACE_ZMAX);

		offset  += part.counts[FACE_ZMIN] + part.counts[FACE_ZMAX];
		drawMin = info->drawYMin && part.counts[FACE_YMIN];
		drawMax = info->drawYMax && part.counts[FACE_YMAX];
		DrawNormalFaces(FACE_YMIN, FACE_YMAX);

		if (!part.spriteCount) continue;
		count = part.spriteCount >> 2; /* 4 per sprite */

		Gfx_SetFaceCulling(true);
		Gfx_BindVb(part.vbs[FACE_COUNT]);
		DrawBatch(0, 0);
		Game_Vertices += count * 4;
		Gfx_SetFaceCulling(false);
#endif
	}
}

void MapRenderer_RenderNormal(float delta) {
	int batch;
	if (!mapChunks) return;

	Gfx_SetVertexFormat(VERTEX_FORMAT_TEXTURED);
	Gfx_EnableMipmaps();
#if CC_GFX_BACKEND != CC_GFX_BACKEND_GL11
	/* Faces that point away from the camera are never visible in these passes */
	/* (also required by DrawPartSections when merging draw calls) */
	Gfx_SetFaceCulling(true);
#endif

	/* Fully opaque blocks first, as they do not need alpha testing */
	/*  (chunks are sorted nearest first, so the GPU can then skip most hidden pixels) */
	Gfx_SetAlphaTest(!opaqueNoAlphaTest);
	for (batch = 0; batch < MapRenderer_1DUsedCount; batch++)
	{
		if (normPartsCount[batch] <= 0) continue;
		if (hasNormParts[batch] || checkNormParts[batch]) {
			Atlas1D_Bind(batch);
			RenderNormalBatch(batch, false);
			checkNormParts[batch] = false;
		}
	}

	/* Then blocks that do need alpha testing (e.g. leaves, glass, sprites) */
	Gfx_SetAlphaTest(true);
	for (batch = 0; batch < MapRenderer_1DUsedCount; batch++)
	{
		if (cutPartsCount[batch] <= 0) continue;
		if (hasCutParts[batch] || checkCutParts[batch]) {
			Atlas1D_Bind(batch);
			RenderNormalBatch(batch, true);
			checkCutParts[batch] = false;
		}
	}

#if CC_GFX_BACKEND != CC_GFX_BACKEND_GL11
	Gfx_SetFaceCulling(false);
#endif
	Gfx_DisableMipmaps();

	CheckWeather(delta);
	Gfx_SetAlphaTest(false);
#if DEBUG_OCCLUSION
	DebugPickedPos();
#endif
}

static void RenderTranslucentBatch(int batch) {
	int batchOffset = chunksCount * batch;
	struct ChunkInfo* info;
	struct ChunkPartInfo part;
	int i;
#if CC_GFX_BACKEND == CC_GFX_BACKEND_GL11
	cc_bool drawMin, drawMax;
	int offset;
#else
	int wanted;
#endif

	for (i = 0; i < renderChunksCount; i++) {
		info = renderChunks[i];
		if (!info->translucentParts) continue;

		part = info->translucentParts[batchOffset];
		if (part.offset < 0) continue;
		hasTranParts[batch] = true;

#if CC_GFX_BACKEND != CC_GFX_BACKEND_GL11
		Gfx_BindVb_Textured(info->vb);

		wanted = 0;
		if (inTranslucent || info->drawXMin) wanted |= SECTION_FACE(FACE_XMIN);
		if (inTranslucent || info->drawXMax) wanted |= SECTION_FACE(FACE_XMAX);
		if (inTranslucent || info->drawZMin) wanted |= SECTION_FACE(FACE_ZMIN);
		if (inTranslucent || info->drawZMax) wanted |= SECTION_FACE(FACE_ZMAX);
		if (inTranslucent || info->drawYMin) wanted |= SECTION_FACE(FACE_YMIN);
		if (inTranslucent || info->drawYMax) wanted |= SECTION_FACE(FACE_YMAX);

		/* Face culling is off for translucent blocks, so cannot draw any unwanted sections */
		DrawPartSections(info, &part, wanted, 0);
#else
		offset  = part.offset;
		drawMin = (inTranslucent || info->drawXMin) && part.counts[FACE_XMIN];
		drawMax = (inTranslucent || info->drawXMax) && part.counts[FACE_XMAX];
		DrawTranslucentFaces(FACE_XMIN, FACE_XMAX);

		offset  += part.counts[FACE_XMIN] + part.counts[FACE_XMAX];
		drawMin = (inTranslucent || info->drawZMin) && part.counts[FACE_ZMIN];
		drawMax = (inTranslucent || info->drawZMax) && part.counts[FACE_ZMAX];
		DrawTranslucentFaces(FACE_ZMIN, FACE_ZMAX);

		offset  += part.counts[FACE_ZMIN] + part.counts[FACE_ZMAX];
		drawMin = (inTranslucent || info->drawYMin) && part.counts[FACE_YMIN];
		drawMax = (inTranslucent || info->drawYMax) && part.counts[FACE_YMAX];
		DrawTranslucentFaces(FACE_YMIN, FACE_YMAX);
#endif
	}
}

void MapRenderer_RenderTranslucent(float delta) {
	int vertices, batch;
	if (!mapChunks) return;

	/* First fill depth buffer */
	vertices = Game_Vertices;
	Gfx_SetVertexFormat(VERTEX_FORMAT_TEXTURED);
	Gfx_SetAlphaBlending(false);
	Gfx_DepthOnlyRendering(true);

	for (batch = 0; batch < MapRenderer_1DUsedCount; batch++) 
	{
		if (tranPartsCount[batch] <= 0) continue;
		if (hasTranParts[batch] || checkTranParts[batch]) {
			RenderTranslucentBatch(batch);
			checkTranParts[batch] = false;
		}
	}
	Game_Vertices = vertices;

	/* Then actually draw the transluscent blocks */
	Gfx_SetAlphaBlending(true);
	Gfx_DepthOnlyRendering(false);
	Gfx_SetDepthWrite(false); /* already calculated depth values in depth pass */

	Gfx_EnableMipmaps();
	for (batch = 0; batch < MapRenderer_1DUsedCount; batch++) 
	{
		if (tranPartsCount[batch] <= 0) continue;
		if (!hasTranParts[batch]) continue;

		Atlas1D_Bind(batch);
		RenderTranslucentBatch(batch);
	}
	Gfx_DisableMipmaps();

	Gfx_SetDepthWrite(true);
	/* If we weren't under water, render weather after to blend properly */
	if (!inTranslucent && Env.Weather != WEATHER_SUNNY) {
		Gfx_SetAlphaTest(true);
		EnvRenderer_RenderWeather(delta);
		Gfx_SetAlphaTest(false);
	}
	Gfx_SetAlphaBlending(false);
}


/*########################################################################################################################*
*---------------------------------------------------Chunk functionality---------------------------------------------------*
*#########################################################################################################################*/
/* Deletes vertex buffer associated with the given chunk and updates internal state */
static void DeleteChunk(struct ChunkInfo* chunk) {
	struct ChunkPartInfo* ptr;
	int i;
#if CC_GFX_BACKEND == CC_GFX_BACKEND_GL11
	int j;
#else
	Gfx_DeleteVb(&chunk->vb);
#endif

	chunk->empty  = false; 
	chunk->allAir = false;
	chunk->noData = true;
	chunk->dirty  = true;

#ifdef OCCLUSION
	chunk.OcclusionFlags = 0;
	chunk.OccludedFlags = 0;
#endif

	if (chunk->normalParts) {
		ptr = chunk->normalParts;
		for (i = 0; i < MapRenderer_1DUsedCount; i++, ptr += chunksCount) {
			if (ptr->offset < 0) continue; 
			normPartsCount[i]--;
#if CC_GFX_BACKEND == CC_GFX_BACKEND_GL11
			for (j = 0; j < CHUNKPART_MAX_VBS; j++) Gfx_DeleteVb(&ptr->vbs[j]);
#endif
		}
		chunk->normalParts = NULL;
	}

	if (chunk->translucentParts) {
		ptr = chunk->translucentParts;
		for (i = 0; i < MapRenderer_1DUsedCount; i++, ptr += chunksCount) {
			if (ptr->offset < 0) continue;
			tranPartsCount[i]--;
#if CC_GFX_BACKEND == CC_GFX_BACKEND_GL11
			for (j = 0; j < CHUNKPART_MAX_VBS; j++) Gfx_DeleteVb(&ptr->vbs[j]);
#endif
		}
		chunk->translucentParts = NULL;
	}

	if (chunk->cutoutParts) {
		ptr = chunk->cutoutParts;
		for (i = 0; i < MapRenderer_1DUsedCount; i++, ptr += chunksCount) {
			if (ptr->offset < 0) continue;
			cutPartsCount[i]--;
#if CC_GFX_BACKEND == CC_GFX_BACKEND_GL11
			for (j = 0; j < CHUNKPART_MAX_VBS; j++) Gfx_DeleteVb(&ptr->vbs[j]);
#endif
		}
		chunk->cutoutParts = NULL;
	}
}

/* Builds the mesh (hence vertex buffer) for the given chunk, and updates internal state */
static void BuildChunk(struct ChunkInfo* chunk, int* chunkUpdates) {
	struct ChunkPartInfo* ptr;
	int i;

	Game.ChunkUpdates++;
	(*chunkUpdates)++;
	if (!Builder_MakeChunk(chunk)) return;

	chunk->dirty  = false;
	chunk->noData = !chunk->normalParts && !chunk->translucentParts && !chunk->cutoutParts;
	chunk->empty  = chunk->noData;
	if (chunk->empty) return;
	
	if (chunk->normalParts) {
		ptr = chunk->normalParts;
		for (i = 0; i < MapRenderer_1DUsedCount; i++, ptr += chunksCount) {
			if (ptr->offset >= 0) normPartsCount[i]++;
		}
	}

	if (chunk->translucentParts) {
		ptr = chunk->translucentParts;
		for (i = 0; i < MapRenderer_1DUsedCount; i++, ptr += chunksCount) {
			if (ptr->offset >= 0) tranPartsCount[i]++;
		}
	}

	if (chunk->cutoutParts) {
		ptr = chunk->cutoutParts;
		for (i = 0; i < MapRenderer_1DUsedCount; i++, ptr += chunksCount) {
			if (ptr->offset >= 0) cutPartsCount[i]++;
		}
	}
}


/*########################################################################################################################*
*----------------------------------------------------Chunks mangagement---------------------------------------------------*
*#########################################################################################################################*/
static void FreeParts(void) {
	Mem_Free(MapRenderer_PartsNormal);
	MapRenderer_PartsNormal      = NULL;
	MapRenderer_PartsTranslucent = NULL;
	MapRenderer_PartsCutout      = NULL;
}

static void FreeChunks(void) {
	Mem_Free(mapChunks);
	Mem_Free(sortedChunks);
	Mem_Free(renderChunks);
	Mem_Free(distances);

	mapChunks    = NULL;
	sortedChunks = NULL;
	renderChunks = NULL;
	distances    = NULL;
}

static void AllocateParts(void) {
	struct ChunkPartInfo* ptr;
	cc_uint32 count = chunksCount * MapRenderer_1DUsedCount;

	ptr = (struct ChunkPartInfo*)Mem_AllocCleared(count * 3, sizeof(struct ChunkPartInfo), "chunk parts");
	MapRenderer_PartsNormal      = ptr;
	MapRenderer_PartsTranslucent = ptr + count;
	MapRenderer_PartsCutout      = ptr + count * 2;
}

static void AllocateChunks(void) {
	mapChunks    = (struct ChunkInfo*) Mem_Alloc(chunksCount, sizeof(struct ChunkInfo),  "chunk info");
	sortedChunks = (struct ChunkInfo**)Mem_Alloc(chunksCount, sizeof(struct ChunkInfo*), "sorted chunk info");
	renderChunks = (struct ChunkInfo**)Mem_Alloc(chunksCount, sizeof(struct ChunkInfo*), "render chunk info");
	distances    = (cc_uint32*)Mem_Alloc(chunksCount, 4, "chunk distances");
}

static void ResetPartFlags(void) {
	int i;
	for (i = 0; i < ATLAS1D_MAX_ATLASES; i++) {
		checkNormParts[i] = true;
		hasNormParts[i]   = false;
		checkTranParts[i] = true;
		hasTranParts[i]   = false;
		checkCutParts[i]  = true;
		hasCutParts[i]    = false;
	}
}

static void ResetPartCounts(void) {
	int i;
	for (i = 0; i < ATLAS1D_MAX_ATLASES; i++) {
		normPartsCount[i] = 0;
		tranPartsCount[i] = 0;
		cutPartsCount[i]  = 0;
	}
}

static void InitChunks(void) {
	int x, y, z, index = 0;
	for (z = 0; z < World.Length; z += CHUNK_SIZE) {
		for (y = 0; y < World.Height; y += CHUNK_SIZE) {
			for (x = 0; x < World.Width; x += CHUNK_SIZE) {
				ChunkInfo_Init(&mapChunks[index], x, y, z);
				sortedChunks[index] = &mapChunks[index];
				renderChunks[index] = &mapChunks[index];
				distances[index]    = 0;
				index++;
			}
		}
	}
}

static void RefreshChunks(void) {
	int i;
	if (!mapChunks) return;

	for (i = 0; i < chunksCount; i++) 
	{
		ChunkInfo_Refresh(&mapChunks[i]);
	}
}

static void DeleteChunks(void) {
	int i;
	if (!mapChunks) return;

	for (i = 0; i < chunksCount; i++) 
	{
		DeleteChunk(&mapChunks[i]);
	}
	ResetPartCounts();
}

void MapRenderer_Refresh(void) {
	int oldCount;
	chunkPos = IVec3_MaxValue();

	if (mapChunks && World.Blocks) {
		DeleteChunks();

		oldCount = MapRenderer_1DUsedCount;
		MapRenderer_1DUsedCount = MapRenderer_UsedAtlases();
		/* Need to reallocate parts array in this case */
		if (MapRenderer_1DUsedCount != oldCount) {
			FreeParts();
			AllocateParts();
		}
	}
	ResetPartCounts();
}

/* Refreshes chunks on the border of the map whose y is less than 'maxHeight'. */
static void RefreshBorderChunks(int maxHeight) {
	int cx, cy, cz;
	cc_bool onBorder;

	chunkPos = IVec3_MaxValue();
	if (!mapChunks || !World.Blocks) return;

	for (cz = 0; cz < World.ChunksZ; cz++) {
		for (cy = 0; cy < World.ChunksY; cy++) {
			for (cx = 0; cx < World.ChunksX; cx++) {
				onBorder = cx == 0 || cz == 0 || cx == (World.ChunksX - 1) || cz == (World.ChunksZ - 1);

				if (onBorder && (cy * CHUNK_SIZE) < maxHeight) {
					MapRenderer_RefreshChunk(cx, cy, cz);
				}
			}
		}
	}
}


/*########################################################################################################################*
*--------------------------------------------------Chunks updating/sorting------------------------------------------------*
*#########################################################################################################################*/
/* Frames that take longer than this reduce how many chunks get built per frame */
/*  (i.e. aims to keep the game at 60 FPS while chunks are being built) */
#define CHUNK_TARGET_TIME ((1.0f/60) + 0.005f)
static int chunksTarget = 12;
static Vec3 lastCamPos;
static float lastYaw, lastPitch;
/* Max distance from camera that chunks are rendered within */
/* This may differ from the view distance configured by the user */
static int renderDistSquared;
/* Max distance from camera that chunks are built within */
/* Chunks past this distance are automatically unloaded */
static int buildDistSquared;

/* Maximum time (in microseconds) that should be spent building chunks in a frame while the camera is moving */
static int buildBudgetMoving;
/* Maximum time (in microseconds) that should be spent building chunks in the current frame */
static int buildBudget;
/* Time at which chunk building for the current frame started */
static cc_uint64 buildStart;
/* Whether the time for building chunks in the current frame has been used up */
static cc_bool buildBudgetSpent;

/* Chunks that need building but cannot currently be seen are remembered in this list, */
/*  and are only built after all the visible chunks, if there is still time left over */
#define MAX_DEFERRED_CHUNKS 32
static struct ChunkInfo* deferredChunks[MAX_DEFERRED_CHUNKS];
static int deferredCount;

/* Whether the given chunk can be built in the current frame */
static cc_bool CanBuildChunk(struct ChunkInfo* chunk, int chunkUpdates) {
	int elapsed;
	if (chunkUpdates >= chunksTarget) return false;
	/* Chunks that already have a mesh are being rebuilt because their blocks changed, */
	/*  so are rebuilt straight away to avoid e.g. brief holes at the edges of chunks */
	if (!chunk->noData) return true;

	if (buildBudgetSpent) return false;
	/* Always build at least one chunk per frame, so that progress is always made */
	if (!chunkUpdates) return true;

	elapsed = (int)Stopwatch_ElapsedMicroseconds(buildStart, Stopwatch_Measure());
	if (elapsed < buildBudget) return true;

	buildBudgetSpent = true;
	return false;
}

static void BuildDeferredChunks(int* chunkUpdates) {
	struct ChunkInfo* chunk;
	int i;

	for (i = 0; i < deferredCount; i++)
	{
		chunk = deferredChunks[i];
		if (!CanBuildChunk(chunk, *chunkUpdates)) continue;

		DeleteChunk(chunk);
		BuildChunk(chunk, chunkUpdates);
	}
}

static int AdjustDist(int dist) {
	if (dist < CHUNK_SIZE) dist = CHUNK_SIZE;
	dist = Utils_AdjViewDist(dist);
	return (dist + 24) * (dist + 24);
}

static void CalcViewDists(void) {
	buildDistSquared  = AdjustDist(Game_UserViewDistance);
	if (Adaptive_MaxDistance) buildDistSquared = AdjustDist(min(Game_UserViewDistance, Game_ViewDistance + 32));
	renderDistSquared = AdjustDist(Game_ViewDistance);
}

static int UpdateChunksAndVisibility(int* chunkUpdates) {
	int renderDistSqr = renderDistSquared;
	int buildDistSqr  = buildDistSquared;

	struct ChunkInfo* chunk;
	int i, j = 0, distSqr, res;

	for (i = 0; i < chunksCount; i++) 
	{
		chunk = sortedChunks[i];
		if (chunk->empty) continue;
		distSqr = distances[i];
		
		/* Auto unload chunks far away chunks */
		if (!chunk->noData && distSqr >= buildDistSqr + 32 * 16) {
			DeleteChunk(chunk); continue;
		}

		if (distSqr > renderDistSqr) {
			chunk->visible  = false;
		} else {
			res = Frustum_TestSphere(chunk->centreX, chunk->centreY, chunk->centreZ, 14); /* 14 ~ sqrt(3 * 8^2) */
			chunk->visible  = res != FRUSTUM_OUTSIDE;
			chunk->skipClip = Gfx_CanSphereSkipClipping(chunk->centreX, chunk->centreY, chunk->centreZ, 14);
		}

		if (chunk->dirty && distSqr <= buildDistSqr) {
			if (!chunk->visible) {
				/* Chunks that cannot be seen are only built if there is time left over */
				if (deferredCount < MAX_DEFERRED_CHUNKS) deferredChunks[deferredCount++] = chunk;
			} else if (CanBuildChunk(chunk, *chunkUpdates)) {
				DeleteChunk(chunk);
				BuildChunk(chunk, chunkUpdates);
			}
		}

		if (chunk->visible && !chunk->empty) { renderChunks[j] = chunk; j++; }
	}

	BuildDeferredChunks(chunkUpdates);
	return j;
}

static int UpdateChunksStill(int* chunkUpdates) {
	int renderDistSqr = renderDistSquared;
	int buildDistSqr  = buildDistSquared;

	struct ChunkInfo* chunk;
	int i, j = 0, distSqr, res;

	for (i = 0; i < chunksCount; i++) 
	{
		chunk = sortedChunks[i];
		if (chunk->empty) continue;
		distSqr = distances[i];

		/* Auto unload chunks far away chunks */
		if (!chunk->noData && distSqr >= buildDistSqr + 32 * 16) {
			DeleteChunk(chunk); continue;
		}

		if (chunk->dirty && distSqr <= buildDistSqr) {
			if (!chunk->visible) {
				/* Chunks that cannot be seen are only built if there is time left over */
				if (deferredCount < MAX_DEFERRED_CHUNKS) deferredChunks[deferredCount++] = chunk;
			} else if (CanBuildChunk(chunk, *chunkUpdates)) {
				DeleteChunk(chunk);
				BuildChunk(chunk, chunkUpdates);

				/* only need to update the visibility of chunks in range. */
				if (distSqr > renderDistSqr) {
					chunk->visible  = false;
				} else {
					res = Frustum_TestSphere(chunk->centreX, chunk->centreY, chunk->centreZ, 14); /* 14 ~ sqrt(3 * 8^2) */
					chunk->visible  = res != FRUSTUM_OUTSIDE;
					chunk->skipClip = Gfx_CanSphereSkipClipping(chunk->centreX, chunk->centreY, chunk->centreZ, 14);
				}
			}
		}

		if (chunk->visible && !chunk->empty) { renderChunks[j] = chunk; j++; }
	}

	BuildDeferredChunks(chunkUpdates);
	return j;
}

static void UpdateChunks(float delta) {
	struct LocalPlayer* p;
	cc_bool samePos;
	int chunkUpdates = 0;

	/* Build more chunks if around 60 FPS or over, otherwise slowdown */
	chunksTarget += delta < CHUNK_TARGET_TIME ? 1 : -1; 
	Math_Clamp(chunksTarget, 4, maxChunkUpdates);

	p = Entities.CurPlayer;
	samePos = Vec3_Equals(&Camera.CurrentPos, &lastCamPos)
		&& p->Base.Pitch == lastPitch && p->Base.Yaw == lastYaw;

	/* Stuttering due to building chunks is far less noticeable when the camera is not moving, */
	/*  so allow more time for building chunks then (so the world still loads reasonably quickly) */
	buildBudget      = samePos ? buildBudgetMoving * 4 : buildBudgetMoving;
	buildStart       = Stopwatch_Measure();
	buildBudgetSpent = false;
	deferredCount    = 0;

	renderChunksCount = samePos ?
		UpdateChunksStill(&chunkUpdates) :
		UpdateChunksAndVisibility(&chunkUpdates);

	lastCamPos = Camera.CurrentPos;
	lastPitch  = p->Base.Pitch;
	lastYaw    = p->Base.Yaw;

	if (!samePos || chunkUpdates) ResetPartFlags();
}

static void SortMapChunks(int left, int right) {
	struct ChunkInfo** values = sortedChunks; struct ChunkInfo* value;
	cc_uint32* keys = distances; cc_uint32 key;

	while (left < right) {
		int i = left, j = right;
		cc_uint32 pivot = keys[(i + j) >> 1];

		/* partition the list */
		while (i <= j) {
			while (pivot > keys[i]) i++;
			while (pivot < keys[j]) j--;
			QuickSort_Swap_KV_Maybe();
		}
		/* recurse into the smaller subset */
		QuickSort_Recurse(SortMapChunks)
	}
}

static void UpdateSortOrder(void) {
	struct ChunkInfo* info;
	IVec3 pos;
	int i, dx, dy, dz;

	/* pos is centre coordinate of chunk camera is in */
	IVec3_Floor(&pos, &Camera.CurrentPos);
	pos.x = (pos.x & ~CHUNK_MASK) + HALF_CHUNK_SIZE;
	pos.y = (pos.y & ~CHUNK_MASK) + HALF_CHUNK_SIZE;
	pos.z = (pos.z & ~CHUNK_MASK) + HALF_CHUNK_SIZE;

	/* If in same chunk, don't need to recalculate sort order */
	if (pos.x == chunkPos.x && pos.y == chunkPos.y && pos.z == chunkPos.z) return;
	chunkPos = pos;
	if (!chunksCount) return;

	for (i = 0; i < chunksCount; i++) {
		info = sortedChunks[i];
		/* Calculate distance to chunk centre */
		dx = info->centreX - pos.x; dy = info->centreY - pos.y; dz = info->centreZ - pos.z;
		distances[i] = dx * dx + dy * dy + dz * dz;

		/* Consider these 3 chunks: */
		/* |       X-1      |        X        |       X+1      | */
		/* |################|########@########|################| */
		/* Assume the player is standing at @, then DrawXMin/XMax is calculated as this */
		/*    X-1: DrawXMin = false, DrawXMax = true  */
		/*    X  : DrawXMin = true,  DrawXMax = true  */
		/*    X+1: DrawXMin = true,  DrawXMax = false */

		info->drawXMin = dx >= 0; info->drawXMax = dx <= 0;
		info->drawZMin = dz >= 0; info->drawZMax = dz <= 0;
		info->drawYMin = dy >= 0; info->drawYMax = dy <= 0;
	}

	SortMapChunks(0, chunksCount - 1);
	ResetPartFlags();
	/*SimpleOcclusionCulling();*/
}

void MapRenderer_Update(float delta) {
	if (!mapChunks) return;
	UpdateSortOrder();
	UpdateChunks(delta);
}


/*########################################################################################################################*
*---------------------------------------------------------General---------------------------------------------------------*
*#########################################################################################################################*/
void MapRenderer_RefreshChunk(int cx, int cy, int cz) {
	struct ChunkInfo* chunk;
	if (cx < 0 || cy < 0 || cz < 0 || cx >= World.ChunksX || cy >= World.ChunksY || cz >= World.ChunksZ) return;

	chunk = &mapChunks[World_ChunkPack(cx, cy, cz)];
	ChunkInfo_Refresh(chunk);
}

void MapRenderer_OnBlockChanged(int x, int y, int z, BlockID block) {
	int cx = x >> CHUNK_SHIFT, cy = y >> CHUNK_SHIFT, cz = z >> CHUNK_SHIFT;
	struct ChunkInfo* chunk;

	chunk = &mapChunks[World_ChunkPack(cx, cy, cz)];
	chunk->allAir &= Blocks.Draw[block] == DRAW_GAS;
	/* TODO: Don't lookup twice, refresh directly using chunk pointer */
	ChunkInfo_Refresh(chunk);
}

static void OnEnvVariableChanged(void* obj, int envVar) {
	if (envVar == ENV_VAR_SUN_COLOR || envVar == ENV_VAR_SHADOW_COLOR) {
		RefreshChunks();
	} else if (envVar == ENV_VAR_EDGE_HEIGHT || envVar == ENV_VAR_SIDES_OFFSET) {
		int oldClip        = Builder_EdgeLevel;
		Builder_SidesLevel = max(0, Env_SidesHeight);
		Builder_EdgeLevel  = max(0, Env.EdgeHeight);

		/* Only need to refresh chunks on map borders up to highest edge level.*/
		RefreshBorderChunks(max(oldClip, Builder_EdgeLevel));
	}
}

static void OnTerrainAtlasChanged(void* obj) {
	static int tilesPerAtlas;
	/* e.g. If old atlas was 256x256 and new is 256x256, don't need to refresh */
	if (MapRenderer_1DUsedCount && tilesPerAtlas != Atlas1D.TilesPerAtlas) {
		MapRenderer_Refresh();
	}

	MapRenderer_1DUsedCount = MapRenderer_UsedAtlases();
	tilesPerAtlas = Atlas1D.TilesPerAtlas;
	ResetPartFlags();
}

static void OnBlockDefinitionChanged(void* obj) {
	MapRenderer_Refresh();
	MapRenderer_1DUsedCount = MapRenderer_UsedAtlases();
	ResetPartFlags();
}

static void OnVisibilityChanged(void* obj) {
	lastCamPos = Vec3_BigPos();
	CalcViewDists();
}
static void DeleteChunks_(void* obj) { DeleteChunks(); }
static void Refresh_(void* obj)      { MapRenderer_Refresh(); }

static void OnNewMap(void) {
	Game.ChunkUpdates = 0;
	DeleteChunks();
	ResetPartCounts();

	chunkPos = IVec3_MaxValue();
	FreeChunks();
	FreeParts();
}

static void OnNewMapLoaded(void) {
	chunksCount = World.ChunksCount;
	/* TODO: Only perform reallocation when map volume has changed */
	/*if (chunksCount != World.ChunksCount) { */
		/* chunksCount = World.ChunksCount; */
		FreeChunks();
		FreeParts();
		AllocateChunks();
		AllocateParts();
	/*}*/

	InitChunks();
	lastCamPos = Vec3_BigPos();
}

static void OnInit(void) {
	Event_Register_(&TextureEvents.AtlasChanged,  NULL, OnTerrainAtlasChanged);
	Event_Register_(&WorldEvents.EnvVarChanged,   NULL, OnEnvVariableChanged);
	Event_Register_(&BlockEvents.BlockDefChanged, NULL, OnBlockDefinitionChanged);

	Event_Register_(&GfxEvents.ViewDistanceChanged, NULL, OnVisibilityChanged);
	Event_Register_(&GfxEvents.ProjectionChanged,   NULL, OnVisibilityChanged);
	Event_Register_(&GfxEvents.ContextLost,         NULL, DeleteChunks_);
	Event_Register_(&GfxEvents.ContextRecreated,    NULL, Refresh_);

	/* This = 87 fixes map being invisible when no textures */
	MapRenderer_1DUsedCount = 87; /* Atlas1D_UsedAtlasesCount(); */
	chunkPos   = IVec3_MaxValue();
	maxChunkUpdates = Options_GetInt(OPT_MAX_CHUNK_UPDATES, 4, 1024, 30);
	/* Options for comparing performance with and without these optimisations */
	opaqueNoAlphaTest = Options_GetBool(OPT_OPAQUE_NO_ALPHATEST, true);
	mergeGap          = Options_GetInt(OPT_DRAW_MERGE_GAP, 0, GFX_MAX_VERTICES, 64);
	buildBudgetMoving = Options_GetInt(OPT_CHUNK_BUILD_MS, 0, 100, 3) * 1000;
	CalcViewDists();
}

struct IGameComponent MapRenderer_Component = {
	OnInit, /* Init */
	OnNewMap, /* Free */
	OnNewMap, /* Reset */
	OnNewMap, /* OnNewMap */
	OnNewMapLoaded /* OnNewMapLoaded */
};
