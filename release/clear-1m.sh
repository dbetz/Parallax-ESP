# clear out all settings and the user flash filesystem

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

USER_SETTINGS1=0x07E000
USER_SETTINGS2=0x07F000

#use for 1MB flash
WIFI_SETTINGS1=0x0FC000
WIFI_SETTINGS2=0x0FE000
WIFI_SETTINGS3=0x0FA000

#use for 4MB flash
#WIFI_SETTINGS1=0x3FC000
#WIFI_SETTINGS2=0x3FE000
#WIFI_SETTINGS3=0x3FA000

#flash filesystem base
FFS_BASE=0x100000

./esptool \
-cp $PORT \
-cd $BOARD \
-cb $BAUD \
-bz $FLASH_SIZE \
-bf $FLASH_SPEED \
-bm $FLASH_INTERFACE \
-ca $USER_SETTINGS1 -cf blank.bin \
-ca $USER_SETTINGS2 -cf blank.bin \
-ca $WIFI_SETTINGS1 -cf esp_init_data_default_v08.bin \
-ca $WIFI_SETTINGS2 -cf blank.bin \
-ca $WIFI_SETTINGS3 -cf blank.bin \
-ca $FFS_BASE -cf blank.bin
