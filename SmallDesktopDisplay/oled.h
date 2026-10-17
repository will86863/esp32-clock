#ifndef OLED_H
#define OLED_H

/*****************************************************************
 * 0.96寸 SSD1306 OLED 极简驱动（I2C，128x64）
 * 与DHT30共用I2C总线，地址0x3C，无外部库依赖
 * 字模：font8x16.h（ASCII 0x20-0x7E）
 *****************************************************************/

#include <Arduino.h>
#include <Wire.h>
#include <string.h>
#include <pgmspace.h>
#include "font8x16.h"

#define OLED_ADDR 0x3C        //SSD1306 I2C地址（绝大多数0.96寸为0x3C）
#define OLED_W    128
#define OLED_H    64

static uint8_t oled_buf[OLED_W * OLED_H / 8];  //显存

//向SSD1306写一条命令
static void oled_writeCmd(uint8_t cmd)
{
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);            //控制字节：命令
  Wire.write(cmd);
  Wire.endTransmission();
}

//清空显存
static void oled_clear()
{
  memset(oled_buf, 0, sizeof(oled_buf));
}

//点亮一个像素
static void oled_setPixel(int x, int y)
{
  if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
  oled_buf[(y / 8) * OLED_W + x] |= (1 << (y % 8));
}

//把显存整屏刷到OLED（分批发送，避免超出Wire缓冲）
static void oled_show()
{
  oled_writeCmd(0x21);         //列地址范围
  oled_writeCmd(0x00);
  oled_writeCmd(OLED_W - 1);
  oled_writeCmd(0x22);         //页地址范围
  oled_writeCmd(0x00);
  oled_writeCmd(OLED_H / 8 - 1);
  for (int i = 0; i < (int)sizeof(oled_buf); i += 32) {
    Wire.beginTransmission(OLED_ADDR);
    Wire.write(0x40);          //控制字节：数据
    for (int j = 0; j < 32 && (i + j) < (int)sizeof(oled_buf); j++) {
      Wire.write(oled_buf[i + j]);
    }
    Wire.endTransmission();
  }
}

//画一个字符（8x16字模，scale=1原尺寸 / 2放大一倍）
static void oled_drawChar(int x, int y, char ch, int scale)
{
  if (ch < 0x20 || ch > 0x7E) ch = '?';
  int idx = ch - 0x20;
  for (int row = 0; row < 16; row++) {
    uint8_t bits = pgm_read_byte(&font8x16[idx][row]);
    if (bits == 0) continue;
    for (int col = 0; col < 8; col++) {
      if (bits & (0x80 >> col)) {
        for (int sy = 0; sy < scale; sy++)
          for (int sx = 0; sx < scale; sx++)
            oled_setPixel(x + col * scale + sx, y + row * scale + sy);
      }
    }
  }
}

//画字符串
static void oled_drawString(int x, int y, const char *s, int scale)
{
  while (*s) {
    oled_drawChar(x, y, *s++, scale);
    x += 8 * scale;
  }
}

//初始化SSD1306（调用前需已Wire.begin）
static void oled_init()
{
  delay(100);
  oled_writeCmd(0xAE);         //关显示
  oled_writeCmd(0xD5);         //设置时钟分频
  oled_writeCmd(0x80);
  oled_writeCmd(0xA8);         //复用率
  oled_writeCmd(0x3F);
  oled_writeCmd(0xD3);         //显示偏移
  oled_writeCmd(0x00);
  oled_writeCmd(0x40);         //起始行
  oled_writeCmd(0x8D);         //电荷泵
  oled_writeCmd(0x14);
  oled_writeCmd(0x20);         //水平寻址模式
  oled_writeCmd(0x00);
  oled_writeCmd(0xA1);         //段重映射
  oled_writeCmd(0xC8);         //COM扫描方向反向
  oled_writeCmd(0xDA);         //COM引脚配置
  oled_writeCmd(0x12);
  oled_writeCmd(0x81);         //对比度
  oled_writeCmd(0xCF);
  oled_writeCmd(0xD9);         //预充电
  oled_writeCmd(0xF1);
  oled_writeCmd(0xDB);         //VCOM电压
  oled_writeCmd(0x40);
  oled_writeCmd(0xA4);         //显示RAM内容
  oled_writeCmd(0xA6);         //正常显示
  oled_writeCmd(0xAF);         //开显示
  oled_clear();
  oled_show();
}

#endif
