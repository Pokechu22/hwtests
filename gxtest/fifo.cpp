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

static void FifoReset()
{
  network_printf("FifoReset()\n");
#if 1
  // Based on GX_AbortFrame, but we don't actually do anything with the GP fifo.
  // This version hangs
  PI_FIFO_RESET = 1;
  SleepTicks(50);
  PI_FIFO_RESET = 0;
  SleepTicks(5);
#else
  // This call works, but other things break
  GX_AbortFrame();  // Note: This does more stuff (including sending some commands) on Wii
#endif
}

static void SetClearRed(u8 r)
{
  network_printf("SetClearRed(%d)\n", r);
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

  u8 result = GXTest::ReadTestBuffer(0, 0, 200).r;
  network_printf("CheckClearRed() => %d\n", result);
  return result;
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
  SetClearRed(8);  // 5 bytes, assuming FifoReset writes nothing (not true when using GX_AbortFrame)
  SetClearRed(9);  // 10 bytes
  SetClearRed(10);  // 15 bytes
  SetClearRed(11);  // 20 bytes
  SetClearRed(12);  // 25 bytes
  SetClearRed(13);  // 30 bytes
  SetClearRed(14);  // 35 bytes
  FifoReset();
  result = CheckClearRed();
  DO_TEST(result == 13, "5th clear should have red=13, not {}", result);  // This might be timing dependent, and also fails when using GX_AbortFrame due to extra data

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
