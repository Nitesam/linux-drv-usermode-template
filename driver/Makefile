obj-m += memrw.o

KDIR       ?= /lib/modules/$(shell uname -r)/build
PWD        := $(shell pwd)
OUTPUT_DIR := /root/NTS_WORK

ccflags-y := -I$(PWD)/../shared

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	@mkdir -p $(OUTPUT_DIR)
	@cp -v $(PWD)/memrw.ko $(OUTPUT_DIR)/
	@echo "  Copied memrw.ko -> $(OUTPUT_DIR)/"

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: all
	insmod $(OUTPUT_DIR)/memrw.ko

uninstall:
	-rmmod memrw

.PHONY: all clean install uninstall
