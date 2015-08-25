obj-m += p9frontall.o
p9frontall-objs := p9_front.o p9_front_driver.o trans_xen9p.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
