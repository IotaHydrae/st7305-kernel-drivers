
# local kernel build dir
KERN_DIR:=/lib/modules/$(shell uname -r)/build

# users kernel dir
# KERN_DIR:=/home/user/linux

MODULE_NAME:=st7305-drmfb

all:
	make -C $(KERN_DIR) M=`pwd` modules

clean:
	make -C $(KERN_DIR) M=`pwd` clean

dtb:
	dtc -@ -I dts -O dtb -o sun8i-h3-spi-st7305-drm.dtbo sun8i-h3-spi-st7305-drm.dts
	make -C overlays

dtb_store: dtb
	sudo cp sun8i-h3-spi-st7305-drm.dtbo /boot/dtb/overlay/

test: all
	sudo rmmod $(MODULE_NAME).ko || true
	sudo insmod $(MODULE_NAME).ko || true

obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-y += st7305.o dither.o drm_mipi_dbi.o
