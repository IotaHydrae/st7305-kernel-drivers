ARCH := arm
CROSS_COMPILE := ${HOME}/luckfox/lyra/prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-
KERN_DIR := ${HOME}/luckfox/lyra/kernel

all:
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERN_DIR) M=`pwd` modules
clean:
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(KERN_DIR) M=`pwd` clean

clena: clean
#CFLAGS_$(MODULE_NAME).o := -DDEBUG

obj-m += st7305_tinydrm.o
st7305_tinydrm-objs := st7305.o dither.o drm_mipi_dbi.o

obj-m += st7735r_tinydrm.o
st7735r_tinydrm-objs := st7735r.o drm_mipi_dbi.o