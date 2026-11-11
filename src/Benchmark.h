#ifndef CC_BENCHMARK_H
#define CC_BENCHMARK_H
#include "Core.h"
/* Opt-in Linux/OpenGL diagnostic. Disabled unless benchmark-mode is set. */
#ifdef CC_BUILD_LINUX
extern int Benchmark_Mode, Benchmark_Draws, Benchmark_BuildUs;
void Benchmark_Init(void);
void Benchmark_Position(void);
void Benchmark_Begin(void);
void Benchmark_End(cc_uint64 start, cc_uint64 draw, cc_uint64 submit);
int Benchmark_Seed(int seed);
#else
#define Benchmark_Init()
#define Benchmark_Position()
#define Benchmark_Begin()
#define Benchmark_End(start, draw, submit)
#define Benchmark_Seed(seed) (seed)
#endif
#endif
