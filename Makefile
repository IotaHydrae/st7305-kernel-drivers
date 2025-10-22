
# local kernel build dir
KERN_DIR:=/lib/modules/$(shell uname -r)/build

# users kernel dir
# KERN_DIR:=/home/user/linux

MODULE_NAME:=st7305_drmfb

all:
	make -C $(KERN_DIR) M=`pwd` modules
	make -C tests/

clean:
	make -C $(KERN_DIR) M=`pwd` modules clean

test: all
	sudo rmmod $(MODULE_NAME).ko || true
	sudo insmod $(MODULE_NAME).ko || true

obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-y += st7305.o
