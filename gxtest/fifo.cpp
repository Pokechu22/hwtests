// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <initializer_list>
#include <math.h>
#include <ogcsys.h>
#include <stdlib.h>
#include <string.h>
#include <wiiuse/wpad.h>
#include "common/hwtests.h"
#include "gxtest/cgx.h"
#include "gxtest/cgx_defaults.h"
#include "gxtest/util.h"

static vu32* const _piReg = (u32*)0xCC003000;
static vu16* const _peReg = (u16*)0xCC001000;
#define PI_FIFO_RESET _piReg[6]

#include <ogc/lwp_watchdog.h> // For diff_ticks and gettime
#include <ogc/machine/processor.h>

static void SleepTicks(u32 delay) {
  // Based on __GX_WaitAbort
  u64 start = gettime();
  u64 end;
  do
  {
    end = gettime();
  } while (diff_ticks(start, end) < (u64)delay);
}

extern void* gp_fifo;

static void FifoReset()
{
  /*
  u32 level;
  _CPU_ISR_Disable(level);  // Not part of GX_AbortFrame
  network_printf("Test 1\n");
  // Based on GX_AbortFrame, but we don't actually do anything with the GP fifo.
  network_printf("Test 2\n");
  PI_FIFO_RESET = 1;
  network_printf("Test 3\n");
  SleepTicks(50);
  network_printf("Test 4\n");
  PI_FIFO_RESET = 0;
  network_printf("Test 5\n");
  SleepTicks(5);
  network_printf("Test 6\n");
  _CPU_ISR_Restore(level);
  */
  u16 old_pe = _peReg[5];
  GX_AbortFrame();
  u16 new_pe = _peReg[5];
  _peReg[5] = old_pe;
  network_printf("%x -> %x -> %x\n", old_pe, new_pe, _peReg[5]);
}

static void SetClearRed(u8 r)
{
  // CGX_LOAD_BP_REG(BPMEM_CLEAR_AR << 24 | r);
  wgPipe->U8 = 0x61;
  wgPipe->U8 = BPMEM_CLEAR_AR;
  wgPipe->U8 = 0;
  wgPipe->U8 = 0;
  wgPipe->U8 = r;
}

static u8 CheckClearRed()
{
  CGX_WaitForGpuToFinish();
  // First, do an EFB copy to clear the buffer with the clear color...
  GXTest::CopyToTestBuffer(0, 0, 199, 49, true);
  // And then do a second one to actually look at that clear color
  GXTest::CopyToTestBuffer(0, 0, 199, 49, false);
  // Flushes pipeline as well as waiting
  CGX_WaitForGpuToFinish();

  //return GXTest::ReadTestBuffer(0, 0, 200).r;
  GXColor color;
  GX_PeekARGB(0, 0, &color);
  return color.r;
}

void FifoTest()
{
  u8 result = 0x40;
  START_TEST();

  PEControl ctrl{.hex = BPMEM_ZCOMPARE << 24};
  ctrl.pixel_format = PixelFormat::RGB8_Z24;
  ctrl.zformat = DepthFormat::ZLINEAR;
  ctrl.early_ztest = false;
  CGX_LOAD_BP_REG(ctrl.hex);

  SetClearRed(4);
  result = CheckClearRed();
  DO_TEST(result == 4, "Initial clear should result in red=4, not {}", result);

  SetClearRed(5);
  result = CheckClearRed();
  DO_TEST(result == 5, "Second clear should result in red=5, not {}", result);

  SetClearRed(6);
  FifoReset();
  result = CheckClearRed();
  DO_TEST(result == 5, "Third clear should not have had color change go through so red=5, not {}", result);

  FifoReset();
  SetClearRed(7);
  result = CheckClearRed();
  DO_TEST(result == 7, "4th clear should have red=7, not {}", result);

  FifoReset();
  SetClearRed(8);  // 5 bytes
  SetClearRed(9);  // 10 bytes
  SetClearRed(10);  // 15 bytes
  SetClearRed(11);  // 20 bytes
  SetClearRed(12);  // 25 bytes
  SetClearRed(13);  // 30 bytes
  SetClearRed(14);  // 35 bytes
  FifoReset();
  result = CheckClearRed();
  DO_TEST(result == 13, "5th clear should have red=13, not {}", result);  // This might be timing dependent

/*
  GX_AbortFrame();  // Clears pipeline
  wgPipe->U32 = 0x18181818;  // Unknown opcode known to cause hangs
  wgPipe->U32 = 0x18181818;  // Unknown opcode known to cause hangs
  GX_AbortFrame();  // Clears pipeline
*/

  result = CheckClearRed();
  DO_TEST(result == 13, "6th clear should have red=13, not {}", result);

  END_TEST();
}

int main()
{
  network_init();
  WPAD_Init();

  GXTest::Init();

  FifoTest();

  network_printf("Shutting down...\n");
  network_shutdown();

  return 0;
}
