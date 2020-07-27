PKGDIR ?= ../..
L4DIR  ?= $(PKGDIR)/../..

# Big endian currently not implemented.
# Missing parts marked with preprocessor errors in code.
SYSTEMS    := x86-l4f amd64-l4f arm-l4f arm64-l4f

TARGET = nvme-drv
SRC_CC = main.cc nvme_device.cc ns.cc ctl.cc

REQUIRES_LIBS  := libio-vbus libblock-device

include $(L4DIR)/mk/prog.mk
