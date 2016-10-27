# flash the firmware

PORT=/dev/ttyUSB0
if [ ! -z $1 ]
then
  PORT=$1
fi
echo Using port $PORT

BAUD=115200
BOARD=none # none, ck, nodemcu, wifio
FLASH_SIZE=2M
FLASH_BLOCK_SIZE=1024
FLASH_SPEED=80
FLASH_INTERFACE=qio

BOOT_LOADER=0x000000
USER1_IMAGE=0x001000

esptool \
-cp $PORT \
-cd $BOARD \
-cb $BAUD \
-bz $FLASH_SIZE \
-bf $FLASH_SPEED \
-bm $FLASH_INTERFACE \
-ca $BOOT_LOADER -cf boot_v1.6.bin \
-ca $USER1_IMAGE -cf httpd.user1.bin
