# 见微随身证 · 板端应用（openvela）

本目录通过专属仓 manifest 映射为：

`packages/demos/contest2026_159_jianwei`

短按 BOOT：验真（`/dev/video0` → BMP → `/api/verify-image`）或守护（`pcm_in0` → WAV → `/api/guard-audio`）。三连按：求助心跳。判断只在云端 `skills/`。

`make menuconfig` 勾选 **见微随身证**，`JIANWEI_CLOUD_HOST` 填电脑局域网 IP。也可把 `defconfig.append` 并进板级 defconfig。

完整编烧见仓库根目录 `README.md` 与 `docs/openvela.md`。
