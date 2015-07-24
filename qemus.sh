#!/bin/sh
./p9/p9front/p9xsupdate.sh $1
sudo /usr/local/lib/xen/bin/qemu-system-i386 -xen-domid $1 -chardev socket,id=libxl-cmd,path=/var/run/xen/qmp-libxl-$1,server,nowait -no-shutdown -mon chardev=libxl-cmd,mode=control -chardev socket,id=libxenstat-cmd,path=/var/run/xen/qmp-libxenstat-$1,server,nowait -mon chardev=libxenstat-cmd,mode=control -nodefaults -xen-attach -name testg -vnc 127.0.0.1:0,to=99 -display none -machine xenpv -m 2048


