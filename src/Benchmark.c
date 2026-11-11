#include "Benchmark.h"
#ifdef CC_BUILD_LINUX
#include "Game.h"
#include "World.h"
#include "Server.h"
#include "Entity.h"
#include "Camera.h"
#include "Window.h"
#include "Platform.h"
#include "Options.h"
#include "ExtMath.h"
#include "Funcs.h"
#include "Block.h"
#include "BlockPhysics.h"
#include "HiZ.h"
#include <stdio.h>
#include <stdlib.h>

int Benchmark_Mode, Benchmark_Draws, Benchmark_BuildUs, Benchmark_PacingUs;
int Benchmark_Pending = -1;
cc_bool Benchmark_Preloading;
cc_bool Benchmark_DisableOcclusion;
int Benchmark_MeshBefore,Benchmark_MeshAfter;
static cc_bool compareImages;
static int comparePhase;
static int maxPending;
static cc_bool freezeScene, capture, captured;
static cc_bool dumpWorld, dumpedWorld;
static int editFixture;
static cc_bool fixtureEdited;
static int heightVertices[64];
static double heightTotals[64];
static cc_bool FrozenAnimations(struct ScheduledTask2* task) { return true; }

void Benchmark_RecordDraw(int worldY, int vertices) {
	int y = worldY >> 4;
	Benchmark_Draws++;
	if (y >= 0 && y < 64) heightVertices[y] += vertices;
}
static int duration, warmup, count, syncGpu;
static cc_uint64 loadedAt;
static void (*finishGpu)(void);
/* Bounded storage; no console or disk writes during the timed section. */
#define MAX_SAMPLES 30000
struct Sample { int frame, logic, draw, wait, present, builds, draws, vertices, distance; };
static struct Sample samples[MAX_SAMPLES];
static cc_uint64 previous;
static int gpuWaitUs;

void Benchmark_Init(void) {
	Benchmark_Mode = Options_GetInt("benchmark-mode", 0, 3, 0);
	if (!Benchmark_Mode || !Server.IsSinglePlayer) { Benchmark_Mode = 0; return; }
	duration = Options_GetInt("benchmark-seconds", 5, 300, 20);
	warmup   = Options_GetInt("benchmark-warmup", 2, 300, 10);
	freezeScene = Options_GetBool("benchmark-freeze", false);
	capture = Options_GetBool("benchmark-capture", false);
	compareImages=Options_GetBool("benchmark-compare-images",false);
	Benchmark_DisableOcclusion=false;
	dumpWorld = Options_GetBool("benchmark-dump", false);
	editFixture = Options_GetInt("benchmark-edit-fixture",0,2,0);
	Benchmark_Preloading = Options_GetBool("benchmark-preload", false);
	if (freezeScene) {
		Game_Tasks.anims.callback = FrozenAnimations;
		Physics_SetEnabled(false);
		Game_HideGui = true;
	}
	syncGpu  = Options_GetBool("benchmark-sync", false);
#if CC_GFX_BACKEND_IS_GL()
	if (syncGpu) finishGpu = (void (*)(void))GLContext_GetAddress("glFinish");
#endif
	if (Options_GetBool("benchmark-uncapped", true)) Game_SetFpsLimit(FPS_LIMIT_NONE);
	Platform_LogConst("BENCH armed: mode 1=spawn / 2=flyover / 3=ground path");
}

int Benchmark_Seed(int seed) {
	return Options_GetInt("benchmark-mode", 0, 3, 0)
		? Options_GetInt("benchmark-seed", 0, Int32_MaxValue, 12345) : seed;
}

void Benchmark_Position(void) {
	struct LocationUpdate update;
	struct LocalPlayer* p = Entities.CurPlayer;
	float elapsed, pathElapsed, angle, radius;
	int y, x, z;
	BlockID block;
	if (!Benchmark_Mode || !World.Loaded || !World.Blocks) return;
	if (dumpWorld && !dumpedWorld) {
		FILE* f=fopen("occupancy.bin","wb");
		cc_uint32 dimensions[3];
		cc_uint8 buffer[16384];
		int start,j,n;
		BlockID id;
		dimensions[0]=World.Width; dimensions[1]=World.Height; dimensions[2]=World.Length;
		if (f) {
			fwrite(dimensions,sizeof(cc_uint32),3,f);
			for (start=0;start<World.Volume;start+=16384) {
				n=min(16384,World.Volume-start);
				for (j=0;j<n;j++) {
					id=World.Blocks[start+j];
#ifdef EXTENDED_BLOCKS
					if (World.IDMask>255) id|=World.Blocks2[start+j]<<8;
#endif
					buffer[j]=Blocks.FullOpaque[id]?1:0;
				}
				fwrite(buffer,1,n,f);
			}
			fclose(f);
		}
		dumpedWorld=true;
	}
	if (!loadedAt) loadedAt = Stopwatch_Measure();
	elapsed = Stopwatch_ElapsedMS(loadedAt, Stopwatch_Measure()) / 1000.0f;
	if(editFixture && !fixtureEdited && !Benchmark_Preloading && elapsed>=warmup-1 && World.Width==64 && World.Height==64 && World.Length==64) {
		int xx,yy,zz;
		for(yy=9;yy<34;yy++) for(zz=24;zz<27;zz++) for(xx=24;xx<27;xx++) {
			Game_UpdateBlock(xx,yy,zz,editFixture==1?BLOCK_AIR:BLOCK_STONE);
		}
		fixtureEdited=true;
	}
	if (freezeScene) Game.Time = 100.0;
	if(compareImages && !Benchmark_Preloading && Benchmark_Pending==0) {
		if(comparePhase==0 && elapsed>=warmup) { Benchmark_DisableOcclusion=true; Game_ScreenshotRequested=true; comparePhase=1; }
		else if(comparePhase==1 && elapsed>=warmup+2) {
			Benchmark_DisableOcclusion=false; Game_ScreenshotRequested=true; comparePhase=2;
		}
	}
	if (capture && !captured && elapsed >= warmup && Benchmark_Pending == 0) {
		Game_ScreenshotRequested = true; captured = true;
	}
	update.flags = LU_HAS_POS | LU_HAS_YAW | LU_HAS_PITCH;
	/* Hold a moving route still before its A/B pictures, while retaining
	   occluder history from the preceding movement during warmup. */
	pathElapsed=compareImages && elapsed>warmup-1?warmup-1:elapsed;
	if(Benchmark_Preloading) pathElapsed=warmup;
	update.pos   = p->Spawn;
	update.yaw   = 45.0f;
	update.pitch = 15.0f;
	if (Benchmark_Mode == 1) {
		update.pos.x = Options_GetFloat("benchmark-x", -4096, 4096, update.pos.x);
		update.pos.y = Options_GetFloat("benchmark-y", -4096, 4096, update.pos.y);
		update.pos.z = Options_GetFloat("benchmark-z", -4096, 4096, update.pos.z);
		update.yaw   = Options_GetFloat("benchmark-yaw", 0, 360, update.yaw);
		update.pitch = Options_GetFloat("benchmark-pitch", -90, 90, update.pitch);
	}
	if (Benchmark_Mode == 2) {
		/* One 20-second orbit, repeated during warmup so streaming also warms up. */
		angle = (pathElapsed - warmup) * (2.0f * MATH_PI / 20.0f);
		radius = min(World.Width * 0.3f, 96.0f);
		update.pos.x = World.Width  * 0.5f + Math_SinF(angle) * radius;
		update.pos.z = World.Length * 0.5f - Math_CosF(angle) * radius;
		update.pos.y = World.Height + 6.0f;
		update.yaw   = Math_ClampAngle(angle * MATH_RAD2DEG + 180.0f);
		update.pitch = 35.0f;
	}
	if (Benchmark_Mode == 3) {
		/* About five blocks/second, following terrain without keyboard timing noise. */
		angle = (pathElapsed - warmup) * (2.0f * MATH_PI / 60.0f);
		radius = min(World.Width * 0.25f, 48.0f);
		update.pos.x = World.Width  * 0.5f + Math_SinF(angle) * radius;
		update.pos.z = World.Length * 0.5f - Math_CosF(angle) * radius;
		x = (int)update.pos.x; z = (int)update.pos.z;
		for (y = World.MaxY; y > 0; y--) {
			block = World_SafeGetBlock(x, y, z);
			if (block == BLOCK_LOG || block == BLOCK_LEAVES) continue;
			if (Blocks.Draw[block] == DRAW_OPAQUE || Blocks.IsLiquid[block]) break;
		}
		update.pos.y = y + 1.01f;
		update.yaw   = Math_ClampAngle(angle * MATH_RAD2DEG + 90.0f);
		update.pitch = 10.0f;
	}
	p->Base.VTABLE->SetLocation(&p->Base, &update);
	Vec3_Set(p->Base.Velocity, 0, 0, 0);
}

void Benchmark_Begin(void) {
	Benchmark_Draws = 0;
	Benchmark_BuildUs = 0;
	Benchmark_PacingUs = 0;
	gpuWaitUs = 0;
	Mem_Set(heightVertices, 0, sizeof(heightVertices));
}

void Benchmark_SyncGpu(void) {
	cc_uint64 start;
	if (!Benchmark_Mode || !finishGpu) return;
	start = Stopwatch_Measure();
	finishGpu();
	gpuWaitUs = (int)Stopwatch_ElapsedMicroseconds(start, Stopwatch_Measure());
}

void Benchmark_End(cc_uint64 start, cc_uint64 draw, cc_uint64 submit) {
	cc_uint64 end;
	struct Sample* s;
	int elapsed, i;
	if (!Benchmark_Mode || !loadedAt) return;
	end = Stopwatch_Measure();
	if (Benchmark_Preloading) {
		if (Benchmark_Pending == 0) {
			Benchmark_Preloading = false;
			loadedAt = end;
			Platform_LogConst("BENCH preloading finished");
		}
		previous = end;
		return;
	}
	elapsed = Stopwatch_ElapsedMS(loadedAt, end);
	if (elapsed < warmup * 1000) { previous = end; return; }
	if (count < MAX_SAMPLES && previous) {
		if (Benchmark_Pending > maxPending) maxPending = Benchmark_Pending;
		for (i = 0; i < 64; i++) heightTotals[i] += heightVertices[i];
		s = &samples[count++];
		s->frame    = (int)Stopwatch_ElapsedMicroseconds(previous, end);
		s->logic    = (int)Stopwatch_ElapsedMicroseconds(start, draw);
		s->draw     = (int)Stopwatch_ElapsedMicroseconds(draw, submit);
		s->wait     = gpuWaitUs;
		s->present  = (int)Stopwatch_ElapsedMicroseconds(submit, end) - gpuWaitUs - Benchmark_PacingUs;
		s->builds   = Benchmark_BuildUs;
		s->draws    = Benchmark_Draws;
		s->vertices = Game_Vertices;
		s->distance = Game_ViewDistance;
	}
	previous = end;
	if (elapsed < (warmup + duration) * 1000 && count < MAX_SAMPLES) return;
	printf("BENCH_META mode=%d size=%d seed=%d width=%d height=%d sync=%d samples=%d\n",
		Benchmark_Mode, World.Width, World.Seed, Game.Width, Game.Height, syncGpu, count);
	printf("BENCH_CSV frame_us,logic_us,draw_us,wait_us,present_us,build_us,draws,vertices,distance\n");
	printf("BENCH_READY max_pending=%d last_pending=%d\n", maxPending, Benchmark_Pending);
	printf("BENCH_MESH before_vertices=%d after_vertices=%d\n",Benchmark_MeshBefore,Benchmark_MeshAfter);
	HiZ_LogStats();
	for (i = 0; i < 64; i++) if (heightTotals[i]) printf("BENCH_HEIGHT y=%d submitted_vertices=%.0f\n",i*16,heightTotals[i]/count);
	for (i = 0; i < count; i++) {
		s = &samples[i];
		printf("BENCH_ROW %d,%d,%d,%d,%d,%d,%d,%d,%d\n", s->frame, s->logic,
			s->draw, s->wait, s->present, s->builds, s->draws, s->vertices, s->distance);
	}
	fflush(stdout);
	Game_Running = false;
}
#endif
