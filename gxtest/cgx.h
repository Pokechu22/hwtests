// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

// "Custom GX"
// Replacement library for accessing the GPU
// libogc's GX contains bugs and uses internal state.
// Both of these things are not particularly good for a test suite.
// Hence, this file provides an alternative set of functions, which
// are roughly based on GX, but are supposed to use no internal state.
// They are based directly on Dolphin's register definitions, hence
// (hopefully) minimizing potential for mistakes.

#include <ogc/gx.h>

#include "common/CommonTypes.h"
#include "BPMemory.h"

#pragma once

/*typedef float f32;

typedef union
{
  volatile u8 U8;
  volatile s8 S8;
  volatile u16 U16;
  volatile s16 S16;
  volatile u32 U32;
  volatile s32 S32;
  volatile f32 F32;
} CWGPipe;

static CWGPipe* const wgPipe = (CWGPipe*)0xCC008000;
*/

#define CGX_LOAD_BP_REG(x)                                                                         \
  do                                                                                               \
  {                                                                                                \
    wgPipe->U8 = 0x61;                                                                             \
    wgPipe->U32 = (u32)(x);                                                                        \
  } while (0)

#define CGX_LOAD_CP_REG(x, y)                                                                      \
  do                                                                                               \
  {                                                                                                \
    wgPipe->U8 = 0x08;                                                                             \
    wgPipe->U8 = (u8)(x);                                                                          \
    wgPipe->U32 = (u32)(y);                                                                        \
  } while (0)

#define CGX_BEGIN_LOAD_XF_REGS(x, n)                                                               \
  do                                                                                               \
  {                                                                                                \
    wgPipe->U8 = 0x10;                                                                             \
    wgPipe->U32 = (u32)(((((n)&0xffff) - 1) << 16) | ((x)&0xffff));                                \
  } while (0)

void CGX_Init();

void CGX_SetViewport(float origin_x, float origin_y, float width, float height, float near,
                     f32 far);

void CGX_LoadPosMatrixDirect(f32 mt[3][4], u32 index);
void CGX_LoadProjectionMatrixPerspective(float mtx[4][4]);
void CGX_LoadProjectionMatrixOrthographic(float mtx[4][4]);

struct EFBCopyParams
{
  EFBCopyFormat format = EFBCopyFormat::RGBA8;
  bool clamp_top = true;
  bool clamp_bottom = true;
  bool yuv = false;
  GammaCorrection gamma = GammaCorrection::Gamma1_0;
  bool half_scale = false;
  bool scale_invert = false;
  bool clear = false;
  FrameToField frame_to_field = FrameToField::Progressive;
  bool copy_to_xfb = false;
  bool intensity_fmt = false;
  bool auto_conv = false;
};

void CGX_DoEfbCopyTex(u16 left, u16 top, u16 width, u16 height, void* dest, const EFBCopyParams& params = {});

// TODO: Add support for other parameters...
void CGX_DoEfbCopyXfb(u16 left, u16 top, u16 width, u16 src_height, u16 dst_height, void* dest,
                      bool clear = false);

void CGX_ForcePipelineFlush();

void CGX_WaitForGpuToFinish();
