#ifndef CC_HIZ_H
#define CC_HIZ_H
#include "Core.h"
#ifdef CC_BUILD_LINUX
void HiZ_Begin(void);
void HiZ_End(void);
int HiZ_AddBox(float x, float y, float z, float sx, float sy, float sz);
int HiZ_AddFace(float x, float y, float z, float sx, float sy, float sz, int diagonal);
cc_bool HiZ_HiddenBounds(float x, float y, float z, float sx, float sy, float sz);
cc_bool HiZ_HiddenBox(float x, float y, float z, float size);
#endif
#endif
