ARCH := arm
CROSS_COMPILE := ${HOME}/luckfox/pico/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-
KERN_DIR := ${HOME}/luckfox/pico/sysdrv/source/objs_kernel

all:
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERN_DIR) M=`pwd` modules
clean:
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERN_DIR) M=`pwd` modules clean

clena: clean
#CFLAGS_$(MODULE_NAME).o := -DDEBUG

obj-m += st7305_tinydrm.o
st7305_tinydrm-objs := st7305.o dither.o drm_mipi_dbi.o drm_fb_cma_helper.o
