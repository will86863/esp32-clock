# SmallDesktopDisplay · 小型桌面显示器

一台 ESP32 + ST7789 240×240 桌面时钟/天气屏，外加一个 Windows 上位机程序（GameTime），
把游戏时长推送到屏幕上的 0.96 寸 OLED。

项目包含**固件**（Arduino/ESP32）与**上位机**（Go + Wails 桌面程序）两部分，
二者通过局域网 UDP 通信，协议已固定，上位机改动不需要重新烧录固件。

```
┌──────────────────┐       UDP 5005 / JSON        ┌─────────────────┐
│  GameTime.exe    │ ───────────────────────────► │  ESP32 固件     │
│  (Windows 上位机) │     每秒一帧                  │  ST7789 + OLED  │
└──────────────────┘                              └─────────────────┘
```

---

## 目录结构

```
V1.4/
├─ SmallDesktopDisplay/       ESP32 固件（Arduino sketch）
│  ├─ SmallDesktopDisplay.ino   主程序（约 4400 行）
│  ├─ number.cpp/.h             数字绘制
│  ├─ weathernum.cpp/.h         天气数字
│  ├─ oled.h / font8x16.h       OLED 驱动与字库
│  ├─ qr.h                      二维码
│  ├─ font/                     字体头文件（含 ZdyLwFont_20.h 自定义字库）
│  ├─ img/                      图片资源（背景、天气图标、太空人动画）
│  └─ ESP32_ST7789_240x240_说明.md   硬件接线与固件说明
│
├─ GameTime/                  Windows 上位机（Go + Wails v2）
│  ├─ main.go                   启动、命令行、资源嵌入
│  ├─ app.go                    暴露给前端的 API
│  ├─ monitor.go                监控主循环（进程 → 会话 → UDP）
│  ├─ protocol.go               UDP 载荷与实时状态
│  ├─ config.go                 配置读写 + 显示名校验
│  ├─ session.go                「最后启动优先」选择逻辑
│  ├─ winproc.go                进程枚举 + 创建时间
│  ├─ steam.go                  Steam 库/appmanifest/候选 exe
│  ├─ shortcut.go               .lnk 解析（COM + 兜底）
│  ├─ lnk_com.go                IShellLinkW COM 调用
│  ├─ gbk.go                    ANSI/GBK 解码
│  ├─ icon.go                   exe 图标提取
│  ├─ openpath.go               打开文件夹（explorer）
│  ├─ stats.go                  会话统计（JSONL）
│  ├─ tray.go / autostart.go    托盘、开机自启
│  ├─ singleinstance.go         单实例互斥
│  ├─ frontend/                 React 18 + Fluent UI v9 + Vite
│  ├─ assets/                   图标（app.ico / appicon.png / tray.ico）
│  └─ tools/gen_icons.mjs       图标生成脚本
│
├─ game-uptime.bak_preWails/  旧版上位机（Go，无界面，仅作参考）
├─ tools/                     数字掩码生成与校验脚本（Python）
├─ _archive/backups/          固件历史快照（.bak_v14 … .bak_v168，共 18 个）
├─ SmallDesktopDisplay_V1.4.bin   发布固件（可直接烧录）
├─ V1.4固件说明.txt             版本变更记录
├─ display_mockup.png         显示效果示意图
└─ misaka111.jpg              原项目作者图片
```

> `_archive/` 已被 `.gitignore` 排除，不会上传到仓库。

---

## 硬件

| 项 | 参数 |
|---|---|
| 主控 | ESP32（经典款，非 C3 / S3） |
| 屏幕 | ST7789 240×240，SPI 27MHz |
| 屏幕引脚 | MOSI=23, SCLK=18, CS=5, DC=27, RST=4（不用 MISO） |
| 背光 | BLK 直连 GPIO33，高电平点亮 |
| I2C | SDA=21, SCL=22（400kHz） |
| 温湿度 | DHT30 / SHT30 @ `0x44`（ADDR 接 GND） |
| OLED | SSD1306 128×64 @ `0x3C`，与温湿度共用 I2C 总线 |
| 触摸 | TTP223，SIG=GPIO25，高电平有效，单击切换背景 |
| 分区方案 | **必须选 Huge APP (3MB No OTA/1MB SPIFFS)** |

⚠️ GPIO5 已被 TFT_CS 占用不可他用；GPIO6–11 接 Flash，绝对不可用。

---

## 编译固件

工具链（**只有这套是有效的**）：

| 项 | 值 |
|---|---|
| ESP32 core | 3.3.11 |
| TFT_eSPI | 2.5.43 |
| TJpg_Decoder | 1.1.0 |
| ArduinoJson | 7.4.3 |

```bash
export ARDUINO_DIRECTORIES_DATA="D:/ProgramData/Arduino15"
export ARDUINO_DIRECTORIES_USER="D:/Users/Evan/Documents/Arduino"

arduino-cli compile \
  --fqbn "esp32:esp32:esp32:PartitionScheme=huge_app" \
  "SmallDesktopDisplay"
```

或在 Arduino IDE 里：开发板选 `ESP32 Dev Module`，分区方案选 `Huge APP`。

---

## GameTime 上位机

监控电脑上指定的程序，把**最后启动的那个**的运行时长每秒发给 ESP32 显示。

### 快速开始

1. 运行 `GameTime.exe`（常驻托盘）。
2. 「程序」页 → 点「选择程序…」，或**直接把桌面快捷方式拖进窗口**
   - 支持 `.exe`、`.lnk`（普通快捷方式）、`.url`（Steam 游戏快捷方式）
3. 「设置」页填 ESP32 的 IP（默认 `192.168.5.86`，端口 `5005`）。
4. 完成，OLED 会显示程序名和 `HH:MM:SS`。

### 功能

- **按程序统计**：可添加多个程序，OLED 只显示最后启动的那个
- **Steam 识别**：拖入 `.url` 自动解析 appid → 游戏名 → 安装目录 → 候选 exe（带图标与体积）
- **今日总时长**：没有程序运行时，OLED 显示 `TODAY TOTAL`
- **统计图表**：今日各程序横向柱状、近 7/30 天竖柱状趋势
- **托盘常驻**：关闭按钮=最小化，左键托盘图标=打开界面
- **开机自启**：`-minimized` 参数启动，默认隐藏到托盘
- **单实例**：重复启动只会把已有窗口叫到前面

### UDP 协议（与固件严格对齐，改上位机不需要动固件）

发往 `<esp32_ip>:5005`，UTF-8 JSON，每秒一帧：

```json
{"running":true,"name":"Rewrite+","up":18121,"today":18121}
{"running":false,"name":"","up":0,"today":19000}
```

| 字段 | 含义 | 固件行为 |
|---|---|---|
| `running` | 是否有已添加程序在运行 | `true` 显示程序名 + 时长 |
| `name` | 显示名（ASCII ≤16 字符） | 居中显示在 OLED 顶部 |
| `up` | 本次会话已运行秒数 | 大字 `HH:MM:SS` |
| `today` | 今日已添加程序合计秒数 | 仅 `running:false` 时显示 `TODAY TOTAL` |

固件侧硬约束（读 `SmallDesktopDisplay.ino` 确认）：

- `char gameName[24]` → name ≤ 23 字节
- `oledDrawGame()` 用 `(128-len*8)/2` 居中且**不截断** → 可见宽度 ≤ 16 字符
- `GAME_TIMEOUT_MS = 5000` → 超过 5 秒收不到数据即视为未运行
- `today` 仅在 `!running && oledTodayEn && gameTodayValid` 时显示

**因此显示名只能是纯英文且 ≤16 字符**，界面会实时校验。

### 编译上位机

```bash
export GOPATH="C:/Users/will/.workbuddy/binaries/go/gopath"
export GOPROXY="https://goproxy.cn,direct"
export GOSUMDB="sum.golang.google.cn"
export GOTOOLCHAIN="go1.27.2"
export PATH="$GOPATH/bin:/c/Users/will/.workbuddy/binaries/go/go/bin:$PATH"
export GOFLAGS="-mod=mod"

cd GameTime
wails build -platform windows/amd64
# 产物：build/bin/GameTime.exe
```

前端单独调试：`cd frontend && npm install && npm run build`

图标重新生成：`node tools/gen_icons.mjs`（改完记得 `cp assets/appicon.png build/appicon.png`）

> ⚠️ 不要在项目根放 `rsrc.syso`，Wails 自己会生成 `GameTime-res.syso`，
> 两个同时存在会报 `too many .rsrc sections`。

### 数据位置

| 路径 | 内容 |
|---|---|
| `%APPDATA%\GameTime\config.json` | 配置 |
| `%APPDATA%\GameTime\sessions.jsonl` | 会话统计 |
| `%APPDATA%\GameTime\session_active.json` | 进行中会话快照（异常退出后补记） |
| `%APPDATA%\GameTime\gametime.log` | 运行日志 |

---

## 依赖

| 组件 | 版本 | 许可 |
|---|---|---|
| Wails | v2.14.0 | MIT |
| Fluent UI React | v9.74.9 | MIT |
| @fluentui/react-nav | v9.4.7 | MIT |
| energye/systray | v1.0.3 | Apache-2.0 |
| Tabler Icons | 3.49.0 | MIT |
| sharp（仅构建期） | 0.35.5 | Apache-2.0 |

运行时只依赖系统自带 **WebView2**（Win10 21H2+ / Win11 预装）。

---

## 致谢

- 原作者：**Misaka**
- 修改：**微车游**
- 上位机重写：本项目 GameTime
