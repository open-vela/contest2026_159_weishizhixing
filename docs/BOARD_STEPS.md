# 有板子：从头做到能演示（按顺序点，不要跳）

电脑和板子必须连 **同一个 WiFi**（2.4GHz；很多家庭 5G 板子连不上）。  
用的窗口只有三个：**Arduino IDE**、**PowerShell**、**浏览器**。不要用 Cursor 烧录。

---

## 第 1 步：插上板子

1. USB 线接 ESP32-S3-EYE 和电脑。
2. 板上电源灯应亮。

---

## 第 2 步：开 PowerShell，查电脑 IP

1. 按键盘 **Win**，输入 `powershell`，回车。
2. 输入下面这一行，回车：

```powershell
ipconfig
```

3. 找到「无线局域网适配器 WLAN」（或「WLAN」），记下 **IPv4 地址**，例如 `192.168.43.15`。  
   不要用 `127.0.0.1`。

---

## 第 3 步：启动云端（板子要连的就是它）

1. 资源管理器打开：

`C:\Users\jwr66\Documents\jianwei-id\scripts`

2. 双击 **`start-cloud.bat`**。
3. 出现黑色窗口，看到类似：

- `http://127.0.0.1:8787`
- `CLOUD_HOST=…… PORT=8787`

4. **这个窗口不要关。**
5. 浏览器（Chrome/Edge）地址栏输入：`http://127.0.0.1:8787` 回车。  
   应出现「见微·随身证」页面。

若黑窗口一闪就没了：把窗口里的红字拍照发我。

---

## 第 4 步：改板子里的 WiFi 和电脑 IP

1. 打开 Arduino IDE。
2. **文件 → 打开**，地址栏粘贴后回车：

`C:\Users\jwr66\Documents\jianwei-id\firmware\jianwei_eye`

3. 先打开 **`jianwei_eye.ino`**。同一文件夹必须还有 **`secrets.h`**。  
   没有的话：把 `secrets.h.example` 复制一份，改名为 `secrets.h`。
4. 在 Arduino 左侧文件列表点 **`secrets.h`**，改这四行（引号留着）：

```cpp
static const char *WIFI_SSID = "你的WiFi名字";
static const char *WIFI_PASS = "你的WiFi密码";
static const char *CLOUD_HOST = "第2步记下的IPv4";
static const uint16_t CLOUD_PORT = 8787;
```

`CLOUD_HOST` 必须和第 3 步黑窗口打印的 IP **完全一样**。  
`Ctrl+S` 保存。

---

## 第 5 步：Arduino 第一次才做——装板卡

已经装过 **esp32 by Espressif** 就跳到第 6 步。

1. 还是 Arduino IDE 这个窗口（不是 PowerShell）。
2. **文件 → 首选项**。
3. 找到 **附加开发板管理器网址**，把下面整行贴进去，确定：

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

4. 窗口 **最左边** 点芯片图标（开发板管理器），搜索 `esp32`。
5. 安装 **esp32 by Espressif Systems**。

---

## 第 6 步：装库 ArduinoJson（第一次才做）

1. 最左边点书本图标（库管理器）。
2. 搜索 `ArduinoJson`。
3. 安装作者 **Benoit Blanchon** 的那个。

---

## 第 7 步：选开发板和 COM 口

还是 Arduino IDE **最顶上一杠**（对勾、箭头旁边）：

1. 点中间那块「选择开发板 / Select Board」。
2. 搜索并选择 **ESP32S3 Dev Module**。
3. **端口**选一个 `COMx`（板子插着才会出现）。

没有 COM：换线/换口，按一下板上 **RST**，再点开端口列表。

菜单 **工具** 里再确认：

- USB CDC On Boot：**Enabled**
- PSRAM：**OPI PSRAM**

---

## 第 8 步：上传到板子

1. 点顶上的 **→ 上传**（对勾右边那个箭头）。
2. 底下出现编译、再写 Flash。
3. 停在 `Connecting...`：按住板上 **BOOT**，点一下 **RST**，松开 BOOT。
4. 看到 **Done uploading** / 上传成功。

---

## 第 9 步：看串口（确认上网）

1. **工具 → 串口监视器**（或右上角放大镜）。
2. 波特率选 **115200**。
3. 按一下板上 **RST**。

成功：先有一串 `WiFi....`，然后打出板子自己的 IP，隔几秒有 `beat 200`。  
失败：

- 一直 `offline` → WiFi 名/密码错，或热点是 5G。
- `beat fail` → `CLOUD_HOST` 不是电脑 IP，或第 3 步云端关了，或防火墙拦了 8787。

---

## 第 10 步：网页 + 板子对上

1. 刷新 http://127.0.0.1:8787
2. 「设备」里 **在线** 变成「是」（约 4 秒一次心跳）。
3. **短按** BOOT 一次松开，串口立即出现 `mode -> verify` 或 `guard`。
4. 网页点「开始验真」，串口下一次心跳应打出风险等级。
5. **长按** BOOT 约 1 秒（屏显 `HELP/HOLD..`）再松开，串口 `HELP`，网页子女摘要出现「求助」。

---

## 卡住就停

不要在展示前再装 openvela。网页四步（验真、守护、模拟求助）可以单独演示。
