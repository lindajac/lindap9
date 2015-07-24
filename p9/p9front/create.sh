#!/bin/sh
sudo xl shutdown testg
sudo xl destroy testg
sudo xenstore-rm /local/domain/0/backend/p9
sudo xl create -p /etc/xen/testg.cfg
sudo xenstore-ls -f > log
emacs log
