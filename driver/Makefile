obj-m = svm_demo.o
KVERSION = $(shell uname -r)
all:
	make -C /lib/modules/$(KVERSION)/build M=$(CURDIR) modules
clean:
	make -C /lib/modules/$(KVERSION)/build M=$(CURDIR) clean
