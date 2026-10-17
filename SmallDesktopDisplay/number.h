#ifndef NUMBER_H
#define NUMBER_H

#include <TFT_eSPI.h>

/* *****************************************************************
 *  V1.6.6 数字字模换成 2bpp 掩膜（方案A：带描边）
 *
 *  原来那三套 JPEG 字模（W_3660_i* / W_1830_i* / O_3660_i*）已不再使用，
 *  这里**不再 include** 它们，让链接器把整块数据丢掉（实测省约 42KB flash）。
 *  取而代之的是 font/DigitMask.h —— 由 tools/gen_digit_mask.py 生成：
 *    每像素 2bit：0=透明 1=描边过渡 2=描边实心 3=笔画
 *
 *  真正的合成（取背景键控、描边色、笔画色）在 .ino 的 drawDigitMask() 里完成，
 *  因为那里才有 bgBuff、配色变量和"自动取反"的描边色。
 * *****************************************************************/
#include "font/DigitMask.h"

class Number
{
public:
  //color       : 笔画颜色。注意 0 就是纯黑，是**合法颜色**，
  //              不再像旧版那样被当成"用字模原色"。
  //outline     : 是否画描边
  //outlineColor: 描边颜色（由 .ino 的 digitOutlineColor() 算好，含自动取反）
  void printfW3660(int numx, int numy, int numn, uint16_t color = 0xFFFF,
                   bool outline = false, uint16_t outlineColor = 0x0000);
  void printfW1830(int numx, int numy, int numn, uint16_t color = 0xFFFF,
                   bool outline = false, uint16_t outlineColor = 0x0000);
};

#endif
