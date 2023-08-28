PKGDIR ?= ../..
L4DIR  ?= $(PKGDIR)/../..

MODE := l4linux
TEST_MODE := l4linux
SYSTEMS := x86-l4f amd64-l4f

TEST_GROUP    := nvme-driver/linux-virtio

EXTRA_TEST    := virtio_read

TEST_TARGET_virtio_read  := virtio_read.sh

# The test image with GPT, will be created in the test setup script
IMAGE_FILE     = $(OBJ_DIR)/test_nvme.img
TEST_SETUP     = $(SRC_DIR)/create-test-disk.sh $(IMAGE_FILE)

REQUIRED_MODULES := l4linux-nvme.io io nvme-drv
NED_CFG       := l4linux-nvme.cfg

QEMU_ARGS     = -drive if=none,file=$(IMAGE_FILE),format=raw,id=D1 \
                -device nvme,drive=D1,serial=1234
QEMU_ARGS     += -d trace:*nvme*
L4LINUX_CONF  := CONFIG_L4_VIRTIO CONFIG_VIRTIO_BLK

# Change these and fix pkg/mk to pass these to the run script
#L4LX_KERNEL   := /path/to/vmlinuz
#LINUX_RAMDISK  := /path/to/ramdisk

include $(L4DIR)/mk/test.mk
