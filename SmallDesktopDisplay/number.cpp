#include "number.h"

/* *****************************************************************
 *  V1.6.6 数字绘制改为 2bpp 掩膜（方案A）
 *
 *  合成（背景键控 / 描边过渡混合 / 描边色 / 笔画色）全部在 .ino 的
 *  drawDigitMask() 里做 —— 那里才有 bgBuff、当前配色和描边色。
 *  这里只负责"选哪张掩膜、传到哪个坐标"。
 * *****************************************************************/

//由 .ino 提供
extern void drawDigitMask(int16_t x, int16_t y, const uint8_t *mask,
                          int16_t MW, int16_t MH, int16_t stride, int16_t pad,
                          uint16_t fillColor, bool outlineOn, uint16_t outlineColor);

static void drawMasked(int x, int y, const uint8_t *mask,
                       int16_t MW, int16_t MH, int16_t stride, int16_t pad,
                       uint16_t color, bool outline, uint16_t outlineColor)
{
  if (mask == nullptr) return;
  drawDigitMask((int16_t)x, (int16_t)y, mask, MW, MH, stride, pad,
                color, outline, outlineColor);
}

//显示 36*60 大小数字（原 W_3660）
void Number::printfW3660(int numx, int numy, int numn, uint16_t color,
                         bool outline, uint16_t outlineColor)
{
  if (numn < 0 || numn > 9)
  {
    Serial.println("显示 W3660 数字错误");
    return;
  }
  drawMasked(numx, numy, DIGITM_A[numn],
             DIGITM_A_MW, DIGITM_A_MH, DIGITM_A_STRIDE, DIGITM_A_PAD,
             color, outline, outlineColor);
}

//显示 18*30 大小数字（原 W_1830）
void Number::printfW1830(int numx, int numy, int numn, uint16_t color,
                         bool outline, uint16_t outlineColor)
{
  if (numn < 0 || numn > 9)
  {
    Serial.println("显示 W1830 数字错误");
    return;
  }
  drawMasked(numx, numy, DIGITM_B[numn],
             DIGITM_B_MW, DIGITM_B_MH, DIGITM_B_STRIDE, DIGITM_B_PAD,
             color, outline, outlineColor);
}
