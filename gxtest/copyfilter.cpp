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

// Restrict the copy filter values to 0-64, instead of 0-63*3
#define SIMPLE_COPY_FILTER_COEFS
// Only use gamma of 1.0, instead of all values
#define SIMPLE_GAMMA

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

enum class Gamma : u8
{
  Gamma1_0 = 0,
  Gamma1_7 = 1,
  Gamma2_2 = 2,
  Invalid2_2 = 3,  // Behaves the same as Gamma2_2?
};

#ifdef SIMPLE_GAMMA
// For now Dolphin doesn't implement gamma for EFB copies
static const std::array<Gamma, 1> GAMMA_VALUES = { Gamma::Gamma1_0 };
#else
static const std::array<Gamma, 4> GAMMA_VALUES = { Gamma::Gamma1_0, Gamma::Gamma1_7, Gamma::Gamma2_2, Gamma::Invalid2_2 };
#endif

#ifdef SIMPLE_COPY_FILTER_COEFS
#define MAX_COPY_FILTER 64
#else
#define MAX_COPY_FILTER 63*3
#endif
void SetCopyFilter(u8 copy_filter_sum)
{
  // Each field in the copy filter ranges from 0-63, and the middle 3 values
  // all apply to the current row of pixels.  This means that up to 63*3
  // can be used for the current row.  Dolphin currently assumes as well that
  // the sum of all values must not go over 64.
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

std::pair<u8, float> Predict(u8 value, u8 copy_filter_sum, Gamma gamma)
{
  float new_value = value;
  // Apply copy filter
  new_value *= copy_filter_sum;
  new_value /= 64;
  new_value = std::floor(new_value);
  // Convert from [0, 255] (assuming sane copy filter) to [0, 1]
  new_value /= 255;
  // Apply gamma
  switch (gamma)
  {
  case Gamma::Gamma1_0:
    // new_value = std::pow(new_value, 1.0f);
    break;
  case Gamma::Gamma1_7:
    new_value = std::pow(new_value, 1 / 1.7f);
    break;
  case Gamma::Gamma2_2:
  case Gamma::Invalid2_2:
  default:
    new_value = std::pow(new_value, 1 / 2.2f);
    break;
  }
  // Convert back from [0, 1] to [0, 255]
  new_value *= 255;
  return std::make_pair(static_cast<u8>(std::round(std::min(new_value, 255.f))), new_value);
}

u8 GetActualValue(Gamma gamma)
{
  GXTest::CopyToTestBuffer(0, 0, 199, 49, false, static_cast<u8>(gamma));
  CGX_WaitForGpuToFinish();
  return GXTest::ReadTestBuffer(4, 4, 200).r;
}

void CopyFilterTest(u8 value)
{
  START_TEST();

  for (u8 copy_filter_sum = 0; copy_filter_sum <= MAX_COPY_FILTER; copy_filter_sum++)
  {
    SetCopyFilter(copy_filter_sum);
    for (Gamma gamma : GAMMA_VALUES)
    {
      auto expected = Predict(value, copy_filter_sum, gamma);
      u8 actual = GetActualValue(gamma);
      DO_TEST(actual == expected.first, "Predicted wrong value for color %d copy filter %d gamma %d: expected %d (%f), was %d", value, copy_filter_sum, static_cast<u8>(gamma), expected.first, expected.second, actual);
    }
  }

  END_TEST();
}

int main()
{
  network_init();
  WPAD_Init();

  GXTest::Init();

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
