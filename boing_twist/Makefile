NAME     := boing_twist
LYNX_SDK ?= ../../sdk
# boing_twist.cfg: the SDK's lynx.cfg plus the cartridge's data files (tools/gen_data.py)
CFG      := boing_twist.cfg
EXTRA_ASFLAGS := -I.
# -Ors: smaller than the SDK's -Oirs (the hot loops are asm); memory is tight
OPT      ?= -Ors -g
include $(LYNX_SDK)/project.mk
