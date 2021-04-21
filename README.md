# ext-filesystem
1 Mb large filesystem, able to be mounted in whichever folder the makefile is run.
To use, you must install the fuse library or mounting will not work.
Mount to mnt using make mount, which will run the filesystem in that terminal. Operations can be performed on that filesystem by cding into mount in a different terminal window. 
In order to unmount the filesystem, use make unmount. Anything created within that filesystem will only be accessible while it is mounted, so after unmount the mnt folder will no longer be accessible. However, data will be saved between mounts unless make clean is run.
