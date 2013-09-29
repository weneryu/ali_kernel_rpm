obj-m := jbd2-tid-hotfix.o
jbd2-tid-hotfix-objs := jbd2-tid.o

KERNEL_DIR = /lib/modules/`uname -r`/build

all:
	make -C ${KERNEL_DIR} M=`pwd` modules

.PHONY: clean

clean:
	make -C ${KERNEL_DIR} M=`pwd` clean
