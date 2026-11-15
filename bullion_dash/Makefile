NAME     := bullion_dash
LYNX_SDK ?= ../../sdk
# size over speed (cc65 -Or: ~2 KB less code than -Oirs); the hot loops are
# asm or table-driven
OPT      ?= -Or -g
# a 1 KB C stack (locals are static; lynx.cfg's default is 2 KB): MAIN ends
# at $BA37 instead of $B837
EXTRA_LDFLAGS += -Wl -D,__STACKSIZE__=0x0400
include $(LYNX_SDK)/project.mk
