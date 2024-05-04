CONFIG_MODULE_SIG=y

module := hybrid-stl
obj-m := $(module).o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
MY_CFLAGS += -g -DDEBUG
ccflags-y += ${MY_CFLAGS}
CC += ${MY_CFLAGS}

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules EXTRA_CFLAGS="$(MY_CFLAGS)"
	gcc format.c -o format
	gcc populate_disk.c -o populate

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm format
	rm populate
