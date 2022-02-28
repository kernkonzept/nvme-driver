PKGDIR ?= ../..
L4DIR  ?= $(PKGDIR)/../..

TARGET = nvme-drv
SRC_CC = main.cc nvme_device.cc ns.cc ctl.cc

REQUIRES_LIBS  := libio-vbus libblock-device

include $(L4DIR)/mk/prog.mk
