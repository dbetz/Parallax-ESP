PORT=/dev/ttyUSB0
if [ ! -z $1 ]
then
  PORT=$1
fi
echo Using port $PORT

BAUD=115200
BOARD=none # none, ck, nodemcu, wifio
FLASH_SIZE=4M
FLASH_BLOCK_SIZE=1024
FLASH_SPEED=80
FLASH_INTERFACE=qio

#use for 1MB flash
#WIFI_SETTINGS=0xFE000

#use for 4MB flash
WIFI_SETTINGS=0x3FE000

#flash filesystem base
FFS_BASE=0x100000

esptool \
-cp $PORT \
-cd $BOARD \
-cb $BAUD \
-bz $FLASH_SIZE \
-bf $FLASH_SPEED \
-bm $FLASH_INTERFACE \
-ca $FFS_BASE -cf blank.bin
