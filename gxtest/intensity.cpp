// Copyright 2022 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cmath>

#include <ogcsys.h>
#include <wiiuse/wpad.h>
#include "common/hwtests.h"
#include "gxtest/cgx.h"
#include "gxtest/cgx_defaults.h"
#include "gxtest/util.h"

static void FillEFB(u8 blue)
{
  PE_CONTROL ctrl;
  ctrl.hex = BPMEM_ZCOMPARE << 24;
  ctrl.pixel_format = PIXELFMT_RGB8_Z24;
  ctrl.zformat = ZC_LINEAR;
  ctrl.early_ztest = 0;
  CGX_LOAD_BP_REG(ctrl.hex);
  CGX_WaitForGpuToFinish();

  GX_PokeDither(false);
  GX_PokeAlphaUpdate(true);
  GX_PokeColorUpdate(true);
  GX_PokeBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_SET);
  GX_PokeZMode(false, GX_ALWAYS, true);

  for (u16 x = 0; x < 256; x++)
  {
    for (u16 y = 0; y < 256; y++)
    {
      GXColor color;
      color.r = static_cast<u8>(x);
      color.g = static_cast<u8>(y);
      color.b = blue;
      color.a = 255;
      GX_PokeARGB(x, y, color);
    }
  }
}

GXTest::Vec4<u8> GetIntensityColor(u8 r, u8 g, u8 b, u8 a)
{
  // BT.601 conversion
  const u8 y = static_cast<u8>(std::round( 0.257f * r +  0.504f * g +  0.098f * b + 16));
  const u8 u = static_cast<u8>(std::round(-0.148f * r + -0.291f * g +  0.439f * b + 128));
  const u8 v = static_cast<u8>(std::round( 0.439f * r + -0.368f * g + -0.071f * b + 128));
  return { y, u, v, a };
}

void IntensityTest(u8 blue)
{
  START_TEST();

  GXTest::CopyToTestBuffer(0, 0, 255, 255, false, GAMMA_1_0, false);
  CGX_WaitForGpuToFinish();
  // First do a sanity-check to make sure that the EFB contains the expected RGB values

  for (u32 x = 0; x < 256; x++)
  {
    for (u32 y = 0; y < 256; y++)
    {
      GXTest::Vec4<u8> actual = GXTest::ReadTestBuffer(x, y, 256);
      GXTest::Vec4<u8> expected = { static_cast<u8>(x), static_cast<u8>(y), blue, 255 };
      DO_TEST(actual.r == expected.r, "EFB has wrong red   value for x %d y %d blue %d: expected %d, was %d", x, y, blue, expected.r, actual.r);
      DO_TEST(actual.g == expected.g, "EFB has wrong green value for x %d y %d blue %d: expected %d, was %d", x, y, blue, expected.g, actual.g);
      DO_TEST(actual.b == expected.b, "EFB has wrong blue  value for x %d y %d blue %d: expected %d, was %d", x, y, blue, expected.b, actual.b);
      DO_TEST(actual.a == expected.a, "EFB has wrong alpha value for x %d y %d blue %d: expected %d, was %d", x, y, blue, expected.a, actual.a);
    }
  }

  // Now do an intensity-format copy
  GXTest::CopyToTestBuffer(0, 0, 255, 255, false, GAMMA_1_0, true);
  CGX_WaitForGpuToFinish();

  for (u32 x = 0; x < 256; x++)
  {
    for (u32 y = 0; y < 256; y++)
    {
      GXTest::Vec4<u8> actual = GXTest::ReadTestBuffer(x, y, 256);
      GXTest::Vec4<u8> expected = GetIntensityColor(x, y, blue, 255);
      DO_TEST(actual.r == expected.r, "Got wrong y value for x %d y %d blue %d: expected %d, was %d", x, y, blue, expected.r, actual.r);
      DO_TEST(actual.g == expected.g, "Got wrong u value for x %d y %d blue %d: expected %d, was %d", x, y, blue, expected.g, actual.g);
      DO_TEST(actual.b == expected.b, "Got wrong v value for x %d y %d blue %d: expected %d, was %d", x, y, blue, expected.b, actual.b);
      DO_TEST(actual.a == expected.a, "Got wrong a value for x %d y %d blue %d: expected %d, was %d", x, y, blue, expected.a, actual.a);
    }
  }

  END_TEST();
}

int main()
{
  network_init();
  WPAD_Init();

  GXTest::Init();

  for (u32 blue = 0; blue < 256; blue++)
  {
    FillEFB(blue);
    IntensityTest(blue);

    WPAD_ScanPads();
    if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
      break;
  }

  report_test_results();
  network_printf("Shutting down...\n");
  network_shutdown();

  return 0;
}
