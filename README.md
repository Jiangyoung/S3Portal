# ESP32-S3 每日一句按键系统

按下 K1-K8 任意按键，ESP32-S3 调用后端 API，后端调用大模型生成一段趣味短内容，最后显示到 OLED 屏幕。

**硬件**: ESP32-S3-WROOM-1-N16R8 · 八位独立按键模块 · 0.96" SSD1306 OLED  
**后端**: Python FastAPI · OpenAI 兼容协议 · Docker 可部署  
**前端**: 单文件 HTML 接线演示页，可点击接线图里的 K1-K8 模拟真实按键

---

## 当前结论

实测 OLED 可以被 I2C 扫描到 `0x3C`，但 U8g2 的硬件 I2C 初始化会卡在 `u8g2.begin()`。当前固件已改为 **U8g2 软件 I2C**，仍然使用同一组接线：

```cpp
SCL = GPIO14
SDA = GPIO13
OLED I2C address = 0x3C
```

按键模块上电灯亮只能说明 VCC/GND 有电。是否接对信号线，需要看串口日志里的 GPIO 电平变化。

---

## 接线方案

### ESP32-S3 开发板方向

USB 接口朝下，左右各 22 针。

```text
         [USB-OTG]  [USB-UART]
            ┌──────────────┐
    3.3V ── │ L1        R1 │ ── GND
    3.3V ── │ L2        R2 │ ── TX
     RST ── │ L3        R3 │ ── RX
   [K1]4 ── │ L4        R4 │ ── IO1
   [K2]5 ── │ L5        R5 │ ── IO2
   [K3]6 ── │ L6        R6 │ ── IO42 [I2S DIN *]
   [K4]7 ── │ L7        R7 │ ── IO41 [I2S LRC *]
  [K5]15 ── │ L8        R8 │ ── IO40 [I2S BCLK*]
  [K6]16 ── │ L9        R9 │ ── IO39 [I2S SD  *]
  [K7]17 ── │ L10      R10 │ ── IO38
  [K8]18 ── │ L11      R11 │ ── IO37
      IO8 ── │ L12      R12 │ ── IO36
      IO3 ── │ L13      R13 │ ── IO35
     IO46 ── │ L14      R14 │ ── IO0
      IO9 ── │ L15      R15 │ ── IO45
     IO10 ── │ L16      R16 │ ── IO48 (板载 RGB)
     IO11 ── │ L17      R17 │ ── IO47
     IO12 ── │ L18      R18 │ ── IO21
[SDA]IO13 ── │ L19      R19 │ ── IO20
[SCL]IO14 ── │ L20      R20 │ ── IO19
      5V  ── │ L21      R21 │ ── GND
      GND ── │ L22      R22 │ ── GND
            └──────────────┘
              * 预留，本期暂不焊接
```

### 八位按键模块

```text
按键模块              ESP32-S3
──────────            ────────
VCC  ──────────────→  L2  (3.3V)
GND  ──────────────→  L22 (GND)
K1   ──────────────→  L4  (GPIO4)
K2   ──────────────→  L5  (GPIO5)
K3   ──────────────→  L6  (GPIO6)
K4   ──────────────→  L7  (GPIO7)
K5   ──────────────→  L8  (GPIO15)
K6   ──────────────→  L9  (GPIO16)
K7   ──────────────→  L10 (GPIO17)
K8   ──────────────→  L11 (GPIO18)
```

模块按键为 active-LOW：未按下为 HIGH/1，按下为 LOW/0。

### OLED SSD1306 128x64 I2C

```text
OLED              ESP32-S3
────              ────────
VCC  ──────────→  L1  (3.3V)
GND  ──────────→  R1  (GND)
SCL  ──────────→  L20 (GPIO14)
SDA  ──────────→  L19 (GPIO13)
```

注意：OLED 模块常见排针顺序不统一，可能是 `VCC-GND-SCL-SDA`，也可能是 `GND-VCC-SCL-SDA`。接线时以实物丝印为准。

### MAX98357A 音频放大器（预留）

```text
MAX98357A          ESP32-S3
─────────          ────────
VIN   ──────────→  L1  (3.3V)
GND   ──────────→  R1  (GND)
DIN   ──────────→  R6  (GPIO42)
BCLK  ──────────→  R8  (GPIO40)
LRC   ──────────→  R7  (GPIO41)
SD    ──────────→  R9  (GPIO39)
扬声器+ ────────→  MAX98357A 接线柱 +
扬声器- ────────→  MAX98357A 接线柱 -
```

---

## K1-K8 功能

8 个按键输出不同类型的趣味短内容。后端 Prompt 配置在 `server/config.yaml`，OLED 第一行是黄色标题栏，第二行开始显示等待文案或返回内容。

| 按键 | 功能 | 按下后的等待文案 | 返回风格 |
|------|------|------------------|----------|
| K1 | 今日咒语 | 正在给今天施法 | 给自己加一个轻松 buff |
| K2 | 赛博签 | 正在抽取赛博签 | 霓虹味今日签 |
| K3 | 反emo | 正在驱散低气压 | 把低气压拧小一点 |
| K4 | 脑洞任务 | 正在加载脑洞任务 | 立刻可做的小怪事 |
| K5 | 冷幽默 | 正在冷却幽默 | 轻微荒诞短句 |
| K6 | 猫语翻译 | 正在翻译猫语 | 猫主子的指令 |
| K7 | 随机冒险 | 正在开启小冒险 | 微型剧情开场 |
| K8 | 未来预言 | 正在读取明天 | 来自明天的小纸条 |

---

## VS Code + PlatformIO 从零复刻

这一节按别人第一次拿到项目来写。完成后应能做到：后端 API 可访问、ESP32 固件上传成功、Serial Monitor 能看到日志、按下 K1-K8 后 OLED 显示内容。

### 1. 安装软件

需要安装：

1. **VS Code**
2. **PlatformIO IDE 插件**
3. **Python 3.10+**
4. **Git**
5. ESP32-S3 开发板 USB 串口驱动，按板子实际芯片安装，常见是 CH343/CH340/CP210x

VS Code 插件安装：

1. 打开 VS Code。
2. 左侧点 Extensions。
3. 搜索 `PlatformIO IDE`。
4. 安装后重启 VS Code。
5. 左侧出现 PlatformIO 图标后说明插件可用。

### 2. 获取项目

```bash
git clone git@github.com:Jiangyoung/S3Portal.git
cd S3Portal
```

如果没有配置 GitHub SSH，也可以用 HTTPS：

```bash
git clone https://github.com/Jiangyoung/S3Portal.git
cd S3Portal
```

然后用 VS Code 打开项目根目录，也就是包含 `platformio.ini` 的目录。不要只打开 `src/` 或 `server/` 子目录。

### 3. 打开 PlatformIO 项目

打开项目后，PlatformIO 会识别 `platformio.ini`：

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
```

首次打开时 PlatformIO 会自动下载 ESP32 平台和依赖库：

- `olikraus/U8g2`
- `bblanchon/ArduinoJson`

这一步需要联网，可能需要等几分钟。下载完成后，左侧 PlatformIO 面板中会出现环境 `esp32-s3-devkitc-1`。

### 4. 接线

按本文“接线方案”完成硬件连接。最少需要：

- ESP32-S3 开发板
- OLED：`VCC/GND/SCL/SDA`
- 八位按键模块：`VCC/GND/K1-K8`

OLED 重点：

```text
OLED SCL -> ESP32 GPIO14
OLED SDA -> ESP32 GPIO13
OLED VCC -> 3.3V
OLED GND -> GND
```

按键重点：

```text
K1 -> GPIO4
K2 -> GPIO5
K3 -> GPIO6
K4 -> GPIO7
K5 -> GPIO15
K6 -> GPIO16
K7 -> GPIO17
K8 -> GPIO18
```

OLED 排针顺序必须按实物丝印接，不要只照模块外观方向猜。

### 5. 启动后端服务

后端负责接收 ESP32 请求并调用大模型。先进入 `server`：

Windows PowerShell：

```powershell
cd server
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
copy .env.example .env
```

Windows CMD：

```bat
cd server
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
copy .env.example .env
```

macOS / Linux：

```bash
cd server
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
cp .env.example .env
```

编辑 `server/.env`，至少配置一组 LLM：

```env
LLM_NAMES=deepseek

LLM_DEEPSEEK_KEY=sk-xxx
LLM_DEEPSEEK_URL=https://api.deepseek.com/v1
LLM_DEEPSEEK_MODEL=deepseek-chat
```

启动后端：

```bash
uvicorn main:app --reload --host 0.0.0.0 --port 10003
```

保持这个终端不要关闭。看到类似下面的日志说明服务已启动：

```text
Uvicorn running on http://0.0.0.0:10003
```

### 6. 测试后端 API

另开一个终端测试：

```bash
curl http://localhost:10003/health
```

应返回类似：

```json
{"status":"ok","llms":["deepseek"],"buttons":{"1":{"name":"今日咒语"}}}
```

再测试按键接口。Windows CMD：

```bat
curl -X POST http://localhost:10003/api/trigger ^
  -H "Content-Type: application/json" ^
  -d "{\"button_id\": 1}"
```

Windows PowerShell 可以写成一行：

```powershell
curl.exe -X POST http://localhost:10003/api/trigger -H "Content-Type: application/json" -d "{\"button_id\":1}"
```

macOS / Linux：

```bash
curl -X POST http://localhost:10003/api/trigger \
  -H "Content-Type: application/json" \
  -d '{"button_id":1}'
```

如果这里失败，先不要上传 ESP32，先把后端修通。

### 7. 找到电脑局域网 IP

ESP32 不能访问电脑里的 `localhost`，必须访问电脑在同一 WiFi/局域网下的 IP。

Windows：

```bat
ipconfig
```

找到当前 WiFi 或以太网网卡的 `IPv4 Address`，例如：

```text
192.168.66.124
```

macOS：

```bash
ipconfig getifaddr en0
```

Linux：

```bash
hostname -I
```

确认手机或 ESP32 连接的是同一个网络段。比如电脑是 `192.168.66.124`，ESP32 也应连到同一个路由器。

Windows 防火墙可能拦截 `10003` 端口。第一次启动 Python/uvicorn 时，如果系统弹出防火墙提示，允许专用网络访问。也可以临时用浏览器从另一台设备访问：

```text
http://电脑IP:10003/health
```

能打开才说明 ESP32 也有机会访问到后端。

### 8. 修改 ESP32 本地私有配置

WiFi 名称、密码和后端地址不要写进 `src/main.cpp`，否则容易把隐私提交到 GitHub。项目使用本地私有配置文件：

```text
include/secrets.h
```

这个文件已被 `.gitignore` 忽略。第一次使用时，先复制模板。

Windows PowerShell：

```powershell
copy include\secrets.example.h include\secrets.h
```

Windows CMD：

```bat
copy include\secrets.example.h include\secrets.h
```

macOS / Linux：

```bash
cp include/secrets.example.h include/secrets.h
```

然后打开 `include/secrets.h`，填写自己的配置：

```cpp
#define WIFI_SSID     "你的WiFi名"
#define WIFI_PASSWORD "你的WiFi密码"
#define SERVER_URL    "http://电脑IP:10003/api/trigger"
```

示例：

```cpp
#define WIFI_SSID     "Home-WiFi"
#define WIFI_PASSWORD "your-real-password"
#define SERVER_URL    "http://192.168.66.124:10003/api/trigger"
```

注意：

- `SERVER_URL` 不能写 `localhost`。
- 电脑 IP 变化后要重新改这里并上传固件。
- 如果后端部署到云服务器，就写云服务器公网地址。
- 不要把 `include/secrets.h` 提交到 GitHub；仓库里只提交 `include/secrets.example.h`。

### 9. 上传固件

用 USB 数据线连接 ESP32-S3 开发板。建议接开发板的 USB-UART/串口下载口。

VS Code 操作：

1. 左侧点 PlatformIO 图标。
2. 展开 `esp32-s3-devkitc-1`。
3. 点击 `Build`，先确认能编译。
4. 点击 `Upload` 上传。

如果上传失败：

- 确认 USB 线支持数据传输，不是纯充电线。
- 确认串口驱动已安装。
- 关闭占用串口的 Serial Monitor。
- 必要时按住 `BOOT`，点击 Upload，看到开始连接后松开 `BOOT`。
- 上传后按一下 `RST/EN` 复位。

命令行方式：

```bash
pio run --target upload
```

### 10. 打开串口监视器

VS Code 操作：

1. 左侧点 PlatformIO 图标。
2. 展开 `esp32-s3-devkitc-1`。
3. 点击 `Monitor`。

也可以按 `Ctrl + Shift + P`，输入并执行：

```text
PlatformIO: Monitor
```

波特率已在 `platformio.ini` 配置为：

```ini
monitor_speed = 115200
```

打开 Monitor 后，按一下开发板 `RST/EN`。正常会看到：

```text
ESP32-S3 Button OLED demo booting...
I2C device found at 0x3C
K1 GPIO4 = 1
...
u8g2.begin done
Connecting WiFi...
Connected: 192.168.x.x
```

如果看到 `WiFi failed`，先检查 WiFi 名称和密码。如果能连 WiFi，但按键后出现 `HTTP error`，检查 `SERVER_URL`、后端是否运行、防火墙是否放行。

### 11. 验证真实按键

按下 K1，应在 Monitor 看到：

```text
K1 GPIO4 raw changed: LOW/PRESSED
Button K1 pressed
Response: ...
K1 GPIO4 raw changed: HIGH/RELEASED
```

OLED 第一行黄色区域显示功能标题，例如：

```text
K1 今日咒语
```

第二行开始蓝色区域先显示对应等待文案，例如 `正在给今天施法`，服务端返回后再显示生成内容。

如果按键板灯亮但没有 `raw changed`，说明只接通了 VCC/GND，K1-K8 信号线没有正确接到 ESP32。

### 12. 打开前端演示页

前端是单文件，不需要 npm。

方式一：直接用浏览器打开：

```text
frontend/index.html
```

方式二：启动静态服务器：

```bash
cd frontend
python -m http.server 3000
```

浏览器访问：

```text
http://localhost:3000
```

页面顶部 Server 默认是：

```text
http://localhost:10003
```

如果前端页面在另一台设备上打开，也要改成：

```text
http://电脑IP:10003
```

点击页面里的 K1-K8，会调用同一个后端接口，并把返回显示在页面中的 OLED 模拟屏上。

### 13. 修改按钮玩法

按钮名称和 Prompt 在：

```text
server/config.yaml
```

修改后需要重启后端服务，ESP32 不一定需要重新上传，除非你也改了固件里的 `BUTTON_TITLES`。

如果要同步 OLED 第一行标题，修改：

```cpp
static const char* BUTTON_TITLES[8] = {
    "今日咒语", "赛博签", "反emo", "脑洞任务",
    "冷幽默", "猫语翻译", "随机冒险", "未来预言"
};
```

如果要同步 OLED 等待文案，修改 `src/main.cpp` 里的 `BUTTON_LOADING_TEXTS`。

如果要同步前端按钮文案，修改 `frontend/index.html` 里的 `STYLES` 和 `LOADING_TEXTS` 数组。

---

## 配置和排障速查

| 文件/位置 | 作用 | 修改后要做什么 |
|-----------|------|----------------|
| `include/secrets.h` | WiFi 名、密码、`SERVER_URL`，本地私有不提交 | 重新 Build / Upload 固件 |
| `include/secrets.example.h` | 给别人复制的配置模板，不写真实密码 | 改模板后提交到 GitHub |
| `src/main.cpp` 的 `BUTTON_TITLES` | OLED 第一行黄色标题 | 重新上传固件 |
| `src/main.cpp` 的 `BUTTON_LOADING_TEXTS` | 按下按键后的等待文案 | 重新上传固件 |
| `server/.env` | LLM API Key、URL、模型名 | 重启后端 |
| `server/config.yaml` | K1-K8 名称和 Prompt | 重启后端 |
| `frontend/index.html` 的 `STYLES` / `LOADING_TEXTS` | 演示页按钮和模拟 OLED 文案 | 刷新浏览器 |

| 现象 | 优先检查 |
|------|----------|
| 按键后短时间不能再按 | 正常。固件和演示页在 HTTP 请求期间会锁定按键，避免重复并发请求；等返回、失败或超时后恢复。想缩短等待，优先优化后端响应或调小 `HTTP_TIMEOUT_MS`。 |
| `I2C scan found no devices` | OLED VCC/GND/SCL/SDA 是否按丝印接，SCL 是否到 GPIO14，SDA 是否到 GPIO13。 |
| 能扫到 `0x3C`，但屏幕不亮 | 当前使用 U8g2 软件 I2C；若仍不亮，确认 OLED 控制芯片是否真是 SSD1306，必要时换 SH1106/SSD1315 构造器测试。 |
| K1-K8 初始不是全 `1` | 对应按键线被拉低或接错，检查 K 线、VCC、GND 和模块排针顺序。 |
| 按下没有 `raw changed` | 按键信号没有进 ESP32 GPIO，只是模块供电灯亮；重新核对 K1-K8 到 GPIO4/5/6/7/15/16/17/18。 |
| 有 `Button Kx pressed`，但 OLED 没有返回文案 | 按键正常，问题通常在 WiFi、`SERVER_URL`、后端服务或防火墙；看串口里的 `Connected:`、`HTTP error:`、`Response:`。 |
| 前端演示页断开 | 先启动后端，再确认页面顶部 Server 是 `http://localhost:10003` 或 `http://电脑IP:10003`。 |

---

## 云服务器部署（Docker）

```bash
# 上传 server/ 目录
scp -r server/ user@<SERVER_IP>:/opt/esp32-server/

# 在服务器上
cd /opt/esp32-server
cp .env.example .env
docker-compose up -d

# 验证
curl http://localhost:10003/health
```

云服务器安全组需放行 TCP `10003` 端口入站。

---

## 项目结构

```text
├── src/main.cpp           ESP32 固件
├── include/
│   └── secrets.example.h  ESP32 本地私有配置模板
├── platformio.ini         PlatformIO 配置
├── frontend/
│   └── index.html         接线图 + 演示面板
├── server/
│   ├── main.py            FastAPI 应用
│   ├── config.yaml        K1-K8 趣味输出配置
│   ├── requirements.txt
│   ├── Dockerfile
│   └── docker-compose.yml
├── README.md
└── HARDWARE.md            硬件规格参考
```

---

## 未来升级：语音播报

1. 按预留引脚接 MAX98357A + 扬声器。
2. 固件增加双击检测。
3. 服务端新增 TTS 接口。
4. ESP32 收到双击事件后显示文案并播放语音。
