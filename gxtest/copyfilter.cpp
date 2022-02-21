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

// Use all copy filter values (0-63*3), instead of only 64
#define FULL_COPY_FILTER_COEFS true
// Use all gamma values, instead of just 1.0 (0)
#define FULL_GAMMA true
// Use all pixel formats, instead of just the ones that work
#define FULL_PIXEL_FORMATS false

static void FillEFB(PixelFormat pixel_fmt)
{
  PEControl ctrl;
  ctrl.hex = BPMEM_ZCOMPARE << 24;
  ctrl.pixel_format = pixel_fmt;
  ctrl.zformat = DepthFormat::ZLINEAR;
  ctrl.early_ztest = false;
  CGX_LOAD_BP_REG(ctrl.hex);
  CGX_WaitForGpuToFinish();

  // Needed for clear to work properly. GX_CopyTex ors with 0xf, but the top bit indicating update also must be set
  CGX_LOAD_BP_REG(BPMEM_ZMODE << 24 | 0x1f);
  CGX_LOAD_BP_REG(BPMEM_CLEAR_Z << 24 | 0x123456);
  GXTest::CopyToTestBuffer(0, 0, 255, 3, {.clear = true});
  CGX_WaitForGpuToFinish();

  GX_PokeDither(false);
  GX_PokeAlphaUpdate(true);
  GX_PokeColorUpdate(true);
  GX_PokeBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_SET);
  GX_PokeAlphaRead(GX_READ_NONE);
  GX_PokeZMode(false, GX_ALWAYS, true);

  // For some reason GX_PokeARGB hangs when using this format
  if (pixel_fmt == PixelFormat::RGB565_Z16)
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
      //GX_PokeZ(x, y, x);
    }
  }
}

#if FULL_GAMMA
static const std::array<GammaCorrection, 4> GAMMA_VALUES = { GammaCorrection::Gamma1_0, GammaCorrection::Gamma1_7, GammaCorrection::Gamma2_2, GammaCorrection::Invalid2_2 };
#else
static const std::array<GammaCorrection, 1> GAMMA_VALUES = { GammaCorrection::Gamma1_0 };
#endif

#if FULL_PIXEL_FORMATS
// static const std::array<PixelFormat, 3> PIXEL_FORMATS = { PixelFormat::RGB8_Z24, PixelFormat::RGBA6_Z24, PixelFormat::RGB565_Z16 };
static const std::array<PixelFormat, 8> PIXEL_FORMATS = { PixelFormat::RGB8_Z24, PixelFormat::RGBA6_Z24, PixelFormat::RGB565_Z16, PixelFormat::Z24, PixelFormat::Y8, PixelFormat::U8, PixelFormat::V8, PixelFormat::YUV420 };
#else
// These formats work, though I don't know why Y8 and YUV420 do
//static const std::array<PixelFormat, 5> PIXEL_FORMATS = { PixelFormat::RGB8_Z24, PixelFormat::RGBA6_Z24, PixelFormat::Y8, PixelFormat::V8, PixelFormat::YUV420 };
static const std::array<PixelFormat, 2> PIXEL_FORMATS = { PixelFormat::RGB8_Z24, PixelFormat::Z24 };
#endif

#define MAX_COPY_FILTER 63*3
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

GXTest::Vec4<u8> PredictEfbColor(u8 x, PixelFormat pixel_fmt, bool efb_peek = false)
{
  const u8 sixbit = static_cast<u8>((x & 0xfc) | ((x & 0xc0) >> 6));
  const u8 fivebit = static_cast<u8>((x & 0xf8) | ((x & 0xe0) >> 5));
  switch (pixel_fmt)
  {
  case PixelFormat::RGB8_Z24:
  //case PixelFormat::Y8:
  case PixelFormat::YUV420:
  default:
    return {x, x, x, 255};
  case PixelFormat::RGBA6_Z24:
    return {sixbit, sixbit, sixbit, sixbit};
  case PixelFormat::RGB565_Z16:
    // Does not work
    return {fivebit, sixbit, fivebit, 255};
  case PixelFormat::Y8:
    // This gives correct results for texture copies...
    if (!efb_peek)
    {
      return {x, x, x, 255};
    }
    else
    {
      // But this is the logic behind peeks?
      if (x <= 1)
        return {0, 0, 0, 255};
      else
        return {255, 255, 255, 255};
    }
  case PixelFormat::U8:
    if (efb_peek)
    {
      // This only works for EFB peeks
      if (x <= 1)
        return {0, 0, 0, 255};
      else if (x & 1)
        return {255, 255, 255, 255};
      else
      {
        /*
        switch (x & 0xc0)
        {
        case 0x00: return (x & 2) ? 44 : 12;
        case 0x40: return (x & 2) ? 109 : 77;
        case 0x80: return (x & 2) ? 174 : 142;
        case 0xc0: return (x & 2) ? 239 : 207;
        }
        */
        u8 value = 12 + 65 * ((x & 0xc0) >> 6) + 32 * ((x & 2) >> 1);
        return {value, value, value, 255};
      }
    }
    else
    {
      // Dunno
      return {0, 0, 0, 255};
    }
  case PixelFormat::V8:
    // This works but makes no sense
    if (x & 1)
      return {x, x, x, 255};
    else
      return {fivebit, fivebit, fivebit, 255};
  case PixelFormat::Z24:
    // Does not work
    return {0x12, 0x34, 0x56, 255};
  }
}

u8 Predict(u8 value, u8 copy_filter_sum, GammaCorrection gamma)
{
  // Apply copy filter
  u32 prediction_i = static_cast<u32>(value) * static_cast<u32>(copy_filter_sum);
  prediction_i >>= 6;  // Divide by 64
  // The clamping seems to happen in the range[0, 511]; if the value is outside
  // an overflow will still occur.  This happens if copy_filter_sum >= 128.
  prediction_i &= 0x1ffu;
  prediction_i = std::min(prediction_i, 0xffu);
  // Apply gamma
  if (gamma != GammaCorrection::Gamma1_0)
  {
    // Convert from [0-255] to [0-1]
    float prediction_f = static_cast<float>(prediction_i) / 255.f;
    switch (gamma)
    {
    case GammaCorrection::Gamma1_7:
      prediction_f = std::pow(prediction_f, 1 / 1.7f);
      break;
    case GammaCorrection::Gamma2_2:
    case GammaCorrection::Invalid2_2:
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

GXTest::Vec4<u8> Predict(GXTest::Vec4<u8> efb_color, u8 copy_filter_sum, GammaCorrection gamma, bool intensity)
{
  const u8 r = Predict(efb_color.r, copy_filter_sum, gamma);
  const u8 g = Predict(efb_color.g, copy_filter_sum, gamma);
  const u8 b = Predict(efb_color.b, copy_filter_sum, gamma);
  const u8 a = efb_color.a;  // Copy filter doesn't apply to alpha
  if (intensity)
  {
    // BT.601 conversion
    const u8 y = static_cast<u8>(std::round(( 66 * r + 129 * g +  25 * b) / 256.0 + 16));
    const u8 u = static_cast<u8>(std::round((-38 * r + -74 * g + 112 * b) / 256.0 + 128));
    const u8 v = static_cast<u8>(std::round((112 * r + -94 * g + -18 * b) / 256.0 + 128));
    return { y, u, v, a };
  }
  else
  {
    return { r, g, b, a };
  }
}

void CopyFilterTest(PixelFormat pixel_fmt, u8 copy_filter_sum, GammaCorrection gamma, bool intensity)
{
  START_TEST();

  GXTest::CopyToTestBuffer(0, 0, 255, 2, {.gamma = gamma, .intensity_fmt = intensity, .auto_conv = intensity});
  CGX_WaitForGpuToFinish();

  for (u32 x = 0; x < 256; x++)
  {
    // Reduce bit depth based on the format
    GXTest::Vec4<u8> efb_color = PredictEfbColor(static_cast<u8>(x), pixel_fmt);
    // Make predictions based on the copy filter and gamma
    GXTest::Vec4<u8> expected = Predict(efb_color, copy_filter_sum, gamma, intensity);
    GXTest::Vec4<u8> actual = GXTest::ReadTestBuffer(x, 1, 256);
    DO_TEST(actual.r == expected.r, "Predicted wrong red   value for x %d pixel format %d copy filter %d gamma %d intensity %d: expected %d from %d, was %d", x, u32(pixel_fmt), copy_filter_sum, u32(gamma), u32(intensity), expected.r, efb_color.r, actual.r);
    DO_TEST(actual.g == expected.g, "Predicted wrong green value for x %d pixel format %d copy filter %d gamma %d intensity %d: expected %d from %d, was %d", x, u32(pixel_fmt), copy_filter_sum, u32(gamma), u32(intensity), expected.g, efb_color.g, actual.g);
    DO_TEST(actual.b == expected.b, "Predicted wrong blue  value for x %d pixel format %d copy filter %d gamma %d intensity %d: expected %d from %d, was %d", x, u32(pixel_fmt), copy_filter_sum, u32(gamma), u32(intensity), expected.b, efb_color.b, actual.b);
    DO_TEST(actual.a == expected.a, "Predicted wrong alpha value for x %d pixel format %d copy filter %d gamma %d intensity %d: expected %d from %d, was %d", x, u32(pixel_fmt), copy_filter_sum, u32(gamma), u32(intensity), expected.a, efb_color.a, actual.a);
  }

  END_TEST();
}

void CheckEFB(PixelFormat pixel_fmt)
{
  // For some reason GX_PokeARGB hangs when using this format
  if (pixel_fmt == PixelFormat::RGB565_Z16)
    return;

  START_TEST();

  if (pixel_fmt != PixelFormat::Z24)
  {
    for (u16 x = 0; x < 256; x++)
    {
      GXColor actual;
      GX_PeekARGB(x, 1, &actual);
      GXTest::Vec4<u8> expected = PredictEfbColor(static_cast<u8>(x), pixel_fmt, true);

      DO_TEST(actual.r == expected.r, "Predicted wrong red   value for x %d pixel format %d using peeks: expected %d, was %d", x, u32(pixel_fmt), expected.r, actual.r);
      DO_TEST(actual.g == expected.g, "Predicted wrong green value for x %d pixel format %d using peeks: expected %d, was %d", x, u32(pixel_fmt), expected.g, actual.g);
      DO_TEST(actual.b == expected.b, "Predicted wrong blue  value for x %d pixel format %d using peeks: expected %d, was %d", x, u32(pixel_fmt), expected.b, actual.b);
      DO_TEST(actual.a == expected.a, "Predicted wrong alpha value for x %d pixel format %d using peeks: expected %d, was %d", x, u32(pixel_fmt), expected.a, actual.a);
    }
  }
  else
  {
    for (u16 x = 0; x < 256; x++)
    {
      u32 actual;
      GX_PeekZ(x, 1, &actual);
      u32 expected = 0x123456;

      DO_TEST(actual == expected, "Predicted wrong z value for x %d pixel format %d using peeks: expected %d, was %d", x, u32(pixel_fmt), expected, actual);
    }
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

  for (PixelFormat pixel_fmt : PIXEL_FORMATS)
  {
    FillEFB(pixel_fmt);
    CheckEFB(pixel_fmt);

#if FULL_COPY_FILTER_COEFS
    for (u8 copy_filter_sum = 0; copy_filter_sum <= MAX_COPY_FILTER; copy_filter_sum++)
#else
    const u8 copy_filter_sum = 64;
#endif
    {
      SetCopyFilter(copy_filter_sum);
      for (GammaCorrection gamma : GAMMA_VALUES)
      {
        CopyFilterTest(pixel_fmt, copy_filter_sum, gamma, false);
        CopyFilterTest(pixel_fmt, copy_filter_sum, gamma, true);

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
