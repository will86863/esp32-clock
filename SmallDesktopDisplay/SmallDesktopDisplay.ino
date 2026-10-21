/* *****************************************************************
 * 
 * SmallDesktopDisplay
 *    小型桌面显示器
 * 
 * 原  作  者：Misaka
 * 修      改：微车游
 * 讨  论  群：811058758、887171863
 * 创 建 日 期：2021.07.19
 * 最后更改日期：2021.09.18
 * 更 改 说 明：V1.1添加串口调试，波特率115200\8\n\1；增加版本号显示。
 *            V1.2亮度和城市代码保存到EEPROM，断电可保存
 *            V1.3.1 更改smartconfig改为WEB配网模式，同时在配网的同时增加亮度、屏幕方向设置。
 *            V1.3.2 增加wifi休眠模式，仅在需要连接的情况下开启wifi，其他时间关闭wifi。增加wifi保存至eeprom（目前仅保存一组ssid和密码）
 *            V1.3.3  修改WiFi保存后无法删除的问题。目前更改为使用串口控制，输入 0x05 重置WiFi数据并重启。
 *                    增加web配网以及串口设置天气更新时间的功能。
 *            V1.3.4  修改web配网页面设置，将wifi设置页面以及其余设置选项放入同一页面中。
 *                    增加web页面设置是否使用DHT传感器。（使能DHT后才可使用）
 *            V1.4    增加web服务器，使用web网页进行设置。由于使用了web服务器，无法开启WiFi休眠。
 *                    注意，此版本中的DHT11传感器和太空人图片选择可以通过web网页设置来进行选择，无需通过使能标志来重新编译。
 *            (修改)  将DHT11传感器更换为DHT30（I2C，SHT30），温湿度测量精度更高（±0.3℃/±2%RH）。
 *            V1.5    新增功能：
 *                    1. 网页后台可自定义TFT文字配色（时钟数字白/橙二选一，其余文字任意配色）
 *                    2. 网页后台可上传最多5张240x240背景图，存于SPIFFS
 *                    3. 背景图支持透明度调节（0-100暗化）
 *                    4. TTP223触摸模块(GPIO25)单击循环切换背景图
 *                    5. 网页UI重构：亮度/天气间隔/背景透明度改为滑块，颜色为取色器
 * 
 * 引 脚 分 配： SCK  GPIO14
 *             MOSI  GPIO13
 *             RES   GPIO2
 *             DC    GPIO0
 *             LCDBL GPIO5
 *             
 *             增加DHT30温湿度传感器(I2C)，传感器接口为 SDA=GPIO21，SCL=GPIO22
 *             增加TTP223触摸模块，SIG=GPIO25（VCC=3.3V，GND=GND，A焊盘不焊=直接输出模式）
 * 
 *    ESP32-C3移植说明：原程序的GPIO13/14在ESP32-C3上属于Flash相关引脚，不能照搬。
 *    TFT实际接线请在TFT_eSPI的User_Setup中改为ESP32-C3可用GPIO；程序功能保持不变。
 *
 *    感谢群友 @你别失望  提醒发现WiFi保存后无法重置的问题，目前已解决。详情查看更改说明！
 * *****************************************************************/
#define Version  "SDD V1.6.9"
/* *****************************************************************
 *  库文件、头文件
 * *****************************************************************/
#include "ArduinoJson.h"
#include <TimeLib.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#include <TFT_eSPI.h> 
#include <SPI.h>
#include <TJpg_Decoder.h>
#include <EEPROM.h>
#include <FS.h>
#include <SPIFFS.h>
#include "qr.h"
#include "number.h"
#include "weathernum.h"


/* *****************************************************************
 *  配置使能位
 * *****************************************************************/
//WEB配网使能标志位----WEB配网打开后会默认关闭smartconfig功能
#define WM_EN   1
//Web服务器使能标志位----打开后将无法使用wifi休眠功能。
#define WebSever_EN  1



//注意，此版本中的DHT30传感器和太空人图片选择可以通过web网页设置来进行选择，无需通过使能标志来重新编译。
//设定DHT30温湿度传感器使能标志
#define DHT_EN  1
//设置太空人图片是否使用
#define imgAst_EN 1

/* *****************************************************************
 *  V1.5 新增功能配置
 * *****************************************************************/
//TTP223触摸模块引脚（单击循环切换背景图）
#define TTP223_PIN      25
//TTP223触发防抖时间（毫秒）
#define TOUCH_DEBOUNCE  300
//背景图最大张数（固定5槽）
#define BG_MAX          5
//单张背景图最大字节数（超过则拒绝上传）
#define BG_MAX_SIZE     (64*1024)
//背景图分辨率
#define BG_WIDTH        240
#define BG_HEIGHT       240

//设置ESP32是否使用固定IP
//V1.5.5 已启用（路由器网段 192.168.5.x）。想改回动态分配(DHCP)就把下一行注释掉。
//注意：电脑端 config.json 里的 esp32_ip 要填这里的固定IP。
#define USE_STATIC_IP
#ifdef USE_STATIC_IP
IPAddress esp32_staticIP(192,168,5,86);   //ESP32固定IP（需在路由器DHCP池外或做地址保留）
IPAddress esp32_gateway(192,168,5,1);      //网关
IPAddress esp32_subnet(255,255,255,0);     //子网掩码
IPAddress esp32_dns1(192,168,5,1);         //DNS1（一般与网关同址）
IPAddress esp32_dns2(114,114,114,114);     //DNS2（备用公共DNS，网关不转发DNS时兜底）
#endif



#if WM_EN
#include <WiFiManager.h>
//WiFiManager 参数
WiFiManager wm; // global wm instance
// WiFiManagerParameter custom_field; // global param ( for non blocking w params )
#endif

/* *****************************************************************
 *  V1.6.8 WiFi 断线自动重连（非阻塞状态机）
 *
 *  修掉的三个旧毛病：
 *   1) Webconfig() 里**无条件** wm.resetSettings()：开机若连不上（路由器还没起来/
 *      信号弱），约 5.6 秒后就把已保存的 WiFi 账号密码**擦掉**再进 AP 门户。
 *      一次瞬时断网 = 凭据丢失，之后网络恢复也永远连不回来。
 *   2) while(!res); —— wm.autoConnect() 失败就无限空转：不重试、不超时、不退出，
 *      也永远走不到 Web_Sever_Init()，于是 192.168.5.86 / sd3.local 全部打不开，
 *      屏幕停在 "WiFi Connect Fail!"，即用户反馈的"卡在等待网络连接页面"。
 *   3) loop() 里没有任何重连逻辑，运行中掉线后不会自己恢复。
 *
 *  现在：开机只尝试一次（有上限），之后由 wifiTask() 按 1→2→4→…→60 秒退避重连；
 *        已配过网就**绝不**清凭据、也绝不自动进配网门户；门户只能手动进入且 180 秒超时。
 *
 *  ⚠️ V1.6.9 补：上面第 2 条的“走不到 Web_Sever_Init()”只是在旧版的死循环里成立；
 *     V1.6.8 改成“8 秒超时进主循环”后又引入了新的漏洞——setup() 里整个
 *     `if(WiFi.status()==WL_CONNECTED){ Web_Sever_Init(); }` 被跳过时，server.begin()
 *     就永远不会执行，而 wifiTask() 重连成功只重建 mDNS。表现为“ping 通、sd3.local
 *     能解析、TCP 80 却 refused”。详见 httpStart() 上方的长注释。
 * *****************************************************************/
#define WIFI_BOOT_WAIT_MS   8000UL   //开机最多等 8 秒，超时就先进主循环由状态机续连
#define WIFI_BACKOFF_MIN    1000UL   //首次重连间隔
#define WIFI_BACKOFF_MAX    60000UL  //退避上限
#define WIFI_PORTAL_TIMEOUT 180      //配网门户最长 3 分钟，到点自动退出回到重连

bool     wifiWasDown        = false; //当前是否处于断开状态（边沿检测用）
bool     wifiBadgeShown     = false; //屏幕上是否已画"无网络"角标
uint32_t wifiNextTryMs      = 0;     //下一次重连的时刻
uint32_t wifiBackoffMs      = WIFI_BACKOFF_MIN;
uint8_t  wifiFailCount      = 0;
bool     webRoutesReady     = false; //HTTP 路由是否已注册（**只能注册一次**）
bool     httpStarted        = false; //V1.6.9 HTTP 是否已 listen（重入守卫）
bool     netPortalRequested = false; //网页/串口请求进入配网门户
void wifiTask();
void wifiBadgeTask();
void wifiEnterPortal();
void wifiBeginConnect();
/* V1.6.9 幂等启动 HTTP（见 Web_Sever_Init 上方的长注释）。
 * ⚠️ 声明与定义都必须在 #if WebSever_EN 里面：它用到 server / handleXxx，
 *    而 wifiTask() 是无条件调用的（WebSever_EN=0 时必须退化成空函数）。
 * 同理 netUp() 也依赖 MDNS + server，一并处理（V1.6.8 遗留：WebSever_EN=0
 *     时 netUp() 未定义，wifiTask() 会直接报错）。 */
#if WebSever_EN
bool httpStart();
void netUp();
#else
inline bool httpStart() { return false; }   //Web 功能关闭时退化成无操作
inline void netUp() {}                     //同上
#endif

//I2C总线引脚（DHT30温湿度传感器与SSD1306 OLED共用）
#define I2C_SDA 21
#define I2C_SCL 22
#include <Wire.h>

#if DHT_EN
/* ------------------------------------------------------------------
 *  I2C 温湿度传感器驱动（V1.6.9 重写）
 *
 *  背景：原代码只实现了 SHT3x 协议（地址 0x44 / 命令 0x2C06 / 读 6 字节），
 *        但实际采购并焊在板上的是 ASAIR 系列的 DHT20 / "DHT30"，
 *        它用的是完全不同的协议：
 *            地址 0x38，命令 0xAC 0x33 0x00，等待 80ms，读 7 字节
 *        两者地址、命令、时长、数据长度、换算公式全都不一样，
 *        所以旧代码在任何情况下都读不到数据，且失败时完全静默。
 *
 *  本版做法：
 *    1) 先按 DHT20/DHT30 协议读（主板实际器件）
 *    2) 失败再回退到 SHT3x 协议（兼容只焊了 SHT30 的板子）
 *    3) 两种都失败时打印明确原因到串口，不再静默返回
 *    4) 读失败时把 t/h 置为 NAN，让显示层能区分"0.0"和"没读到"
 * ------------------------------------------------------------------ */
#define DHT20_ADDR      0x38    //ASAIR DHT20 / "DHT30" 固定地址
#define SHT30_ADDR_GND  0x44    //SHT3x ADDR 接 GND
#define SHT30_ADDR_VCC  0x45    //SHT3x ADDR 接 VCC

//上次使用的是哪种协议（0=未确定 1=DHT20 2=SHT3x），避免每次重试
static uint8_t dhtProtocol = 0;

//SHT3x 使用的 CRC8：多项式 0x31，初值 0xFF（DHT20 用同一算法）
static uint8_t dhtCrc8(const uint8_t *d, int n)
{
  uint8_t crc = 0xFF;
  for (int i = 0; i < n; i++) {
    crc ^= d[i];
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

//读取 DHT20 / DHT30，成功返回 true
static bool readDHT20(float &t, float &h, const char **why)
{
  Wire.beginTransmission(DHT20_ADDR);
  Wire.write(0xAC);           //触发测量
  Wire.write(0x33);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) { *why = "DHT20 no ACK"; return false; }

  delay(80);                  //手册要求 >= 80ms

  if (Wire.requestFrom((uint8_t)DHT20_ADDR, (uint8_t)7) != 7) {
    *why = "DHT20 read short";
    return false;
  }

  uint8_t b[7];
  for (int i = 0; i < 7; i++) b[i] = Wire.read();

  if (dhtCrc8(b, 6) != b[6]) { *why = "DHT20 CRC bad"; return false; }

  //20bit 湿度 + 20bit 温度
  uint32_t rawH = ((uint32_t)b[1] << 12) | ((uint32_t)b[2] << 4) | (b[3] >> 4);
  uint32_t rawT = ((uint32_t)(b[3] & 0x0F) << 16) | ((uint32_t)b[4] << 8) | b[5];

  h = (float)rawH * 100.0f / 1048576.0f;          //2^20
  t = (float)rawT * 200.0f / 1048576.0f - 50.0f;
  return true;
}

//读取 SHT3x（兼容旧板），成功返回 true
static bool readSHT3x(uint8_t addr, float &t, float &h, const char **why)
{
  Wire.beginTransmission(addr);
  Wire.write(0x2C);           //单次测量，高重复性，时钟拉伸使能
  Wire.write(0x06);
  if (Wire.endTransmission() != 0) { *why = "SHT3x no ACK"; return false; }

  delay(20);
  if (Wire.requestFrom(addr, (uint8_t)6) != 6) { *why = "SHT3x read short"; return false; }

  uint8_t b[6];
  for (int i = 0; i < 6; i++) b[i] = Wire.read();

  if (dhtCrc8(&b[0], 2) != b[2] || dhtCrc8(&b[3], 2) != b[4]) {
    *why = "SHT3x CRC bad";
    return false;
  }

  uint16_t rawT = ((uint16_t)b[0] << 8) | b[1];
  uint16_t rawH = ((uint16_t)b[3] << 8) | b[4];
  t = -45.0f + 175.0f * (float)rawT / 65535.0f;
  h = 100.0f * (float)rawH / 65535.0f;
  return true;
}

//统一入口：自动识别协议，失败时输出可诊断的原因
bool readDHT30(float &t, float &h)
{
  const char *why = "";

  //先按上次成功的协议试，避免每帧都重新探测
  if (dhtProtocol == 1) {
    if (readDHT20(t, h, &why)) return true;
  } else if (dhtProtocol == 2) {
    if (readSHT3x(SHT30_ADDR_GND, t, h, &why)) return true;
    if (readSHT3x(SHT30_ADDR_VCC, t, h, &why)) return true;
  }

  //未确定或上次失败：完整探测一次
  if (readDHT20(t, h, &why)) {
    dhtProtocol = 1;
    Serial.printf("[DHT] 使用 DHT20/DHT30 @0x%02X  T=%.2fC H=%.2f%%\r\n",
                  DHT20_ADDR, t, h);
    return true;
  }
  if (readSHT3x(SHT30_ADDR_GND, t, h, &why)) {
    dhtProtocol = 2;
    Serial.printf("[DHT] 使用 SHT3x @0x%02X  T=%.2fC H=%.2f%%\r\n",
                  SHT30_ADDR_GND, t, h);
    return true;
  }
  if (readSHT3x(SHT30_ADDR_VCC, t, h, &why)) {
    dhtProtocol = 2;
    Serial.printf("[DHT] 使用 SHT3x @0x%02X  T=%.2fC H=%.2f%%\r\n",
                  SHT30_ADDR_VCC, t, h);
    return true;
  }

  //全部失败：打印原因（限流，避免每秒刷屏）
  static uint32_t lastErrMs = 0;
  static uint8_t  errCount  = 0;
  if (millis() - lastErrMs > 10000UL) {
    lastErrMs = millis();
    errCount++;
    Serial.printf("[DHT] ★读不到传感器 (第%u次) 原因=%s\r\n", errCount, why);
    Serial.println("[DHT]   检查：1)传感器VDD/GND  2)SDA->GPIO21 SCL->GPIO22");
    Serial.println("[DHT]         3)DHT20地址应为0x38  4)上拉电阻(OLED自带可满足)");
    //强制下次重新探测，避免卡在错的协议上
    dhtProtocol = 0;
  }

  t = NAN;
  h = NAN;
  return false;
}
#endif

/* *****************************************************************
 *  游戏时长显示（0.96寸SSD1306 OLED，数据来自电脑端UDP）
 *  电脑端每秒发送 {"running":bool,"name":"进程名","up":秒数[,"today":秒数]}
 *  V1.6.8 新增 today：今日"所有已添加程序"累计运行秒数。
 *         无程序运行时若开关打开，OLED 改显示 TODAY TOTAL + 该值。
 * *****************************************************************/
#include "oled.h"
#define GAME_UDP_PORT   5005    //UDP监听端口（与电脑端config.json的esp32_port一致）
#define GAME_TIMEOUT_MS 5000    //超时未收到数据则视为未运行
WiFiUDP GameUdp;
char gameName[24] = "NO GAME";
long gameUp = 0;
bool gameRunning = false;
unsigned long gameLastMs = 0;
unsigned long oledLastMs = 0;
//V1.6.8 今日总时长
long gameToday = 0;             //今日累计秒数（上位机算好发过来）
bool gameTodayValid = false;    //是否收到过 today 字段（没收到过就别显示，避免误报 0）
bool oledTodayEn = true;        //"无程序时显示今日总时长"开关（默认开）
void gameOledTask();
void oledDrawGame();





/* *****************************************************************
 *  字库、图片库
 * *****************************************************************/
#include "font/ZdyLwFont_20.h"
#include "img/misaka.h"
#include "img/temperature.h"
#include "img/humidity.h"

#if imgAst_EN
#include "img/pangzi/i0.h"
#include "img/pangzi/i1.h"
#include "img/pangzi/i2.h"
#include "img/pangzi/i3.h"
#include "img/pangzi/i4.h"
#include "img/pangzi/i5.h"
#include "img/pangzi/i6.h"
#include "img/pangzi/i7.h"
#include "img/pangzi/i8.h"
#include "img/pangzi/i9.h"

int Anim = 0;           //太空人图标显示指针记录
int AprevTime = 0;      //太空人更新时间记录
#endif



/* *****************************************************************
 *  参数设置
 * *****************************************************************/

struct config_type
{
  char stassid[32];//定义配网得到的WIFI名长度(最大32字节)
  char stapsw[64];//定义配网得到的WIFI密码长度(最大64字节)
};

//---------------修改此处""内的信息--------------------
//如开启WEB配网则可不用设置这里的参数，前一个为wifi ssid，后一个为密码
config_type wificonf ={{""},{""}};


int updateweater_time = 10; //天气更新时间  X 分钟
int LCD_Rotation = 0;   //LCD屏幕方向
int LCD_BL_PWM = 50;//屏幕亮度0-100，默认50
String cityCode = "101250101";  //天气城市代码 长沙:101250101株洲:101250301衡阳:101250401
//----------------------------------------------------

//LCD屏幕相关设置
TFT_eSPI tft = TFT_eSPI();  // 引脚请自行配置tft_espi库中的 User_Setup.h文件
TFT_eSprite clk = TFT_eSprite(&tft);
/* ---------------- 背光 PWM 调光 ----------------
 * V1.6.7 重要修正：背光引脚改由**固件自己**定义，不再依赖 TFT_eSPI 的 User_Setup.h。
 *   为什么：同一台电脑上常有多份 TFT_eSPI（例如"项目内 libraries/ 一份 + Arduino
 *   用户库目录一份"），而 IDE 实际编译用的是**用户库目录**那份。只在项目内那份里写
 *   `#define TFT_BL 33` 完全不会生效 → LCD_BL_PIN 退化成 -1 → GPIO33 从未被配置成
 *   输出 → 接线全对也黑屏（本项目真实踩过这个坑）。
 *   现在无论用哪份库、哪个 core，GPIO33 都会被正常驱动。
 *   若某份 User_Setup.h 里确实定义了 TFT_BL，仍以它为准（方便换脚）。
 */
#ifndef LCD_BL_PIN_DEFAULT
  #define LCD_BL_PIN_DEFAULT 33   //背光控制脚（把屏的 BLK 接到这里）
#endif
#if defined(TFT_BL)
  #define LCD_BL_PIN TFT_BL
#else
  #define LCD_BL_PIN LCD_BL_PIN_DEFAULT
#endif

/* 背光开关的极性（V1.6.7 起改为**运行时可切换**：存 EEPROM，网页/串口都能改）
 *  为什么必须是"高边开关"：BLK 原来是接 3.3V 才亮的，说明 BLK 是背光 LED 的**阳极**
 *  馈电脚（反向接 3.3V 不可能点亮）。要调光只能在「3.3V → BLK」之间串一个开关，
 *  LED 阴极在模块内部直接接地，动不了。
 *    ① 直驱【本机实际接法】：BLK 直接接 GPIO33，正逻辑，GPIO 高 = 亮 → lcdBlActiveLow = false
 *       ⚠️ 电流：GPIO 会持续供出背光 LED 的全部电流。ESP32 单脚推荐 ≤ 12mA、
 *          绝对上限 40mA。多数 1.3" ST7789 模块的 BLK 电流在 8~25mA。
 *          若亮度明显不如原来直连 3.3V、或 ESP32 发热/复位，说明超了，
 *          此时串一个 33~68Ω 限流电阻，或改用下面的高边开关。
 *    ② 高边开关：P-MOSFET(AO3401) 或 PNP(S8550)，S/E 接 3.3V，D/C 接 BLK，
 *       G/B 经 1k 接 GPIO33，并加 10k 上拉到 3.3V 防上电浮空乱亮。
 *       → GPIO 越低越亮 = 反逻辑 = lcdBlActiveLow = true
 *  ⚠️ 极性选错时表现为"亮度越大越暗"，100% 时直接黑屏。
 *     黑屏时不用重刷：串口监视器发一个字母 p 即可切换，或开网页「显示」卡里切换。
 */
bool lcdBlActiveLow = false;    //false = 高电平点亮 = BLK 直连 GPIO33（本项目实际接法）
//亮度下限：0 保留给"未初始化"，且避免误设成 0 导致黑屏找不到路
#define LCD_BL_MIN 5
#define LCD_PWM_FREQ 5000
#define LCD_PWM_RESOLUTION 10
#define LCD_PWM_CHANNEL 0
uint16_t bgColor = 0x0000;

/* *****************************************************************
 *  V1.5 文字配色变量
 *  V1.5.1 时钟数字改为"动态着色"：不再依赖白/橙两套字模，
 *        而是在解码回调里把字模的明暗重映射到目标颜色，支持任意配色。
 *  textColor2：日期/星期/上午下午/城市名/滚动条 文字颜色
 *  textColor3：室内温湿度 文字颜色
 *  hourColor/minuteColor/secondColor：时/分/秒 三组数字独立配色
 * *****************************************************************/
uint16_t textColor2  = 0xFFFF; //日期星期等（默认白）
uint16_t textColor3  = 0xFFFF; //室内温湿度（默认白）

//V1.5.1 时钟数字三组独立配色（默认沿用原设计：白/橙/白）
uint16_t hourColor   = 0xFFFF; //小时 默认白
uint16_t minuteColor = 0xFD20; //分钟 默认橙 (RGB565: 0xFD20 ≈ #FFA000)
uint16_t secondColor = 0xFFFF; //秒钟 默认白

//动态着色开关与当前目标色（解码回调据此重映射像素）
uint16_t tintTarget   = 0;     //0=不做着色
uint8_t  tintMode     = 0;     //0=关闭着色 1=按明暗着色

/* *****************************************************************
 *  V1.5 背景图变量
 * *****************************************************************/
uint8_t  bgIndex   = 0;        //当前使用的背景图槽位（0~4）
uint8_t  bgCount   = 0;        //已上传的背景图数量
uint8_t  bgAlpha   = 255;      //背景暗化系数 0~255（255=原图，0=纯黑）

/* *****************************************************************
 *  V1.5.4 背景缓存改为「静态 8bpp 缓冲」——彻底修复文字/数字黑框
 *
 *  根因（已由链接器实测确认）：
 *    旧版用 bgCanvas.createSprite(240,240,16bpp) 需要一个 115,200 字节的
 *    *连续* 内存块。但本板 ESP32 的 DRAM 极为紧张——实测仅额外加 115KB 的
 *    静态数组就让 dram0_0_seg 溢出 39,256 字节 → 实际可用不足 76KB。
 *    因此 createSprite 必然失败 → bgCanvasReady=false →
 *      · 所有文字 sprite 走 fillSprite(0x0000) → 整屏规整黑矩形（"文字黑框"）
 *      · 数字/图标键控条件含 bgCanvasReady → 全部失效（"数字黑框"）
 *    一个原因解释了用户报告的全部现象。
 *
 *  方案：
 *    改用 240x240x1 = 57,600 字节的静态数组，存 RGB332（8bpp 调色）。
 *    仅需 ~58KB，实测可放；且静态分配由链接器在启动时预留，永不失败。
 *    贴底/键控时按需把 RGB332 展开为 "交换序 RGB565"，与字模同格式互换。
 *    背景是装饰性内容，8bpp 色彩层次足够，肉眼几乎无差。
 * *****************************************************************/
#define BG_BUF_PIXELS  (BG_WIDTH * BG_HEIGHT)
uint8_t  bgBuff[BG_BUF_PIXELS];           //57,600 字节静态区：RGB332 背景缓存
bool     bgCanvasReady = false;           //背景缓存是否已就绪（有背景图时为 true）

/* *****************************************************************
 *  V1.6 每张壁纸独立预设 + 文字可读性增强
 *  预设：5 个颜色 + 背景暗化 + 三个功能开关，每槽位 16 字节存 EEPROM
 * *****************************************************************/
#define PRESET_SIZE   16
#define P_HOUR        0    //2字节 小时色
#define P_MIN         2    //2字节 分钟色
#define P_SEC         4    //2字节 秒钟色
#define P_TXT2        6    //2字节 日期/城市/滚动条
#define P_TXT3        8    //2字节 室内温湿度
#define P_ALPHA       10   //1字节 背景暗化百分比 0-100
#define P_FLAGS       11   //1字节 bit0=自动换色 bit1=自适应底板 bit2=文字描边
#define P_MAGIC       12   //1字节 本槽位已由本固件写入的标记 = PRESET_MAGIC
#define P_CHK         13   //1字节 校验：前 12 字节求和后取反
#define P_NUMOC       14   //2字节 V1.6.6 数字描边的"固定色"（仅在关闭自动取反时使用）

#define PRESET_MAGIC  0xA5

#define PF_AUTOCOLOR  0x01   //自动换色
#define PF_SCRIM      0x02   //自适应底板
#define PF_OUTLINE    0x04   //文字描边（矢量文字）
#define PF_NUMOUTLINE 0x08   //V1.6.6 数字描边开关（与文字描边独立）
//注意这个位是"取反"语义：置位 = 用固定色，**清位 = 自动取反**。
//这样旧版预设（flags 里没有这个位）升级后自动落到"自动取反"，符合直觉。
#define PF_NUMOUTFIX  0x10

/* ★★★ V1.6.3 根因修复：绝不能用"擦除值"判断有效性 ★★★
 * 实测证据（设备在线实测，2026-09-26）：
 *   本机 EEPROM 里"从未被写过"的字节读出来是 0x00，而不是 0xFF。
 *   验证方法：bgVer(216~220) 从来没有被写入过，而网页缩略图显示 v=0 ——
 *   代码逻辑是"读到 0xFF 才改判成 1"，读到 0 说明实际值就是 0x00。
 *
 * 由此产生的连环故障（正是用户报的"换壁纸后除温湿度外全变黑"）：
 *   1) readColor16 只把 0xFF,0xFF 当未初始化 → 0x00,0x00 被当成"用户设的纯黑"
 *   2) presetMask = EEPROM.read(132) & 0x1F，读到 0xFF 时得到 0x1F
 *      → 五个槽位全被认定为"已有预设"，loadPreset 就去原样读
 *   3) 从未写过的槽位读出 0x0000 = 黑，写进全局配色
 *   4) 接着 savePreset 又把这份黑配色落盘到各槽位 → 每张壁纸都变黑
 *
 * 所以：有效性必须靠"写入时留下的正向标记"（魔数 + 校验），
 *       读到垃圾/零/擦除值时一律判定为"无预设"，绝不加载。
 */

uint8_t  presetMask      = 0;     //哪些槽位已保存过预设（bit i = 槽位 i）
uint8_t  bgSlotVer[BG_MAX] = {0}; //V1.6.1 各槽位图片版本号（缩略图缓存标记）
bool     bgAutoColor     = false; //方案2 自动换色（保留色相，只调明暗）
bool     bgScrimOn       = false; //方案1 自适应底板
bool     bgOutlineOn     = false; //方案3 文字描边（矢量文字：日期/城市/温湿度/滚动条）
//V1.6.6 时钟数字描边（方案A：2bpp 掩膜）。与上面"文字描边"是**独立**开关，
//       因为数字是位图字模、走的是完全不同的绘制路径。
bool     bgNumOutlineOn   = false; //数字描边开关
bool     bgNumOutlineAuto = true;  //自动取反：亮字配黑边 / 暗字配白边
uint16_t bgNumOutlineColor = 0x0000;//自动取反关闭时使用的固定描边色

/* ---- V1.6.6 亮度定时：按时段自动调亮度 ----
 * 每段 5 字节：起时, 起分, 止时, 止分, 亮度(0 表示该段不启用)
 * 支持跨零点（如 23:00 -> 07:00）。
 * 定时开启时亮度完全由时段决定，手动滑块被忽略；不匹配任何段则回落到手动亮度。 */
#define BL_SCHED_MAX 4
struct BlSeg { uint8_t sh, sm, eh, em, br; };
BlSeg   blSeg[BL_SCHED_MAX];
bool    blSchedEn    = false;
uint8_t blCurBright  = 255;    //当前实际下发的亮度（255=未知，用于避免重复写 PWM）
uint8_t  bgScrimStrength = 100;   //底板强度 0-100（全局；100=恰好达到对比度目标，可调低）
bool     bgAutoRotate    = false; //壁纸自动循环
uint16_t bgRotateMin     = 10;    //自动循环间隔（分钟）
uint32_t bgLastRotateMs  = 0;     //上次自动切换时刻

/* ---- V1.6.4 天气数据缓存 ----
 * 由 weaterData() 解析后写入，drawWeather() 只读它来绘制。
 * 目的：把"联网抓取"与"绘制"解耦，切背景时能用缓存零延迟重画天气区。
 * 声明在这里（而非天气函数附近）是为了让 refreshAfterBgChange() 也能读到 wxValid。 */
bool     wxValid  = false;   //是否已拿到过可用天气数据
String   wxTempS  = "--";    //sk["temp"]
String   wxSDS    = "--";    //sk["SD"]
String   wxCityS  = "--";    //sk["cityname"]
String   wxAqiTxt = "--";    //空气质量文字
uint16_t wxAqiCol = 0;       //空气质量底色
int      wxCode   = 0;       //天气图标代码

//区域背景平均亮度（0-255）与"该用深色字"的滞回状态
uint16_t lumClockZone    = 128;   //时钟数字所在区域
uint16_t lumTextZone     = 128;   //普通文字所在区域
bool     zoneDarkClock   = false;
bool     zoneDarkText    = false;

// ---- 背景缓存读写辅助（RGB332 <-> 交换序 RGB565） ----
// 说明：TJpgDec(setSwapBytes(true)) 输出的 RGB565 是"字节交换序"，
//       与 TFT 直发所需的字节序一致；字模 bitmap 也是该序。这里统一按该序处理。
// RGB332 标准布局：bit7..5=R(3)  bit4..2=G(3)  bit1..0=B(2)
//
// V1.5.5 修复"贴片区偏黑"：原实现用截断（r5>>2 / g6>>3 / b5>>3），解码时又用
//        (x<<2)|(x>>1) 这类方式补低位，使前两个量化档的重建点落在档中心**下方**约
//        1.5 级（5bit 计），最暗档 r5=1..3 更是直接塌成 0（纯黑）。真实图片暗部像素
//        占比高，于是整体净向暗偏、暗部细节丢失——实测暗色调背景平均偏暗 10.25/255，
//        14.6% 像素被压成纯黑。
//        改为"最近值舍入"后解码值回到档中心附近（R/G/B 最大误差 2/4/5，均已达该位宽
//        的理论下限），误差不再随图像明暗分布倾倒。实测同一张暗色调背景：
//        平均偏移 -10.25 → -1.23，压成纯黑 14.6% → 3.0%。
// 写入：把"交换序 RGB565"降为 RGB332 存进 bgBuff
static inline uint8_t bg565to332(uint16_t swapped)
{
  uint16_t c = (uint16_t)((swapped << 8) | (swapped >> 8));   //还原自然序
  uint8_t  r5 = (c >> 11) & 0x1F;
  uint8_t  g6 = (c >> 5)  & 0x3F;
  uint8_t  b5 =  c        & 0x1F;
  //最近值舍入：round(源值 * 目标级数 / 源级数)
  uint8_t  r3 = (uint8_t)((r5 * 7 + 15) / 31);   //5bit -> 3bit
  uint8_t  g3 = (uint8_t)((g6 * 7 + 31) / 63);   //6bit -> 3bit
  uint8_t  b2 = (uint8_t)((b5 * 3 + 15) / 31);   //5bit -> 2bit
  return (uint8_t)((r3 << 5) | (g3 << 2) | b2);
}
// 读取：把 bgBuff 里的 RGB332 展开为"交换序 RGB565"（可直接与字模/屏幕互用）
static inline uint16_t bg332to565sw(uint8_t p)
{
  uint8_t r3 = (p >> 5) & 0x07;
  uint8_t g3 = (p >> 2) & 0x07;
  uint8_t b2 =  p       & 0x03;
  //取该量化档的中点，使误差上下对称
  uint16_t r5 = (uint16_t)((r3 * 31 + 3) / 7);   //3bit -> 5bit
  uint16_t g6 = (uint16_t)((g3 * 63 + 3) / 7);   //3bit -> 6bit
  uint16_t b5 = (uint16_t)((b2 * 31 + 1) / 3);   //2bit -> 5bit
  uint16_t c  = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
  return (uint16_t)((c << 8) | (c >> 8));     //转为交换序
}

/* *****************************************************************
 *  V1.6 文字可读性增强（方案1/2/3）
 *  方案2 自动换色：按区域背景亮度决定文字明暗，**保留用户选定的色相与饱和度**
 *  方案1 自适应底板：把文字背后的背景按需压暗/提亮，保证亮度差不低于阈值
 *  方案3 文字描边：vlw 文字八方向描边（JPEG 字模不适用，见文末说明）
 * *****************************************************************/
#define AUTOC_DARK_V   62     //亮背景 -> 文字明度压到 62/255（保留色相）
#define AUTOC_LIGHT_V  238    //暗背景 -> 文字明度提到 238/255
#define AUTOC_HI       150    //背景亮度 > 此值 用深色字
#define AUTOC_LO       120    //背景亮度 < 此值 用浅色字（与上者构成滞回，防抖）
#define CONTRAST_NEED  110    //底板目标：文字与背景的亮度差

//RGB332 -> 自然序 RGB565（与 bg332to565sw 同源，只是不做字节交换）
static inline uint16_t bg332to565nat(uint8_t p)
{
  uint8_t r3 = (p >> 5) & 0x07;
  uint8_t g3 = (p >> 2) & 0x07;
  uint8_t b2 =  p       & 0x03;
  uint16_t r5 = (uint16_t)((r3 * 31 + 3) / 7);
  uint16_t g6 = (uint16_t)((g3 * 63 + 3) / 7);
  uint16_t b5 = (uint16_t)((b2 * 31 + 1) / 3);
  return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

//自然序 RGB565 -> 0..255 感知亮度
static inline uint16_t lum565nat(uint16_t nat)
{
  uint16_t r = ((nat >> 11) & 0x1F) * 255 / 31;
  uint16_t g = ((nat >> 5)  & 0x3F) * 255 / 63;
  uint16_t b = ( nat        & 0x1F) * 255 / 31;
  return (uint16_t)((r * 30 + g * 59 + b * 11) / 100);
}

//【方案2】保留色相与饱和度，只把明度改到 targetV：
//所有分量按同一比例缩放，R:G:B 比例不变 => 色相/饱和度不变，只有明暗变化。
//★V1.6.3 修字节序：入参与返回都是**自然序** RGB565。
//  证据（设备实测）：网页写 #112233，读回 #102031 —— 正是自然序
//  (r=17>>3=2, g=34>>2=8, b=51>>3=6) 的十六进制展开，说明全局配色存的是自然序。
//  旧代码在这里做了两次错误交换（先当交换序解码、又把自然序结果交换后返回），
//  导致"自动换色"算出来的色相全错。tft_output 里 tintTarget 也是按自然序解码的，
//  两处必须一致。
static uint16_t setColorValue(uint16_t color, uint8_t targetV)
{
  uint16_t nat = color;                       //入参已是自然序
  uint8_t r = ((nat >> 11) & 0x1F) * 255 / 31;
  uint8_t g = ((nat >> 5)  & 0x3F) * 255 / 63;
  uint8_t b = ( nat        & 0x1F) * 255 / 31;
  uint8_t m = r; if (g > m) m = g; if (b > m) m = b;
  if (m == 0) return 0x0000;                       //纯黑没有色相可保留
  //最大分量正好落到 targetV，其余分量等比缩小 => 不会溢出
  uint32_t k = ((uint32_t)targetV << 8) / m;
  r = (uint8_t)((r * k) >> 8);
  g = (uint8_t)((g * k) >> 8);
  b = (uint8_t)((b * k) >> 8);
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));   //自然序返回
}

//按背景亮度决定明暗档，带滞回（状态由调用方保持，避免字色来回跳）
static uint16_t autoContrastColor(uint16_t color, uint16_t bgLum, bool &darkState)
{
  if (darkState) { if (bgLum < AUTOC_LO) darkState = false; }   //已用深色，背景变暗才回浅色
  else           { if (bgLum > AUTOC_HI) darkState = true;  }   //已用浅色，背景变亮才转深色
  return setColorValue(color, darkState ? AUTOC_DARK_V : AUTOC_LIGHT_V);
}

//【方案3】描边色：文字偏亮用黑描边，文字偏暗用白描边
//★V1.6.3 修字节序：入参是自然序 RGB565（同 setColorValue 的说明），不要再交换。
static uint16_t outlineColorFor(uint16_t color)
{
  return (lum565nat(color) > 110) ? 0x0000 : 0xFFFF;
}

//【方案1】底板：按强度把背景亮度拉向 need，返回"效果后的背景亮度"
static uint16_t scrimTargetLum(uint16_t bgLum, uint16_t textLum)
{
  int need = (textLum >= 128) ? ((int)textLum - CONTRAST_NEED)   //亮文字->压暗背景
                              : ((int)textLum + CONTRAST_NEED);  //暗文字->提亮背景
  if (need < 0)   need = 0;
  if (need > 255) need = 255;
  int eff = (int)bgLum + (need - (int)bgLum) * (int)bgScrimStrength / 100;
  if (eff < 0)   eff = 0;
  if (eff > 255) eff = 255;
  return (uint16_t)eff;
}

//把单个像素按 scrim 规则调整（输入/输出均为自然序 RGB565）
static inline uint16_t scrimPixelNat(uint16_t nat, uint16_t bgLum, uint16_t eff)
{
  if (eff == bgLum) return nat;
  uint16_t r = ((nat >> 11) & 0x1F) * 255 / 31;
  uint16_t g = ((nat >> 5)  & 0x3F) * 255 / 63;
  uint16_t b = ( nat        & 0x1F) * 255 / 31;
  if (eff < bgLum)                       //压暗：整体等比缩放
  {
    if (bgLum == 0) return nat;
    uint32_t k = (uint32_t)eff * 256 / bgLum;
    r = (uint16_t)((r * k) >> 8);
    g = (uint16_t)((g * k) >> 8);
    b = (uint16_t)((b * k) >> 8);
  }
  else                                   //提亮：向白色插值
  {
    if (bgLum >= 255) return nat;
    uint32_t w = (uint32_t)(eff - bgLum) * 256 / (255 - bgLum);
    r = (uint16_t)(r + (((255 - r) * w) >> 8));
    g = (uint16_t)(g + (((255 - g) * w) >> 8));
    b = (uint16_t)(b + (((255 - b) * w) >> 8));
  }
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

//------------------------ 区域背景亮度统计 ------------------------
//每个矩形 4 个值：x0,y0,x1,y1。用扁平 int16_t 数组，避免结构体在
//Arduino 自动生成函数原型时引发"类型未定义"的顺序问题。
static const int16_t LUM_CLOCK_RECTS[] = {15, 78, 224, 145};                    //时钟时分秒
static const int16_t LUM_TEXT_RECTS[]  = {5, 12, 235, 76,  5, 146, 235, 238};   //文字（避开时钟带）

static uint16_t avgLumRects(const int16_t *rs, uint8_t count, uint8_t step)
{
  uint32_t sum = 0, cnt = 0;
  for (uint8_t i = 0; i < count; i++)
  {
    int16_t x0 = rs[i * 4 + 0], y0 = rs[i * 4 + 1];
    int16_t x1 = rs[i * 4 + 2], y1 = rs[i * 4 + 3];
    for (int16_t y = y0; y <= y1; y += step)
    {
      if (y < 0 || y >= BG_HEIGHT) continue;
      const uint8_t *row = bgBuff + (int32_t)y * BG_WIDTH;
      for (int16_t x = x0; x <= x1; x += step)
      {
        if (x < 0 || x >= BG_WIDTH) continue;
        uint8_t  p = row[x];
        uint16_t r = ((p >> 5) & 7) * 255 / 7;
        uint16_t g = ((p >> 2) & 7) * 255 / 7;
        uint16_t b = ( p       & 3) * 255 / 3;
        sum += (r * 30 + g * 59 + b * 11) / 100;
        cnt++;
      }
    }
  }
  return cnt ? (uint16_t)(sum / cnt) : 128;
}

//统计两个区域的背景亮度，并直接刷新滞回状态（换壁纸时做一次"硬判定"）
void computeZoneLums()
{
  lumClockZone  = avgLumRects(LUM_CLOCK_RECTS, 1, 2);
  lumTextZone   = avgLumRects(LUM_TEXT_RECTS,  2, 2);
  zoneDarkClock = (lumClockZone > AUTOC_HI);
  zoneDarkText  = (lumTextZone  > AUTOC_HI);
  Serial.printf("[V1.6] 区域背景亮度: 时钟=%u 文字=%u (深色字判定 %d/%d)\r\n",
                lumClockZone, lumTextZone, (int)zoneDarkClock, (int)zoneDarkText);
}

//------------------------ 最终配色的统一入口 ------------------------
uint16_t finalText2() { return bgAutoColor ? autoContrastColor(textColor2, lumTextZone,  zoneDarkText)  : textColor2; }
uint16_t finalText3() { return bgAutoColor ? autoContrastColor(textColor3, lumTextZone,  zoneDarkText)  : textColor3; }
uint16_t finalHour()  { return bgAutoColor ? autoContrastColor(hourColor,   lumClockZone, zoneDarkClock) : hourColor; }
uint16_t finalMin()   { return bgAutoColor ? autoContrastColor(minuteColor, lumClockZone, zoneDarkClock) : minuteColor; }
uint16_t finalSec()   { return bgAutoColor ? autoContrastColor(secondColor, lumClockZone, zoneDarkClock) : secondColor; }

//------------------------ 带描边的文字绘制（vlw 字体专用） ------------------------
void drawTextEx(TFT_eSprite &sp, const String &s, int16_t x, int16_t y, uint16_t color, bool outline)
{
  if (outline)
  {
    sp.setTextColor(outlineColorFor(color));
    for (int8_t dy = -1; dy <= 1; dy++)
      for (int8_t dx = -1; dx <= 1; dx++)
        if (dx || dy) sp.drawString(s, x + dx, y + dy);
  }
  sp.setTextColor(color);
  sp.drawString(s, x, y);
}

void drawFloatEx(TFT_eSprite &sp, float v, uint8_t dp, int16_t x, int16_t y, uint16_t color, bool outline)
{
  if (outline)
  {
    sp.setTextColor(outlineColorFor(color));
    for (int8_t dy = -1; dy <= 1; dy++)
      for (int8_t dx = -1; dx <= 1; dx++)
        if (dx || dy) sp.drawFloat(v, dp, x + dx, y + dy);
  }
  sp.setTextColor(color);
  sp.drawFloat(v, dp, x, y);
}

/* *****************************************************************
 *  V1.5.3 字模透明键控（消除数字字模黑底造成的"黑框"）
 *  JPEG 字模无 alpha 通道，其"透明区域"实为纯黑像素。
 *  绘制时把亮度低于阈值的像素替换为背景图对应像素，使背景透出。
 * *****************************************************************/
#define TINT_KEY_THRESHOLD  48     //亮度 < 48/255 视为字模黑底（含抗锯齿过渡区）
bool tft_output_keying = true;     //键控总开关（仅对走 tft_output 的字模生效）

//V1.5.4 诊断开关：置 1 时串口打印背景贴底的详细状态（排查用，正常使用置 0）
#define BG_DIAG  0

//V1.5 时钟各字段刷新标志（提前定义，供网页保存配色时强制重绘）
unsigned char Hour_sign   = 60;
unsigned char Minute_sign = 60;
unsigned char Second_sign = 60;

void setLCDBacklight(int brightness)
{
  if (LCD_BL_PIN < 0) return;
  brightness = constrain(brightness, 0, 100);
  uint32_t duty = (uint32_t)brightness * 1023 / 100;
  if (lcdBlActiveLow)
  {
    //V1.6.7 高边开关（P-MOSFET/PNP）是反逻辑：GPIO 低电平才导通。
    //        所以占空比要取反，否则亮度滑块会"越往大越暗"。
    duty = 1023 - duty;
  }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(LCD_BL_PIN, duty);
#else
  ledcWrite(LCD_PWM_CHANNEL, duty);
#endif
}

void initLCDBacklight()
{
  if (LCD_BL_PIN < 0) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(LCD_BL_PIN, LCD_PWM_FREQ, LCD_PWM_RESOLUTION);
#else
  ledcSetup(LCD_PWM_CHANNEL, LCD_PWM_FREQ, LCD_PWM_RESOLUTION);
  ledcAttachPin(LCD_BL_PIN, LCD_PWM_CHANNEL);
#endif
}

//其余状态标志位
uint8_t Wifi_en = 1; //wifi状态标志位  1：打开    0：关闭
uint8_t UpdateWeater_en = 0; //更新时间标志位
int prevTime = 0;       //滚动显示更新标志位
int DHT_img_flag = 0;   //DHT传感器使用标志位


//EEPROM参数存储地址位
int BL_addr = 1;//被写入数据的EEPROM地址编号  1亮度
int Ro_addr = 2; //被写入数据的EEPROM地址编号  2 旋转方向
int DHT_addr = 3;//3 DHT使能标志位
int UpWeT_addr = 4; //4 更新时间记录
//V1.5 新增地址（避开已用的 1~4、10~14、30+）
int TextCol2_addr  = 6;  //6,7 日期/城市/滚动条 文字色（16bit 分两字节）
int TextCol3_addr  = 8;  //8,9 室内温湿度文字色（16bit 分两字节）
//V1.5.1 时/分/秒三组独立颜色（各占2字节）
int HourCol_addr   = 17; //17,18 小时颜色
int MinCol_addr    = 19; //19,20 分钟颜色
int SecCol_addr    = 21; //21,22 秒钟颜色
int BgAlpha_addr   = 15; //15 背景暗化系数
int BgIndex_addr   = 16; //16 当前背景图槽位
int CC_addr = 10;//被写入数据的EEPROM地址编号  10城市
int wifi_addr = 30; //被写入数据的EEPROM地址编号  20wifi-ssid-psw

/* ---------------- V1.6 新增地址（每张壁纸独立预设 + 自动循环） ----------------
   已用：1~4、6~9、15~16、17~22、30~55(wifi)  ->  新地址从 32 起但会与 wifi 重叠！
   wifi 用的是 30 + sizeof(wificonf)=64 -> 30~93。故新地址放在 128 之后，绝不冲突。 */
int ScrimStr_addr   = 128; //128      底板强度 0-100（全局）
int RotateOn_addr   = 129; //129      壁纸自动循环开关
int RotateMin_addr  = 130; //130,131  自动循环间隔（分钟，uint16）
int PresetMask_addr = 132; //132      预设有效位掩码（bit i = 槽位 i 已保存）
int PresetBase_addr = 136; //136~215  5 个槽位 × 16 字节预设
//V1.6.1 缩略图缓存标记：每槽位一个"版本号"，上传/删除时自增。
//        网页缩略图URL带上它，图片没变时浏览器直接走缓存，不再重下整张240x240原图。
int BgVer_addr      = 216; //216~220  5 个槽位的图片版本号
//V1.6.2 全局配色"已写入"标志。
//  ⚠️ 原来用 0xFFFF 当"未初始化"哨兵，但白色本身就是 0xFFFF —— 于是"把某色设成白色"
//     永远存不住（读回时被当成未初始化，回落默认值）。这里用一个独立标志位替代哨兵。
int ColValid_addr   = 221; //221      0x5A=全局配色已写过（此后按原始16bit读）
//V1.6.5 背光亮度"已写过"标志。
//  ⚠️ 和配色完全同一个坑（见 V1.6.3 的说明）：本机 EEPROM 未写过的字节读出来是 **0x00**，
//     而旧代码 `if(EEPROM.read(BL_addr) <= 100)` 会把 0x00 当成"亮度 = 0" ——
//     以前 TFT_BL 没定义所以看不出来；一旦接上背光调光，那就是**开机黑屏**。
//     所以必须用一个独立标志区分"真的设成 0"和"从来没写过"。
int BLValid_addr    = 222; //222      0x5A=背光亮度已写过
//V1.6.6 亮度定时（按时段自动调亮度）
//  223       开关 0/1
//  224~243   4 段 x 5 字节（起时,起分,止时,止分,亮度）
//  244       魔数 0x5A（区分"真存过"和"未初始化"，同 V1.6.3 的教训）
int BlSchedEn_addr    = 223;
int BlSchedBase_addr  = 224;
int BlSchedMagic_addr = 244;
//V1.6.7 背光极性（运行时可切换）
//  245 标志 0x5A=已写过；246 值 0=高电平点亮(直驱) 1=低电平点亮(P-MOS/PNP)
//  同样用"已写过"标志，绝不拿值本身当判据（V1.6.3 的教训）
int BLPolValid_addr   = 245;
int BLPol_addr        = 246;
//V1.6.8 OLED「无程序时显示今日总时长」开关（全局，非每壁纸）
//  247 标志 0x5A=已写过；248 值 0/1
int OledTodayValid_addr = 247;
int OledTodayEn_addr    = 248;

/* ---------------- V1.6.6 亮度定时（按时段自动调亮度） ----------------
 * 规则：定时开启时亮度**完全**由时段决定（手动滑块被忽略）；
 *       当前时间不匹配任何时段 -> 回落到手动亮度。
 *       跨零点支持（如 23:00 -> 07:00）。
 * ------------------------------------------------------------------- */

//NTP 是否已经同步（没同步时 hour()/minute() 是 1970 年，绝不能用）
bool timeValid()
{
  return (now() > 1600000000L);        // 2020-09-13 之后才算有效
}

//"HH:MM"（也容忍带秒）-> 时/分
static bool parseHM(const String &s, uint8_t *ph, uint8_t *pm)
{
  int c = s.indexOf(':');
  if (c < 1) return false;
  int h = s.substring(0, c).toInt();
  int m = s.substring(c + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  *ph = (uint8_t)h; *pm = (uint8_t)m;
  return true;
}

//时/分 -> "HH:MM"（给网页 input[type=time] 用）
static String hmStr(uint8_t h, uint8_t m)
{
  char b[8];
  sprintf(b, "%02u:%02u", (unsigned)h, (unsigned)m);
  return String(b);
}

//默认时段（只把值填好，开关默认关闭，不改变用户当前观感）
void blSchedDefaults()
{
  blSeg[0].sh = 7;  blSeg[0].sm = 0; blSeg[0].eh = 19; blSeg[0].em = 0; blSeg[0].br = 100;
  blSeg[1].sh = 19; blSeg[1].sm = 0; blSeg[1].eh = 23; blSeg[1].em = 0; blSeg[1].br = 60;
  blSeg[2].sh = 23; blSeg[2].sm = 0; blSeg[2].eh = 7;  blSeg[2].em = 0; blSeg[2].br = 20;
  blSeg[3].sh = 0;  blSeg[3].sm = 0; blSeg[3].eh = 0;  blSeg[3].em = 0; blSeg[3].br = 0;   //不启用
}

//从 EEPROM 读定时配置；没写过就装默认值并落盘（之后都走确定路径）
void loadBlSched()
{
  if (EEPROM.read(BlSchedMagic_addr) != 0x5A)
  {
    blSchedEn = false;
    blSchedDefaults();
    saveBlSched();
    Serial.println("[V1.6.6] 亮度定时：首次初始化（装默认时段，开关关闭）");
    return;
  }
  blSchedEn = (EEPROM.read(BlSchedEn_addr) == 1);
  bool bad = false;
  for (uint8_t i = 0; i < BL_SCHED_MAX; i++)
  {
    int a = BlSchedBase_addr + (int)i * 5;
    blSeg[i].sh = EEPROM.read(a + 0);
    blSeg[i].sm = EEPROM.read(a + 1);
    blSeg[i].eh = EEPROM.read(a + 2);
    blSeg[i].em = EEPROM.read(a + 3);
    blSeg[i].br = EEPROM.read(a + 4);
    if (blSeg[i].sh > 23 || blSeg[i].sm > 59 || blSeg[i].eh > 23 ||
        blSeg[i].em > 59 || blSeg[i].br > 100) bad = true;
  }
  if (bad) { blSchedDefaults(); saveBlSched(); }   //数据不合法 -> 整体回落默认
  Serial.printf("[V1.6.6] 亮度定时:%s  时段 %s-%s:%d%% | %s-%s:%d%% | %s-%s:%d%% | %s-%s:%d%%\r\n",
                blSchedEn ? "开" : "关",
                hmStr(blSeg[0].sh, blSeg[0].sm).c_str(), hmStr(blSeg[0].eh, blSeg[0].em).c_str(), blSeg[0].br,
                hmStr(blSeg[1].sh, blSeg[1].sm).c_str(), hmStr(blSeg[1].eh, blSeg[1].em).c_str(), blSeg[1].br,
                hmStr(blSeg[2].sh, blSeg[2].sm).c_str(), hmStr(blSeg[2].eh, blSeg[2].em).c_str(), blSeg[2].br,
                hmStr(blSeg[3].sh, blSeg[3].sm).c_str(), hmStr(blSeg[3].eh, blSeg[3].em).c_str(), blSeg[3].br);
}

void saveBlSched()
{
  EEPROM.write(BlSchedEn_addr, blSchedEn ? 1 : 0);
  for (uint8_t i = 0; i < BL_SCHED_MAX; i++)
  {
    int a = BlSchedBase_addr + (int)i * 5;
    EEPROM.write(a + 0, blSeg[i].sh);
    EEPROM.write(a + 1, blSeg[i].sm);
    EEPROM.write(a + 2, blSeg[i].eh);
    EEPROM.write(a + 3, blSeg[i].em);
    EEPROM.write(a + 4, blSeg[i].br);
  }
  EEPROM.write(BlSchedMagic_addr, 0x5A);
  EEPROM.commit();
  delay(5);
}

//当前 时:分 落在哪一段；返回 -1 = 不匹配
int blSchedMatch(int hh, int mm)
{
  int t = hh * 60 + mm;
  for (uint8_t i = 0; i < BL_SCHED_MAX; i++)
  {
    if (blSeg[i].br == 0) continue;                     //亮度 0 = 该段不启用
    int s = (int)blSeg[i].sh * 60 + blSeg[i].sm;
    int e = (int)blSeg[i].eh * 60 + blSeg[i].em;
    if (s == e) continue;                               //起止相同 = 该段不启用
    if (s < e) { if (t >= s && t < e) return (int)i; }
    else       { if (t >= s || t < e) return (int)i; }  //跨零点
  }
  return -1;
}

//按"定时优先、否则手动"算出目标亮度并下发（值没变就不重复写 PWM）
void blApply(bool force)
{
  uint8_t target = (uint8_t)constrain(LCD_BL_PWM, LCD_BL_MIN, 100);
  if (blSchedEn && timeValid())
  {
    int idx = blSchedMatch(hour(), minute());
    if (idx >= 0) target = (uint8_t)constrain((int)blSeg[idx].br, LCD_BL_MIN, 100);
  }
  if (force || target != blCurBright)
  {
    blCurBright = target;
    setLCDBacklight(target);
    Serial.printf("[V1.6.6] 亮度下发 %d%%\r\n", target);
  }
}

//loop 里周期调用：10 秒查一次（本来也只需要分钟级精度）
void blSchedTask()
{
  static uint32_t lastMs = 0;
  if (millis() - lastMs < 10000UL) return;
  lastMs = millis();
  blApply(false);
}


time_t prevDisplay = 0;       //显示时间显示记录
unsigned long weaterTime = 0; //天气更新时间记录
String SMOD = "";//串口数据存储


/*** Component objects ***/
Number      dig;
WeatherNum  wrat;


uint32_t targetTime = 0;   

int tempnum = 0;   //温度百分比
int huminum = 0;   //湿度百分比
int tempcol =0xffff;   //温度显示颜色
int humicol =0xffff;   //湿度显示颜色

//Web网站服务器
WebServer server(80);// 建立esp8266网站服务器对象


//NTP服务器参数
static const char ntpServerName[] = "ntp6.aliyun.com";
const int timeZone = 8;     //东八区

//wifi连接UDP设置参数
WiFiUDP Udp;
WiFiClient wificlient;
unsigned int localPort = 8000;
float duty=0;


//函数声明
time_t getNtpTime();
void digitalClockDisplay(int reflash_en);
void printDigits(int digits);
String num2str(int digits);
void sendNTPpacket(IPAddress &address);
//V1.6.4 新增 allowFetch：整屏重绘时可以要求"不联网"，
//        避免切背景时被一次阻塞式天气抓取拖慢（见 refreshAfterBgChange）
void LCD_reflash(int en, bool allowFetch = true);
void savewificonfig();
void readwificonfig();
void deletewificonfig();
#if WebSever_EN
void Web_Sever_Init();
void Web_Sever();
//V1.5 Web 新增处理函数
void handleSave();
void handleUpload();
void handleUploadDone();
void handleDelBg();
void handleSetBg();
void handlePreview();
//V1.6.8 网络管理入口
void handleWifiConfig();
void handleWifiForget();
String colorToHex(uint16_t c);
uint16_t hexToColor(String hex);
uint8_t bgAlphaToPercent();
uint8_t percentToBgAlpha(int p);
#endif
void saveCityCodetoEEP(int * citycode);
void readCityCodefromEEP(int * citycode);
void loading(byte delayTime);
//V1.5 新增函数声明
void loadTextColors();
void saveTextColors();
void drawBg();
void refreshAfterBgChange();
void bgDrawRegion(int16_t x, int16_t y, int16_t w, int16_t h);
//V1.5.4 返回 true=已把背景贴入 sprite；false=无背景画布可用（调用方需用透明方式绘制，切勿填黑）
bool bgFillSprite(TFT_eSprite &sp, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t textColor);
void drawIconWithBg(int16_t x, int16_t y, const uint8_t *icon, uint32_t iconSize);
void bgTouchTask();
void bgSwitchTo(uint8_t slot);
void bgScanFiles();
bool bgSlotExists(uint8_t slot);
String bgFilePath(uint8_t slot);

//V1.6 新增函数声明
void computeZoneLums();
uint16_t finalText2();
uint16_t finalText3();
uint16_t finalHour();
uint16_t finalMin();
uint16_t finalSec();
void drawTextEx(TFT_eSprite &sp, const String &s, int16_t x, int16_t y, uint16_t color, bool outline);
void drawFloatEx(TFT_eSprite &sp, float v, uint8_t dp, int16_t x, int16_t y, uint16_t color, bool outline);
void savePreset(uint8_t slot);
void loadPreset(uint8_t slot);
void loadGlobalDisplaySettings();
void saveGlobalDisplaySettings();
void bgAutoRotateTask();
void loadBgVersions();
void bgBumpVer(uint8_t slot);

/* *****************************************************************
 *  函数
 * *****************************************************************/
//读取保存城市代码
void saveCityCodetoEEP(int * citycode)
{
  for(int cnum=0;cnum<5;cnum++)
  {
    EEPROM.write(CC_addr+cnum,*citycode%100);//城市地址写入城市代码
    EEPROM.commit();//保存更改的数据
    *citycode = *citycode/100;
    delay(5);
  }
}
void readCityCodefromEEP(int * citycode)
{
  for(int cnum=5;cnum>0;cnum--)
  {          
    *citycode = *citycode*100;
    *citycode += EEPROM.read(CC_addr+cnum-1); 
    delay(5);
  }
}

//wifi ssid，psw保存到eeprom
void savewificonfig()
{
  //开始写入
  uint8_t *p = (uint8_t*)(&wificonf);
  for (int i = 0; i < sizeof(wificonf); i++)
  {
    EEPROM.write(i + wifi_addr, *(p + i)); //在闪存内模拟写入
  }
  delay(10);
  EEPROM.commit();//执行写入ROM
  delay(10);
}
//删除原有eeprom中的信息
void deletewificonfig()
{
  config_type deletewifi ={{""},{""}};
  uint8_t *p = (uint8_t*)(&deletewifi);
  for (int i = 0; i < sizeof(deletewifi); i++)
  {
    EEPROM.write(i + wifi_addr, *(p + i)); //在闪存内模拟写入
  }
  delay(10);
  EEPROM.commit();//执行写入ROM
  delay(10);
}

//从eeprom读取WiFi信息ssid，psw
void readwificonfig()
{
  uint8_t *p = (uint8_t*)(&wificonf);
  for (int i = 0; i < sizeof(wificonf); i++)
  {
    *(p + i) = EEPROM.read(i + wifi_addr);
  }
  // EEPROM.commit();
  // ssid = wificonf.stassid;
  // pass = wificonf.stapsw;
  Serial.printf("Read WiFi Config.....\r\n");
  Serial.printf("SSID:%s\r\n",wificonf.stassid);
  Serial.printf("PSW:%s\r\n",wificonf.stapsw);
  Serial.printf("Connecting.....\r\n");
}

//TFT屏幕输出函数（用于普通图标/图片，不做暗化处理）
//V1.5.1：若 tintMode!=0，则把解码像素按"明暗"重映射到 tintTarget 颜色，
//        使 JPEG 数字字模支持任意配色（保留原图抗锯齿/描边层次）
//V1.5.3：新增背景透明键控（color key）。JPEG 字模是 3 通道、无 alpha，
//        其"透明区域"实为纯黑像素（实测数字字模约 52% 像素为 (0,0,0)）。
//        直接整块 pushImage 会在屏幕上盖出黑色矩形（即用户看到的"黑框"）。
//        这里对近黑像素改为从背景画布取色填入，使背景图透出来。
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  if ( y >= tft.height() ) return 0;

  //有背景图时：把字模的黑底替换为背景对应区域（透明键控）
  bool useKey = (bgCount > 0 && bgCanvasReady && tft_output_keying);

#if BG_DIAG
  static uint32_t _toN = 0;
  static uint32_t _keyHits = 0;
  if (_toN < 10) {
    Serial.printf("[tft_output#%u] x=%d y=%d w=%d h=%d useKey=%d\n",
                  _toN, x, y, w, h, (int)useKey);
    _toN++;
  }
#endif

  uint32_t pixelCount = (uint32_t)w * h;
  for (uint32_t i = 0; i < pixelCount; i++)
  {
    uint16_t c = bitmap[i];

    //还原为自然 RGB565（便于分量计算）
    uint16_t nat = (uint16_t)((c << 8) | (c >> 8));
    uint8_t r8 = (((nat >> 11) & 0x1F) * 255) / 31;
    uint8_t g8 = (((nat >> 5)  & 0x3F) * 255) / 63;
    uint8_t b8 = (( nat        & 0x1F) * 255) / 31;
    uint16_t lum = (uint16_t)((r8 * 30 + g8 * 59 + b8 * 11) / 100);

    //V1.5.3 透明键控：近黑像素视为"透明"，改填背景图对应像素
    if (useKey && lum < TINT_KEY_THRESHOLD)
    {
      int32_t bx = x + (int32_t)(i % w);
      int32_t by = y + (int32_t)(i / w);
      if (bx >= 0 && bx < BG_WIDTH && by >= 0 && by < BG_HEIGHT)
      {
#if BG_DIAG
        _keyHits++;
#endif
        bitmap[i] = bg332to565sw(bgBuff[(int32_t)by * BG_WIDTH + bx]);  //RGB332 -> 交换序565
        continue;
      }
    }

    if (tintMode != 0)
    {
      //目标色分量（8bit）
      uint16_t tr8 = (((tintTarget >> 11) & 0x1F) * 255) / 31;
      uint16_t tg8 = (((tintTarget >> 5)  & 0x3F) * 255) / 63;
      uint16_t tb8 = (( tintTarget        & 0x1F) * 255) / 31;

      //亮度作为着色强度（保留原图明暗层次）
      uint8_t nr8 = (uint8_t)((uint32_t)tr8 * lum / 255);
      uint8_t ng8 = (uint8_t)((uint32_t)tg8 * lum / 255);
      uint8_t nb8 = (uint8_t)((uint32_t)tb8 * lum / 255);

      uint16_t res = (uint16_t)(((nr8 >> 3) << 11) | ((ng8 >> 2) << 5) | (nb8 >> 3));
      bitmap[i] = (uint16_t)((res << 8) | (res >> 8));   //交换回去
    }
  }

  tft.pushImage(x, y, w, h, bitmap);
#if BG_DIAG
  {
    static uint32_t _rep = 0;
    if (_rep < 12) {
      Serial.printf("    键控命中累计=%u (本块 %ux%u)\n", _keyHits, w, h);
      _rep++;
    }
  }
#endif
  // Return 1 to decode next block
  return 1;
}

//V1.5.3 背景透明键控开关与阈值
//阈值 32 表示亮度 <32/255 的像素视为字模黑底（纯黑=0，抗锯齿最暗边缘约 20-40）
void setKeying(bool on) { tft_output_keying = on; }

//供 number.cpp 调用：设置/关闭本次解码的着色目标色
void setTintColor(uint16_t color)
{
  tintTarget = color;
  tintMode   = 1;
}

void clearTint()
{
  tintMode = 0;
}

//======================== V1.5 新增功能函数 ========================

/* ---------------- 文字配色：EEPROM 读写 ---------------- */
//V1.6.3 判断某个 16bit 颜色地址是否"看起来从未被写过"。
//★关键：实测本机 EEPROM 未写过的字节读出来是 0x00，**不是 0xFF**
//  （证据：bgVer 216~220 从未写入，网页显示 v=0，而只有读到 0xFF 才会改判）。
//  旧代码只把 0xFF,0xFF 当未初始化 → 0x0000 被当成"用户设的纯黑"，
//  于是首次启动就把全局配色读成全黑，并被 savePreset 传染到每一张壁纸。
//  所以 00/00 与 FF/FF 都要判为未初始化。
static bool colorAddrUnwritten(int addr)
{
  uint8_t lo = EEPROM.read(addr);
  uint8_t hi = EEPROM.read(addr + 1);
  return (lo == 0x00 && hi == 0x00) || (lo == 0xFF && hi == 0xFF);
}

//原样读取，不做任何"未初始化"判断（调用方自己保证该地址已被写过）
static uint16_t readColor16Raw(int addr)
{
  return (uint16_t)EEPROM.read(addr) | ((uint16_t)EEPROM.read(addr + 1) << 8);
}

static void writeColor16(int addr, uint16_t c)
{
  EEPROM.write(addr,     c & 0xFF);
  EEPROM.write(addr + 1, (c >> 8) & 0xFF);
}

void loadTextColors()
{
  //V1.6.3 默认值集中在此，且用 colorAddrUnwritten() 判定"未写过"，
  //        不再依赖 0xFFFF 哨兵（白色本身就是 0xFFFF，会误判）。
  const uint16_t defT2 = 0xFFFF;   //日期等 默认白
  const uint16_t defT3 = 0xFFFF;   //温湿度 默认白
  const uint16_t defH  = 0xFFFF;   //小时   默认白
  const uint16_t defM  = 0xFD20;   //分钟   默认橙
  const uint16_t defS  = 0xFFFF;   //秒钟   默认白

  textColor2  = colorAddrUnwritten(TextCol2_addr) ? defT2 : readColor16Raw(TextCol2_addr);
  textColor3  = colorAddrUnwritten(TextCol3_addr) ? defT3 : readColor16Raw(TextCol3_addr);
  hourColor   = colorAddrUnwritten(HourCol_addr)  ? defH  : readColor16Raw(HourCol_addr);
  minuteColor = colorAddrUnwritten(MinCol_addr)   ? defM  : readColor16Raw(MinCol_addr);
  secondColor = colorAddrUnwritten(SecCol_addr)   ? defS  : readColor16Raw(SecCol_addr);

  //"已写过"标志缺失时立刻落盘一次：让本次以及后续启动都走确定路径，
  //避免每次开机都靠"猜"某个地址有没有被写过。
  if (EEPROM.read(ColValid_addr) != 0x5A)
  {
    saveTextColors();
    Serial.println("[V1.6.3] 全局配色首次初始化（写入默认值 + 有效标志）");
  }

  Serial.printf("[V1.6.3] 配色: 时=%04X 分=%04X 秒=%04X text2=%04X text3=%04X\r\n",
                hourColor, minuteColor, secondColor, textColor2, textColor3);
}

void saveTextColors()
{
  writeColor16(TextCol2_addr, textColor2);
  writeColor16(TextCol3_addr, textColor3);
  writeColor16(HourCol_addr,  hourColor);
  writeColor16(MinCol_addr,   minuteColor);
  writeColor16(SecCol_addr,   secondColor);
  EEPROM.write(ColValid_addr, 0x5A);   //V1.6.2 标记"已写过"，此后按原始16bit读
  EEPROM.commit();
  delay(5);
}

/* ---------------- V1.6 每张壁纸独立预设 ---------------- */
static int presetAddr(uint8_t slot) { return PresetBase_addr + (int)slot * PRESET_SIZE; }

//校验值：前 P_MAGIC 个字节求和后取反。
//注意用 ~sum 而不是 sum：全 0x00 时 sum=0 -> 0xFF；全 0xFF 时也不会等于 0xFF，
//两种情况都不会被误判成"有效记录"。
static uint8_t presetSum(int a)
{
  uint8_t s = 0;
  for (uint8_t i = 0; i < P_MAGIC; i++) s = (uint8_t)(s + EEPROM.read(a + i));
  return (uint8_t)(~s);
}

//V1.6.3 该槽位是否存有"本固件写入的"有效预设。
//只有魔数与校验都对才认——这样无论 EEPROM 里是 0x00、0xFF 还是别的垃圾，
//都不会被当成配色加载（这是黑配色的根因，见上方 P_MAGIC 处说明）。
static bool presetValid(int a)
{
  if (EEPROM.read(a + P_MAGIC) != PRESET_MAGIC) return false;
  return EEPROM.read(a + P_CHK) == presetSum(a);
}

//把当前全局配色/开关 存入指定槽位的预设
void savePreset(uint8_t slot)
{
  if (slot >= BG_MAX) return;
  int a = presetAddr(slot);
  writeColor16(a + P_HOUR, hourColor);
  writeColor16(a + P_MIN,  minuteColor);
  writeColor16(a + P_SEC,  secondColor);
  writeColor16(a + P_TXT2, textColor2);
  writeColor16(a + P_TXT3, textColor3);
  EEPROM.write(a + P_ALPHA, bgAlphaToPercent());
  EEPROM.write(a + P_FLAGS,
               (bgAutoColor ? PF_AUTOCOLOR : 0) |
               (bgScrimOn   ? PF_SCRIM    : 0) |
               (bgOutlineOn ? PF_OUTLINE  : 0) |
               (bgNumOutlineOn   ? PF_NUMOUTLINE : 0) |
               (bgNumOutlineAuto ? 0 : PF_NUMOUTFIX));   //取反语义：非自动才置位
  writeColor16(a + P_NUMOC, bgNumOutlineColor);          //数字描边固定色
  //先算校验（覆盖前 12 字节），再写魔数——顺序不能反：
  //中途掉电只会得到一个"魔数不对/校验不过"的无效记录，而不会得到一个假的有效记录。
  EEPROM.write(a + P_CHK,   presetSum(a));
  EEPROM.write(a + P_MAGIC, PRESET_MAGIC);
  presetMask |= (uint8_t)(1 << slot);       //仅为 /dbg 展示，不再用于判断有效性
  EEPROM.write(PresetMask_addr, presetMask);
  EEPROM.commit();
  delay(5);
  Serial.printf("[V1.6.3] 已保存第 %d 张壁纸的预设 (校验=0x%02X)\r\n",
                slot + 1, EEPROM.read(a + P_CHK));
}

//从指定槽位载入预设到全局；该槽位尚无有效预设时用当前值初始化它（继承现有观感）
void loadPreset(uint8_t slot)
{
  if (slot >= BG_MAX) return;
  int a = presetAddr(slot);
  //V1.6.3 改成"魔数+校验"判断，不再使用 presetMask。
  //presetMask 读到的 0xFF & 0x1F 会得到 0x1F（五槽全"有效"），
  //于是从未写过的槽位被原样读出 0x0000（黑）——这就是用户看到的黑配色。
  if (!presetValid(a)) { savePreset(slot); return; }
  //有效记录里必须原样读！不能用 readColor16（0xFFFF 哨兵会把用户设的白色误判为未初始化）。
  hourColor   = readColor16Raw(a + P_HOUR);
  minuteColor = readColor16Raw(a + P_MIN);
  secondColor = readColor16Raw(a + P_SEC);
  textColor2  = readColor16Raw(a + P_TXT2);
  textColor3  = readColor16Raw(a + P_TXT3);
  uint8_t ap = EEPROM.read(a + P_ALPHA);
  bgAlpha = (ap <= 100) ? percentToBgAlpha(ap) : 255;
  uint8_t f = EEPROM.read(a + P_FLAGS);
  bgAutoColor = (f & PF_AUTOCOLOR) != 0;
  bgScrimOn   = (f & PF_SCRIM)    != 0;
  bgOutlineOn = (f & PF_OUTLINE)  != 0;
  //V1.6.6 数字描边：开关独立；"自动取反"是取反语义（旧预设没有这个位 -> 自动）
  bgNumOutlineOn    = (f & PF_NUMOUTLINE) != 0;
  bgNumOutlineAuto  = (f & PF_NUMOUTFIX)  == 0;
  bgNumOutlineColor = readColor16Raw(a + P_NUMOC);
  Hour_sign = 60; Minute_sign = 60; Second_sign = 60;   //强制重绘时钟
  Serial.printf("[V1.6.6] 已载入第 %d 张壁纸预设 时=%04X 分=%04X 秒=%04X t2=%04X t3=%04X 数字描边=%d(自动=%d)\r\n",
                slot + 1, hourColor, minuteColor, secondColor, textColor2, textColor3,
                (int)bgNumOutlineOn, (int)bgNumOutlineAuto);
}

//载入全局显示设置（底板强度 / 自动循环 / 预设掩码）
void loadGlobalDisplaySettings()
{
  uint8_t s = EEPROM.read(ScrimStr_addr);
  bgScrimStrength = (s <= 100) ? s : 100;
  bgAutoRotate = (EEPROM.read(RotateOn_addr) == 1);
  uint16_t m = (uint16_t)EEPROM.read(RotateMin_addr) |
               ((uint16_t)EEPROM.read(RotateMin_addr + 1) << 8);
  bgRotateMin = (m >= 1 && m <= 1440) ? m : 10;
  presetMask = (uint8_t)(EEPROM.read(PresetMask_addr) & 0x1F);
  Serial.printf("[V1.6] 底板强度=%u 自动循环=%d 间隔=%u分 预设掩码=0x%02X\r\n",
                bgScrimStrength, (int)bgAutoRotate, bgRotateMin, presetMask);
}

void saveGlobalDisplaySettings()
{
  EEPROM.write(ScrimStr_addr, bgScrimStrength);
  EEPROM.write(RotateOn_addr, bgAutoRotate ? 1 : 0);
  EEPROM.write(RotateMin_addr,     bgRotateMin & 0xFF);
  EEPROM.write(RotateMin_addr + 1, (bgRotateMin >> 8) & 0xFF);
  EEPROM.commit();
  delay(5);
}

/* ---------------- V1.6.1 缩略图版本号（缓存标记） ---------------- */
//读出各槽位版本号；读不到（首次）时按"该槽位是否有图"给个初值
void loadBgVersions()
{
  bool dirty = false;
  for (uint8_t i = 0; i < BG_MAX; i++)
  {
    uint8_t v = EEPROM.read(BgVer_addr + i);
    //V1.6.3 0x00 与 0xFF 都算"未写过"。
    //实测本机未写过的字节读出来是 0x00 而不是 0xFF —— 旧代码只判 0xFF，
    //于是版本号永远是 0，缩略图 URL 不随图片变化（缓存标记失效）。
    if (v == 0xFF || v == 0x00) {
      v = bgSlotExists(i) ? 1 : 0;
      EEPROM.write(BgVer_addr + i, v);
      dirty = true;
    }
    bgSlotVer[i] = v;
  }
  if (dirty) { EEPROM.commit(); delay(5); }
}

//图片内容变了 -> 版本号自增，网页缩略图URL随之变化，浏览器才会重新拉取
void bgBumpVer(uint8_t slot)
{
  if (slot >= BG_MAX) return;
  bgSlotVer[slot] = (uint8_t)(bgSlotVer[slot] + 1);
  if (bgSlotVer[slot] == 0) bgSlotVer[slot] = 1;   //0 保留给"无图"
  EEPROM.write(BgVer_addr + slot, bgSlotVer[slot]);
  EEPROM.commit();
  delay(5);
}

/* ---------------- 背景图：文件名与状态 ---------------- */
//槽位 -> 文件名，如 /bg0.jpg
String bgFilePath(uint8_t slot)
{
  return String("/bg") + String(slot) + String(".jpg");
}

//扫描 SPIFFS，统计已上传的背景图数量（用于跳过空槽）
void bgScanFiles()
{
  bgCount = 0;
  for (uint8_t i = 0; i < BG_MAX; i++)
  {
    if (SPIFFS.exists(bgFilePath(i)))
      bgCount++;
  }
  Serial.printf("[V1.5] 背景图已上传 %d 张\r\n", bgCount);
}

//判断某槽位是否有图
bool bgSlotExists(uint8_t slot)
{
  if (slot >= BG_MAX) return false;
  return SPIFFS.exists(bgFilePath(slot));
}

//从指定槽位开始，向后找到第一个"有图"的槽位（跳过空槽）；返回 255 表示全空
uint8_t bgNextValidSlot(uint8_t from)
{
  for (uint8_t n = 1; n <= BG_MAX; n++)
  {
    uint8_t s = (from + n) % BG_MAX;
    if (bgSlotExists(s)) return s;
  }
  return 255;
}

/* ---------------- 背景图：绘制 ---------------- */

//背景解码专用回调：把解码结果同时写入背景画布（用于后续 sprite 贴底）
//说明：TJpg_Decoder 的回调固定输出到 tft，因此这里在回调内额外拷贝到画布
bool tft_output_bg(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  if (y >= BG_HEIGHT) return 0;

  //暗化处理
  //注意：TJpgDec.setSwapBytes(true) 后，bitmap 中是"字节交换"过的 RGB565，
  //      需先交换回来做分量运算，再交换回去交给 pushImage
  if (bgAlpha < 255)
  {
    uint32_t pixelCount = (uint32_t)w * h;
    for (uint32_t i = 0; i < pixelCount; i++)
    {
      uint16_t c = bitmap[i];
      c = (uint16_t)((c << 8) | (c >> 8));   //还原为自然 RGB565

      uint8_t r = (c >> 11) & 0x1F;
      uint8_t g = (c >> 5)  & 0x3F;
      uint8_t b =  c        & 0x1F;
      r = (uint8_t)((uint32_t)r * bgAlpha >> 8);
      g = (uint8_t)((uint32_t)g * bgAlpha >> 8);
      b = (uint8_t)((uint32_t)b * bgAlpha >> 8);
      c = (uint16_t)((r << 11) | (g << 5) | b);

      bitmap[i] = (uint16_t)((c << 8) | (c >> 8));  //再交换回去
    }
  }

  //写入屏幕
  //TJpgDec(setSwapBytes(true)) 输出"交换序"，TFT 的 _swapBytes 默认 false 直发，
  //正好与屏幕 SPITFT 所需字节序吻合 → 屏幕显示正确。
  tft.pushImage(x, y, w, h, bitmap);

  //V1.5.4 同时把该块降为 RGB332 存入静态背景缓冲（供 sprite 贴底 / 字模键控）
  if (bgCanvasReady)
  {
    for (int16_t row = 0; row < (int16_t)h; row++)
    {
      int32_t by = y + row;
      if (by < 0 || by >= BG_HEIGHT) continue;
      int32_t bx = x;
      int32_t rows = (int32_t)row * w;
      int32_t cnt  = w;
      if (bx < 0) { cnt += bx; rows -= bx; bx = 0; }
      if (bx + cnt > BG_WIDTH) cnt = BG_WIDTH - bx;
      uint8_t *dstRow = bgBuff + (int32_t)by * BG_WIDTH + bx;
      for (int32_t k = 0; k < cnt; k++)
        dstRow[k] = bg565to332(bitmap[rows + k]);
    }
  }

  return 1;
}

//在屏幕指定位置绘制当前背景图的一块区域（直接推到屏幕）
//x,y,w,h 为目标区域屏幕坐标
void bgDrawRegion(int16_t x, int16_t y, int16_t w, int16_t h)
{
  if (bgCount == 0 || !bgCanvasReady) return;
  if (x >= BG_WIDTH || y >= BG_HEIGHT) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > BG_WIDTH)  w = BG_WIDTH  - x;
  if (y + h > BG_HEIGHT) h = BG_HEIGHT - y;
  if (w <= 0 || h <= 0) return;

  //V1.5.4 直接从 RGB332 背景缓冲展开成 565 推到屏幕（逐行，行缓冲在栈上）
  uint16_t line[BG_WIDTH];
  if (w > BG_WIDTH) w = BG_WIDTH;
  for (int16_t row = 0; row < h; row++)
  {
    const uint8_t *srcRow = bgBuff + (int32_t)(y + row) * BG_WIDTH + x;
    for (int16_t k = 0; k < w; k++) line[k] = bg332to565sw(srcRow[k]);
    tft.pushImage(x, y + row, w, 1, line);
  }
}

//把背景图的一块区域填进指定 sprite（供 sprite 内部先铺底，再绘文字）
//关键：避免 sprite 用纯黑 fillSprite 把背景图整块盖住
//返回值：true=已成功贴入背景；false=背景画布不可用（调用方必须改用透明绘制）
bool bgFillSprite(TFT_eSprite &sp, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t textColor)
{
#if BG_DIAG
  static uint32_t _dgN = 0;
  if (_dgN < 15) {
    Serial.printf("[bgFill#%u] sp=%dx%d x=%d y=%d w=%d h=%d ready=%d cnt=%d bpp=%d\n",
                  _dgN, sp.width(), sp.height(), x, y, w, h,
                  bgCanvasReady, bgCount, sp.getColorDepth());
    if (bgCanvasReady) {
      Serial.printf("    bgBuff[0]=0x%02X bgBuff[%d]=0x%02X\n",
                    bgBuff[0], 120 * BG_WIDTH + 120, bgBuff[120 * BG_WIDTH + 120]);
    }
    _dgN++;
  }
#endif
  //没有背景图（bgCount==0）：按 V1.4 原行为填黑底，屏幕就是黑底，不会突兀。
  if (bgCount == 0)
  {
    sp.fillSprite(bgColor);
    return true;
  }
  //有背景图但缓存尚未就绪（理论上静态缓冲恒可用，仅极端情形）：
  //返回 false 让调用方决定，绝不在此填黑。
  if (!bgCanvasReady)
  {
    return false;
  }

  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= BG_WIDTH || y >= BG_HEIGHT) return false;
  if (x + w > BG_WIDTH)  w = BG_WIDTH  - x;
  if (y + h > BG_HEIGHT) h = BG_HEIGHT - y;
  if (w <= 0 || h <= 0)  return false;

  uint16_t *dst = (uint16_t *)sp.getPointer();
  if (dst == nullptr) return false;

  int16_t sprW = sp.width();       // sprite 实际行宽（16bpp 下等于 createSprite 的 w）
  if (sprW < w) w = sprW;          // 防御：目标比源窄时按目标宽截断
  if (w <= 0) return false;

  //V1.6 自适应底板（方案1）：先算出该 sprite 文字亮度对应的"目标背景亮度"
  //V1.6.3 修字节序：textColor 是自然序 RGB565（同 setColorValue 的说明），不要再交换。
  uint16_t textLum  = lum565nat(textColor);
  uint16_t scrimEff = lumTextZone;
  if (bgScrimOn && bgScrimStrength > 0)
    scrimEff = scrimTargetLum(lumTextZone, textLum);

  //V1.5.4 逐行把 RGB332 背景展开成"交换序 RGB565"写入 sprite。
  //        直接写 sp.getPointer()，绕开 pushImage 的 viewport/datum 逻辑。
  for (int16_t row = 0; row < h; row++)
  {
    const uint8_t *srcRow = bgBuff + (int32_t)(y + row) * BG_WIDTH + x;
    uint16_t      *dstRow = dst + (int32_t)row * sprW;
    if (bgScrimOn && bgScrimStrength > 0)
    {
      for (int16_t k = 0; k < w; k++)
      {
        uint16_t nat = scrimPixelNat(bg332to565nat(srcRow[k]), lumTextZone, scrimEff);
        dstRow[k] = (uint16_t)((nat << 8) | (nat >> 8));   //转交换序
      }
    }
    else
    {
      for (int16_t k = 0; k < w; k++) dstRow[k] = bg332to565sw(srcRow[k]);
    }
  }
#if BG_DIAG
  if (_dgN <= 15 && dst) {
    Serial.printf("    -> 写入OK sprW=%d sp[0]=0x%04X sp[mid]=0x%04X\n",
                  sprW, dst[0], dst[(int32_t)(h / 2) * sprW + w / 2]);
  }
#endif
  return true;
}

//V1.6.4 从 JPEG 头部读出真实宽高（扫描 SOFn 段）。失败返回 false。
//为什么要读真实尺寸：还原区域必须**恰好等于图标尺寸**，写死会连带擦掉旁边的元素。
static bool jpegSizeOf(const uint8_t *jpg, uint32_t len, int16_t *pw, int16_t *ph)
{
  if (jpg == nullptr || len < 16) return false;
  uint32_t i = 2;                                  //跳过 SOI(FF D8)
  while (i + 9 < len)
  {
    if (jpg[i] != 0xFF) { i++; continue; }
    uint8_t mk = jpg[i + 1];
    if (mk == 0xFF) { i++; continue; }             //填充字节
    if (mk == 0xD8 || mk == 0x01 || (mk >= 0xD0 && mk <= 0xD7)) { i += 2; continue; }
    if (mk == 0xD9) return false;                  //已到 EOI，仍未见 SOF
    uint16_t seg = ((uint16_t)jpg[i + 2] << 8) | jpg[i + 3];
    //SOF0~SOF15，排除 DHT(C4)/JPG(C8)/DAC(CC)
    if (mk >= 0xC0 && mk <= 0xCF && mk != 0xC4 && mk != 0xC8 && mk != 0xCC)
    {
      int16_t h = (int16_t)(((uint16_t)jpg[i + 5] << 8) | jpg[i + 6]);
      int16_t w = (int16_t)(((uint16_t)jpg[i + 7] << 8) | jpg[i + 8]);
      if (w <= 0 || h <= 0) return false;
      *pw = w; *ph = h;
      return true;
    }
    if (seg < 2) return false;
    i += 2 + seg;
  }
  return false;
}

//在指定位置绘制一个 JPEG 图标：先恢复该区域背景，再叠图标
//用于温度/湿度图标、天气图标等会周期性重绘的图片
void drawIconWithBg(int16_t x, int16_t y, const uint8_t *icon, uint32_t iconSize)
{
  //★V1.6.4 还原区域改成"图标真实尺寸"，不再写死 60x60。
  //  实测 temperature / humidity 只有 24x24，而 60x60 的还原框会向右盖到 x=75，
  //  把 45~96 处的两条温湿度指示条（tempWin@(45,192) / humidityWin@(45,222)）削掉左半截。
  //  开机时是先画图标、后画天气，所以看不出来；但切背景时是"先画天气条、再画图标"
  //  → 条就被削了。改读 JPEG 头后，换任何尺寸的图标都不会再出这个问题。
  int16_t w = 0, h = 0;
  if (!jpegSizeOf(icon, iconSize, &w, &h)) { w = 60; h = 60; }   //解析失败才退回保守值
  if (x + w > BG_WIDTH)  w = BG_WIDTH  - x;
  if (y + h > BG_HEIGHT) h = BG_HEIGHT - y;
  if (w <= 0 || h <= 0) return;                                  //整个图标都在屏外：直接不画

  if (bgCount > 0 && bgCanvasReady)
    bgDrawRegion(x, y, w, h);   //V1.5.4 从 RGB332 缓冲还原该区域
  else
    tft.fillRect(x, y, w, h, bgColor);

  TJpgDec.drawJpg(x, y, icon, iconSize);
}

/* *****************************************************************
 *  V1.6.6 时钟数字描边（方案A）：2bpp 掩膜 + 运行时合成
 *
 *  掩膜每像素 2bit（见 font/DigitMask.h，由 tools/gen_digit_mask.py 生成）：
 *    0 = 透明      -> 直接取背景像素（透明键控，复用 bgBuff）
 *    1 = 描边过渡  -> 描边色按 55% 与背景混合（让外缘不硬）
 *    2 = 描边实心  -> 描边色
 *    3 = 笔画      -> 笔画色
 *
 *  为什么换成掩膜（对比原来的 JPEG 字模 + 解码回调里键控着色）：
 *    · 描边成为"独立的一级"，于是笔画和描边能用**两种颜色**；
 *      原来 JPEG 走的是单点斜坡（lum=0→黑、lum=255→目标色），只能一种颜色。
 *    · 2bpp 掩膜两套合计 7.8KB，比 JPEG 字模约 42KB 省 34KB，
 *      而且免掉每秒 6 次的 JPEG 解码。
 *    · 掩膜里已经含描边环、并向外留了 PAD，所以贴图坐标要减掉 PAD，视觉位置不变。
 * *****************************************************************/

//描边过渡级的混合权重：越大越接近描边色
#define DIGITM_AA_WEIGHT 140

//合成缓冲：按最大那套掩膜（40x64）留
uint16_t digitMixBuf[DIGITM_A_MW * DIGITM_A_MH];

//16bit 交换序 <-> 自然序
static inline uint16_t swap16c(uint16_t c) { return (uint16_t)((c << 8) | (c >> 8)); }

//把两个"交换序 RGB565"按权重 t/255 混合（t 越大越接近 a）
static uint16_t mix565sw(uint16_t aSw, uint16_t bSw, uint8_t t)
{
  uint16_t a = swap16c(aSw), b = swap16c(bSw);
  uint16_t r = (uint16_t)((((a >> 11) & 0x1F) * t + ((b >> 11) & 0x1F) * (255 - t)) / 255);
  uint16_t g = (uint16_t)((((a >> 5)  & 0x3F) * t + ((b >> 5)  & 0x3F) * (255 - t)) / 255);
  uint16_t bl= (uint16_t)((( a        & 0x1F) * t + ( b        & 0x1F) * (255 - t)) / 255);
  return swap16c((uint16_t)((r << 11) | (g << 5) | bl));
}

//绘制一个 2bpp 掩膜数字（含描边 + 配色 + 背景键控）
//  x,y   : 数字左上角的屏幕坐标（与原来 JPEG 字模一致，内部会减掉 PAD）
//  mask  : 掩膜数据；MW/MH 掩膜尺寸；stride 每行字节数；pad 描边外扩量
void drawDigitMask(int16_t x, int16_t y, const uint8_t *mask,
                   int16_t MW, int16_t MH, int16_t stride, int16_t pad,
                   uint16_t fillColor, bool outlineOn, uint16_t outlineColor)
{
  if (mask == nullptr) return;
  if ((int32_t)MW * MH > (int32_t)DIGITM_A_MW * DIGITM_A_MH) return;   //防御：缓冲不够就放弃

  const int16_t  ox     = (int16_t)(x - pad);      //掩膜左上角的屏幕坐标
  const int16_t  oy     = (int16_t)(y - pad);
  const uint16_t fillSw = swap16c(fillColor);
  const uint16_t outSw  = swap16c(outlineColor);
  const bool     useBg  = (bgCount > 0 && bgCanvasReady);

  for (int16_t ry = 0; ry < MH; ry++)
  {
    const uint8_t *mRow = mask + (int32_t)ry * stride;
    uint16_t      *dRow = digitMixBuf + (int32_t)ry * MW;
    const int16_t  sy   = (int16_t)(oy + ry);
    const uint8_t *bRow = (useBg && sy >= 0 && sy < BG_HEIGHT)
                          ? (bgBuff + (int32_t)sy * BG_WIDTH) : nullptr;

    for (int16_t rx = 0; rx < MW; rx++)
    {
      const int16_t sx = (int16_t)(ox + rx);
      uint16_t bg = 0;                                   //屏外或无背景：用纯黑
      if (bRow != nullptr && sx >= 0 && sx < BG_WIDTH) bg = bg332to565sw(bRow[sx]);

      //行内 MSB 优先：第 rx 个像素占 (rx&3) 的高低位对
      const uint8_t lv = (uint8_t)((mRow[rx >> 2] >> (6 - ((rx & 3) << 1))) & 0x03);

      uint16_t c;
      if (lv == 3)          c = fillSw;                  //笔画
      else if (!outlineOn)  c = bg;                       //描边关掉：1/2 级都当透明
      else if (lv == 2)     c = outSw;                    //描边实心
      else if (lv == 1)     c = mix565sw(outSw, bg, DIGITM_AA_WEIGHT);  //描边过渡
      else                  c = bg;                       //透明 -> 键控成背景
      dRow[rx] = c;
    }
  }

  //裁剪到屏幕内再推（正常布局不会越界，这里是防御；旋转后逻辑屏仍是 240x240）
  const int16_t px0 = ox < 0 ? 0 : ox;
  const int16_t py0 = oy < 0 ? 0 : oy;
  const int16_t px1 = (int16_t)((ox + MW) > BG_WIDTH  ? BG_WIDTH  : (ox + MW));
  const int16_t py1 = (int16_t)((oy + MH) > BG_HEIGHT ? BG_HEIGHT : (oy + MH));
  if (px1 <= px0 || py1 <= py0) return;

  if (px0 == ox && py0 == oy && px1 == ox + MW && py1 == oy + MH)
  {
    tft.pushImage(ox, oy, MW, MH, digitMixBuf);          //常见路径：整块一次推完
  }
  else
  {
    //注意：pushImage 是按 w 连续读的，不能用"带行跨的子块"，所以这里逐行推
    for (int16_t ry = py0; ry < py1; ry++)
      tft.pushImage(px0, ry, (int16_t)(px1 - px0), 1,
                    digitMixBuf + (int32_t)(ry - oy) * MW + (px0 - ox));
  }
}

//数字描边色：自动取反（亮字配黑边 / 暗字配白边）或用户指定的固定色
uint16_t digitOutlineColor(uint16_t fill)
{
  if (bgNumOutlineAuto) return (lum565nat(fill) > 110) ? 0x0000 : 0xFFFF;
  return bgNumOutlineColor;
}

//整屏铺背景图
void drawBg()
{
  //无图：纯黑底
  if (bgCount == 0 || !bgSlotExists(bgIndex))
  {
    tft.fillScreen(0x0000);
    //V1.6 屏幕是纯黑，区域亮度按 0 处理，保证自动换色给出浅色文字
    lumClockZone = lumTextZone = 0;
    zoneDarkClock = zoneDarkText = false;
    return;
  }

  //V1.5.4 背景缓存是全局静态数组，永远可用 → 直接标记就绪
  bgCanvasReady = true;

  //解码 JPEG：同时写屏幕 + 降采样存入 RGB332 背景缓冲
  TJpgDec.setCallback(tft_output_bg);
  TJpgDec.drawFsJpg(0, 0, bgFilePath(bgIndex));
  TJpgDec.setCallback(tft_output);

  Serial.printf("[V1.5.4] 已绘制背景图: %s (alpha=%d)\r\n",
                bgFilePath(bgIndex).c_str(), bgAlpha);

  //V1.6 背景已就绪，统计两个区域的背景亮度（供自动换色/自适应底板使用）
  computeZoneLums();
}

//背景图变更后的统一刷新：铺背景 → 重绘时钟/文字 → 重绘温湿度图标
//（修复：原先 drawBg() 会用 fillScreen 把温湿度图标一并擦掉，导致图标消失）
void refreshAfterBgChange()
{
  drawBg();
  //V1.6.4 整屏重绘但**不联网**；只有"还没有任何天气数据"时才允许联网兜底一次。
  //原先这里是 LCD_reflash(1)，而 en==1 会强制走一次阻塞式 getCityWeater()
  //（DNS+连接+响应，慢的时候好几秒）—— 这就是"切背景刷新时天气非常慢才显示"。
  //天气区改由 LCD_reflash 内部用缓存 drawWeather() 立即补画。
  LCD_reflash(1, !wxValid);
#if DHT_EN
  if (DHT_img_flag != 0)
  {
    drawIconWithBg(15,183,temperature, sizeof(temperature));  //温度图标
    drawIconWithBg(15,213,humidity, sizeof(humidity));        //湿度图标
    IndoorTem();
  }
#endif
}

/* ---------------- 背景图：切换（触摸触发） ---------------- */
void bgSwitchTo(uint8_t slot)
{
  if (!bgSlotExists(slot)) return;
  if (slot == bgIndex) { bgLastRotateMs = millis(); return; }   //同一张：只重置循环计时

  savePreset(bgIndex);        //V1.6 先把当前观感存回原槽位
  bgIndex = slot;
  loadPreset(bgIndex);        //V1.6 再载入新槽位的配色与三个功能开关
  EEPROM.write(BgIndex_addr, bgIndex);
  EEPROM.commit();
  delay(5);
  bgLastRotateMs = millis();  //V1.6 手动/自动切换都重置自动循环计时

  Serial.printf("[V1.5] 切换背景图 -> 第 %d 张\r\n", bgIndex + 1);

  refreshAfterBgChange();
}

/* ---------------- V1.6 壁纸自动循环 ---------------- */
void bgAutoRotateTask()
{
  if (!bgAutoRotate) return;
  if (bgCount < 2) return;                    //只有一张图时不循环
  uint32_t nowMs = millis();
  if (bgLastRotateMs == 0) { bgLastRotateMs = nowMs; return; }
  uint32_t period = (uint32_t)bgRotateMin * 60000UL;
  if (period < 10000UL) period = 10000UL;     //下限 10 秒，避免误设导致狂切
  if (nowMs - bgLastRotateMs < period) return;

  bgLastRotateMs = nowMs;
  uint8_t next = bgNextValidSlot(bgIndex);
  if (next != 255 && next != bgIndex)
  {
    Serial.println("[V1.6] 自动循环切换背景图");
    bgSwitchTo(next);
  }
}

/* ---------------- TTP223 触摸检测（边沿+防抖） ---------------- */
void bgTouchTask()
{
  static uint8_t  lastState = 0;
  static uint32_t lastTrigTime = 0;

  uint8_t nowState = digitalRead(TTP223_PIN);

  //检测上升沿，且距上次触发超过防抖时间
  if (nowState == HIGH && lastState == LOW)
  {
    if (millis() - lastTrigTime > TOUCH_DEBOUNCE)
    {
      lastTrigTime = millis();

      if (bgCount == 0)
      {
        Serial.println("[V1.5] 触摸：暂无背景图可切换");
      }
      else
      {
        uint8_t next = bgNextValidSlot(bgIndex);
        if (next != 255) bgSwitchTo(next);
      }
    }
  }
  lastState = nowState;
}

//======================== V1.5 新增功能函数 END ========================


//进度条函数
byte loadNum = 6;
void loading(byte delayTime)//绘制进度条
{
  clk.setColorDepth(16);  //V1.5.1 改16bpp，避免8bpp量化导致背景贴片变黑
  
  clk.createSprite(200, 100);//创建窗口
  clk.fillSprite(0x0000);   //填充率

  clk.drawRoundRect(0,0,200,16,8,0xFFFF);       //空心圆角矩形
  clk.fillRoundRect(3,3,loadNum,10,5,0xFFFF);   //实心圆角矩形
  clk.setTextDatum(CC_DATUM);   //设置文本数据
  clk.setTextColor(TFT_GREEN);   //V1.5.1 单参数=透明背景，避免黑色方块
  clk.drawString("Connecting to WiFi......",100,40,2);
  clk.setTextColor(TFT_WHITE);   //V1.5.1 单参数=透明背景
  clk.drawRightString(Version,180,60,2);
  clk.pushSprite(20,120);  //窗口位置
  
  //clk.setTextDatum(CC_DATUM);
  //clk.setTextColor(TFT_WHITE, 0x0000); 
  //clk.pushSprite(130,180);
  
  clk.deleteSprite();
  loadNum += 1;
  delay(delayTime);
}

//湿度图标显示函数
void humidityWin()
{
  clk.setColorDepth(16);  //V1.5.1 改16bpp，避免8bpp量化导致背景贴片变黑
  
  huminum = huminum/2;
  clk.createSprite(52, 6);  //创建窗口
  bgFillSprite(clk,45,222,52,6,0xFFFF);   //V1.6 贴背景，消除 52x6 黑条
  clk.drawRoundRect(0,0,52,6,3,0xFFFF);  //空心圆角矩形  起始位x,y,长度，宽度，圆弧半径，颜色
  clk.fillRoundRect(1,1,huminum,4,2,humicol);   //实心圆角矩形
  clk.pushSprite(45,222);  //窗口位置
  clk.deleteSprite();
}

//温度图标显示函数
void tempWin()
{
  clk.setColorDepth(16);  //V1.5.1 改16bpp，避免8bpp量化导致背景贴片变黑
  
  clk.createSprite(52, 6);  //创建窗口
  bgFillSprite(clk,45,192,52,6,0xFFFF);   //V1.6 贴背景，消除 52x6 黑条
  clk.drawRoundRect(0,0,52,6,3,0xFFFF);  //空心圆角矩形  起始位x,y,长度，宽度，圆弧半径，颜色
  clk.fillRoundRect(1,1,tempnum,4,2,tempcol);   //实心圆角矩形
  clk.pushSprite(45,192);  //窗口位置
  clk.deleteSprite();
}

#if DHT_EN
//外接DHT30传感器，显示数据
//V1.6.9 读失败时显示 "--" 而不是误导性的 0.0（原来失败即静默显示 0.0℃/0.0%）
void IndoorTem()
{
  float t = 0, h = 0;
  bool ok = readDHT30(t, h);
  String s = "内温";
  uint16_t c3 = finalText3();          //V1.6 自动换色后的最终文字色
   /***绘制相关文字***/
  clk.setColorDepth(16);  //V1.5.1 改16bpp，避免8bpp量化导致背景贴片变黑
  clk.loadFont(ZdyLwFont_20);
  
  //位置
  clk.createSprite(58, 30);
  bgFillSprite(clk,172,150,58,30,c3);   //V1.5 先贴背景
  clk.setTextDatum(CC_DATUM);
  drawTextEx(clk, s, 29, 16, c3, bgOutlineOn);   //V1.6 带描边
  clk.pushSprite(172,150);
  clk.deleteSprite();
  
  //温度
  clk.createSprite(60, 24); 
  bgFillSprite(clk,170,184,60,24,c3);   //V1.5 先贴背景
  clk.setTextDatum(CC_DATUM);
  if (ok) drawFloatEx(clk, t, 1, 20, 13, c3, bgOutlineOn);
  else    drawTextEx(clk, "--", 20, 13, c3, bgOutlineOn);   //V1.6.9 读不到就明确显示
  drawTextEx(clk, "℃", 50, 13, c3, bgOutlineOn);
  clk.pushSprite(170,184);
  clk.deleteSprite();
  
  //湿度
  clk.createSprite(60, 24); 
  bgFillSprite(clk,170,214,60,24,c3);   //V1.5 先贴背景
  clk.setTextDatum(CC_DATUM);
  if (ok) drawFloatEx(clk, h, 1, 20, 13, c3, bgOutlineOn);
  else    drawTextEx(clk, "--", 20, 13, c3, bgOutlineOn);   //V1.6.9
  drawTextEx(clk, "%", 50, 13, c3, bgOutlineOn);
  clk.pushSprite(170,214);
  clk.deleteSprite();
}
#endif

#if !WM_EN
//微信配网函数
void SmartConfig(void)
{
  WiFi.mode(WIFI_STA);    //设置STA模式
  //tft.pushImage(0, 0, 240, 240, qr);
  tft.pushImage(0, 0, 240, 240, qr);
  Serial.println("\r\nWait for Smartconfig...");    //打印log信息
  WiFi.beginSmartConfig();      //开始SmartConfig，等待手机端发出用户名和密码
  while (1)
  {
    Serial.print(".");
    delay(100);                   // wait for a second
    if (WiFi.smartConfigDone())//配网成功，接收到SSID和密码
    {
    Serial.println("SmartConfig Success");
    Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
    Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
    break;
    }
  }
  loadNum = 194;
}
#endif


//串口调试设置函数
void Serial_set()
{
  String incomingByte = "";
  if(Serial.available()>0)
  {
    
    while(Serial.available()>0)//监测串口缓存，当有数据输入时，循环赋值给incomingByte
    {
      incomingByte += char(Serial.read());//读取单个字符值，转换为字符，并按顺序一个个赋值给incomingByte
      delay(2);//不能省略，因为读取缓冲区数据需要时间
    }    
    incomingByte.trim();   //V1.6.7 去掉串口监视器附带的 \r \n 与空格

    //V1.6.7 背光极性一键切换 —— 黑屏自救通道：
    //  极性选错时屏幕全黑、网页又不好找，此时串口发一个字母 p 就能切回来，无需重刷。
    //  切换后顺便把亮度拉满，方便立刻看出有没有生效。
    if (incomingByte == "p" || incomingByte == "P")
    {
      lcdBlActiveLow = !lcdBlActiveLow;
      EEPROM.write(BLPol_addr, lcdBlActiveLow ? 1 : 0);
      EEPROM.write(BLPolValid_addr, 0x5A);
      LCD_BL_PWM = 100;
      EEPROM.write(BL_addr, LCD_BL_PWM);
      EEPROM.write(BLValid_addr, 0x5A);
      EEPROM.commit(); delay(5);
      blApply(true);
      SMOD = "";
      Serial.printf("[V1.6.7] 背光极性 -> %s（亮度已设为 100%%）\r\n",
                    lcdBlActiveLow ? "低电平点亮(P-MOS/PNP)" : "高电平点亮(直驱)");
      Serial.println("         若仍不对，再发一次 p 切回；也可开网页「显示」卡里改。");
      return;
    }

    if(SMOD=="0x01")//设置1亮度设置
    {
      int LCDBL = atoi(incomingByte.c_str());//int n = atoi(xxx.c_str());//String转int
      if(LCDBL>=0 && LCDBL<=100)
      {
        EEPROM.write(BL_addr, LCDBL);//亮度地址写入亮度值
        EEPROM.commit();//保存更改的数据
        delay(5);
        LCD_BL_PWM = EEPROM.read(BL_addr); 
        delay(5);
        SMOD = "";
        Serial.printf("亮度调整为：");
        setLCDBacklight(LCD_BL_PWM);
        Serial.println(LCD_BL_PWM);
        Serial.println("");
      }
      else
        Serial.println("亮度调整错误，请输入0-100");
    } 
    if(SMOD=="0x02")//设置2地址设置
    {
      int CityCODE = 0;
      int CityC = atoi(incomingByte.c_str());//int n = atoi(xxx.c_str());//String转int
      if(CityC>=101000000 && CityC<=102000000 || CityC == 0)
      {
        saveCityCodetoEEP(&CityC);
        // for(int cnum=0;cnum<5;cnum++)
        // {
        //   EEPROM.write(CC_addr+cnum,CityC%100);//城市地址写入城市代码
        //   EEPROM.commit();//保存更改的数据
        //   CityC = CityC/100;
        //   delay(5);
        // }
        readCityCodefromEEP(&CityC);
        // for(int cnum=5;cnum>0;cnum--)
        // {          
        //   CityCODE = CityCODE*100;
        //   CityCODE += EEPROM.read(CC_addr+cnum-1); 
        //   delay(5);
        // }
        
        cityCode = String(CityC);
        
        if(cityCode == "0")
        {
          Serial.println("城市代码调整为：自动");
          getCityCode();  //获取城市代码
        }
        else
        {
          Serial.printf("城市代码调整为：");
          Serial.println(cityCode);
        }
        Serial.println("");
        getCityWeater();//更新城市天气  
        SMOD = "";
      }
      else
        Serial.println("城市调整错误，请输入9位城市代码，自动获取请输入0");
    }   
    if(SMOD=="0x03")//设置3屏幕显示方向
    {
      int RoSet = atoi(incomingByte.c_str());
      if(RoSet >= 0 && RoSet <= 3)
      {
        EEPROM.write(Ro_addr, RoSet);//屏幕方向地址写入方向值
        EEPROM.commit();//保存更改的数据
        SMOD = "";
      //设置屏幕方向后重新刷屏并显示
        tft.setRotation(RoSet);
        tft.fillScreen(0x0000);
        LCD_reflash(1);//屏幕刷新程序
        UpdateWeater_en = 1;
        drawIconWithBg(15,183,temperature, sizeof(temperature));  //温度图标
        drawIconWithBg(15,213,humidity, sizeof(humidity));  //湿度图标

        Serial.print("屏幕方向设置为：");
        Serial.println(RoSet);
      }
      else 
      {
        Serial.println("屏幕方向值错误，请输入0-3内的值");
      }
    }
    if(SMOD=="0x04")//设置天气更新时间
    {
      int wtup = atoi(incomingByte.c_str());//int n = atoi(xxx.c_str());//String转int
      if(wtup>=1 && wtup<=60)
      {
        EEPROM.write(UpWeT_addr, wtup);//亮度地址写入亮度值
        EEPROM.commit();//保存更改的数据
        delay(5);
        updateweater_time = wtup;
        SMOD = "";
        Serial.printf("天气更新时间更改为：");
        Serial.print(updateweater_time);
        Serial.println("分钟");
      }
      else
        Serial.println("更新时间太长，请重新设置（1-60）");
    } 
    //V1.6.8 进入配网模式。
    //  为什么必须留这个入口：断网时网页根本打不开，串口是唯一能救回来的通道。
    //  这里只置标志，真正的门户切换交给 loop() 里的 wifiTask()，
    //  避免在串口处理里长时间阻塞。
    //  注意用 else if：保持原来 if(0x04)/else 的语义不变。
    else if(SMOD=="0x06")
    {
      SMOD = "";
      Serial.println("[V1.6.8] 即将进入配网模式：开热点 AutoConnectAP（无密码，180 秒超时）");
      netPortalRequested = true;
    }
    else
    {
      SMOD = incomingByte;
      delay(2);
      if(SMOD=="0x01")
        Serial.println("请输入亮度值，范围0-100");
      else if(SMOD=="0x02")
        Serial.println("请输入9位城市代码，自动获取请输入0"); 
      else if(SMOD=="0x03")
      {
        Serial.println("请输入屏幕方向值，"); 
        Serial.println("0-USB接口朝下");
        Serial.println("1-USB接口朝右");
        Serial.println("2-USB接口朝上");
        Serial.println("3-USB接口朝左");
      }
      else if(SMOD=="0x04")
      {
        Serial.print("当前天气更新时间："); 
        Serial.print(updateweater_time);
        Serial.println("分钟");
        Serial.println("请输入天气更新时间（1-60）分钟"); 
      }
      else if(SMOD=="0x05")
      {
        Serial.println("重置WiFi设置中......");
        delay(10);
        wm.resetSettings();
        deletewificonfig();
        delay(10);
        Serial.println("重置WiFi成功");
        SMOD = "";
        ESP.restart();
      }
      else
      {
        Serial.println("");
        Serial.println("请输入需要修改的代码：");
        Serial.println("亮度设置输入        0x01");
        Serial.println("地址设置输入        0x02");
        Serial.println("屏幕方向设置输入    0x03");
        Serial.println("更改天气更新时间    0x04");
        Serial.println("重置WiFi(会重启)    0x05");
        Serial.println("进入配网模式        0x06");
        Serial.println("切换背光极性        p");
        Serial.println("");
      }
    }
  }
}

#if WebSever_EN
//web网站相关函数

//--------------------- V1.5 网页 UI（卡片式） ---------------------
//V1.5.5 配色预设色块：点色块即选，右侧原生取色器保留自定义
//V1.6.1 色块改为**浏览器端生成**：服务端只输出空的 .sw 容器，
//       页面体积 20,991 -> 14,940 字节（-29%），加载更快。
//       ⚠️ 调色板现在定义在网页 JS 里（变量 PAL），改色块请改那里。
//生成一个「基础色块 + 自定义取色」的配色行（色块由 JS 填充）
String colorPickRow(const String &label, const char *name, uint16_t color)
{
  String s;
  s.reserve(240);
  s += "<div class='crow'><div class='ctop'><span class='lb'>" + label + "</span>";
  s += "<input type='color' name='" + String(name) + "' value='" + colorToHex(color) +
       "' oninput=\"syncSw('" + String(name) + "')\"></div>";
  s += "<div class='sw' data-for='" + String(name) + "'></div></div>";
  return s;
}

//V1.6 生成一个「开/关」分段按钮行
String segRow(const String &label, const char *name, bool on)
{
  String s;
  s.reserve(400);
  s += "<div class='row'><span class='lb'>" + label + "</span><div class='seg'>";
  s += "<label><input type='radio' name='" + String(name) + "' value='1'" + (on ? " checked" : "") + "><span>开</span></label>";
  s += "<label><input type='radio' name='" + String(name) + "' value='0'" + (on ? "" : " checked") + "><span>关</span></label>";
  s += "</div></div>";
  return s;
}

//生成完整设置页面 HTML
String buildConfigPage()
{
  String h;
  //V1.6.1 一次预留到位（实测页面约 15KB），避免反复扩容+整体拷贝
  h.reserve(16000);

  h += F("<!DOCTYPE html><html lang='zh'><head><meta charset='utf-8'>");
  h += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  h += F("<title>SDD 控制台</title><style>");
  h += F(":root{--bg:#0f1420;--card:#1a2130;--card2:#222b3d;--line:#2e3a50;--txt:#e8edf7;--dim:#8a97ad;--acc:#4ea1ff;--acc2:#31d0aa;}");
  h += F("*{box-sizing:border-box;margin:0;padding:0;}");
  h += F("body{background:linear-gradient(160deg,#0f1420,#151d2e 60%,#0f1420);color:var(--txt);");
  h += F("font-family:-apple-system,'Segoe UI','Microsoft YaHei',sans-serif;padding:18px;min-height:100vh;}");
  h += F(".wrap{max-width:560px;margin:0 auto;}");
  h += F(".hd{display:flex;align-items:baseline;justify-content:space-between;margin-bottom:16px;}");
  h += F(".hd h1{font-size:20px;font-weight:600;letter-spacing:.5px;}");
  h += F(".hd span{font-size:12px;color:var(--dim);}");
  h += F(".card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px;margin-bottom:14px;}");
  h += F(".card h2{font-size:13px;font-weight:600;color:var(--acc);margin-bottom:14px;letter-spacing:1px;}");
  h += F(".row{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:13px;}");
  h += F(".row:last-child{margin-bottom:0;}");
  h += F(".lb{font-size:13px;color:var(--txt);flex-shrink:0;}");
  h += F(".val{font-size:12px;color:var(--acc2);min-width:56px;text-align:right;font-variant-numeric:tabular-nums;}");
  h += F("input[type=range]{-webkit-appearance:none;appearance:none;height:6px;border-radius:3px;");
  h += F("background:var(--card2);flex:1;outline:none;cursor:pointer;}");
  h += F("input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;border-radius:50%;");
  h += F("background:var(--acc);border:2px solid #0f1420;box-shadow:0 0 0 1px var(--acc);cursor:pointer;}");
  h += F("input[type=range]::-moz-range-thumb{width:16px;height:16px;border-radius:50%;background:var(--acc);border:2px solid #0f1420;cursor:pointer;}");
  h += F("input[type=color]{width:46px;height:34px;border:1px solid var(--line);border-radius:8px;background:var(--card2);cursor:pointer;padding:2px;}");
  // V1.5.5 配色行：基础色块 + 自定义取色器
  h += F(".crow{margin-bottom:14px;}");
  h += F(".crow:last-child{margin-bottom:0;}");
  h += F(".ctop{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:8px;}");
  h += F(".sw{display:flex;flex-wrap:wrap;gap:5px;}");
  h += F(".sw i{width:20px;height:20px;border-radius:5px;cursor:pointer;border:2px solid transparent;");
  h += F("box-shadow:inset 0 0 0 1px rgba(255,255,255,.14);transition:transform .12s;display:block;}");
  h += F(".sw i:hover{transform:scale(1.14);}");
  h += F(".sw i.on{transform:scale(1.12);box-shadow:0 0 0 2px var(--acc),inset 0 0 0 1px rgba(255,255,255,.14);}");
  h += F("input[type=text]{width:100%;padding:10px 12px;border-radius:9px;border:1px solid var(--line);");
  h += F("background:var(--card2);color:var(--txt);font-size:14px;outline:none;}");
  h += F("input[type=text]:focus{border-color:var(--acc);}");
  //V1.6.6 亮度定时：时段行 + 时间/数字输入框
  h += F("input[type=time],input[type=number]{padding:8px 10px;border-radius:9px;border:1px solid var(--line);");
  h += F("background:var(--card2);color:var(--txt);font-size:13px;outline:none;font-variant-numeric:tabular-nums;}");
  h += F("input[type=time]:focus,input[type=number]:focus{border-color:var(--acc);}");
  h += F(".bsrow{display:flex;align-items:center;gap:8px;margin-bottom:10px;}");
  h += F(".bsrow .no{font-size:12px;color:var(--dim);width:14px;text-align:center;flex-shrink:0;}");
  h += F(".bsrow .to{color:var(--dim);font-size:12px;}");
  h += F(".bsrow input[type=number]{width:62px;text-align:right;}");
  h += F(".bsrow .pc{font-size:12px;color:var(--acc2);}");
  h += F(".seg{display:flex;gap:6px;flex-wrap:wrap;}");
  h += F(".seg label{font-size:12px;padding:7px 12px;border-radius:8px;background:var(--card2);");
  h += F("border:1px solid var(--line);cursor:pointer;user-select:none;transition:.15s;}");
  h += F(".seg input{display:none;}");
  h += F(".seg input:checked+span{color:#fff;}");
  h += F(".seg label:has(input:checked){background:var(--acc);border-color:var(--acc);color:#fff;}");
  h += F(".slots{display:grid;grid-template-columns:repeat(5,1fr);gap:8px;}");
  h += F(".slot{position:relative;aspect-ratio:1;border-radius:10px;border:1px dashed var(--line);");
  h += F("background:var(--card2);display:flex;align-items:center;justify-content:center;");
  h += F("font-size:11px;color:var(--dim);overflow:hidden;cursor:pointer;}");
  h += F(".slot.on{border:2px solid var(--acc2);}");
  h += F(".slot img{width:100%;height:100%;object-fit:cover;}");
  h += F(".slot .no{position:absolute;top:4px;left:5px;font-size:10px;background:rgba(0,0,0,.6);");
  h += F("padding:1px 5px;border-radius:5px;color:#fff;}");
  h += F(".slot .del{position:absolute;top:3px;right:3px;width:18px;height:18px;line-height:16px;");
  h += F("text-align:center;border-radius:50%;background:rgba(220,60,60,.9);color:#fff;font-size:12px;text-decoration:none;}");
  h += F(".btn{width:100%;padding:14px;border:none;border-radius:11px;background:linear-gradient(135deg,var(--acc),#2f7ce0);");
  h += F("color:#fff;font-size:15px;font-weight:600;cursor:pointer;letter-spacing:1px;margin-top:4px;}");
  h += F(".btn:active{transform:scale(.985);}");
  h += F(".tip{font-size:11px;color:var(--dim);margin-top:8px;line-height:1.6;}");
  h += F(".ft{text-align:center;font-size:11px;color:var(--dim);margin-top:18px;}");
  // V1.5.2 裁剪面板样式
  h += F("#mCrop{display:none;position:fixed;inset:0;z-index:99;background:rgba(0,0,0,.82);");
  h += F("align-items:center;justify-content:center;flex-direction:column;}");
  h += F("#cWrap{position:relative;touch-action:none;line-height:0;}");
  h += F("#cCard{display:block;border-radius:8px;cursor:move;}");
  h += F("#cBar{display:flex;gap:10px;align-items:center;margin-top:16px;}");
  h += F("#cBar button{width:38px;height:38px;border-radius:50%;border:1px solid var(--line);");
  h += F("background:var(--card2);color:#fff;font-size:19px;cursor:pointer;}");
  h += F("#cBar button.ok{width:auto;padding:0 22px;border-radius:19px;background:var(--acc);");
  h += F("border:none;font-size:14px;font-weight:600;}");
  h += F("#cBar button.ca{width:auto;padding:0 18px;border-radius:19px;font-size:14px;}");
  h += F("#cTip{color:#cfd6e4;font-size:12px;margin-bottom:12px;}");
  h += F("</style></head><body><div class='wrap'>");

  h += F("<div class='hd'><h1>SDD 控制台</h1><span>V1.6.9</span></div>");

  //=== 显示设置 ===
  h += F("<div class='card'><h2>显示</h2>");
  h += F("<div class='row'><span class='lb'>屏幕方向</span><div class='seg'>");
  const char* rotName[4] = {"USB下","USB右","USB上","USB左"};
  for (int i = 0; i < 4; i++) {
    h += "<label><input type='radio' name='web_set_rotation' value='" + String(i) + "'" + (LCD_Rotation == i ? " checked" : "") + "><span>" + String(rotName[i]) + "</span></label>";
  }
  h += F("</div></div>");
  //V1.6.5 亮度滑块回归：BLK 已从 3.3V 改接到 GPIO33（串高边开关），背光终于可调
  h += F("<div class='row' style='margin-top:13px'><span class='lb'>屏幕亮度</span>");
  h += "<input type='range' name='lcd_bl' min='5' max='100' value='" + String(LCD_BL_PWM) + "' oninput=\"document.getElementById('vbl').textContent=this.value+'%'\">";
  h += "<span class='val' id='vbl'>" + String(LCD_BL_PWM) + "%</span></div>";
  //V1.6.7 背光极性：BLK 接线方式不同极性也不同，选错会"越亮越暗/黑屏"，
  //        所以放到网页上随时可切（黑屏时也能用手机开这个页面改）
  h += F("<div class='row' style='margin-top:13px'><span class='lb'>背光极性</span><div class='seg'>");
  h += String("<label><input type='radio' name='bl_pol' value='0'") + (lcdBlActiveLow ? "" : " checked") + "><span>高电平点亮</span></label>";
  h += String("<label><input type='radio' name='bl_pol' value='1'") + (lcdBlActiveLow ? " checked" : "") + "><span>低电平点亮</span></label>";
  h += F("</div></div>");
  h += F("<div class='tip'>背光 PWM 调光，保存后立即生效并断电保持。下限 5%（0 保留给未初始化，避免黑屏）。<br>"
         "BLK 直连 GPIO33 → 选<b>高电平点亮</b>；BLK 经 P-MOSFET/PNP 高边开关 → 选<b>低电平点亮</b>。"
         "选错会表现为「亮度越大越暗」。黑屏时也可在串口发字母 <b>p</b> 切换。</div></div>");

  //=== V1.6.8 网络状态（断线自动重连 + 手动配网） ===
  h += F("<div class='card'><h2>网络</h2>");
  {
    bool up = (WiFi.status() == WL_CONNECTED);
    h += "<div class='row'><span class='lb'>状态</span><span class='val'>";
    h += up ? String("已连接") : String("未连接 · 自动重连中");
    h += "</span></div>";
    h += "<div class='row'><span class='lb'>SSID</span><span class='val'>";
    h += up ? WiFi.SSID() : String("—");
    h += "</span></div>";
    h += "<div class='row'><span class='lb'>IP 地址</span><span class='val'>";
    h += up ? WiFi.localIP().toString() : String("—");
    h += "</span></div>";
    if (up) {
      h += "<div class='row'><span class='lb'>信号强度</span><span class='val'>";
      h += String(WiFi.RSSI());
      h += " dBm</span></div>";
    }
  }
  h += F("<div class='tip'>断网后会自动按 <b>1→2→4→…→60 秒</b>退避重连，屏幕右上角会出现红色"
         "「无网络」角标；恢复后角标自动消失。<b>已保存的 WiFi 密码不会被清除</b>。</div>");
  h += F("<div style='margin-top:12px'>"
         "<a href='/wificonfig' style='display:inline-block;padding:9px 14px;border-radius:9px;"
         "border:1px solid var(--line);background:var(--card2);color:inherit;text-decoration:none;"
         "font-size:13px;margin-right:8px'>重新配网</a>"
         "<a href='/wififorget' style='display:inline-block;padding:9px 14px;border-radius:9px;"
         "border:1px solid var(--line);background:var(--card2);color:inherit;text-decoration:none;"
         "font-size:13px' onclick=\"return confirm('确定要清除已保存的 WiFi 并重新配网吗？')\">忘记网络</a>"
         "</div>");
  h += F("<div class='tip' style='margin-top:8px'>「重新配网」保留原密码，只是切换一次网络；"
         "「忘记网络」会清除凭据（换路由器时用）。两者都会开热点 <b>AutoConnectAP</b>，"
         "3 分钟没配完自动退回。</div>");
  h += F("</div>");

  //=== V1.6.8 游戏时长 OLED ===
  h += F("<div class='card'><h2>游戏时长 OLED</h2>");
  h += segRow("无程序运行时显示今日总时长", "oled_today", oledTodayEn);
  h += F("<div class='tip' style='margin-top:10px'>"
         "OLED 平时显示「正在运行的程序名 + 本次时长」；没有已添加的程序在运行时，"
         "打开这项就改显示 <b>TODAY TOTAL + 今日所有程序合计时长</b>。<br>"
         "需要电脑端 game-uptime 一起支持（发送 <code>today</code> 字段）；"
         "没收到过该字段时仍显示 NO GAME，不会误报 00:00:00。</div>");
  h += F("</div>");

  //=== V1.6.6 亮度定时 ===
  h += F("<div class='card'><h2>亮度定时</h2>");
  h += segRow("按时段自动调亮度", "bl_sched", blSchedEn);
  h += F("<div class='tip' style='margin:10px 0 12px'>开启后亮度<b>完全</b>由下面时段决定（上面那个手动滑块会被忽略）；当前时间不在任何时段内时，回落到手动亮度。</div>");
  for (uint8_t i = 0; i < BL_SCHED_MAX; i++) {
    h += "<div class='bsrow'><span class='no'>" + String(i + 1) + "</span>";
    h += "<input type='time' name='bl" + String(i + 1) + "s' value='" + hmStr(blSeg[i].sh, blSeg[i].sm) + "'>";
    h += F("<span class='to'>至</span>");
    h += "<input type='time' name='bl" + String(i + 1) + "e' value='" + hmStr(blSeg[i].eh, blSeg[i].em) + "'>";
    h += "<input type='number' name='bl" + String(i + 1) + "b' min='0' max='100' value='" + String(blSeg[i].br) + "'>";
    h += F("<span class='pc'>%</span></div>");
  }
  h += F("<div class='tip'>亮度填 <b>0</b> 或起止时间相同 = 该段不启用。支持跨零点（如 23:00 至 07:00）。</div></div>");

  //=== 文字配色（属于当前壁纸） ===
  h += F("<div class='card'><h2>文字配色 · 当前壁纸</h2>");
  h += "<div class='tip' style='margin:0 0 12px'>以下配色与开关属于<b>第 " + String(bgIndex + 1) + " 张</b>壁纸，切换壁纸时会自动切换。</div>";
  h += colorPickRow("小时数字", "hour_color", hourColor);
  h += colorPickRow("分钟数字", "minute_color", minuteColor);
  h += colorPickRow("秒钟数字", "second_color", secondColor);
  h += F("<div class='tip' style='margin:10px 0 10px'>以下为普通文字配色</div>");
  h += colorPickRow("日期 / 城市 / 滚动条", "text_color2", textColor2);
  h += colorPickRow("室内温湿度", "text_color3", textColor3);
  //V1.6.3 一键恢复默认配色（配色一旦被弄乱，不必再逐个手动调回来）
  h += F("<div style='margin-top:12px'><button class='btn' onclick=\"if(confirm('把当前壁纸的配色恢复为默认？\\n白 / 橙 / 白 / 白 / 白'))location.href='/resetcolor'\">恢复默认配色</button></div>");
  h += F("<div class='tip' style='margin:14px 0 10px'>文字清晰度增强（每张壁纸独立）</div>");
  h += segRow("自动换色", "bg_autocolor", bgAutoColor);
  h += F("<div class='tip' style='margin:2px 0 10px'>按时钟/文字各自的背景亮度，把所选颜色<b>保留色相</b>只调明暗：背景亮转深色字，背景暗转浅色字。</div>");
  h += segRow("自适应底板", "bg_scrim", bgScrimOn);
  h += F("<div class='tip' style='margin:2px 0 10px'>把文字背后的背景局部压暗或提亮，保证与文字有足够亮度差（不是纯黑框，背景仍可见）。</div>");
  h += segRow("文字描边", "bg_outline", bgOutlineOn);
  h += F("<div class='tip' style='margin:2px 0 10px'>给<b>矢量文字</b>（日期/城市/温湿度/滚动条）加一圈对比描边。</div>");
  //V1.6.6 数字描边：数字是位图字模，走独立的 2bpp 掩膜机制，所以单独给开关
  h += segRow("数字描边", "bg_numoutline", bgNumOutlineOn);
  h += F("<div class='tip' style='margin:2px 0 10px'>给<b>时钟数字</b>（时/分/秒）加描边。与上面的「文字描边」是两套独立机制（数字是位图字模，用 2bpp 掩膜实现）。</div>");
  h += segRow("描边自动取反", "bg_numoutauto", bgNumOutlineAuto);
  h += F("<div class='tip' style='margin:2px 0 10px'>开启：<b>亮字配黑边、暗字配白边</b>，随配色自动变。关闭：用下面选的固定色。</div>");
  h += colorPickRow("数字描边色", "bg_numoutcolor", bgNumOutlineColor);
  h += F("</div>");

  //=== 背景图 ===
  h += F("<div class='card'><h2>背景图</h2>");
  h += F("<div class='row'><span class='lb'>背景暗化</span>");
  h += "<input type='range' name='bg_alpha' min='0' max='100' value='" + String(bgAlphaToPercent()) + "' oninput=\"document.getElementById('val').textContent=this.value+'%'\">";
  h += "<span class='val' id='val'>" + String(bgAlphaToPercent()) + "%</span></div>";
  h += F("<div class='row' style='margin-top:13px'><span class='lb'>底板强度</span>");
  h += "<input type='range' name='bg_scrim_str' min='0' max='100' value='" + String(bgScrimStrength) + "' oninput=\"document.getElementById('vsc').textContent=this.value+'%'\">";
  h += "<span class='val' id='vsc'>" + String(bgScrimStrength) + "%</span></div>";
  h += F("<div class='tip' style='margin:10px 0 8px'>建议图片尺寸 240×240（JPEG）</div>");

  h += F("<div class='slots'>");
  for (int i = 0; i < BG_MAX; i++) {
    bool has = bgSlotExists(i);
    h += "<div class='slot" + String(has && bgIndex == i ? " on" : "") + "' onclick=\"location.href='/setbg?slot=" + String(i) + "'\">";
    if (has) {
      //V1.6.1 用"版本号"而非 millis()：图片没变时URL不变，浏览器直接走缓存，
      //       避免每次刷新都重下 5 张 240x240 原图（这是页面慢的主因）
      h += "<img src='/preview?slot=" + String(i) + "&v=" + String(bgSlotVer[i]) + "' width='100%' height='100%' style='object-fit:cover'>";
      h += "<span class='no'>" + String(i + 1) + "</span>";
      h += "<a class='del' href='/delbg?slot=" + String(i) + "' onclick=\"event.stopPropagation();return confirm('删除第 " + String(i + 1) + " 张背景图？')\">×</a>";
    } else {
      h += "<span>" + String(i + 1) + " 空</span>";
    }
    h += F("</div>");
  }
  h += F("</div>");

  h += F("<div class='tip' style='margin:12px 0 8px'>选择槽位上传（覆盖已有图）：</div>");
  h += F("<div class='slots'>");
  for (int i = 0; i < BG_MAX; i++) {
    h += "<label class='slot'><input type='file' accept='image/*' style='display:none' onchange=\"pickBg(this," + String(i) + ")\"><span class='no'>" + String(i + 1) + "</span><span>上传</span></label>";
  }
  h += F("</div>");
  h += F("<div class='tip'>支持 JPG / PNG / WebP 等任意图片：上传前会在浏览器本地自动<b>裁剪为 1:1 并压缩到 240×240</b>后保存，无需自行处理。</div>");
  h += F("<div class='tip'>触摸模块单击可循环切换已有背景图。</div></div>");

  //=== 壁纸自动循环 ===
  h += F("<div class='card'><h2>壁纸自动循环</h2>");
  h += segRow("自动循环", "bg_rotate", bgAutoRotate);
  h += F("<div class='row' style='margin-top:13px'><span class='lb'>切换间隔</span>");
  h += "<input type='range' name='bg_rotate_min' min='1' max='180' value='" + String(bgRotateMin) + "' oninput=\"document.getElementById('vrot').textContent=this.value+' 分'\">";
  h += "<span class='val' id='vrot'>" + String(bgRotateMin) + " 分</span></div>";
  h += F("<div class='tip'>需至少上传 2 张背景图才会循环（只有一张时不生效）。切换时会自动套用该张壁纸自己的配色与开关。</div></div>");

  //=== 天气 ===
  h += F("<div class='card'><h2>天气</h2>");
  h += "<div class='row'><span class='lb'>城市代码</span></div>";
  h += "<input type='text' name='web_ccode' value='" + cityCode + "' placeholder='如 101250101'>";
  h += F("<div class='row' style='margin-top:13px'><span class='lb'>更新间隔</span>");
  h += "<input type='range' name='web_upwe_t' min='1' max='60' value='" + String(updateweater_time) + "' oninput=\"document.getElementById('vupt').textContent=this.value+' 分'\">";
  h += "<span class='val' id='vupt'>" + String(updateweater_time) + " 分</span></div></div>";

  //=== 传感器 ===
  h += F("<div class='card'><h2>传感器</h2>");
  h += F("<div class='row'><span class='lb'>DHT30 温湿度</span><div class='seg'>");
  h += "<label><input type='radio' name='web_DHT11_en' value='0'" + String(DHT_img_flag == 0 ? " checked" : "") + "><span>关闭</span></label>";
  h += "<label><input type='radio' name='web_DHT11_en' value='1'" + String(DHT_img_flag != 0 ? " checked" : "") + "><span>开启</span></label>";
  h += F("</div></div></div>");

  h += F("<button class='btn' onclick='doSave()'>保存设置</button>");

  //=== JS ===
  h += F("<script>");
  h += F("function doSave(){var f=document.createElement('form');f.method='POST';f.action='/save';");
  h += F("var names=['web_set_rotation','lcd_bl','bl_pol','oled_today','hour_color','minute_color','second_color','text_color2','text_color3',"
         "'bg_alpha','bg_scrim_str','bg_autocolor','bg_scrim','bg_outline','bg_numoutline','bg_numoutauto','bg_numoutcolor',"
         "'bg_rotate','bg_rotate_min','bl_sched',"
         "'bl1s','bl1e','bl1b','bl2s','bl2e','bl2b','bl3s','bl3e','bl3b','bl4s','bl4e','bl4b',"
         "'web_ccode','web_upwe_t','web_DHT11_en'];");
  h += F("names.forEach(function(n){var els=document.querySelectorAll('[name=\"'+n+'\"]');");
  h += F("for(var i=0;i<els.length;i++){var e=els[i];if(e.type==='radio'&&!e.checked)continue;");
  h += F("var h=document.createElement('input');h.type='hidden';h.name=n;h.value=e.value;f.appendChild(h);}});");
  h += F("document.body.appendChild(f);f.submit();}");
  // ==== V1.5.2 客户端裁剪 + 压缩 ====
  // pickBg: 读取本地文件 -> 弹出裁剪面板
  h += F("var _srcImg=null,_slot=0,_crop={x:0,y:0,s:0},_base=0,_drag=null;");
  h += F("function pickBg(inp,slot){if(!inp.files||!inp.files[0]){return;}");
  h += F("var f=inp.files[0];_slot=slot;");
  h += F("var r=new FileReader();");
  h += F("r.onload=function(ev){var im=new Image();");
  h += F("im.onload=function(){_srcImg=im;openCrop(im);};");
  h += F("im.onerror=function(){alert('图片读取失败，请换一张');};");
  h += F("im.src=ev.target.result;};");
  h += F("r.readAsDataURL(f);}");

  // 打开裁剪面板，初始化正方形选框（居中、边长=短边）
  h += F("function openCrop(im){var m=document.getElementById('mCrop');");
  h += F("var cw=280;var sc=Math.min(cw/im.width,cw/im.height);");
  h += F("var dw=Math.round(im.width*sc),dh=Math.round(im.height*sc);");
  h += F("var cv=document.getElementById('cCard');");
  h += F("cv.width=dw;cv.height=dh;cv.style.width=dw+'px';cv.style.height=dh+'px';");
  h += F("var ctx=cv.getContext('2d');ctx.drawImage(im,0,0,dw,dh);");
  h += F("_base=sc;var s=Math.round(Math.min(dw,dh)*0.86);");
  h += F("_crop={x:Math.round((dw-s)/2),y:Math.round((dh-s)/2),s:s};");
  h += F("drawCropBox();m.style.display='flex';}");

  // 绘制裁剪遮罩框
  h += F("function drawCropBox(){var cv=document.getElementById('cCard');");
  h += F("var ctx=cv.getContext('2d');ctx.drawImage(_srcImg,0,0,cv.width,cv.height);");
  h += F("ctx.save();ctx.fillStyle='rgba(0,0,0,0.55)';ctx.fillRect(0,0,cv.width,cv.height);");
  h += F("ctx.clearRect(_crop.x,_crop.y,_crop.s,_crop.s);");
  h += F("ctx.drawImage(_srcImg,_crop.x/_base,_crop.y/_base,_crop.s/_base,_crop.s/_base,_crop.x,_crop.y,_crop.s,_crop.s);");
  h += F("ctx.strokeStyle='#4da3ff';ctx.lineWidth=2;ctx.strokeRect(_crop.x,_crop.y,_crop.s,_crop.s);");
  h += F("ctx.restore();}");

  // 交互：拖拽移动 / 滚轮缩放
  h += F("function _xy(e){var cv=document.getElementById('cCard');var r=cv.getBoundingClientRect();");
  h += F("var p=e.touches?e.touches[0]:e;return{x:p.clientX-r.left,y:p.clientY-r.top};}");
  h += F("function cropDown(e){var p=_xy(e);_drag={ox:p.x-_crop.x,oy:p.y-_crop.y};e.preventDefault();}");
  h += F("function cropMove(e){if(!_drag)return;var p=_xy(e);var cv=document.getElementById('cCard');");
  h += F("_crop.x=Math.max(0,Math.min(cv.width-_crop.s,p.x-_drag.ox));");
  h += F("_crop.y=Math.max(0,Math.min(cv.height-_crop.s,p.y-_drag.oy));drawCropBox();e.preventDefault();}");
  h += F("function cropUp(){_drag=null;}");
  h += F("function cropZoom(d){var cv=document.getElementById('cCard');");
  h += F("var cx=_crop.x+_crop.s/2,cy=_crop.y+_crop.s/2;");
  h += F("var ns=Math.max(40,Math.min(Math.min(cv.width,cv.height),_crop.s+d));");
  h += F("_crop.x=Math.max(0,Math.min(cv.width-ns,cx-ns/2));_crop.y=Math.max(0,Math.min(cv.height-ns,cy-ns/2));");
  h += F("_crop.s=ns;drawCropBox();}");
  h += F("function cropClose(){document.getElementById('mCrop').style.display='none';_srcImg=null;}");

  // 确认裁剪：从原图取对应区域 -> 缩放到 240x240 -> JPEG 压缩到 <=60KB -> 上传
  h += F("function cropOK(){var cv=document.getElementById('cCard');");
  h += F("var sx=_crop.x/_base,sy=_crop.y/_base,ss=_crop.s/_base;");
  h += F("var out=document.createElement('canvas');out.width=240;out.height=240;");
  h += F("var octx=out.getContext('2d');octx.fillStyle='#000';octx.fillRect(0,0,240,240);");
  h += F("octx.imageSmoothingEnabled=true;octx.imageSmoothingQuality='high';");
  h += F("octx.drawImage(_srcImg,sx,sy,ss,ss,0,0,240,240);");
  h += F("var q=0.92,url=out.toDataURL('image/jpeg',q),n=0;");
  h += F("while(url.length>82000&&q>0.28){q-=0.07;url=out.toDataURL('image/jpeg',q);n++;if(n>12)break;}");
  h += F("cropClose();");
  h += F("out.toBlob(function(b){var fd=new FormData();fd.append('file',b,'bg.jpg');");
  h += F("fetch('/upload?slot='+_slot,{method:'POST',body:fd}).then(function(r){return r.text();})");
  h += F(".then(function(t){alert(t+'\\n(已压缩 '+(b.size/1024).toFixed(1)+'KB)');location.href='/';})");
  h += F(".catch(function(e){alert('上传失败:'+e);location.href='/';});},'image/jpeg',q);}");
  // ==== V1.5.5/V1.6.1 配色：色块（浏览器端生成）+ 点选 + 高亮同步 ====
  h += F("var PAL=['#FFFFFF','#FFE8C0','#FFC000','#FF7F00','#FF0000','#FF6FA5','#FF00FF','#8000FF',");
  h += F("'#4EA1FF','#0000FF','#00FFFF','#00FF00','#7CFFB2','#9AA5B1','#000000'];");
  h += F("function syncSw(n){var inp=document.querySelector(\"[name='\"+n+\"']\");if(!inp)return;");
  h += F("var v=(inp.value||'').toUpperCase();");
  h += F("var sw=document.querySelectorAll(\".sw[data-for='\"+n+\"'] i\");");
  h += F("for(var i=0;i<sw.length;i++){sw[i].className=(sw[i].getAttribute('data-v').toUpperCase()===v)?'on':'';}}");
  h += F("function buildSw(){var bs=document.querySelectorAll('.sw');");
  h += F("for(var i=0;i<bs.length;i++){var s='';");
  h += F("for(var k=0;k<PAL.length;k++){s+=\"<i style='background:\"+PAL[k]+\"' data-v='\"+PAL[k]+\"'></i>\";}");
  h += F("bs[i].innerHTML=s;}");
  h += F("var cs=document.querySelectorAll('.crow input[type=color]');");
  h += F("for(var i=0;i<cs.length;i++){syncSw(cs[i].getAttribute('name'));}}");
  h += F("buildSw();");
  h += F("document.addEventListener('click',function(e){var t=e.target;if(t.tagName!=='I')return;");
  h += F("var p=t.parentNode;if(!p||p.className!=='sw')return;");
  h += F("var n=p.getAttribute('data-for');var inp=document.querySelector(\"[name='\"+n+\"']\");");
  h += F("if(inp){inp.value=t.getAttribute('data-v');syncSw(n);}});");
  h += F("</script>");

  h += F("<div class='ft'>SmallDesktopDisplay V1.6.9 · By WCY</div>");

  // ==== V1.5.2 裁剪面板 DOM ====
  h += F("<div id='mCrop'><div id='cTip'>拖动方块调整范围 · 滚轮/双指缩放</div>");
  h += F("<div id='cWrap'><canvas id='cCard'></canvas></div>");
  h += F("<div id='cBar'><button onclick='cropZoom(24)'>+</button>");
  h += F("<button onclick='cropZoom(-24)'>−</button>");
  h += F("<button class='ca' onclick='cropClose()'>取消</button>");
  h += F("<button class='ok' onclick='cropOK()'>确定并上传</button></div></div>");
  h += F("<script>(function(){var c=document.getElementById('cCard');");
  h += F("c.addEventListener('mousedown',cropDown);window.addEventListener('mousemove',cropMove);");
  h += F("window.addEventListener('mouseup',cropUp);");
  h += F("c.addEventListener('touchstart',cropDown,{passive:false});");
  h += F("c.addEventListener('touchmove',cropMove,{passive:false});");
  h += F("c.addEventListener('touchend',cropUp);");
  h += F("c.addEventListener('wheel',function(e){cropZoom(e.deltaY<0?16:-16);e.preventDefault();},{passive:false});})();</script>");

  h += F("</div></body></html>");

  return h;
}

//RGB565 -> #RRGGBB
String colorToHex(uint16_t c)
{
  uint8_t r = (c >> 11) & 0x1F;
  uint8_t g = (c >> 5)  & 0x3F;
  uint8_t b =  c        & 0x1F;
  r = (r * 255 + 15) / 31;
  g = (g * 255 + 31) / 63;
  b = (b * 255 + 15) / 31;
  char buf[8];
  sprintf(buf, "#%02X%02X%02X", r, g, b);
  return String(buf);
}

//#RRGGBB -> RGB565
uint16_t hexToColor(String hex)
{
  if (hex.startsWith("#")) hex = hex.substring(1);
  long v = strtol(hex.c_str(), NULL, 16);
  uint8_t r = (v >> 16) & 0xFF;
  uint8_t g = (v >> 8)  & 0xFF;
  uint8_t b =  v        & 0xFF;
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

//V1.6.3 校验网页传来的颜色串是否合法（必须是 #RRGGBB，7 个字符）。
//★为什么必须校验：hexToColor("") 会返回 0x0000 —— strtol 解析空串得到 0。
//  所以只要某个字段缺失、为空、或格式不对，"保存"就会把该配色静默改成纯黑。
//  这正是"换壁纸后除温湿度外全变黑"的放大器：一次坏请求把黑配色写进全局，
//  再被 savePreset 落盘到每一张壁纸。宁可保持原值，也不要改成黑色。
bool validHexColor(const String &s)
{
  if (s.length() != 7 || s[0] != '#') return false;
  for (uint8_t i = 1; i < 7; i++)
  {
    char c = s[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
      return false;
  }
  return true;
}

//V1.6.3 五个颜色的默认值（供"恢复默认配色"按钮使用）
void applyDefaultColors()
{
  hourColor   = 0xFFFF;   //小时 白
  minuteColor = 0xFD20;   //分钟 橙
  secondColor = 0xFFFF;   //秒钟 白
  textColor2  = 0xFFFF;   //日期/城市/滚动条 白
  textColor3  = 0xFFFF;   //温湿度 白
}

//bgAlpha(0-255) -> 网页百分比(0-100)
uint8_t bgAlphaToPercent()
{
  return (uint8_t)((uint32_t)bgAlpha * 100 / 255);
}

//网页百分比 -> bgAlpha
uint8_t percentToBgAlpha(int p)
{
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return (uint8_t)((uint32_t)p * 255 / 100);
}

//主配置页
void handleconfig()
{
  //V1.6.1 页面是动态的，禁止缓存（体积小，重新拉取代价低）
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", buildConfigPage());
}

//保存设置（POST /save）
void handleSave()
{
  String msg = "";

  //屏幕方向
  if (server.hasArg("web_set_rotation")) {
    int v = constrain(server.arg("web_set_rotation").toInt(), 0, 3);
    if (v != LCD_Rotation) {
      LCD_Rotation = v;
      EEPROM.write(Ro_addr, v);
      EEPROM.commit(); delay(5);
      tft.setRotation(v);
      refreshAfterBgChange();
    }
  }

  //V1.6.5 屏幕亮度（背光 PWM，即时生效）
  if (server.hasArg("lcd_bl")) {
    int v = constrain(server.arg("lcd_bl").toInt(), LCD_BL_MIN, 100);
    if (v != LCD_BL_PWM) {
      LCD_BL_PWM = v;
      EEPROM.write(BL_addr, LCD_BL_PWM);
      EEPROM.write(BLValid_addr, 0x5A);   //同时写"已写过"标志，见 setup 里的说明
      EEPROM.commit(); delay(5);
      blApply(true);                      //V1.6.6 走统一入口：定时优先，否则用手动值
      Serial.printf("[V1.6.6] 手动亮度 -> %d%%\r\n", LCD_BL_PWM);
    }
  }

  //V1.6.7 背光极性（选错会黑屏，所以放到网页上随时能切，不用重刷固件）
  if (server.hasArg("bl_pol")) {
    bool v = (server.arg("bl_pol").toInt() != 0);
    if (v != lcdBlActiveLow) {
      lcdBlActiveLow = v;
      EEPROM.write(BLPol_addr, lcdBlActiveLow ? 1 : 0);
      EEPROM.write(BLPolValid_addr, 0x5A);
      EEPROM.commit(); delay(5);
      blApply(true);
      Serial.printf("[V1.6.7] 背光极性 -> %s\r\n",
                    lcdBlActiveLow ? "低电平点亮(P-MOS/PNP)" : "高电平点亮(直驱)");
    }
  }

  //V1.6.8 OLED：没有已添加程序在运行时，是否改显示"今日总时长"
  if (server.hasArg("oled_today")) {
    bool v = (server.arg("oled_today").toInt() != 0);
    if (v != oledTodayEn) {
      oledTodayEn = v;
      EEPROM.write(OledTodayEn_addr, oledTodayEn ? 1 : 0);
      EEPROM.write(OledTodayValid_addr, 0x5A);
      EEPROM.commit(); delay(5);
      oledLastMs = 0;                    //置 0 让下一次 gameOledTask 立刻重画
      Serial.printf("[V1.6.8] OLED 今日总时长显示 -> %s\r\n", oledTodayEn ? "开" : "关");
    }
  }

  //V1.5.1 时钟时/分/秒三组独立配色
  //V1.6.3 一律先校验格式：非法/缺失的字段保持原值，绝不落成黑色（见 validHexColor）
  bool clkColorChanged = false;
  String cv;
  cv = server.arg("hour_color");
  if (validHexColor(cv)) { hourColor   = hexToColor(cv); clkColorChanged = true; }
  else if (cv.length()) Serial.printf("[V1.6.3] hour_color 格式非法，已忽略: %s\r\n", cv.c_str());
  cv = server.arg("minute_color");
  if (validHexColor(cv)) { minuteColor = hexToColor(cv); clkColorChanged = true; }
  else if (cv.length()) Serial.printf("[V1.6.3] minute_color 格式非法，已忽略: %s\r\n", cv.c_str());
  cv = server.arg("second_color");
  if (validHexColor(cv)) { secondColor = hexToColor(cv); clkColorChanged = true; }
  else if (cv.length()) Serial.printf("[V1.6.3] second_color 格式非法，已忽略: %s\r\n", cv.c_str());
  if (clkColorChanged) {
    Hour_sign = 60; Minute_sign = 60; Second_sign = 60;   //强制重绘时钟
  }

  //文字配色 2 / 3
  cv = server.arg("text_color2");
  if (validHexColor(cv)) textColor2 = hexToColor(cv);
  cv = server.arg("text_color3");
  if (validHexColor(cv)) textColor3 = hexToColor(cv);

  saveTextColors();

  //背景暗化
  if (server.hasArg("bg_alpha")) {
    bgAlpha = percentToBgAlpha(server.arg("bg_alpha").toInt());
    EEPROM.write(BgAlpha_addr, bgAlpha);
    EEPROM.commit(); delay(5);
  }

  //城市代码
  if (server.hasArg("web_ccode")) {
    int cc = server.arg("web_ccode").toInt();
    if (cc >= 101000000 && cc <= 102000000) {
      saveCityCodetoEEP(&cc);
      readCityCodefromEEP(&cc);
      cityCode = cc;
    }
  }

  //天气更新时间
  if (server.hasArg("web_upwe_t")) {
    int v = constrain(server.arg("web_upwe_t").toInt(), 1, 60);
    updateweater_time = v;
    EEPROM.write(UpWeT_addr, v);
    EEPROM.commit(); delay(5);
  }

  //DHT 使能
  if (server.hasArg("web_DHT11_en")) {
    int v = server.arg("web_DHT11_en").toInt() ? 1 : 0;
    if (v != DHT_img_flag) {
      DHT_img_flag = v;
      EEPROM.write(DHT_addr, v);
      EEPROM.commit(); delay(5);
      LCD_reflash(1);
    }
  }

  //V1.6 当前壁纸的功能开关
  bool featChanged = false;
  if (server.hasArg("bg_autocolor")) { bgAutoColor = server.arg("bg_autocolor").toInt() != 0; featChanged = true; }
  if (server.hasArg("bg_scrim"))     { bgScrimOn   = server.arg("bg_scrim").toInt()     != 0; featChanged = true; }
  if (server.hasArg("bg_outline"))   { bgOutlineOn = server.arg("bg_outline").toInt()   != 0; featChanged = true; }

  //V1.6.6 数字描边（独立于"文字描边"）：开关 / 自动取反 / 固定色
  if (server.hasArg("bg_numoutline")) { bgNumOutlineOn   = server.arg("bg_numoutline").toInt() != 0; featChanged = true; }
  if (server.hasArg("bg_numoutauto")) { bgNumOutlineAuto = server.arg("bg_numoutauto").toInt() != 0; featChanged = true; }
  cv = server.arg("bg_numoutcolor");
  if (validHexColor(cv)) { bgNumOutlineColor = hexToColor(cv); featChanged = true; }

  //V1.6 全局：底板强度 + 自动循环
  if (server.hasArg("bg_scrim_str"))
    bgScrimStrength = (uint8_t)constrain(server.arg("bg_scrim_str").toInt(), 0, 100);
  if (server.hasArg("bg_rotate")) {
    bgAutoRotate = server.arg("bg_rotate").toInt() != 0;
    bgLastRotateMs = millis();
  }
  if (server.hasArg("bg_rotate_min"))
    bgRotateMin = (uint16_t)constrain(server.arg("bg_rotate_min").toInt(), 1, 1440);
  saveGlobalDisplaySettings();

  //V1.6.6 亮度定时：开关 + 4 段（起/止时间 + 每段亮度）
  bool blSchedChanged = false;
  if (server.hasArg("bl_sched")) {
    blSchedEn = (server.arg("bl_sched").toInt() != 0);
    blSchedChanged = true;
  }
  for (uint8_t i = 0; i < BL_SCHED_MAX; i++)
  {
    String ks = "bl" + String(i + 1) + "s";
    String ke = "bl" + String(i + 1) + "e";
    String kb = "bl" + String(i + 1) + "b";
    uint8_t h, m;
    if (server.hasArg(ks) && parseHM(server.arg(ks), &h, &m)) { blSeg[i].sh = h; blSeg[i].sm = m; blSchedChanged = true; }
    if (server.hasArg(ke) && parseHM(server.arg(ke), &h, &m)) { blSeg[i].eh = h; blSeg[i].em = m; blSchedChanged = true; }
    if (server.hasArg(kb)) { blSeg[i].br = (uint8_t)constrain(server.arg(kb).toInt(), 0, 100); blSchedChanged = true; }
  }
  if (blSchedChanged) {
    saveBlSched();
    blApply(true);
    Serial.println("[V1.6.6] 亮度定时配置已更新");
  }

  //V1.6 把配色 / 暗化 / 三个开关写入"当前壁纸"的预设
  savePreset(bgIndex);

  if (featChanged && !clkColorChanged) { Hour_sign = 60; Minute_sign = 60; Second_sign = 60; }

  //V1.6.1 先返回响应、再做重绘。
  //原先顺序反了：浏览器要等整个重绘（含解码 240x240 JPEG）跑完才收到跳转，
  //体感就是"保存后卡很久才回到页面"。响应先发出去，重绘随后进行。
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");

  //保存后重绘
  refreshAfterBgChange();
}

//上传背景图：分块写入 SPIFFS（POST /upload?slot=N）
File bgUploadFile;
uint8_t bgUploadSlot = 255;
size_t  bgUploadSize = 0;
bool    bgUploadAbort = false;

void handleUpload()
{
  HTTPUpload& up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    bgUploadSlot = 255;
    if (server.hasArg("slot")) bgUploadSlot = server.arg("slot").toInt();
    bgUploadSize = 0;
    bgUploadAbort = false;

    Serial.printf("[V1.5] 开始接收背景图 slot=%d\r\n", bgUploadSlot);

    if (bgUploadSlot >= BG_MAX) {
      bgUploadAbort = true;
      return;
    }
    //先写入临时文件，确认完整后再改名（避免半张图覆盖旧图）
    if (SPIFFS.exists("/bg_tmp.jpg")) SPIFFS.remove("/bg_tmp.jpg");
    bgUploadFile = SPIFFS.open("/bg_tmp.jpg", FILE_WRITE);
    if (!bgUploadFile) {
      Serial.println("[V1.5] 打开临时文件失败");
      bgUploadAbort = true;
    }
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (bgUploadAbort || bgUploadSlot >= BG_MAX) return;

    bgUploadSize += up.currentSize;
    if (bgUploadSize > BG_MAX_SIZE) {
      Serial.println("[V1.5] 图片过大，中止");
      bgUploadAbort = true;
      bgUploadFile.close();
      SPIFFS.remove("/bg_tmp.jpg");
      return;
    }
    if (bgUploadFile)
      bgUploadFile.write(up.buf, up.currentSize);
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (bgUploadFile) bgUploadFile.close();

    if (bgUploadAbort || bgUploadSlot >= BG_MAX) {
      SPIFFS.remove("/bg_tmp.jpg");
      return;
    }

    //替换目标槽位
    String dst = bgFilePath(bgUploadSlot);
    if (SPIFFS.exists(dst)) SPIFFS.remove(dst);
    SPIFFS.rename("/bg_tmp.jpg", dst);
    bgBumpVer(bgUploadSlot);              //V1.6.1 图片变了，缩略图缓存标记自增

    bgScanFiles();
    //V1.6 换图后按目标槽位的预设切换观感（新槽位会自动继承当前设置）
    if (bgIndex != bgUploadSlot) savePreset(bgIndex);
    bgIndex = bgUploadSlot;
    loadPreset(bgIndex);
    EEPROM.write(BgIndex_addr, bgIndex);
    EEPROM.commit();
    bgLastRotateMs = millis();

    Serial.printf("[V1.5] 背景图已保存: %s (%u 字节)\r\n", dst.c_str(), (unsigned)bgUploadSize);
  }
}

//上传完成响应
void handleUploadDone()
{
  if (bgUploadAbort)
    server.send(200, "text/plain; charset=utf-8", "上传失败：图片过大（限 64KB）或槽位错误");
  else
    server.send(200, "text/plain; charset=utf-8", "上传成功！已应用为背景图。");

  refreshAfterBgChange();
}

//删除背景图（GET /delbg?slot=N）
void handleDelBg()
{
  if (!server.hasArg("slot")) { server.send(400, "text/plain", "missing slot"); return; }
  int slot = server.arg("slot").toInt();
  if (slot < 0 || slot >= BG_MAX) { server.send(400, "text/plain", "bad slot"); return; }

  String path = bgFilePath(slot);
  if (SPIFFS.exists(path)) SPIFFS.remove(path);
  bgBumpVer(slot);                        //V1.6.1 图片已删除，缩略图缓存标记自增
  bgScanFiles();

  //修正当前索引
  if (!bgSlotExists(bgIndex)) {
    uint8_t n = bgNextValidSlot(bgIndex);
    bgIndex = (n == 255) ? 0 : n;
    EEPROM.write(BgIndex_addr, bgIndex);
    EEPROM.commit();
  }

  //V1.6.1 先响应再重绘（否则浏览器要等整屏重绘完才收到跳转）
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");

  refreshAfterBgChange();
}

//切换当前背景图（GET /setbg?slot=N）
void handleSetBg()
{
  if (!server.hasArg("slot")) { server.send(400, "text/plain", "missing slot"); return; }
  int slot = server.arg("slot").toInt();
  if (slot < 0 || slot >= BG_MAX) { server.send(400, "text/plain", "bad slot"); return; }

  if (bgSlotExists(slot)) {
    if (slot != bgIndex) {
      savePreset(bgIndex);      //V1.6 存回原槽位
      bgIndex = slot;
      loadPreset(bgIndex);      //V1.6 载入新槽位的配色与开关
      EEPROM.write(BgIndex_addr, bgIndex);
      EEPROM.commit();
      bgLastRotateMs = millis();
    }
  }

  //V1.6.1 先响应再重绘（否则浏览器要等整屏重绘完才收到跳转）
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");

  if (bgSlotExists(slot) && slot == bgIndex) refreshAfterBgChange();
}

//V1.6.3 恢复默认配色（GET /resetcolor）：把"当前壁纸"的配色恢复为默认值并落盘。
//一旦配色被弄乱，用这个按钮一键恢复，不必逐个调。
void handleResetColor()
{
  applyDefaultColors();
  saveTextColors();     //全局配色
  savePreset(bgIndex);  //当前壁纸的预设
  Hour_sign = 60; Minute_sign = 60; Second_sign = 60;
  Serial.println("[V1.6.3] 已恢复默认配色");
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
  refreshAfterBgChange();
}

//V1.6.3 诊断页（GET /dbg）：把 EEPROM 里配色与预设的原始字节全列出来。
//之所以常驻：这次"黑配色"就是因为看不到设备内部状态，只能靠猜。
//以后再有类似问题，直接打开 http://<ip>/dbg 就能一眼看出是哪个字节不对。
void handleDbg()
{
  char buf[160];
  String h;
  h.reserve(3200);
  h += F("<!DOCTYPE html><html><head><meta charset='utf-8'><title>SDD 诊断</title><style>");
  h += F("body{font-family:Consolas,monospace;background:#12161f;color:#dfe6f3;padding:14px;font-size:13px;line-height:1.7}");
  h += F("table{border-collapse:collapse;margin:8px 0}td,th{border:1px solid #33405a;padding:2px 7px;text-align:center}");
  h += F("th{background:#1d2635}b{color:#7fd1ff}.ok{color:#5ee08a}.bad{color:#ff6b6b}h4{margin:14px 0 2px}</style></head><body>");
  h += F("<h3>SDD 诊断</h3>");
  h += "<div>版本 <b>" Version "</b>　当前壁纸 <b>第" + String(bgIndex + 1) + "张</b>　有图槽位 <b>" + String(bgCount) + "</b></div>";
  sprintf(buf, "<div>presetMask=0x%02X　ColValid=0x%02X　bgAlpha=%d(%d%%)　底板强度=%d　自动循环=%d(%d分)</div>",
          presetMask, EEPROM.read(ColValid_addr), bgAlpha, bgAlphaToPercent(),
          bgScrimStrength, (int)bgAutoRotate, bgRotateMin);
  h += buf;

  //V1.6.5 背光状态（便于确认接线与极性是否对）
  //V1.6.7 极性已改为运行时设置，这里显示实际生效值
  sprintf(buf, "<div>背光: 引脚=%d　亮度=%d%%　极性=%s　BLValid=0x%02X　BL原始值=%d　极性标志=0x%02X</div>",
          LCD_BL_PIN, LCD_BL_PWM,
          (lcdBlActiveLow ? "低电平点亮(P-MOS/PNP)" : "高电平点亮(直驱)"),
          EEPROM.read(BLValid_addr), EEPROM.read(BL_addr), EEPROM.read(BLPolValid_addr));
  h += buf;

  //V1.6.6 数字描边 + 亮度定时
  sprintf(buf, "<div>数字描边: %s　自动取反: %s　固定色=#%04X　亮度定时: %s</div>",
          bgNumOutlineOn ? "开" : "关", bgNumOutlineAuto ? "开" : "关",
          bgNumOutlineColor, blSchedEn ? "开" : "关");
  h += buf;
  {
    String bs = F("<div>时段: ");
    for (uint8_t i = 0; i < BL_SCHED_MAX; i++) {
      bs += String(i + 1) + "\u3000" + hmStr(blSeg[i].sh, blSeg[i].sm) + "→" +
            hmStr(blSeg[i].eh, blSeg[i].em) + "\u3000" + String(blSeg[i].br) + "%\u3000";
    }
    bs += "\u2192 当前实发 " + String(blCurBright) + "%</div>";
    h += bs;
  }

  //全局配色
  h += F("<h4>全局配色</h4><table><tr><th>项</th><th>地址</th><th>原始字节</th><th>解析值</th><th>看起来未写过</th></tr>");
  const int      gaddr[5] = {HourCol_addr, MinCol_addr, SecCol_addr, TextCol2_addr, TextCol3_addr};
  const char    *gname[5] = {"小时", "分钟", "秒钟", "日期/城市", "温湿度"};
  const uint16_t gval[5]  = {hourColor, minuteColor, secondColor, textColor2, textColor3};
  for (uint8_t i = 0; i < 5; i++)
  {
    sprintf(buf, "<tr><td>%s</td><td>%d,%d</td><td>%02X %02X</td><td>#%04X</td><td>%s</td></tr>",
            gname[i], gaddr[i], gaddr[i] + 1,
            EEPROM.read(gaddr[i]), EEPROM.read(gaddr[i] + 1),
            gval[i], colorAddrUnwritten(gaddr[i]) ? "是" : "-");
    h += buf;
  }
  h += F("</table>");

  //每槽位预设
  h += F("<h4>每张壁纸的预设（魔数 0xA5 + 校验通过才算有效）</h4>");
  h += F("<table><tr><th>槽</th><th>有图</th><th>ver</th><th>magic</th><th>chk</th><th>有效</th>");
  h += F("<th>小时</th><th>分钟</th><th>秒钟</th><th>日期</th><th>温湿度</th><th>暗化%</th><th>标志</th></tr>");
  for (uint8_t s = 0; s < BG_MAX; s++)
  {
    int a  = presetAddr(s);
    bool ok = presetValid(a);
    h += "<tr><td>" + String(s + 1) + "</td>";
    h += "<td>" + String(bgSlotExists(s) ? "有" : "-") + "</td>";
    h += "<td>" + String(EEPROM.read(BgVer_addr + s)) + "</td>";
    sprintf(buf, "<td class='%s'>%02X</td><td class='%s'>%02X/%02X</td><td class='%s'>%s</td>",
            (EEPROM.read(a + P_MAGIC) == PRESET_MAGIC ? "ok" : "bad"), EEPROM.read(a + P_MAGIC),
            (ok ? "ok" : "bad"), EEPROM.read(a + P_CHK), presetSum(a),
            (ok ? "ok" : "bad"), (ok ? "有效" : "无效"));
    h += buf;
    for (uint8_t k = 0; k < 5; k++)
    {
      sprintf(buf, "<td>%04X</td>", readColor16Raw(a + k * 2));
      h += buf;
    }
    sprintf(buf, "<td>%d</td><td>%02X</td></tr>", EEPROM.read(a + P_ALPHA), EEPROM.read(a + P_FLAGS));
    h += buf;
  }
  h += F("</table>");
  h += F("<div>注：chk 显示“实际/应有”。某项显示 <span class='bad'>bad</span> 说明该槽位没有有效预设，"
         "切换过去时会“继承当前观感”而不是加载它。</div>");
  h += F("<p><a style='color:#7fd1ff' href='/'>← 返回控制台</a></p>");
  h += F("</body></html>");

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", h);
}

//缩略图预览（GET /preview?slot=N）——直接返回 SPIFFS 中的 JPEG 原图
void handlePreview()
{
  if (!server.hasArg("slot")) { server.send(404, "text/plain", "no slot"); return; }
  int slot = server.arg("slot").toInt();
  if (slot < 0 || slot >= BG_MAX || !bgSlotExists(slot)) {
    server.send(404, "text/plain", "not found");
    return;
  }

  File f = SPIFFS.open(bgFilePath(slot), FILE_READ);
  if (!f) { server.send(404, "text/plain", "open fail"); return; }

  //V1.6.1 允许浏览器长期缓存：URL 里带版本号，图片变了版本号才变，不会看到旧图
  server.sendHeader("Cache-Control", "public, max-age=604800");
  server.sendHeader("ETag", String("\"bg") + String(slot) + "-" + String(bgSlotVer[slot]) + "\"");
  server.streamFile(f, "image/jpeg");
  f.close();
}
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

/* ---------------- V1.6.8 配网入口（两个网页按钮） ----------------
 * 说明：这里**只置标志、立刻返回**，真正的门户切换交给 loop() 里的 wifiTask()。
 * 不能在 HTTP 回调里直接开配网门户 —— 那会把 WebServer 自己的处理流程卡住。
 */
void handleWifiConfig()
{
  netPortalRequested = true;
  server.send(200, "text/html; charset=utf-8",
    "<!DOCTYPE html><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<div style=\"font:15px/1.8 -apple-system,'Segoe UI',sans-serif;padding:24px;max-width:520px\">"
    "<h3 style='margin:0 0 10px'>正在进入配网模式…</h3>"
    "<p>ESP32 会开出一个热点 <b>AutoConnectAP</b>（无密码）。</p>"
    "<p>用手机连上它，在弹出的页面里选你的 WiFi、填密码即可。</p>"
    "<p>已有的 WiFi 密码<b>不会被清除</b>；如果 3 分钟内没配完，会自动退出并回到原来的重连流程。</p>"
    "<p style='color:#888'>（切换瞬间本页会失去响应，属正常现象）</p>"
    "<p><a href='/'>返回控制台</a></p></div>");
}

void handleWifiForget()
{
  netPortalRequested = true;
#if WM_EN
  wm.resetSettings();      //真正"忘记网络"：只在用户显式点这个按钮时才做
#endif
  deletewificonfig();
  server.send(200, "text/html; charset=utf-8",
    "<!DOCTYPE html><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<div style=\"font:15px/1.8 -apple-system,'Segoe UI',sans-serif;padding:24px;max-width:520px\">"
    "<h3 style='margin:0 0 10px'>已清除已保存的 WiFi，正在进入配网模式…</h3>"
    "<p>连上热点 <b>AutoConnectAP</b>，重新选择 WiFi 并填密码。</p>"
    "<p style='color:#888'>（切换瞬间本页会失去响应，属正常现象）</p></div>");
}

//Web服务初始化
/* V1.6.8 HTTP 路由注册 —— **只能跑一次**。
 *  旧版把路由注册和 mDNS 混在 Web_Sever_Init() 里，重连后再调用会重复 server.on()，
 *  白白堆积 handler。这里按"只做一次 / 每次重连都要做"拆开。 */
void webRoutesInit()
{
  if (webRoutesReady) return;
  webRoutesReady = true;

  server.on("/", handleconfig);
  server.on("/save", HTTP_POST, handleSave);          //V1.5 保存设置
  server.on("/upload", HTTP_POST, handleUploadDone, handleUpload);  //V1.5 上传背景图
  server.on("/delbg", HTTP_GET, handleDelBg);         //V1.5 删除背景图
  server.on("/setbg", HTTP_GET, handleSetBg);         //V1.5 切换当前背景图
  server.on("/preview", HTTP_GET, handlePreview);     //V1.5 缩略图预览
  server.on("/resetcolor", HTTP_GET, handleResetColor);  //V1.6.3 恢复默认配色
  server.on("/dbg", HTTP_GET, handleDbg);                //V1.6.3 诊断页（看 EEPROM 原始字节）
  server.on("/wificonfig", HTTP_GET, handleWifiConfig);  //V1.6.8 重新配网
  server.on("/wififorget", HTTP_GET, handleWifiForget);  //V1.6.8 忘记网络并配网
  //V1.6.1 favicon 直接回 204：省掉浏览器每次加载都发一次 404 请求的往返
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204, "text/plain", ""); });
  server.onNotFound(handleNotFound);
  Serial.println("[V1.6.8] HTTP 路由已注册（只做一次）");
}

/* V1.6.8 网络服务上线 —— **幂等**，每次（重）连上都要跑一次。
 *  只重建 mDNS：WiFi 断开时 netif 会消失，mDNS 必须重新注册，否则 sd3.local 失效。
 *  HTTP 服务器监听的 socket 绑定在 0.0.0.0:80，重连后仍然可用，不需要 begin() 第二次。
 *  ⚠️ 旧版 mDNS 起不来就 30 秒重启设备，容易变成重启循环；现在只记日志等下轮重试。 */
void netUp()
{
  Serial.println("mDNS responder building...");
  MDNS.end();
  if (MDNS.begin("SD3"))
  {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS responder started  ->  http://sd3.local");
  }
  else
  {
    Serial.println("[V1.6.8] mDNS 启动失败，稍后随重连再试（不重启设备）");
  }
}

/* ==================================================================
 *  V1.6.9 ★根本修复：HTTP 服务会因为“开机 8 秒没连上”而永远不启动
 *
 *  现象：设备在线（ping 通、TTL=64）、sd3.local 能解析、
 *        _http._tcp 也有 SRV 记录声明端口 80，
 *        但 TCP 80 直接 ECONNREFUSED —— 后台网页打不开。
 *
 *  根因（V1.6.8 引入的回归）：
 *    setup() 里 `if(WiFi.status()==WL_CONNECTED){ ...Web_Sever_Init()... }`
 *    把 Web 初始化包在了“本次开机是否当场连上”的条件里；而 V1.6.8 又把
 *    原来的“死等连上”改成了“最多等 8 秒”。于是：
 *      开机 8 秒内没连上 -> 整个 if 跳过 -> server.begin() 永不执行
 *      -> 之后 wifiTask() 退避重连成功 -> 只调 netUp() 重建 mDNS
 *      -> mDNS 活着、HTTP 却从来没 listen -> 网页永远打不开。
 *
 *  修法：把 listen 抽成独立的幂等函数 httpStart()，在这三个地方调用：
 *    1) Web_Sever_Init()              —— 开机当场连上的快路径
 *    2) wifiTask() 的重连恢复分支     —— 后台重连成功后补开
 *    3) Web_Sever() 的懒启动兜底      —— 只要 WiFi 已连就不让它继续睡着
 *  ⚠️ NetworkServer::begin() 自带 `if (_listening) return;` 守卫，
 *     重复调用不会泄漏 socket；外层 httpStarted 只是为了让日志与调试清楚。
 * ================================================================== */
bool httpStart()
{
  if (httpStarted) return true;                     //已启动：幂等返回
  if (WiFi.status() != WL_CONNECTED) return false;  //没网不能 listen

  webRoutesInit();                                  //路由只注册一次（内部自带守卫）
  server.begin();
  httpStarted = true;
  Serial.println("[V1.6.9] HTTP 服务已开启（listen 0.0.0.0:80）");
  Serial.print("[V1.6.9] 本地 IP： ");
  Serial.println(WiFi.localIP());
  return true;
}

/* 开机路径：mDNS + HTTP 一起拉起来（保持原有行为） */
void Web_Sever_Init()
{
  netUp();
  httpStart();
}
//Web网页设置函数
void Web_Sever()
{
//  MDNS.update();
  //V1.6.9 懒启动兜底：只要 WiFi 已连而 HTTP 还没起来，就补一次。
  //  这两条判断都是纯内存比较，不耗时；放在 handleClient() 之前，
  //  保证“后台重连成功的那一瞬间”网页就能打开，不用等下一次 Wi-Fi 边沿。
  if (!httpStarted && WiFi.status() == WL_CONNECTED) httpStart();
  server.handleClient();
}
//web服务打开后LCD显示登陆网址及IP
void Web_sever_Win()
{
  IPAddress IP_adr = WiFi.localIP();
  // strcpy(IP_adr,WiFi.localIP().toString());
  clk.setColorDepth(16);  //V1.5.1 改16bpp，避免8bpp量化导致背景贴片变黑
  
  clk.createSprite(200, 70);//创建窗口
  clk.fillSprite(0x0000);   //填充率

  // clk.drawRoundRect(0,0,200,100,5,0xFFFF);       //空心圆角矩形
  clk.setTextDatum(CC_DATUM);   //设置文本数据
  clk.setTextColor(TFT_GREEN);   //V1.5.1 单参数=透明背景
  clk.drawString("Connect to Config:",70,10,2);
  // clk.drawString("IP:",45,60,2);
  clk.setTextColor(TFT_WHITE);   //V1.5.1 单参数=透明背景
  clk.drawString("http://sd3.local",100,40,4);
  // clk.drawString(&IP_adr,125,70,2);
  clk.pushSprite(20,40);  //窗口位置
    
  clk.deleteSprite();
}
#endif

#if WM_EN
//WEB配网LCD显示函数
void Web_win()
{
  clk.setColorDepth(16);  //V1.5.1 改16bpp，避免8bpp量化导致背景贴片变黑
  
  clk.createSprite(200, 60);//创建窗口
  clk.fillSprite(0x0000);   //填充率

  clk.setTextDatum(CC_DATUM);   //设置文本数据
  clk.setTextColor(TFT_GREEN);   //V1.5.1 单参数=透明背景
  clk.drawString("WiFi Connect Fail!",100,10,2);
  clk.drawString("SSID:",45,40,2);
  clk.setTextColor(TFT_WHITE);   //V1.5.1 单参数=透明背景
  clk.drawString("AutoConnectAP",125,40,2);
  clk.pushSprite(20,50);  //窗口位置
    
  clk.deleteSprite();
}

//WEB配网函数
/* V1.6.8 配网门户。
 * 与旧版的关键区别：**不再 resetSettings()**。
 *   WiFiManager 的门户本来就允许直接填新 SSID/密码并保存，不需要先擦掉旧凭据；
 *   而无条件清空的代价是"开机那一刻路由器恰好没起来"就把凭据永久弄丢，
 *   之后网络恢复也连不回来。真正的"忘记网络"改由显式操作触发（网页按钮/串口命令）。
 */
void Webconfig()
{
  WiFi.mode(WIFI_AP_STA); // 门户需要 AP+STA 并存（旧版写 WIFI_STA 是错的）
  
  // add a custom input field
  int customFieldLength = 40;
  
  // new (&custom_field) WiFiManagerParameter("customfieldid", "Custom Field Label", "Custom Field Value", customFieldLength,"placeholder=\"Custom Field Placeholder\"");
  
  // test custom html input type(checkbox)
  //  new (&custom_field) WiFiManagerParameter("customfieldid", "Custom Field Label", "Custom Field Value", customFieldLength,"placeholder=\"Custom Field Placeholder\" type=\"checkbox\""); // custom html type
  
  // test custom html(radio)
  // const char* custom_radio_str = "<br/><label for='customfieldid'>Custom Field Label</label><input type='radio' name='customfieldid' value='1' checked> One<br><input type='radio' name='customfieldid' value='2'> Two<br><input type='radio' name='customfieldid' value='3'> Three";
  // new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom html input

  const char* set_rotation = "<br/><label for='set_rotation'>Set Rotation</label>\
                              <input type='radio' name='set_rotation' value='0' checked> One<br>\
                              <input type='radio' name='set_rotation' value='1'> Two<br>\
                              <input type='radio' name='set_rotation' value='2'> Three<br>\
                              <input type='radio' name='set_rotation' value='3'> Four<br>";
  WiFiManagerParameter  custom_rot(set_rotation); // custom html input
  WiFiManagerParameter  custom_bl("LCDBL","LCD BackLight(1-100)","10",3);
  #if DHT_EN
  WiFiManagerParameter  custom_DHT11_en("DHT11_en","Enable DHT30 sensor","0",1);
  #endif
  WiFiManagerParameter  custom_weatertime("WeaterUpdateTime","Weather Update Time(Min)","10",3);
  WiFiManagerParameter  custom_cc("CityCode","CityCode","101250101",9);
  WiFiManagerParameter  p_lineBreak_notext("<p></p>");

  // wm.addParameter(&p_lineBreak_notext);
  // wm.addParameter(&custom_field);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_cc);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_bl);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_weatertime);
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_rot);
  #if DHT_EN
  wm.addParameter(&p_lineBreak_notext);
  wm.addParameter(&custom_DHT11_en);
  #endif
  wm.setSaveParamsCallback(saveParamCallback);
  
  // custom menu via array or vector
  // 
  // menu tokens, "wifi","wifinoscan","info","param","close","sep","erase","restart","exit" (sep is seperator) (if param is in menu, params will not show up in wifi page!)
  // const char* menu[] = {"wifi","info","param","sep","restart","exit"}; 
  // wm.setMenu(menu,6);
  std::vector<const char *> menu = {"wifi","restart"};
  wm.setMenu(menu);
  
  // set dark theme
  wm.setClass("invert");
  
  //set static ip
  // wm.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0)); // set static ip,gw,sn
  // wm.setShowStaticFields(true); // force show static ip fields
  // wm.setShowDnsFields(true);    // force show dns field always

  // wm.setConnectTimeout(20); // how long to try to connect for before continuing
//  wm.setConfigPortalTimeout(30); // auto close configportal after n seconds
  // wm.setCaptivePortalEnable(false); // disable captive portal redirection
  // wm.setAPClientCheck(true); // avoid timeout if client connected to softap

  // wifi scan settings
  // wm.setRemoveDuplicateAPs(false); // do not remove duplicate ap names (true)
  wm.setMinimumSignalQuality(20);  // set min RSSI (percentage) to show in scans, null = 8%
  // wm.setShowInfoErase(false);      // do not show erase button on info page
  // wm.setScanDispPerc(true);       // show RSSI as percentage not graph icons
  
  // wm.setBreakAfterConfig(true);   // always exit configportal even if wifi save fails

  //V1.6.8 两处关键改动：
  //  ① 门户加超时，到点自动退出（旧版没设超时，会一直挂着）
  //  ② **去掉 while(!res);** —— 失败就返回，交给 wifiTask() 继续退避重连。
  //     旧版这个死循环正是"卡在等待网络连接页面"的直接原因：
  //     它永远走不到 Web_Sever_Init()，网页也就永远打不开。
  wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT);

  bool res = wm.autoConnect("AutoConnectAP"); // anonymous ap
  Serial.printf("[V1.6.8] 配网门户已退出，结果=%s\r\n", res ? "已连接" : "未连接/超时，继续自动重连");
}

String getParam(String name){
  //read parameter from server, for customhmtl input
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback(){
  int cc;
  
  Serial.println("[CALLBACK] saveParamCallback fired");
  // Serial.println("PARAM customfieldid = " + getParam("customfieldid"));
  // Serial.println("PARAM CityCode = " + getParam("CityCode"));
  // Serial.println("PARAM LCD BackLight = " + getParam("LCDBL"));
  // Serial.println("PARAM WeaterUpdateTime = " + getParam("WeaterUpdateTime"));
  // Serial.println("PARAM Rotation = " + getParam("set_rotation"));
  // Serial.println("PARAM DHT11_en = " + getParam("DHT11_en"));
  
  //将从页面中获取的数据保存
  #if DHT_EN
  DHT_img_flag = getParam("DHT11_en").toInt();
  #endif
  updateweater_time = getParam("WeaterUpdateTime").toInt();
  cc =  getParam("CityCode").toInt();
  LCD_Rotation = getParam("set_rotation").toInt();
  LCD_BL_PWM = getParam("LCDBL").toInt();

  //对获取的数据进行处理
  //城市代码
  Serial.print("CityCode = ");
  Serial.println(cc);
  if(cc>=101000000 && cc<=102000000 || cc == 0)
  {
    saveCityCodetoEEP(&cc);
    // for(int cnum=0;cnum<5;cnum++)
    // {
    //   EEPROM.write(CC_addr+cnum,cc%100);//城市地址写入城市代码
    //   EEPROM.commit();//保存更改的数据
    //   cc = cc/100;
    //   delay(5);
    // }
    readCityCodefromEEP(&cc);
    // for(int cnum=5;cnum>0;cnum--)
    // {          
    //   CCODE = CCODE*100;
    //   CCODE += EEPROM.read(CC_addr+cnum-1); 
    //   delay(5);
    // }
    cityCode = cc;
  }
  //屏幕方向
  Serial.print("LCD_Rotation = ");
  Serial.println(LCD_Rotation);
  if(EEPROM.read(Ro_addr) != LCD_Rotation)
  {
    EEPROM.write(Ro_addr, LCD_Rotation);
    EEPROM.commit();
    delay(5);
  }
  tft.setRotation(LCD_Rotation);
  tft.fillScreen(0x0000);
  Web_win();
  loadNum--;
  loading(1);
  //V1.6.5 配网门户里也能改亮度：写值的同时写"已写过"标志（原因见 setup 里的说明）；
  //        并把下限夹住，避免误填 0 变成黑屏后不好恢复。
  LCD_BL_PWM = constrain(LCD_BL_PWM, LCD_BL_MIN, 100);
  if(EEPROM.read(BL_addr) != LCD_BL_PWM || EEPROM.read(BLValid_addr) != 0x5A)
  {
    EEPROM.write(BL_addr, LCD_BL_PWM);
    EEPROM.write(BLValid_addr, 0x5A);
    EEPROM.commit();
    delay(5);
  }
  //屏幕亮度
  Serial.printf("亮度调整为：");
  setLCDBacklight(LCD_BL_PWM);
  Serial.println(LCD_BL_PWM);
  //天气更新时间
  Serial.printf("天气更新时间调整为：");
  Serial.println(updateweater_time);

  #if DHT_EN
  //是否使用DHT30传感器
  Serial.printf("DHT30传感器：");
  EEPROM.write(DHT_addr, DHT_img_flag);
  EEPROM.commit();//保存更改的数据
  Serial.println((DHT_img_flag?"已启用":"未启用"));
  #endif
}
#endif





/* ==================================================================
 *  V1.6.8 WiFi 重连状态机（全部非阻塞，由 loop() 每轮调用）
 * ================================================================== */

//按当前凭据发起连接（固定 IP 时连 DNS 一起配，否则天气域名解析不了）
void wifiBeginConnect()
{
#ifdef USE_STATIC_IP
  WiFi.config(esp32_staticIP, esp32_gateway, esp32_subnet, esp32_dns1, esp32_dns2);
#endif
  WiFi.begin(wificonf.stassid, wificonf.stapsw);
}

//屏幕右上角"无网络"角标：只用线条画，不填色块，避免又变成"黑框"盖住背景图
//位置 x=222..239 / y=0..15 —— 该区域没有任何其它元素
void wifiBadgeDraw()
{
  tft.drawCircle(231, 7, 6, TFT_RED);
  tft.drawLine(227, 3, 235, 11, TFT_RED);
  wifiBadgeShown = true;
}

void wifiBadgeErase()
{
  //优先从背景缓冲还原；没有背景图时填黑底（本来就全黑，无副作用）
  if (bgCount > 0 && bgCanvasReady) bgDrawRegion(222, 0, 18, 16);
  else                              tft.fillRect(222, 0, 18, 16, bgColor);
  wifiBadgeShown = false;
}

//每秒调一次：断开时持续重画（被整屏重绘冲掉也能自愈），恢复后擦掉一次。
// ⚠️ 必须独立计时，不能挂在时钟刷新里 —— 断网时 NTP 同步不上，now() 一直不变，
//    时钟刷新分支根本不会执行，角标就永远画不出来。
void wifiBadgeTask()
{
  if (wifiWasDown) wifiBadgeDraw();
  else if (wifiBadgeShown) wifiBadgeErase();
}

//进入配网门户：只有"用户显式请求"或"从未配过网"才会走到这里，**不清旧凭据**
void wifiEnterPortal()
{
  Serial.println("[V1.6.8] 进入配网门户（180 秒超时）");
#ifdef USE_STATIC_IP
  //先临时切回 DHCP：否则换到别的网段(如 192.168.1.x)后固定 IP 永远连不上
  WiFi.config(IPAddress(0,0,0,0), IPAddress(0,0,0,0), IPAddress(0,0,0,0));
  Serial.println("[V1.6.8] 已临时切回 DHCP，配网完成后再按配置恢复");
#endif
#if WM_EN
  Web_win();       //屏幕提示 AutoConnectAP
  Webconfig();     //门户（已不再清凭据、不再死循环）
#endif
#if !WM_EN
  SmartConfig();
#endif

  //门户退出后：把可能新写入的凭据同步进 EEPROM，并立刻再连一次
  if (WiFi.status() == WL_CONNECTED)
  {
    if (WiFi.SSID().length() > 0) {
      strncpy(wificonf.stassid, WiFi.SSID().c_str(), sizeof(wificonf.stassid) - 1);
      wificonf.stassid[sizeof(wificonf.stassid) - 1] = 0;
    }
    if (WiFi.psk().length() > 0) {
      strncpy(wificonf.stapsw, WiFi.psk().c_str(), sizeof(wificonf.stapsw) - 1);
      wificonf.stapsw[sizeof(wificonf.stapsw) - 1] = 0;
    }
    savewificonfig();
    Serial.println("[V1.6.8] 配网成功，凭据已保存");
  }
  else
  {
    readwificonfig();                 //沿用旧凭据（可能只是门户超时退出）
    Serial.println("[V1.6.8] 配网未完成，回到自动重连");
  }
  wifiBackoffMs = WIFI_BACKOFF_MIN;
  wifiFailCount = 0;
  wifiNextTryMs = 0;                  //下一轮 loop 立刻重连
}

//主状态机：每轮 loop 调用，绝不阻塞
void wifiTask()
{
  static uint32_t badgeMs = 0;

  //网页按钮/串口命令请求配网
  if (netPortalRequested)
  {
    netPortalRequested = false;
    wifiEnterPortal();
    return;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    if (wifiWasDown)                        //刚刚恢复
    {
      wifiWasDown   = false;
      wifiBackoffMs = WIFI_BACKOFF_MIN;
      wifiFailCount = 0;
      wifiBadgeErase();
      netUp();                              //netif 变了，mDNS 必须重建
      /* V1.6.9 ★关键：重连成功后必须补开 HTTP。
       *   V1.6.8 只重建设 mDNS —— 如果开机 8 秒内没连上，
       *   setup() 里那段 if 被跳过，server.begin() 就永远不会执行，
       *   于是 ping 通、sd3.local 能解析、但 80 端口永远 refused。 */
      httpStart();
      UpdateWeater_en = 1;                  //立刻补拉一次天气
      Hour_sign = Minute_sign = Second_sign = 60;   //强制重绘时钟
      Serial.printf("[V1.6.8] WiFi 已恢复，IP=%s\r\n", WiFi.localIP().toString().c_str());
    }
  }
  else
  {
    if (!wifiWasDown)                       //刚刚断开
    {
      wifiWasDown = true;
      wifiBadgeTask();
      Serial.println("[V1.6.8] WiFi 断开，开始自动重连");
    }

    if (millis() >= wifiNextTryMs)
    {
      if (strlen(wificonf.stassid) == 0)
      {
        wifiEnterPortal();                  //从来没配过网，只能进门户
      }
      else
      {
        wifiFailCount++;
        Serial.printf("[V1.6.8] 第 %u 次重连尝试 (ssid=%s)\r\n", wifiFailCount, wificonf.stassid);
        WiFi.disconnect(false, false);
        delay(10);
        wifiBeginConnect();
        wifiNextTryMs = millis() + wifiBackoffMs;
        wifiBackoffMs = (wifiBackoffMs * 2 > WIFI_BACKOFF_MAX) ? WIFI_BACKOFF_MAX : wifiBackoffMs * 2;
      }
    }
  }

  if (millis() - badgeMs >= 1000) { badgeMs = millis(); wifiBadgeTask(); }
}

void setup()
{
  Serial.begin(115200);
  EEPROM.begin(1024);
  // // ESP32-C3不支持ESP8266的forceSleepWake()
  // wm.resetSettings();    //在初始化中使wifi重置，需重新配置WiFi
  
  Wire.begin(I2C_SDA, I2C_SCL);      //初始化I2C总线（DHT30与OLED共用）
  Wire.setClock(400000);             //I2C提速到400kHz，加快OLED刷新

  //V1.5 初始化触摸引脚与SPIFFS
  pinMode(TTP223_PIN, INPUT_PULLDOWN);   //TTP223为高电平有效，用下拉保证空闲为低
  if (!SPIFFS.begin(true))
  {
    Serial.println("[V1.5] SPIFFS 挂载失败");
  }
  else
  {
    Serial.printf("[V1.5] SPIFFS 已挂载，总 %u / 已用 %u 字节\r\n",
                  (unsigned)SPIFFS.totalBytes(), (unsigned)SPIFFS.usedBytes());
    bgScanFiles();
  }

 #if DHT_EN
  //从eeprom读取DHT传感器使能标志
  DHT_img_flag = EEPROM.read(DHT_addr);
 #endif
 //从eeprom读取背光亮度设置
  //V1.6.5 改用独立的"已写过"标志，不再用 `<=100` 判断。
  //        本机未写过的字节是 0x00，0x00 <= 100 成立 → 旧写法会得到"亮度 0" → 接上背光就是黑屏。
  if (EEPROM.read(BLValid_addr) == 0x5A)
  {
    uint8_t b = EEPROM.read(BL_addr);
    LCD_BL_PWM = (b >= LCD_BL_MIN && b <= 100) ? b : 100;
  }
  else
  {
    LCD_BL_PWM = 100;                    //未写过：默认最亮，与原来"BLK 直连 3.3V"的观感一致
    EEPROM.write(BL_addr, LCD_BL_PWM);
    EEPROM.write(BLValid_addr, 0x5A);
    EEPROM.commit();
    delay(5);
  }
  //V1.6.7 读取背光极性（运行时设置，同样用"已写过"标志）
  if (EEPROM.read(BLPolValid_addr) == 0x5A)
    lcdBlActiveLow = (EEPROM.read(BLPol_addr) == 1);
  else
  {
    lcdBlActiveLow = false;              //默认：高电平点亮（直驱，最常见接法）
    EEPROM.write(BLPol_addr, 0);
    EEPROM.write(BLPolValid_addr, 0x5A);
    EEPROM.commit(); delay(5);
  }
  //V1.6.8 读取 OLED「无程序运行时显示今日总时长」开关（同样用"已写过"标志，
  //        绝不用值本身判断初始化 —— 见 V1.6.3 的教训）
  if (EEPROM.read(OledTodayValid_addr) == 0x5A)
  {
    oledTodayEn = (EEPROM.read(OledTodayEn_addr) != 0);
  }
  else
  {
    oledTodayEn = true;                  //默认开启（用户要求的功能）
    EEPROM.write(OledTodayEn_addr, 1);
    EEPROM.write(OledTodayValid_addr, 0x5A);
    EEPROM.commit(); delay(5);
  }
  loadBlSched();                       //V1.6.6 先读定时配置（必须早于 blApply）
  initLCDBacklight();
  blApply(true);                       //按"定时优先，否则手动"下发亮度
  Serial.printf("[V1.6.7] 背光: 手动=%d%% 实发=%d%% 引脚=%d 极性=%s\r\n",
                LCD_BL_PWM, blCurBright, LCD_BL_PIN,
                lcdBlActiveLow ? "低电平点亮(P-MOS/PNP)" : "高电平点亮(直驱)");
  if (LCD_BL_PIN < 0)
    Serial.println("[V1.6.7] ★背光引脚无效，未启用 PWM 调光");
  else
    Serial.println("[V1.6.7] 屏幕不亮 / 越亮越暗 -> 串口发字母 p 切换背光极性");
  //从eeprom读取屏幕方向设置
  if(EEPROM.read(Ro_addr)>=0&&EEPROM.read(Ro_addr)<=3)
    LCD_Rotation = EEPROM.read(Ro_addr);
  
  //从eeprom读取天气更新时间
  if(EEPROM.read(UpWeT_addr) >= 1 && EEPROM.read(UpWeT_addr) <= 60)
     updateweater_time = EEPROM.read(UpWeT_addr);

  //V1.5 读取文字配色与背景设置
  loadTextColors();
  {
    uint8_t a = EEPROM.read(BgAlpha_addr);
    bgAlpha = (a == 0xFF) ? 255 : a;      //未初始化过则默认原图
    uint8_t bi = EEPROM.read(BgIndex_addr);
    bgIndex = (bi < BG_MAX) ? bi : 0;
  }

  //V1.6 读取全局显示设置 + 当前壁纸的预设（预设会覆盖上面的配色/暗化值）
  loadGlobalDisplaySettings();
  loadBgVersions();            //V1.6.1 缩略图缓存标记
  if (bgSlotExists(bgIndex)) loadPreset(bgIndex);
  

  tft.begin(); /* TFT init */
  tft.invertDisplay(1);//反转所有显示颜色：1反转，0正常
  tft.setRotation(LCD_Rotation);
  tft.fillScreen(0x0000);
  tft.setTextColor(TFT_BLACK);

  //V1.5.4 背景缓存为全局静态数组（57,600 字节），由链接器预留，恒定可用。
  //        此处打印内存状况，便于确认是否还有"分配失败"类问题。
  if (bgCount > 0)
  {
    bgCanvasReady = true;   //静态缓冲，必然可用
    Serial.printf("[V1.5.4] 静态背景缓冲已就绪 %u 字节; heap=%u 最大连续块=%u\r\n",
                  (unsigned)BG_BUF_PIXELS, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
  else
  {
    bgCanvasReady = false;
    Serial.println("[V1.5.4] bgCount==0，未启用背景缓存");
  }

  targetTime = millis() + 1000; 
  readwificonfig();//读取存储的wifi信息
  Serial.print("正在连接WIFI ");
  Serial.println(wificonf.stassid);
  #ifdef USE_STATIC_IP
  //V1.5.5 固定IP：必须带DNS，否则天气接口的域名(weather.com.cn)解析不了
  Serial.printf("[IP] 使用固定IP: %s  网关: %s\r\n",
                esp32_staticIP.toString().c_str(), esp32_gateway.toString().c_str());
  #endif
  wifiBeginConnect();
  
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  //V1.6.8 开机只尝试这一次，而且**有上限**（最多 8 秒 / 进度条走完）。
  //  旧版在这里死等，等不到就清掉 WiFi 密码并进配网门户 —— 那是错的：
  //  路由器晚十几秒起来就白丢凭据。
  //  现在超时就先进主循环，由 wifiTask() 慢慢退避重连，屏幕先把界面画出来，
  //  右上角显示"无网络"角标，不再卡在进度条上。
  {
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED &&
           (millis() - t0) < WIFI_BOOT_WAIT_MS && loadNum < 194)
    {
      loading(30);
    }
    delay(10);
    while(loadNum < 194) //让动画走完
    {
      loading(1);
    }
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("[V1.6.8] 开机连接成功");
    }
    else
    {
      Serial.println("[V1.6.8] 开机未连上，交由 wifiTask() 自动重连（不清除已保存的 WiFi）");
      wifiWasDown   = true;
      wifiNextTryMs = millis() + 2000;   //2 秒后开始第一轮重试
      wifiBadgeTask();
    }
  }

  if(WiFi.status() == WL_CONNECTED)
  {
    // Serial.print("SSID:");
    // Serial.println(WiFi.SSID().c_str());
    // Serial.print("PSW:");
    // Serial.println(WiFi.psk().c_str());
    strcpy(wificonf.stassid,WiFi.SSID().c_str());//名称复制
    strcpy(wificonf.stapsw,WiFi.psk().c_str());//密码复制
    savewificonfig();
    readwificonfig();
  }
  #if WebSever_EN
  /* V1.6.9 ★关键改动：不再把 Web 初始化包在“开机当场连上”的条件里。
   *
   *  旧写法（V1.6.8）：
   *      if(WiFi.status()==WL_CONNECTED){ ... Web_Sever_Init(); Web_sever_Win(); delay(10000); }
   *  后果：开机 8 秒内没连上 → 整个块跳过 → server.begin() 永不执行
   *        → 之后重连成功也只重建 mDNS → 网页永远打不开（就是本次的故障）。
   *
   *  新写法：
   *      ① 先无条件调一次 Web_Sever_Init()：里面 netUp() + httpStart() 都是幂等的，
   *         已连上就立刻 listen；没连上则 httpStart() 直接返回 false（不影响开机）。
   *      ② 屏幕提示只在已连上时画（没连上屏幕上没有 IP 可展示）。
   *      ③ 不再 delay(10000) 干等 —— 那是旧版为了让屏幕上的网址停留 10 秒，
   *         但代价是开机后 10 秒内网页打不开，正好放大了故障观感。
   *         现在改成只等 2 秒（足够看清提示），HTTP 是异步 handleClient()，不靠这段 delay。
   *      ④ 真正的兜底在 wifiTask() 与 Web_Sever()：一旦 WiFi 连上就补开。
   */
  Web_Sever_Init();
  if (WiFi.status() == WL_CONNECTED) Web_sever_Win();
  delay(2000);
  #endif

  // Serial.print("本地IP： ");
  // Serial.println(WiFi.localIP());
  Serial.println("启动UDP");
  Udp.begin(localPort);
  //初始化游戏时长OLED并启动UDP监听
  oled_init();
  GameUdp.begin(GAME_UDP_PORT);
  Serial.println("OLED已就绪，游戏时长UDP监听已启动");
  Serial.println("等待同步...");
  setSyncProvider(getNtpTime);
  setSyncInterval(300);

  

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);
  
  int CityCODE = 0;
  readCityCodefromEEP(&CityCODE);
  // for(int cnum=5;cnum>0;cnum--)
  // {          
  //   CityCODE = CityCODE*100;
  //   CityCODE += EEPROM.read(CC_addr+cnum-1); 
  //   delay(5);
  // }
  if(CityCODE>=101000000 && CityCODE<=102000000) 
    cityCode = String(CityCODE);  
  else if (WiFi.status() == WL_CONNECTED)   //V1.6.8 离线时别硬闯网络，等重连后再取
    getCityCode();  //获取城市代码
   
  tft.fillScreen(TFT_BLACK);//清屏

  drawBg();   //V1.5 铺背景图（无图时为纯黑）

  drawIconWithBg(15,183,temperature, sizeof(temperature));  //温度图标
  drawIconWithBg(15,213,humidity, sizeof(humidity));  //湿度图标

  //V1.6.8 离线下不发这个请求（会阻塞好几秒）；重连成功后 wifiTask() 会置
  //        UpdateWeater_en=1 让它立刻补拉一次。
  if (WiFi.status() == WL_CONNECTED) getCityWeater();
#if DHT_EN
  if(DHT_img_flag != 0)
  IndoorTem();
#endif
#if !WebSever_EN
  // ESP32-C3不支持ESP8266的forceSleepBegin() //wifi off
  Serial.println("WIFI休眠......");
  Wifi_en = 0;
#endif
}



void loop()
{
  #if WebSever_EN
  Web_Sever();
  #endif
  wifiTask();       //V1.6.8 WiFi 断线自动重连（非阻塞状态机）
  bgTouchTask();    //V1.5 TTP223触摸切换背景图
  bgAutoRotateTask(); //V1.6 壁纸自动循环
  blSchedTask();    //V1.6.6 亮度定时（按时段自动调亮度）
  LCD_reflash(0);
  gameOledTask();   //游戏时长OLED任务（收UDP、刷新OLED）
  Serial_set();
}

/* *****************************************************************
 *  游戏时长OLED任务：接收电脑端UDP数据并刷新OLED显示
 * *****************************************************************/
void oledDrawGame()
{
  oled_clear();
  //顶部小字：
  //  有程序在跑        -> 显示该程序名
  //  没程序跑 + 开关开 -> 显示 TODAY TOTAL（今日所有已添加程序合计）
  //  其它              -> NO GAME
  //V1.6.8 gameTodayValid 用于兼容旧版上位机：没收到过 today 字段就不显示 TODAY，
  //        避免上位机还没升级时一直显示 00:00:00 让人误以为"今天没玩"。
  const char *name;
  long up;
  if (gameRunning)
  {
    name = gameName;
    up   = gameUp;
  }
  else if (oledTodayEn && gameTodayValid)
  {
    name = "TODAY TOTAL";
    up   = gameToday;
  }
  else
  {
    name = "NO GAME";
    up   = 0;
  }
  int len = strlen(name);
  if (len > 16) len = 16;
  oled_drawString((128 - len * 8) / 2, 1, name, 1);
  //大字：HH:MM:SS（2倍大小，满屏128宽）
  if (up < 0) up = 0;
  if (up > 99L * 3600 + 59 * 60 + 59) up = 99L * 3600 + 59 * 60 + 59; //封顶99小时
  int h = up / 3600;
  int m = (up % 3600) / 60;
  int s = up % 60;
  char buf[9];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
  oled_drawString(0, 24, buf, 2);
  oled_show();
}

void gameOledTask()
{
  int packetSize = GameUdp.parsePacket();
  if (packetSize) {
    char buf[160];
    int len = GameUdp.read(buf, sizeof(buf) - 1);
    if (len > 0) {
      buf[len] = 0;
      StaticJsonDocument<256> doc;
      if (deserializeJson(doc, buf) == DeserializationError::Ok) {
        gameRunning = doc["running"] | false;
        const char *n = doc["name"] | "";
        strncpy(gameName, n, sizeof(gameName) - 1);
        gameName[sizeof(gameName) - 1] = 0;
        gameUp = doc["up"] | 0;
        //V1.6.8 今日总时长（可选字段）。只有真的收到过才置 valid ——
        //       这样旧版上位机（不发 today）不会把 OLED 顶行写成 TODAY TOTAL 00:00:00。
        if (doc.containsKey("today")) {
          gameToday = doc["today"] | 0;
          gameTodayValid = true;
        }
        gameLastMs = millis();
      }
    }
  }
  if (gameRunning && (millis() - gameLastMs > GAME_TIMEOUT_MS)) {
    gameRunning = false;  //超时未收到数据，视为未运行
  }
  if (millis() - oledLastMs >= 1000) {
    oledLastMs = millis();
    oledDrawGame();
  }
}

void LCD_reflash(int en, bool allowFetch)
{
  if (now() != prevDisplay || en == 1) 
  {
    prevDisplay = now();
    digitalClockDisplay(en);
    prevTime=0;  
  }
  
  //两秒钟更新一次
  if(second()%2 ==0&& prevTime == 0 || en == 1){
#if DHT_EN
    if(DHT_img_flag != 0)
    IndoorTem();
#endif
    scrollBanner();
  }
#if imgAst_EN
  //V1.5 启用背景图时停用太空人动画（避免动画在背景上留下拖影）
  if(DHT_img_flag == 0 && bgCount == 0)
  imgAnim();
#endif


  //V1.6.4 把"是否需要联网更新天气"与"是否整屏重绘"解耦：
  //  原先 en==1 也会强制走一次阻塞式 getCityWeater()（DNS+连接+响应，慢时好几秒），
  //  而切换背景恰好调的就是 LCD_reflash(1) -> 天气区要等网络才画出来。
  //  现在 allowFetch=false 时永不联网；绘制由下面的 drawWeather() 用缓存完成。
  bool wxFresh = false;
  if(allowFetch && (millis() - weaterTime > (60000UL*updateweater_time) || en == 1 || UpdateWeater_en != 0)){
    if(Wifi_en == 0)
    {
      // ESP32-C3不支持ESP8266的forceSleepWake()//wifi on
      Serial.println("WIFI恢复......");
      Wifi_en = 1;
    }

    if(WiFi.status() == WL_CONNECTED)
    {
      // Serial.println("WIFI已连接");
      getCityWeater();
      wxFresh = true;
      if(UpdateWeater_en != 0) UpdateWeater_en = 0;
      weaterTime = millis();
      // while(!getNtpTime());
      getNtpTime();
      #if !WebSever_EN
      // ESP32-C3不支持ESP8266的forceSleepBegin() // Wifi Off
      Serial.println("WIFI休眠......");
      Wifi_en = 0;
      #endif
    }
  }

  //V1.6.4 整屏重绘后补画天气区（走缓存，0 网络开销）。
  //为什么必须有：drawBg() 会把整屏背景重铺一遍，天气文字/图标/温湿度条都被盖掉了，
  //若不在这里补画，就要等到下一次天气更新（最多 updateweater_time 分钟）才恢复。
  //wxFresh 时 weaterData() 内部已经画过一次，这里不重复。
  if (en == 1 && !wxFresh) drawWeather();
}

// 发送HTTP请求并且将服务器响应通过串口输出
void getCityCode(){
 String URL = "http://wgeo.weather.com.cn/ip/?_="+String(now());
  //创建 HTTPClient 对象
  HTTPClient httpClient;
 
  //配置请求地址。此处也可以不使用端口号和PATH而单纯的
  httpClient.begin(wificlient,URL); 
  
  //设置请求头中的User-Agent
  httpClient.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X) AppleWebKit/604.1.38 (KHTML, like Gecko) Version/11.0 Mobile/15A372 Safari/604.1");
  httpClient.addHeader("Referer", "http://www.weather.com.cn/");
 
  //启动连接并发送HTTP请求
  int httpCode = httpClient.GET();
  Serial.print("Send GET request to URL: ");
  Serial.println(URL);
  
  //如果服务器响应OK则从服务器获取响应体信息并通过串口输出
  if (httpCode == HTTP_CODE_OK) {
    String str = httpClient.getString();
    
    int aa = str.indexOf("id=");
    if(aa>-1)
    {
       //cityCode = str.substring(aa+4,aa+4+9).toInt();
       cityCode = str.substring(aa+4,aa+4+9);
       Serial.println(cityCode); 
       getCityWeater();
    }
    else
    {
      Serial.println("获取城市代码失败");  
    }
    
    
  } else {
    Serial.println("请求城市代码错误：");
    Serial.println(httpCode);
  }
 
  //关闭ESP8266与服务器连接
  httpClient.end();
}



// 获取城市天气
void getCityWeater(){
 //String URL = "http://d1.weather.com.cn/dingzhi/" + cityCode + ".html?_="+String(now());//新
 String URL = "http://d1.weather.com.cn/weather_index/" + cityCode + ".html?_="+String(now());//原来
  //创建 HTTPClient 对象
  HTTPClient httpClient;
  
  httpClient.begin(wificlient, URL); 
  
  //设置请求头中的User-Agent
  httpClient.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X) AppleWebKit/604.1.38 (KHTML, like Gecko) Version/11.0 Mobile/15A372 Safari/604.1");
  httpClient.addHeader("Referer", "http://www.weather.com.cn/");
 
  //启动连接并发送HTTP请求
  int httpCode = httpClient.GET();
  Serial.println("正在获取天气数据");
  Serial.println(URL);
  
  //如果服务器响应OK则从服务器获取响应体信息并通过串口输出
  if (httpCode == HTTP_CODE_OK) {

    String str = httpClient.getString();
    int indexStart = str.indexOf("weatherinfo\":");
    int indexEnd = str.indexOf("};var alarmDZ");

    String jsonCityDZ = str.substring(indexStart+13,indexEnd);
    //Serial.println(jsonCityDZ);

    indexStart = str.indexOf("dataSK =");
    indexEnd = str.indexOf(";var dataZS");
    String jsonDataSK = str.substring(indexStart+8,indexEnd);
    //Serial.println(jsonDataSK);

    
    indexStart = str.indexOf("\"f\":[");
    indexEnd = str.indexOf(",{\"fa");
    String jsonFC = str.substring(indexStart+5,indexEnd);
    //Serial.println(jsonFC);
    
    weaterData(&jsonCityDZ,&jsonDataSK,&jsonFC);
    Serial.println("获取成功");
    
  } else {
    Serial.println("请求城市天气错误：");
    Serial.print(httpCode);
  }
 
  //关闭ESP8266与服务器连接
  httpClient.end();
}


String scrollText[7];//天气信息存储

/* ---------------- V1.6.4 天气：把「抓取/解析」与「绘制」彻底分开 ----------------
 * 原先 weaterData() 既解析又绘制，而 LCD_reflash(en==1)（切换背景就会调它）
 * 会强制走一次阻塞式 getCityWeater()：DNS+连接+响应慢的时候要好几秒，
 * 这期间天气区域一直是空的 —— 就是"切换背景刷新时天气非常慢才显示"的原因。
 *
 * 现在：把绘制需要的字段缓存到 wx*，绘制由 drawWeather() 独立负责。
 *   · 切背景 -> LCD_reflash(1, allowFetch=false) -> 走缓存重绘，0 网络开销
 *   · 到点更新 -> 仍走 getCityWeater()，成功后自动按新数据重绘
 * 附带好处：网络失败时天气区不会变成空白，仍保留上一次的数据。
 * --------------------------------------------------------------------------- */
//按缓存数据重绘整个天气区域（不联网、无阻塞）
void drawWeather()
{
  if (!wxValid) return;        //还没拿到过数据：什么都不画，避免画半截

  clk.setColorDepth(16);       //V1.5.1 改16bpp，避免8bpp量化导致背景贴片变黑
  clk.loadFont(ZdyLwFont_20);
  uint16_t c2 = finalText2();  //V1.6 自动换色后的最终文字色
  uint16_t c3 = finalText3();

  //室外温度
  clk.createSprite(58, 24);
  bgFillSprite(clk,100,184,58,24,c3);   //V1.5 先贴背景
  clk.setTextDatum(CC_DATUM);
  drawTextEx(clk, wxTempS+"℃", 28, 13, c3, bgOutlineOn);
  clk.pushSprite(100,184);
  clk.deleteSprite();
  tempnum = wxTempS.toInt();
  tempnum = tempnum+10;
  if(tempnum<10)
    tempcol=0x00FF;
  else if(tempnum<28)
    tempcol=0x0AFF;
  else if(tempnum<34)
    tempcol=0x0F0F;
  else if(tempnum<41)
    tempcol=0xFF0F;
  else if(tempnum<49)
    tempcol=0xF00F;
  else
  {
    tempcol=0xF00F;
    tempnum=50;
  }
  tempWin();

  //室外湿度
  clk.createSprite(58, 24);
  bgFillSprite(clk,100,214,58,24,c3);   //V1.5 先贴背景
  clk.setTextDatum(CC_DATUM);
  drawTextEx(clk, wxSDS, 28, 13, c3, bgOutlineOn);
  clk.pushSprite(100,214);
  clk.deleteSprite();
  huminum = atoi(wxSDS.substring(0,2).c_str());
  if(huminum>90)
    humicol=0x00FF;
  else if(huminum>70)
    humicol=0x0AFF;
  else if(huminum>40)
    humicol=0x0F0F;
  else if(huminum>20)
    humicol=0xFF0F;
  else
    humicol=0xF00F;
  humidityWin();

  //城市名称
  clk.createSprite(94, 30);
  bgFillSprite(clk,15,15,94,30,c2);   //V1.5 先贴背景
  clk.setTextDatum(CC_DATUM);
  drawTextEx(clk, wxCityS, 44, 16, c2, bgOutlineOn);
  clk.pushSprite(15,15);
  clk.deleteSprite();

  //PM2.5空气指数徽章（自带彩色底 + 黑字，对比度由底色保证，不参与自动换色/描边）
  clk.createSprite(56, 24);
  bgFillSprite(clk,104,18,56,24,c2);   //V1.5 先贴背景
  clk.fillRoundRect(0,0,50,24,4,wxAqiCol);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(0x0000);
  clk.drawString(wxAqiTxt,25,13);
  clk.pushSprite(104,18);
  clk.deleteSprite();

  //天气图标：先恢复该区域背景（图标实测 56x56），再叠图标
  if (bgCount > 0 && bgCanvasReady)
  {
    int16_t iw = 56, ih = 56;
    if (170 + iw > BG_WIDTH)  iw = BG_WIDTH  - 170;
    if (15  + ih > BG_HEIGHT) ih = BG_HEIGHT - 15;
    if (iw > 0 && ih > 0)
      bgDrawRegion(170, 15, iw, ih);   //V1.5.4 从 RGB332 缓冲还原该区域
  }
  else
  {
    tft.fillRect(170, 15, 56, 56, bgColor);
  }
  wrat.printfweather(170,15,wxCode);

  clk.unloadFont();
}

//天气信息写到屏幕上
void weaterData(String *cityDZ,String *dataSK,String *dataFC)
{
  //解析第一段JSON
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, *dataSK);
  JsonObject sk = doc.as<JsonObject>();

  //★V1.6.4 这里只做"解析 -> 存缓存/滚动文本"，绘制全部交给 drawWeather()。
  //  这样切背景时能用缓存零延迟重画天气区，不必等一次阻塞式 HTTP 抓取。
  wxTempS = sk["temp"].as<String>();
  wxSDS   = sk["SD"].as<String>();
  wxCityS = sk["cityname"].as<String>();
  wxCode  = atoi((sk["weathercode"].as<String>()).substring(1,3).c_str());

  //PM2.5空气指数
  uint16_t pm25BgColor = tft.color565(156,202,127);//优
  String aqiTxt = "优";
  int pm25V = sk["aqi"];
  if(pm25V>200){
    pm25BgColor = tft.color565(136,11,32);//重度
    aqiTxt = "重度";
  }else if(pm25V>150){
    pm25BgColor = tft.color565(186,55,121);//中度
    aqiTxt = "中度";
  }else if(pm25V>100){
    pm25BgColor = tft.color565(242,159,57);//轻
    aqiTxt = "轻度";
  }else if(pm25V>50){
    pm25BgColor = tft.color565(247,219,100);//良
    aqiTxt = "良";
  }
  wxAqiCol = pm25BgColor;
  wxAqiTxt = aqiTxt;

  scrollText[0] = "实时天气 "+sk["weather"].as<String>();
  scrollText[1] = "空气质量 "+aqiTxt;
  scrollText[2] = "风向 "+sk["WD"].as<String>()+sk["WS"].as<String>();

  //左上角滚动字幕：解析第二段 JSON
  deserializeJson(doc, *cityDZ);
  JsonObject dz = doc.as<JsonObject>();
  scrollText[3] = "今日"+dz["weather"].as<String>();

  deserializeJson(doc, *dataFC);
  JsonObject fc = doc.as<JsonObject>();
  scrollText[4] = "最低温度"+fc["fd"].as<String>()+"℃";
  scrollText[5] = "最高温度"+fc["fc"].as<String>()+"℃";

  wxValid = true;      //数据已就绪，此后 drawWeather() 可随时用缓存重绘
  drawWeather();       //立即按新数据绘制一次
}

int currentIndex = 0;
TFT_eSprite clkb = TFT_eSprite(&tft);

void scrollBanner(){
  //if(millis() - prevTime > 2333) //3秒切换一次
//  if(second()%2 ==0&& prevTime == 0)
//  { 
    if(scrollText[currentIndex])
    {
      clkb.setColorDepth(16);  //V1.5.1 改16bpp，避免8bpp量化导致背景贴片变黑
      clkb.loadFont(ZdyLwFont_20);
      clkb.createSprite(150, 30); 
      uint16_t c2 = finalText2();   //V1.6 自动换色后的最终文字色
      bgFillSprite(clkb,10,45,150,30,c2);   //V1.5 先贴背景
      clkb.setTextWrap(false);
      clkb.setTextDatum(CC_DATUM);
      drawTextEx(clkb, scrollText[currentIndex], 74, 16, c2, bgOutlineOn);
      clkb.pushSprite(10,45);
       
      clkb.deleteSprite();
      clkb.unloadFont();
      
      if(currentIndex>=5)
        currentIndex = 0;  //回第一个
      else
        currentIndex += 1;  //准备切换到下一个        
    }
    prevTime = 1;
//  }
}


#if imgAst_EN
void imgAnim()
{
  int x=160,y=160;
  if(millis() - AprevTime > 37) //x ms切换一次
  {
    Anim++;
    AprevTime = millis();
  }
  if(Anim==10)
    Anim=0;

  switch(Anim)
  {
    case 0:
      TJpgDec.drawJpg(x,y,i0, sizeof(i0));
      break;
    case 1:
      TJpgDec.drawJpg(x,y,i1, sizeof(i1));
      break;
    case 2:
      TJpgDec.drawJpg(x,y,i2, sizeof(i2));
      break;
    case 3:
      TJpgDec.drawJpg(x,y,i3, sizeof(i3));
      break;
    case 4:
      TJpgDec.drawJpg(x,y,i4, sizeof(i4));
      break;
    case 5:
      TJpgDec.drawJpg(x,y,i5, sizeof(i5));
      break;
    case 6:
      TJpgDec.drawJpg(x,y,i6, sizeof(i6));
      break;
    case 7:
      TJpgDec.drawJpg(x,y,i7, sizeof(i7));
      break;
    case 8: 
      TJpgDec.drawJpg(x,y,i8, sizeof(i8));
      break;
    case 9: 
      TJpgDec.drawJpg(x,y,i9, sizeof(i9));
      break;
    default:
      Serial.println("显示Anim错误");
      break;
  }
}
#endif

void digitalClockDisplay(int reflash_en){ 
  int timey=82;
  unsigned char hour12 = hour() % 12;//转换为12小时制
  if(hour12 == 0) hour12 = 12;//0点显示12

  //V1.6 时钟数字也参与自动换色（按时钟区域背景亮度，保留原色相只调明暗）
  uint16_t cHour = finalHour();
  uint16_t cMin  = finalMin();
  uint16_t cSec  = finalSec();
  uint16_t c2    = finalText2();

  //V1.6.6 数字描边色：自动取反（亮字配黑边 / 暗字配白边）或用户固定色。
  //        时/分/秒三组笔画色各自可能不同，所以描边色要分别算。
  const uint16_t oHour = digitOutlineColor(cHour);
  const uint16_t oMin  = digitOutlineColor(cMin);
  const uint16_t oSec  = digitOutlineColor(cSec);
  const bool     oOn   = bgNumOutlineOn;   //数字描边是独立开关

  if(hour12!=Hour_sign || reflash_en == 1)//时钟刷新
  {
    //V1.6.6 掩膜字模：最后一次参数是描边开关/描边色（见 number.cpp）
    dig.printfW3660(20,timey,hour12/10, cHour, oOn, oHour);
    dig.printfW3660(60,timey,hour12%10, cHour, oOn, oHour);
    Hour_sign = hour12;

    //上午/下午显示（秒钟上方空白处）
    clk.setColorDepth(16);  //V1.5.1 改16bpp，避免8bpp量化导致背景贴片变黑
    clk.loadFont(ZdyLwFont_20);
    clk.createSprite(44, 24);
    bgFillSprite(clk,179,85,44,24,c2);   //V1.5 先贴背景
    clk.setTextDatum(CC_DATUM);
    drawTextEx(clk, hour()<12?"上午":"下午", 22, 12, c2, bgOutlineOn);
    clk.pushSprite(179,85);
    clk.deleteSprite();
    clk.unloadFont();
  }
  if(minute()!=Minute_sign  || reflash_en == 1)//分钟刷新
  {
    //V1.5.1 分钟动态着色
    dig.printfW3660(101,timey,minute()/10, cMin, oOn, oMin);
    dig.printfW3660(141,timey,minute()%10, cMin, oOn, oMin);
    Minute_sign = minute();
  }
  if(second()!=Second_sign  || reflash_en == 1)//秒钟刷新
  {
    //V1.5.1 秒钟动态着色
    dig.printfW1830(182,timey+30,second()/10, cSec, oOn, oSec);
    dig.printfW1830(202,timey+30,second()%10, cSec, oOn, oSec);
    Second_sign = second();
  }
  
  if(reflash_en == 1) reflash_en = 0;
  /***日期****/
  clk.setColorDepth(16);  //V1.5.1 改16bpp，避免8bpp量化导致背景贴片变黑
  clk.loadFont(ZdyLwFont_20);
   
  //星期
  clk.createSprite(58, 30);
  bgFillSprite(clk,102,150,58,30,c2);   //V1.5 先贴背景
  clk.setTextDatum(CC_DATUM);
  drawTextEx(clk, week(), 29, 16, c2, bgOutlineOn);
  clk.pushSprite(102,150);
  clk.deleteSprite();
  
  //月日
  clk.createSprite(95, 30);
  bgFillSprite(clk,5,150,95,30,c2);   //V1.5 先贴背景
  clk.setTextDatum(CC_DATUM);
  drawTextEx(clk, monthDay(), 49, 16, c2, bgOutlineOn);
  clk.pushSprite(5,150);
  clk.deleteSprite();
  
  clk.unloadFont();
  /***日期****/
}

//星期
String week()
{
  String wk[7] = {"日","一","二","三","四","五","六"};
  String s = "周" + wk[weekday()-1];
  return s;
}

//月日
String monthDay()
{
  String s = String(month());
  s = s + "月" + day() + "日";
  return s;
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP时间在消息的前48字节中
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  //Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  //Serial.print(ntpServerName);
  //Serial.print(": ");
  //Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      //Serial.println(secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // 无法获取时间时返回0
}

// 向NTP服务器发送请求
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
