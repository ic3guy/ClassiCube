#include "Adaptive.h"
#include "Game.h"
#include "World.h"
#include "Gui.h"
#include "Platform.h"
#include "Options.h"
#include "Funcs.h"
#include "ExtMath.h"

int Adaptive_MaxDistance;
static int targetFPS, minDistance, frames, slowFrames, fastWindows;
static float totalMs, totalWorkMs;
static cc_uint64 previousFrame;

void Adaptive_Init(void) {
	targetFPS   = Options_GetInt("gfx-targetfps", 0, 240, 0);
	/* A frame target must not silently shorten the user's chosen view. */
	if (!Options_GetBool("gfx-adaptiveview", false)) targetFPS = 0;
	minDistance = Options_GetInt("gfx-minviewdist", 16, 512, 32);
	totalMs = totalWorkMs = 0;
	frames = slowFrames = fastWindows = 0;
	previousFrame = 0;
	if (!targetFPS) { Adaptive_MaxDistance = 0; return; }
	/* Start with a useful nearby area instead of first loading the whole map. */
	Adaptive_MaxDistance = min(Game_UserViewDistance, max(minDistance, 96));
	Game_SetViewDistance(Game_UserViewDistance);
}

void Adaptive_Update(cc_uint64 frameStart, int pacingUs) {
	float ms, workMs, target, slowThreshold;
	int distance, lower, upper, step, capFPS = 0;
	cc_bool paced;
	if (!targetFPS || !World.Loaded || Gui_GetBlocksWorld()) return;
	workMs = ((int)Stopwatch_ElapsedMicroseconds(frameStart, Stopwatch_Measure()) - pacingUs) / 1000.0f;
	switch (Game_FpsLimit) {
	case FPS_LIMIT_30:  capFPS = 30;  break;
	case FPS_LIMIT_60:  capFPS = 60;  break;
	case FPS_LIMIT_120: capFPS = 120; break;
	case FPS_LIMIT_144: capFPS = 144; break;
	}
	paced = Game_FpsLimit == FPS_LIMIT_VSYNC || (capFPS && capFPS <= targetFPS);
	ms = workMs;
	if (paced && previousFrame) {
		ms = (int)Stopwatch_ElapsedMicroseconds(previousFrame, frameStart) / 1000.0f;
	}
	previousFrame = frameStart;
	/* Loading, suspension, and dialogs are not representative rendering samples. */
	if (ms > 250.0f) { totalMs = totalWorkMs = 0; frames = slowFrames = fastWindows = 0; return; }
	target = 1000.0f / (capFPS && capFPS < targetFPS ? capFPS : targetFPS);
	slowThreshold = paced ? 1.12f : 1.2f;
	totalMs += ms;
	totalWorkMs += workMs;
	frames++;
	if (ms > target * slowThreshold) slowFrames++;
	if (totalMs < (paced ? 1000.0f : 250.0f) || frames < 8) return;

	ms = totalMs / frames;
	workMs = totalWorkMs / frames;
	lower = min(minDistance, Game_UserViewDistance);
	upper = min(Game_UserViewDistance, Game_MaxViewDistance);
	distance = Adaptive_MaxDistance;
	if (ms > target * (paced ? 1.05f : 0.95f) || slowFrames * (paced ? 20 : 10) > frames) {
		/* Reduce quickly; keep spare time for movement and occasional chunk builds. */
		step = ms > target * 1.5f ? 24 : 8;
		distance -= step;
		fastWindows = 0;
	} else if (ms < target * (paced ? 1.03f : 0.72f) && workMs < target * 0.72f && !slowFrames) {
		/* Grow only after sustained headroom, to avoid oscillating around a boundary. */
		if (++fastWindows >= 6) { distance += 8; fastWindows = 0; }
	} else {
		fastWindows = 0;
	}
	Math_Clamp(distance, lower, upper);
	totalMs = totalWorkMs = 0;
	frames = slowFrames = 0;
	if (distance == Adaptive_MaxDistance) return;
	Adaptive_MaxDistance = distance;
	Game_SetViewDistance(Game_UserViewDistance);
}
