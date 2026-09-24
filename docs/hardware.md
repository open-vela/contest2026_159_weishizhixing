# ESP32-S3-EYE 与本项目

官方 openvela 板级（不要复制进本仓当「自研 BSP」）：

https://github.com/open-vela/vendor_espressif/blob/dev-ai-contest-2026/boards/esp32s3/esp32s3-eye/README_zh-cn.md

## 本应用用到的节点

| 外设 | 节点 | 见微里干什么 |
| --- | --- | --- |
| BOOT GPIO0 | `/dev/buttons` | 短按切验真/守护，长按 ≥0.8s 求助 |
| ST7789 | `/dev/lcd0` | 等级与一句话 |
| OV2640 | `/dev/video0` | 验真拍照（V4L2） |
| PDM 麦 | `/dev/audio/pcm_in0` | 守护拾音 |
| Wi-Fi | `wlan0` | 访问云端 |
| LED GPIO3 | `/dev/userleds` | 验真模式指示 |

## Arduino 回退时的引脚

与原理图一致：BOOT=0，LED=3。Arduino 回退已打开 OV2640 拍照和 ST7789 大字等级（HIGH/CHECK/OK）；完整中文仍在网页看板。openvela 路径用 `/dev/video0`、`/dev/lcd0`。

## 编译烧录（openvela）

必须 `dev-ai-contest-2026` 分支。`trunk` 编不过。

```bash
./build.sh $(pwd)/vendor/espressif/boards/esp32s3/esp32s3-eye/configs/openvela -j$(nproc)
esptool --chip esp32s3 --port <PORT> --baud 460800 \
  --before default-reset --after hard-reset \
  write-flash 0x0 nuttx/nuttx.bin
```

串口 115200。`nsh> jianwei` 启动应用。Wi-Fi 用板级文档中的 `wapi` 示例。
