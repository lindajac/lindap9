#!/bin/sh
sudo xl shutdown testnok
sudo xl destroy testnok
sudo xenstore-rm /local/domain/0/backend/p9
sudo xl create -p /etc/xen/testnok.cfg
sudo xenstore-ls -f > log
emacs log
