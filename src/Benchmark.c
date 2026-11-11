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
#include <stdio.h>
#include <stdlib.h>

int Benchmark_Mode, Benchmark_Draws, Benchmark_BuildUs;
static int duration, warmup, count, syncGpu;
static cc_uint64 loadedAt;
static void (*finishGpu)(void);
/* Bounded storage; no console or disk writes during the timed section. */
#define MAX_SAMPLES 30000
struct Sample { int frame, logic, draw, wait, present, builds, draws, vertices, distance; };
static struct Sample samples[MAX_SAMPLES];
static cc_uint64 previous;

void Benchmark_Init(void) {
	Benchmark_Mode = Options_GetInt("benchmark-mode", 0, 2, 0);
	if (!Benchmark_Mode || !Server.IsSinglePlayer) { Benchmark_Mode = 0; return; }
	duration = Options_GetInt("benchmark-seconds", 5, 300, 20);
	warmup   = Options_GetInt("benchmark-warmup", 2, 120, 10);
	syncGpu  = Options_GetBool("benchmark-sync", false);
	if (syncGpu) finishGpu = (void (*)(void))GLContext_GetAddress("glFinish");
	Game_SetFpsLimit(FPS_LIMIT_NONE);
	Platform_LogConst("BENCH armed: fixed seed 12345, mode 1=spawn / 2=flyover");
}

int Benchmark_Seed(int seed) {
	return Options_GetInt("benchmark-mode", 0, 2, 0) ? 12345 : seed;
}

void Benchmark_Position(void) {
	struct LocationUpdate update;
	struct LocalPlayer* p = Entities.CurPlayer;
	float elapsed, angle, radius;
	if (!Benchmark_Mode || !World.Loaded || !World.Blocks) return;
	if (!loadedAt) loadedAt = Stopwatch_Measure();
	elapsed = Stopwatch_ElapsedMS(loadedAt, Stopwatch_Measure()) / 1000.0f;
	update.flags = LU_HAS_POS | LU_HAS_YAW | LU_HAS_PITCH;
	update.pos   = p->Spawn;
	update.yaw   = 45.0f;
	update.pitch = 15.0f;
	if (Benchmark_Mode == 2) {
		/* One 20-second orbit, repeated during warmup so streaming also warms up. */
		angle = (elapsed - warmup) * (2.0f * MATH_PI / 20.0f);
		radius = min(World.Width * 0.3f, 96.0f);
		update.pos.x = World.Width  * 0.5f + Math_SinF(angle) * radius;
		update.pos.z = World.Length * 0.5f - Math_CosF(angle) * radius;
		update.pos.y = World.Height + 6.0f;
		update.yaw   = Math_ClampAngle(angle * MATH_RAD2DEG + 180.0f);
		update.pitch = 35.0f;
	}
	p->Base.VTABLE->SetLocation(&p->Base, &update);
	Vec3_Set(p->Base.Velocity, 0, 0, 0);
}

void Benchmark_Begin(void) {
	Benchmark_Draws = 0;
	Benchmark_BuildUs = 0;
}

void Benchmark_End(cc_uint64 start, cc_uint64 draw, cc_uint64 submit) {
	cc_uint64 now, waited, end;
	struct Sample* s;
	int elapsed, i;
	if (!Benchmark_Mode || !loadedAt) return;
	now = Stopwatch_Measure();
	if (finishGpu) finishGpu();
	waited = Stopwatch_Measure();
	/* Called after presentation: sync wait is residual GPU work, not pure GPU time. */
	end = waited;
	elapsed = Stopwatch_ElapsedMS(loadedAt, end);
	if (elapsed < warmup * 1000) { previous = end; return; }
	if (count < MAX_SAMPLES && previous) {
		s = &samples[count++];
		s->frame    = (int)Stopwatch_ElapsedMicroseconds(previous, end);
		s->logic    = (int)Stopwatch_ElapsedMicroseconds(start, draw);
		s->draw     = (int)Stopwatch_ElapsedMicroseconds(draw, submit);
		s->wait     = (int)Stopwatch_ElapsedMicroseconds(now, waited);
		s->present  = (int)Stopwatch_ElapsedMicroseconds(submit, now);
		s->builds   = Benchmark_BuildUs;
		s->draws    = Benchmark_Draws;
		s->vertices = Game_Vertices;
		s->distance = Game_ViewDistance;
	}
	previous = end;
	if (elapsed < (warmup + duration) * 1000 && count < MAX_SAMPLES) return;
	printf("BENCH_META mode=%d size=%d width=%d height=%d sync=%d samples=%d\n",
		Benchmark_Mode, World.Width, Game.Width, Game.Height, syncGpu, count);
	printf("BENCH_CSV frame_us,logic_us,draw_us,wait_us,present_us,build_us,draws,vertices,distance\n");
	for (i = 0; i < count; i++) {
		s = &samples[i];
		printf("BENCH_ROW %d,%d,%d,%d,%d,%d,%d,%d,%d\n", s->frame, s->logic,
			s->draw, s->wait, s->present, s->builds, s->draws, s->vertices, s->distance);
	}
	fflush(stdout);
	Game_Running = false;
}
#endif
