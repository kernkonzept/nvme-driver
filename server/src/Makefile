PKGDIR ?= ../..
L4DIR  ?= $(PKGDIR)/../..

TARGET = nvme-drv
SRC_CC = main.cc nvme_device.cc ns.cc ctl.cc

CXXFLAGS-arm    += -mno-unaligned-access
CXXFLAGS-arm64  += -mstrict-align
CXXFLAGS        += $(CXXFLAGS-$(ARCH))

REQUIRES_LIBS  := libio-vbus libblock-device

include $(L4DIR)/mk/prog.mk
