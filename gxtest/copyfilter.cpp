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

static void FillEFB(u8 a, u8 r, u8 g, u8 b)
{
  PE_CONTROL ctrl;
  ctrl.hex = BPMEM_ZCOMPARE << 24;
  ctrl.pixel_format = PIXELFMT_RGB8_Z24;
  ctrl.zformat = ZC_LINEAR;
  ctrl.early_ztest = 0;
  CGX_LOAD_BP_REG(ctrl.hex);

  CGX_LOAD_BP_REG(BPMEM_CLEAR_AR << 24 | u32(a) << 8 | u32(r));
  CGX_LOAD_BP_REG(BPMEM_CLEAR_GB << 24 | u32(g) << 8 | u32(b));

  GXTest::CopyToTestBuffer(0, 0, 199, 49, true);
}

#if FULL_GAMMA
static const std::array<u32, 4> GAMMA_VALUES = { GAMMA_1_0, GAMMA_1_7, GAMMA_2_2, GAMMA_INVALID_2_2 };
#else
static const std::array<u32, 1> GAMMA_VALUES = { GAMMA_1_0 };
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

u8 GetActualValue(u32 gamma)
{
  GXTest::CopyToTestBuffer(0, 0, 199, 49, false, gamma);
  CGX_WaitForGpuToFinish();
  return GXTest::ReadTestBuffer(4, 4, 200).r;
}

void CopyFilterTest(u8 value)
{
  START_TEST();

  for (u8 copy_filter_sum = 0; copy_filter_sum <= MAX_COPY_FILTER; copy_filter_sum++)
  {
    SetCopyFilter(copy_filter_sum);
    for (u32 gamma : GAMMA_VALUES)
    {
      u8 expected = Predict(value, copy_filter_sum, gamma);
      u8 actual = GetActualValue(gamma);
      DO_TEST(actual == expected, "Predicted wrong value for color %d copy filter %d gamma %d: expected %d, was %d", value, copy_filter_sum, gamma, expected, actual);
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

  for (u32 i = 0; i < 256; i++)
  {
    FillEFB(0, i, 0, 0);
    CopyFilterTest(static_cast<u8>(i));

    WPAD_ScanPads();
    if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
      break;
  }

  network_printf("Shutting down...\n");
  network_shutdown();

  return 0;
}
