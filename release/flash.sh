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

esptool \
-cp $PORT \
-cd $BOARD \
-cb $BAUD \
-bz $FLASH_SIZE \
-bf $FLASH_SPEED \
-bm $FLASH_INTERFACE \
-ca 0x00000 -cf boot_v1.5.bin \
-ca 0x01000 -cf httpd.user1.bin
