# SPDX-License-Identifier: Apache-2.0
# 见微随身证 — NuttX / openvela 板端应用
# 合入专属仓后由 manifest 映射到 packages/demos/<队名>_jianwei

include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_JIANWEI_PROGNAME)
PRIORITY  = $(CONFIG_JIANWEI_PRIORITY)
STACKSIZE = $(CONFIG_JIANWEI_STACKSIZE)
MODULE    = $(CONFIG_JIANWEI)

MAINSRC = jianwei_main.c
CSRCS   = jianwei_net.c jianwei_ui.c jianwei_cam.c jianwei_mic.c

include $(APPDIR)/Application.mk
