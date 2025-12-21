
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
	dtc -@ -Hepapr -I dts -O dtb -o st7305-drmfb.dtbo st7305-drmfb.dts
	make -C overlays

dtb_load: dtb
	sudo dtoverlay ./st7305-drmfb.dtbo

test: all
	sudo rmmod $(MODULE_NAME).ko || true
	sudo modprobe -r drm_mipi_dbi || true
	sudo modprobe drm_mipi_dbi || true
	sudo insmod $(MODULE_NAME).ko || true

obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-y += st7305.o dither.o
