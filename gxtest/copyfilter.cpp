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

// Use all copy filter values (0-63*3), instead of just 0-64
#define FULL_COPY_FILTER_COEFS true
// Use all gamma values, instead of just 1.0 (0)
#define FULL_GAMMA true
// Use all pixel formats, instead of just the ones that work
#define FULL_PIXEL_FORMATS false

static void FillEFB(u32 pixel_fmt)
{
  PE_CONTROL ctrl;
  ctrl.hex = BPMEM_ZCOMPARE << 24;
  ctrl.pixel_format = pixel_fmt;
  ctrl.zformat = ZC_LINEAR;
  ctrl.early_ztest = 0;
  CGX_LOAD_BP_REG(ctrl.hex);
  CGX_WaitForGpuToFinish();

  GX_PokeDither(false);
  GX_PokeAlphaUpdate(true);
  GX_PokeColorUpdate(true);
  GX_PokeBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_SET);
  GX_PokeZMode(false, GX_ALWAYS, true);

  // For some reason GX_PokeARGB hangs when using this format
  if (pixel_fmt == PIXELFMT_RGB565_Z16)
    return;

  for (u16 x = 0; x < 256; x++)
  {
    for (u16 y = 0; y < 3; y++)
    {
      // Fill the EFB with a gradient where all components = x
      GXColor color;
      color.r = static_cast<u8>(x);
      color.g = static_cast<u8>(x);
      color.b = static_cast<u8>(x);
      color.a = static_cast<u8>(x);
      GX_PokeARGB(x, y, color);
      GX_PokeZ(x, y, x);
    }
  }
}

#if FULL_GAMMA
static const std::array<u32, 4> GAMMA_VALUES = { GAMMA_1_0, GAMMA_1_7, GAMMA_2_2, GAMMA_INVALID_2_2 };
#else
static const std::array<u32, 1> GAMMA_VALUES = { GAMMA_1_0 };
#endif

#if FULL_PIXEL_FORMATS
// static const std::array<u32, 3> PIXEL_FORMATS = { PIXELFMT_RGB8_Z24, PIXELFMT_RGBA6_Z24, PIXELFMT_RGB565_Z16 };
static const std::array<u32, 8> PIXEL_FORMATS = { PIXELFMT_RGB8_Z24, PIXELFMT_RGBA6_Z24, PIXELFMT_RGB565_Z16, PIXELFMT_Z24, PIXELFMT_Y8, PIXELFMT_U8, PIXELFMT_V8, PIXELFMT_YUV420 };
#else
// These formats work, though I don't know why Y8 and YUV420 do
static const std::array<u32, 4> PIXEL_FORMATS = { PIXELFMT_RGB8_Z24, PIXELFMT_RGBA6_Z24, PIXELFMT_Y8, PIXELFMT_YUV420 };
#endif

#if FULL_COPY_FILTER_COEFS
#define MAX_COPY_FILTER 63*3
#else
#define MAX_COPY_FILTER 64
#endif
void SetCopyFilter(u8 copy_filter_sum)
{
  // Each field in the copy filter ranges from 0-63, and the middle 3 values
  // all apply to the current row of pixels.  This means that up to 63*3
  // can be used for the current row.
  // We currently ignore the case of copy_filter_sum >= MAX_COPY_FILTER.

  u8 w4 = std::min<u8>(copy_filter_sum, 63);
  u8 w3 = 0;
  if (copy_filter_sum > 63)
    w3 = std::min<u8>(copy_filter_sum - 63, 63);
  u8 w5 = 0;
  if (copy_filter_sum > 63*2)
    w5 = std::min<u8>(copy_filter_sum - 63 * 2, 63);

  u32 copy_filter_reg_0 = u32(w3) << 12 | u32(w4) << 18;
  u32 copy_filter_reg_1 = u32(w5);

  CGX_LOAD_BP_REG(BPMEM_COPYFILTER0 << 24 | copy_filter_reg_0);
  CGX_LOAD_BP_REG(BPMEM_COPYFILTER1 << 24 | copy_filter_reg_1);
}

GXTest::Vec4<u8> GetEfbColor(u8 x, u32 pixel_fmt)
{
  // const u8 sixbit = static_cast<u8>(((x & 0xfc) * 255) / 0xfc);
  // const u8 fivebit = static_cast<u8>(((x & 0xf8) * 255) / 0xf8);
  const u8 sixbit = static_cast<u8>((x & 0xfc) | ((x & 0xc0) >> 6));
  const u8 fivebit = static_cast<u8>((x & 0xf8) | ((x & 0xe0) >> 5));
  switch (pixel_fmt)
  {
  case PIXELFMT_RGB8_Z24:
  case PIXELFMT_Y8:
  case PIXELFMT_YUV420:
  default:
    return {x, x, x, 255};
  case PIXELFMT_RGBA6_Z24:
    return {sixbit, sixbit, sixbit, sixbit};
  case PIXELFMT_RGB565_Z16:
    // Does not work
    return {fivebit, sixbit, fivebit, 255};
  case PIXELFMT_U8:
  case PIXELFMT_V8:
    // Does not work
    return {fivebit, fivebit, fivebit, 255};
  case PIXELFMT_Z24:
    // Does not work
    return {x, 0, 0, 255};
  }
}

u8 Predict(u8 value, u8 copy_filter_sum, u32 gamma)
{
  // Apply copy filter
  u32 prediction_i = static_cast<u32>(value) * static_cast<u32>(copy_filter_sum);
  prediction_i >>= 6;  // Divide by 64
  // The clamping seems to happen in the range[0, 511]; if the value is outside
  // an overflow will still occur.  This happens if copy_filter_sum >= 128.
  prediction_i &= 0x1ffu;
  prediction_i = std::min(prediction_i, 0xffu);
  // Apply gamma
  if (gamma != GAMMA_1_0)
  {
    // Convert from [0-255] to [0-1]
    float prediction_f = static_cast<float>(prediction_i) / 255.f;
    switch (gamma)
    {
    case GAMMA_1_7:
      prediction_f = std::pow(prediction_f, 1 / 1.7f);
      break;
    case GAMMA_2_2:
    case GAMMA_INVALID_2_2:
    default:
      prediction_f = std::pow(prediction_f, 1 / 2.2f);
      break;
    }
    // Due to how exponentials work, std::pow will always map from [0, 1] to [0, 1],
    // so no overflow can occur here.  (pow is continuous, 0^x is 0 for x > 0,
    // and 1^x is 1, so y in [0, 1] has y^x in [0, 1])
    // Convert back from [0, 1] to [0, 255]
    prediction_i = static_cast<u32>(std::round(prediction_f * 255.f));
  }
  return static_cast<u8>(prediction_i);
}

void CopyFilterTest(u32 pixel_fmt, u8 copy_filter_sum, u32 gamma)
{
  START_TEST();

  GXTest::CopyToTestBuffer(0, 0, 255, 2, false, gamma);
  CGX_WaitForGpuToFinish();

  for (u32 x = 0; x < 256; x++)
  {
    // Reduce bit depth based on the format
    GXTest::Vec4<u8> efb_color = GetEfbColor(static_cast<u8>(x), pixel_fmt);
    // Make predictions based on the copy filter and gamma
    u8 expected_r = Predict(efb_color.r, copy_filter_sum, gamma);
    u8 expected_g = Predict(efb_color.g, copy_filter_sum, gamma);
    u8 expected_b = Predict(efb_color.b, copy_filter_sum, gamma);
    u8 expected_a = efb_color.a;  // Copy filter doesn't apply to alpha
    GXTest::Vec4<u8> actual = GXTest::ReadTestBuffer(x, 1, 256);
    DO_TEST(actual.r == expected_r, "Predicted wrong red   value for x %d pixel format %d copy filter %d gamma %d: expected %d from %d, was %d", x, pixel_fmt, copy_filter_sum, gamma, expected_r, efb_color.r, actual.r);
    DO_TEST(actual.g == expected_g, "Predicted wrong green value for x %d pixel format %d copy filter %d gamma %d: expected %d from %d, was %d", x, pixel_fmt, copy_filter_sum, gamma, expected_g, efb_color.g, actual.g);
    DO_TEST(actual.b == expected_b, "Predicted wrong blue  value for x %d pixel format %d copy filter %d gamma %d: expected %d from %d, was %d", x, pixel_fmt, copy_filter_sum, gamma, expected_b, efb_color.b, actual.b);
    DO_TEST(actual.a == expected_a, "Predicted wrong alpha value for x %d pixel format %d copy filter %d gamma %d: expected %d from %d, was %d", x, pixel_fmt, copy_filter_sum, gamma, expected_a, efb_color.a, actual.a);
  }

  END_TEST();
}

int main()
{
  network_init();
  WPAD_Init();

  GXTest::Init();
  network_printf("FULL_COPY_FILTER_COEFS: %s\n", FULL_COPY_FILTER_COEFS ? "true" : "false");
  network_printf("FULL_GAMMA: %s\n", FULL_GAMMA ? "true" : "false");
  network_printf("FULL_PIXEL_FORMATS: %s\n", FULL_PIXEL_FORMATS ? "true" : "false");

  for (u32 pixel_fmt : PIXEL_FORMATS)
  {
    FillEFB(pixel_fmt);
    for (u8 copy_filter_sum = 0; copy_filter_sum <= MAX_COPY_FILTER; copy_filter_sum++)
    {
      SetCopyFilter(copy_filter_sum);
      for (u32 gamma : GAMMA_VALUES)
      {
        CopyFilterTest(pixel_fmt, copy_filter_sum, gamma);

        WPAD_ScanPads();
        if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
          goto done;
      }
    }
  }
done:

  report_test_results();
  network_printf("Shutting down...\n");
  network_shutdown();

  return 0;
}
