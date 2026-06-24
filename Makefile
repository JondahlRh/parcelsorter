obj-m	:= parcelsorter.o

KDIR	:= /lib/modules/$(shell uname -r)/build
PWD	:= $(shell pwd)
EXTRA_CFLAGS := -I/usr/realtime/include -I/usr/src/linux/include

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	rm -r .tmp_versions
	rm  .`basename $(obj-m) .o`.*
	rm `basename $(obj-m) .o`.o
	rm `basename $(obj-m) .o`.ko
	rm `basename $(obj-m) .o`.mod.*
