# 指定目標驅動程式檔案
obj-m += driver.o

ARCH = arm64

# 指定交叉編譯工具鏈的前綴
CROSS_COMPILE = aarch64-linux-gnu-


KDIR = ~/linux

all:
	make -C $(KDIR) M=$(shell pwd) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	make -C $(KDIR) M=$(shell pwd) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
