PKGDIR	= .
L4DIR	?= $(PKGDIR)/../..

TARGET = server examples
TARGET_test = test

include $(L4DIR)/mk/subdir.mk
