klz22 & jeh365

Ufsdisk_chk first checks for the magic number, then makes a duplicate freebitmap array
and iterates through all inodes to set the duplicate array. Then it checks that array
against our freebitmapblocks to see if there's a problem.