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
  PEControl ctrl;
  ctrl.hex = BPMEM_ZCOMPARE << 24;
  ctrl.pixel_format = PixelFormat::RGB8_Z24;
  ctrl.zformat = DepthFormat::ZLINEAR;
  ctrl.early_ztest = false;
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
  const u8 y = static_cast<u8>(std::round(( 66 * r + 129 * g +  25 * b) / 256.0 + 16));
  const u8 u = static_cast<u8>(std::round((-38 * r + -74 * g + 112 * b) / 256.0 + 128));
  const u8 v = static_cast<u8>(std::round((112 * r + -94 * g + -18 * b) / 256.0 + 128));
  return { y, u, v, a };
}

void IntensityTest(u8 blue, bool yuv, bool intensity_fmt, bool auto_conv)
{
  START_TEST();

  GXTest::CopyToTestBuffer(0, 0, 255, 255, {.yuv = yuv, .intensity_fmt = intensity_fmt, .auto_conv = auto_conv});
  CGX_WaitForGpuToFinish();

  for (u32 x = 0; x < 256; x++)
  {
    for (u32 y = 0; y < 256; y++)
    {
      GXTest::Vec4<u8> actual = GXTest::ReadTestBuffer(x, y, 256);
      bool actually_is_intensity = intensity_fmt && auto_conv;
      GXTest::Vec4<u8> expected = actually_is_intensity ? GetIntensityColor(x, y, blue, 255) : GXTest::Vec4<u8>{static_cast<u8>(x), static_cast<u8>(y), blue, 255};
      DO_TEST(actual.r == expected.r, "Got wrong red   / y value for x %d y %d blue %d, %d %d %d: expected %d, was %d", x, y, blue, yuv, intensity_fmt, auto_conv, expected.r, actual.r);
      DO_TEST(actual.g == expected.g, "Got wrong green / u value for x %d y %d blue %d, %d %d %d: expected %d, was %d", x, y, blue, yuv, intensity_fmt, auto_conv, expected.g, actual.g);
      DO_TEST(actual.b == expected.b, "Got wrong blue  / v value for x %d y %d blue %d, %d %d %d: expected %d, was %d", x, y, blue, yuv, intensity_fmt, auto_conv, expected.b, actual.b);
      DO_TEST(actual.a == expected.a, "Got wrong alpha     value for x %d y %d blue %d, %d %d %d: expected %d, was %d", x, y, blue, yuv, intensity_fmt, auto_conv, expected.a, actual.a);
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
    for (u32 counter = 0; counter < 8; counter++)
    {
      const bool yuv = (counter & 1);
      const bool intensity_fmt = (counter & 2);
      const bool auto_conv = (counter & 4);
      IntensityTest(blue, yuv, intensity_fmt, auto_conv);

      WPAD_ScanPads();
      if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
        break;
    }
  }

  report_test_results();
  network_printf("Shutting down...\n");
  network_shutdown();

  return 0;
}
