PKGDIR ?= ../..
L4DIR  ?= $(PKGDIR)/../..

# force static to avoid dependencies to stdlib-sh
# as we don't build anything in here at all
MODE = static

SYSTEMS    := x86-l4f amd64-l4f arm-l4f arm64-l4f

TEST_GROUP    := nvme-driver/examples

EXTRA_TEST    := virtio-block-chksum-sync virtio-block-chksum-async

# The test image with GPT, will be created in the test setup script
IMAGE_FILE    = $(OBJ_DIR)/test_nvme_$${TEST_TARGET}.img
TEST_SETUP    = $(SRC_DIR)/create-test-disk.sh $(IMAGE_FILE)

TEST_TARGET_nvme_md5_sync  := nvme-md5-sync
TEST_TARGET_nvme_md5_async := nvme-md5-async

TEST_EXPECTED := md5sum.output

TEST_TAGS     = !hardware

REQUIRED_MODULES := nvme.io io nvme-drv
NED_CFG       := nvme.cfg

QEMU_ARGS     = -drive if=none,file=$(IMAGE_FILE),format=raw,id=D1 \
                -device nvme,drive=D1,serial=1234
#QEMU_ARGS    +=-d trace:*nvme*

ifeq ($(ARCH),arm)
  # We are not able to access the 64-bit MMIO region on ARM32
  QEMU_ARGS += -machine virt,highmem=off
endif
ifneq ($(filter arm arm64,$(ARCH)),)
  REQUIRED_MODULES += pcie-ecam.io
endif

include $(L4DIR)/mk/test.mk

