#!/bin/sh
echo "$@"
DOMID = ${"$@": `expr "$@" : 'DOMID'` + 6 : 1}
echo $DOMID
./p9/p9front/p9xsupdate.sh $DOMID
sudo xenstore-ls -f > log
sudo exec /usr/local/lib/xen/bin/qemutmp "$@"
sudo xl unpause testg
sudo xl console testg

