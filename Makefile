#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#

obj-m	:= main.o

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
