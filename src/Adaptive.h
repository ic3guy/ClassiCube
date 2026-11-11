#ifndef CC_ADAPTIVE_H
#define CC_ADAPTIVE_H
#include "Core.h"
/* Optional frame-time controller. Zero preserves the user's full view distance. */
extern int Adaptive_MaxDistance;
void Adaptive_Init(void);
void Adaptive_Update(cc_uint64 frameStart, int pacingUs);
#endif
