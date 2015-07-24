#!/bin/sh
sudo xenstore-write /local/domain/0/backend/p9 ""
sudo xenstore-chmod /local/domain/0/backend/p9 n0
sudo  xenstore-write /local/domain/0/backend/p9/$1 ""
sudo xenstore-chmod /local/domain/0/backend/p9/$1 n0
sudo xenstore-write /local/domain/0/backend/p9/$1/0 ""
sudo xenstore-chmod /local/domain/0/backend/p9/$1/0 n0 r$1
sudo xenstore-write /local/domain/0/backend/p9/$1/0/frontend "/local/domain/$1/device/p9/0"
sudo xenstore-chmod /local/domain/0/backend/p9/$1/0/frontend n0 r$1
sudo xenstore-write /local/domain/0/backend/p9/$1/0/frontend-id "$1"
sudo xenstore-chmod /local/domain/0/backend/p9/$1/0/frontend-id n0 r$1
sudo xenstore-write /local/domain/0/backend/p9/$1/0/online "$1"
sudo xenstore-chmod /local/domain/0/backend/p9/$1/0/online n0 r$1
sudo xenstore-write /local/domain/0/backend/p9/$1/0/state "1"
sudo xenstore-chmod /local/domain/0/backend/p9/$1/0/state n0 r$1

sudo xenstore-write /local/domain/$1/device/p9  ""
sudo xenstore-chmod /local/domain/$1/device/p9 n0 r$1
sudo xenstore-write /local/domain/$1/device/p9/0  ""
sudo xenstore-chmod /local/domain/$1/device/p9/0 n$1 r0
sudo  xenstore-write /local/domain/$1/device/p9/0/backend  "/local/domain/0/backend/p9/$1/0"

sudo xenstore-chmod /local/domain/$1/device/p9/0/backend n$1 r0
sudo  xenstore-write /local/domain/$1/device/p9/0/backend-id  "0"
sudo xenstore-chmod /local/domain/$1/device/p9/0/backend-id n$1 r0
sudo  xenstore-write /local/domain/$1/device/p9/0/state  "1"
sudo xenstore-chmod /local/domain/$1/device/p9/0/state n$1 r0
