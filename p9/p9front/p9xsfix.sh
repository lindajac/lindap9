#!/bin/sh
sudo  xenstore-rm /local/domain/0/backend/p9/2
sudo  xenstore-write /local/domain/0/backend/p9/3 ""
sudo xenstore-chmod /local/domain/0/backend/p9/3 n0
sudo xenstore-rm /local/domain/0/backend/p9/2/0 
sudo xenstore-write /local/domain/0/backend/p9/3/0 ""
sudo xenstore-chmod /local/domain/0/backend/p9/3/0 n0 r3
sudo xenstore-rm /local/domain/0/backend/p9/2/0/frontend 
sudo xenstore-write /local/domain/0/backend/p9/3/0/frontend "/local/domain/3/device/p9/0"
sudo xenstore-chmod /local/domain/0/backend/p9/3/0/frontend n0 r3

sudo xenstore-rm /local/domain/0/backend/p9/2/0/frontend-id
sudo xenstore-write /local/domain/0/backend/p9/3/0/frontend-id "2"
sudo xenstore-chmod /local/domain/0/backend/p9/3/0/frontend-id n0 r3
sudo xenstore-rm /local/domain/0/backend/p9/2/0/online 
sudo xenstore-write /local/domain/0/backend/p9/3/0/online "1"
sudo xenstore-chmod /local/domain/0/backend/p9/3/0/online n0 r3
sudo xenstore-rm /local/domain/0/backend/p9/2/0/state 
sudo xenstore-write /local/domain/0/backend/p9/3/0/state "1"
sudo xenstore-chmod /local/domain/0/backend/p9/3/0/state n0 r3
sudo xenstore-rm /local/domain/2/device/p9 
sudo xenstore-write /local/domain/3/device/p9  ""
sudo xenstore-chmod /local/domain/3/device/p9 n0 r3
sudo xenstore-rm /local/domain/2/device/p9/0  
sudo xenstore-write /local/domain/3/device/p9/0  ""
sudo xenstore-chmod /local/domain/3/device/p9/0 n3 r0
sudo  xenstore-rm /local/domain/2/device/p9/0/backend
sudo  xenstore-write /local/domain/3/device/p9/0/backend  "/local/domain/0/backend/p9/3/0"
sudo xenstore-chmod /local/domain/3/device/p9/0/backend n3 r0

sudo  xenstore-rm /local/domain/2/device/p9/0/backend-id
sudo  xenstore-write /local/domain/3/device/p9/0/backend-id  "0"
sudo xenstore-chmod /local/domain/3/device/p9/0/backend-id n3 r0

sudo  xenstore-rm /local/domain/2/device/p9/0/state  
sudo  xenstore-write /local/domain/3/device/p9/0/state  "1"
sudo xenstore-chmod /local/domain/3/device/p9/0/state n3 r0
