obj-m += dln2-adc.o gpio-dln2.o dln2.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
install:  
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules_install  
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
