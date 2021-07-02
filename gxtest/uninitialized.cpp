// Copyright 2021 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include <ogcsys.h>
#include <string.h>
#include <wiiuse/wpad.h>
#include "common/hwtests.h"
#include "gxtest/cgx.h"
#include "gxtest/cgx_defaults.h"
#include "gxtest/util.h"

static bool s_has_color_0 = false, s_has_color_1 = false;

static void PerformInitalSetup() {
  // Set clear color to #c080c0
  CGX_LOAD_BP_REG(BPMEM_CLEAR_AR << 24 | 0x00c0);
  CGX_LOAD_BP_REG(BPMEM_CLEAR_GB << 24 | 0x80c0);

  CGX_BEGIN_LOAD_XF_REGS(0x1009, 1);
  wgPipe->U32 = 2;  // 2 color channels

  LitChannel chan;
  chan.hex = 0;
  chan.matsource = 1;  // from vertex
  CGX_BEGIN_LOAD_XF_REGS(0x100e, 4);  // color channel 0 and 1, then alpha channel 0 and 1
  wgPipe->U32 = chan.hex;
  wgPipe->U32 = chan.hex;
  wgPipe->U32 = chan.hex;
  wgPipe->U32 = chan.hex;

  CGX_LOAD_BP_REG(CGXDefault<TevStageCombiner::AlphaCombiner>(0).hex);
  // Configure TEV to use the RAS color for the color channel
  // Test cases change TwoTevStageOrders to specify if that comes from vertex color 0 or 1
  auto cc = CGXDefault<TevStageCombiner::ColorCombiner>(0);
  cc.a = TEVCOLORARG_RASC;
  CGX_LOAD_BP_REG(cc.hex);

  auto genmode = CGXDefault<GenMode>();
  genmode.numtevstages = 0;  // One stage
  genmode.numcolchans = 2;  // Two color channels, expected to match XF 0x1009 value
  CGX_LOAD_BP_REG(genmode.hex);

  PE_CONTROL ctrl;
  ctrl.hex = BPMEM_ZCOMPARE << 24;
  ctrl.pixel_format = PIXELFMT_RGB8_Z24;
  ctrl.zformat = ZC_LINEAR;
  ctrl.early_ztest = 0;
  CGX_LOAD_BP_REG(ctrl.hex);

  // Perform an initial clear now that the color format is set
  GXTest::CopyToTestBuffer(0, 0, 31, 31, true);
  CGX_WaitForGpuToFinish();

  /* TODO: Should reset this matrix..
  float mtx[3][4];
  memset(&mtx, 0, sizeof(mtx));
  mtx[0][0] = 1.0;
  mtx[1][1] = 1.0;
  mtx[2][2] = 1.0;
  CGX_LoadPosMatrixDirect(mtx, 0);*/

  float mtx[4][4];
  memset(mtx, 0, sizeof(mtx));
  // Configured to use pixel coordinates
  mtx[0][0] = 1.0f / (GXTest::GetEfbWidth()/2);
  mtx[1][1] = 1.0f / (GXTest::GetEfbHeight()/2);
  mtx[2][2] = -1;
  CGX_LoadProjectionMatrixOrthographic(mtx);
}

static void SetVertexFormat(bool has_color_0, bool has_color_1) {
  s_has_color_0 = has_color_0;
  s_has_color_1 = has_color_1;

  u32 numcolors = 0;

  VAT vtxattr;
  TVtxDesc vtxdesc;
  vtxattr.g0.Hex = 0;
  vtxattr.g1.Hex = 0;
  vtxattr.g2.Hex = 0;
  vtxdesc.Hex = 0;

  vtxattr.g0.PosElements = VA_TYPE_POS_XYZ;
  vtxattr.g0.PosFormat = VA_FMT_F32;
  vtxdesc.Position = VTXATTR_DIRECT;

  if (has_color_0)
  {
    vtxattr.g0.Color0Elements = VA_TYPE_CLR_RGBA;
    vtxattr.g0.Color0Comp = VA_FMT_RGBA8;
    vtxdesc.Color0 = VTXATTR_DIRECT;
    numcolors++;
  }
  if (has_color_1)
  {
    vtxattr.g0.Color1Elements = VA_TYPE_CLR_RGBA;
    vtxattr.g0.Color1Comp = VA_FMT_RGBA8;
    vtxdesc.Color1 = VTXATTR_DIRECT;
    numcolors++;
  }

  // TODO: Figure out what this does and why it needs to be 1 for Dolphin not to error out
  vtxattr.g0.ByteDequant = 1;

  // TODO: Not sure if the order of these two is correct
  CGX_LOAD_CP_REG(0x50, vtxdesc.Hex0);
  CGX_LOAD_CP_REG(0x60, vtxdesc.Hex1);

  CGX_LOAD_CP_REG(0x70, vtxattr.g0.Hex);
  CGX_LOAD_CP_REG(0x80, vtxattr.g1.Hex);
  CGX_LOAD_CP_REG(0x90, vtxattr.g2.Hex);

  // Set the vertex spec, which has the number of color channels, normals, and texture coordinates
  // We have 0-2 colors only
  // Not setting this causes a hang on actual hardware
  CGX_BEGIN_LOAD_XF_REGS(0x1008, 1);
  wgPipe->U32 = numcolors;
}

static void DrawPoint(u32 x, u32 y, u32 color_0, u32 color_1) {
  // x and y are offsets from the top left of the screen

  wgPipe->U8 = 0xB8;  // draw points
  wgPipe->U16 = 1;    // 1 vertex
  wgPipe->F32 = f32(x) - GXTest::GetEfbWidth()/2 + 1;
  wgPipe->F32 = -f32(y) + GXTest::GetEfbHeight()/2 - 1;
  wgPipe->F32 = 1.0f;
  if (s_has_color_0)
    wgPipe->U32 = color_0;
  if (s_has_color_1)
    wgPipe->U32 = color_1;
}

void TestTest()
{
  START_TEST();

  PerformInitalSetup();

  auto tref = CGXDefault<TwoTevStageOrders>(0);
  tref.colorchan0 = 1;  // Color channel 1
  CGX_LOAD_BP_REG(tref.hex);

  network_printf("Confirming pixel drawing works...\n");
  // This test also puts the vertex components into a deterministic state
  for (int i = 0; i < 32; i++)
  {
    SetVertexFormat(true, true);
    DrawPoint(i, i, 0, i << 24);
  }

  GXTest::DebugDisplayEfbContents();
  GXTest::CopyToTestBuffer(0, 0, 31, 31, true);
  CGX_WaitForGpuToFinish();

  for (int x = 0; x < 32; x++)
  {
    for (int y = 0; y < 32; y++)
    {
      GXTest::Vec4<u8> result = GXTest::ReadTestBuffer(x, y, 32);
      if (x == y)
        DO_TEST(result.r == x, "Color was not set at x=%d, y=%d - got %02x, wanted %02x",
                x, y, result.r, x);
      else
        DO_TEST(result.r == 0xc0, "Color should not have been set at x=%d, y=%d - got %02x",
                x, y, result.r);
    }
  }

  END_TEST();
}

void TestUninitSimple()
{
  START_TEST();

  PerformInitalSetup();

  auto tref = CGXDefault<TwoTevStageOrders>(0);
  tref.colorchan0 = 0;  // Color channel 0
  CGX_LOAD_BP_REG(tref.hex);

  network_printf("Simple uninitialized color test...\n");
  // The previous test should have set color 0 to entirely black.
  for (int x = 0; x < 32; x++)
  {
    u32 color = (x * 7) << 24 | 0xffffff;

    // Vertex with uninitialized color at y=0
    SetVertexFormat(false, false);
    DrawPoint(x, 0, color, 0);
    // Vertex with initialized color at y=1
    SetVertexFormat(true, false);
    DrawPoint(x, 1, color, 0);
  }

  GXTest::DebugDisplayEfbContents();
  GXTest::CopyToTestBuffer(0, 0, 31, 31, true);
  CGX_WaitForGpuToFinish();

  for (int x = 0; x < 32; x++)
  {
    u8 expected_color = (x >= 16) ? (x - 16) * 7 : 0;
    GXTest::Vec4<u8> result = GXTest::ReadTestBuffer(x, 0, 32);
    DO_TEST(result.r == expected_color, "Wrong color at x=%d - got %02x, expected %02x",
            x, result.r, expected_color);
  }

  END_TEST();
}

void TestUninitIncrement()
{
  START_TEST();

  PerformInitalSetup();

  auto tref = CGXDefault<TwoTevStageOrders>(0);
  tref.colorchan0 = 0;  // Color channel 0
  CGX_LOAD_BP_REG(tref.hex);

  network_printf("Testing component incrementation...\n");

  // Put everything into a deterministic state
  for (int x = 0; x < 32; x++)
  {
    u32 color_0 = (x * 7) << 24 | 0xff00ff;
    u32 color_1 = (x * 7) << 24 | 0x00ffff;

    SetVertexFormat(true, true);
    DrawPoint(x, 0, color_0, color_1);
  }

  // Only provide color 1, but draw color 0; it should be the same for the whole row
  for (int x = 0; x < 32; x++)
  {
    u32 color_1 = ((x ^ 8) * 7) << 24 | 0x00ffff;

    SetVertexFormat(false, true);
    DrawPoint(x, 1, 0, color_1);
  }

  tref.colorchan0 = 1;  // Color channel 1
  CGX_LOAD_BP_REG(tref.hex);

  // Draw color 1; it should be the same for the whole row
  for (int x = 0; x < 32; x++)
  {
    SetVertexFormat(false, false);
    DrawPoint(x, 2, 0, 0);
  }

  GXTest::DebugDisplayEfbContents();
  GXTest::CopyToTestBuffer(0, 0, 31, 31, true);
  CGX_WaitForGpuToFinish();

  for (int x = 0; x < 32; x++)
  {
    GXTest::Vec4<u8> result = GXTest::ReadTestBuffer(x, 1, 32);
    DO_TEST(result.r == 16*7, "Wrong color at x=%d, y=1 - got %02x, expected %02x",
            x, result.r, 16*7);
    result = GXTest::ReadTestBuffer(x, 2, 32);
    DO_TEST(result.r == (16^8)*7, "Wrong color at x=%d, y=2 - got %02x, expected %02x",
            x, result.r, (16^8)*7);
  }

  END_TEST();
}

void TestUninitSeparate()
{
  START_TEST();

  PerformInitalSetup();

  auto tref = CGXDefault<TwoTevStageOrders>(0);
  tref.colorchan0 = 0;  // Color channel 0
  CGX_LOAD_BP_REG(tref.hex);

  network_printf("Testing component separation...\n");

  int c0 = 0;
  int c1 = 0;
  // Put everything into a deterministic state
  for (int x = 0; x < 32; x++)
  {
    u32 color_0 = ((c0++ * 7) & 0xff) << 24 | 0xff00ff;
    u32 color_1 = ((c1++ * 7) & 0xff) << 24 | 0x00ffff;

    SetVertexFormat(true, true);
    DrawPoint(x, 0, color_0, color_1);
  }

  // Draw vertices without either color 0 or 1, and then alternate providing color 0 or 1
  for (int x = 0; x < 32; x++)
  {
    SetVertexFormat(false, false);

    tref.colorchan0 = 0;  // Color channel 0
    CGX_LOAD_BP_REG(tref.hex);

    DrawPoint(x, 1, 0, 0);

    tref.colorchan0 = 1;  // Color channel 1
    CGX_LOAD_BP_REG(tref.hex);

    DrawPoint(x, 2, 0, 0);

    if ((x & 1) == 0)
    {
      tref.colorchan0 = 0;  // Color channel 0
      CGX_LOAD_BP_REG(tref.hex);

      SetVertexFormat(true, false);
      u32 color_0 = ((c0++ * 7) & 0xff) << 24 | 0xff00ff;
      DrawPoint(x, 3, color_0, 0);
    }
    else
    {
      tref.colorchan0 = 1;  // Color channel 1
      CGX_LOAD_BP_REG(tref.hex);

      SetVertexFormat(false, true);
      u32 color_1 = ((c1++ * 7) & 0xff) << 24 | 0x00ffff;
      DrawPoint(x, 4, 0, color_1);
    }
  }

  GXTest::DebugDisplayEfbContents();
  GXTest::CopyToTestBuffer(0, 0, 31, 31, true);
  CGX_WaitForGpuToFinish();

  c0 = 32;
  c1 = 32;
  for (int x = 0; x < 32; x++)
  {
    GXTest::Vec4<u8> result_0 = GXTest::ReadTestBuffer(x, 1, 32);
    GXTest::Vec4<u8> result_1 = GXTest::ReadTestBuffer(x, 2, 32);

    u8 expected_0 = ((c0 - 16) * 7) & 0xff;
    u8 expected_1 = ((c1 - 16) * 7) & 0xff;

    DO_TEST(result_0.r == expected_0, "Wrong color 0 at x=%d, y=1 - got %02x, expected %02x",
            x, result_0.r, expected_0);
    DO_TEST(result_1.r == expected_1, "Wrong color 1 at x=%d, y=2 - got %02x, expected %02x",
            x, result_1.r, expected_1);

    if ((x & 1) == 0)
      c0++;
    else
      c1++;
  }


  END_TEST();
}

int main()
{
  network_init();
  WPAD_Init();

  GXTest::Init();

  TestTest();
  TestUninitSimple();
  TestUninitIncrement();
  TestUninitSeparate();

  network_printf("Shutting down...\n");
  network_shutdown();

  return 0;
}
