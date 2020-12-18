# Parallax-ESP README #

# Firmware compilation summary 
##### (refer to build environment setup instructions later in this document)

Compile the binaries from the root of the Parallax-ESP folder

    make clean
    make

 or make with optional flags to compile for the Parallax Badge WX, SKU 20300, https://www.parallax.com/product/badge-wx-for-blocklyprop/

    make EXTRA_CFLAGS=-DAUTO_LOAD    


 # Firmware over serial programming details

 Run these commands to program the Parallax WiFi module with either 2MB or 4MB flash.
 - Note that the Makefile is set for 2048 flash size, and 1024 image size (512+512), leaving 1024 bytes free for user files.
 - Note that the following example commands are for linux. Adjust the com port appropriately, and for Windows use "COMx" notation instead of "/dev/ttyUSBx"


1. Clear entire flash <optional>

    ```python
    sudo python -m esptool --baud 921600 --port /dev/ttyUSB0 --before no_reset --after no_reset erase_flash
    ```


2. Program new firmware, bootloader and inital settings. Note: Adjust the com port to your setup

    ```python
    sudo python -m esptool --baud 921600 --port /dev/ttyUSB0 \
    --before no_reset --after no_reset write_flash \
    --flash_size 2MB --flash_freq 80m --flash_mode qio \
    0x000000 resources/boot_v1.7.bin \
    0x001000 build/httpd.user1.bin \
    0x1fc000 resources/esp_init_data_default_v08.bin \
    0x1fe000 resources/blank.bin 0x07e000 resources/blank.bin 
    ```


- Tip: If wanting to use a module with 4MB flash, then either use the above 2MB firmware (in which case only 2MB of flash will be used), 
or re-compile the firmware after adjusting makefile to use the larger flash size. And then change the 2MB to 4MB in the esptool command.


- Tip: Verify flash memory size of the Parallax WiFi module
       (In the reply, "Device: 4015" means 2MB, "Device: 4016" means 4MB).


    ```python
    sudo python -m esptool --baud 921600 --port /dev/ttyUSB0 \
    --before no_reset --after no_reset flash_id
    ```



# Original README.md content continues...

This project contains firmware for the Parallax WX Wi-Fi module. The code is based on
the esp-httpd project by by Jeroen Domburg with some features added from the esp-link
project by Thorsten von Eicken.

It includes the following features:

A simple web server that can serve a fixed set of files from an embedded flash filesystem
as well as files from a user-writable flash filesystem.

A transparent bridge to an attached microcontroller.

A simple serial command protocol that allows an attached microcontroller to respond to
HTTP requests and to initiate TCP and WebSockets requests.

A loader for the Parallax Propeller.

Detailed documentation on these features is available on the Parallax web site.

The main code is based on the esp-httpd demonstration project whose README file is below.

The Parallax additions to esp-httpd and esp-link are released under the MIT license.

# Building

As a first step, clone and follow the build instructions for esp-open-sdk:

    https://github.com/pfalcon/esp-open-sdk repository.

Next, setup the path to the ESP toolchain:

    export XTENSA_TOOLS_ROOT=/path/to/esp-open-sdk/xtensa-lx106-elf/bin/

Check out the submodules of Parallax-ESP:

    cd Parallax-ESP
    git submodule init
    git submodule update

Build the firmware (from the Parallax-ESP directory) for Parallax WX Module:

    make

Build the firmware (from the Parallax-ESP directory) for Parallax BadgeWX Module:

    make EXTRA_CFLAGS=-DAUTO_LOAD

# Discovery Protocol

Here is a description of the discovery protocol used by the WX module:

1) PropLoader broadcasts a UDP packet to port 32420. The packet contains a single 32 bit
zero value. This is a flag indicating that it is a discovery request. This allows PropLoader
to ignore it since it will receive the broadcast packet just like the WX modules.

2) PropLoader waits for responses from each WX module. Each response has the following
JSON code. Note that this will result in the first 32 bit value in the packet being
non-zero.

            {
              "name": <module-name>,
              "description”:<description>,
              "reset pin": <pin-number>,
              "rx pullup": enabled | disabled,
              "mac address": <mac-address>
             }

3) PropLoader collects these responses and then sends more discovery requests, this time
including the IP addresses of all modules from which it has already received responses.
These are 32 bit numbers that follow the initial 32 bit zero value. PropLoader does this
tree times in attempt to find modules whose responses might have collided with other
network traffic.

4) If a WX module receives a discovery request that includes its own IP address in the
payload, it ignores it.

5) Before a WX module sends a discovery response it first waits a random interval between
10 and 60 milliseconds. This is an attempt to prevent discovery responses from colliding.

6) The WX modules send their discovery replies to port 32420.

7) All 32 bit numbers are sent in little-endian byte order.

Below this line is the original ESP-HTTPD README file. This is included for reference purposes.
You should noet try to follow the instructions below.
##########################################################################################

# esp-httpd README #

This is the demonstration project for the small but powerful libesphttpd webserver 
for ESP8266(EX) chips. It is an example of how to make a module that can have 
the AP it connects to configured over a webbrowser. It also illustrates multiple 
flash layouts and some OTA update functionality.

## ABOUT THE WEBSERVER ##

The Good (aka: what's awesome)
 - Supports multiple connections, for eg simultaneous html/css/js/images downloading
 - Static files stored in flash, in an (optionally compressed) RO filesystem
 - Pluggable using external cgi routines
 - Simple template engine for mixed c and html things
 - Usable as an embedded library - should be easy to drop into your existing projects
 - Includes websocket support

The Bad (aka: what can be improved)
 - Not built for speediness, although it's reasonable fast.
 - Built according to what I remember of the HTTP protocol, not according to the
   RFCs. Should work with most modern browsers, though.
 - No support for https.

The Ugly (aka: bugs, misbehaviour)
- Possible buffer overflows (usually not remotely exploitable) due to no os_snprintf
  This can be theoretically remedied by either Espressif including an os_snprintf in 
  their libs or by using some alternate printf lib, like elm-chans xprintf

## SOURCE OF THIS CODE ##
The official esphttpd repo lives at http://git.spritesserver.nl/esphttpd.git/ and
http://git.spritesserver.nl/libesphttpd.git/ . If you're a fan of Github, you can also
peruse the official mirror at https://github.com/Spritetm/esphttpd and https://github.com/Spritetm/libesphttpd . If
you want to discuss this code, there is a subforum at esp8266.com: http://www.esp8266.com/viewforum.php?f=34 .


## ABOUT THE EXAMPLE ##

When you flash the example into an ESP8266(EX) module, you get a small webserver with a few example
pages. If you've already connected your module to your WLAN before, it'll keep those settings. When
you haven't or the settings are wrong, keep GPIO0 for >5 seconds. The module will reboot into
its STA+AP mode. Connect a computer to the newly formed access point and browse to 
http://192.168.4.1/wifi in order to connect the module to your WiFi network. The example also
allows you to control a LED that's connected to GPIO2.

## BUILDING EVERYTHING ##

For this, you need an environment that can compile ESP8266 firmware. Environments for this still
are in flux at the moment, but I'm using esp-open-sdk: https://github.com/pfalcon/esp-open-sdk .
You probably also need an UNIX-like system; I'm working on Debian Linux myself. 

To manage the paths to all this, you can source a small shell fragment into your current session. For
example, I source a file with these contents:

	export PATH=${PWD}/esp-open-sdk/xtensa-lx106-elf/bin:$PATH
	export SDK_BASE=${PWD}/esp-open-sdk/sdk
	export ESPTOOL=${PWD}/esptool/esptool.py
	export ESPPORT=/dev/ttyUSB0
	export ESPBAUD=460800

Actual setup of the SDK and toolchain is out of the scope of this document, so I hope this helps you
enough to set up your own if you haven't already. 

If you have that, you can clone out the source code:
git clone http://git.spritesserver.nl/esphttpd.git/

This project makes use of heatshrink, which is a git submodule. To fetch the code:

	cd esphttpd
	git submodule init
	git submodule update

Now, build the code:

	make

Depending on the way you built it, esp-open-sdk sometimes patches Espressifs SDK, needing a slightly different
compiling process. If this is needed, you will get errors during compiling complaining about uint8_t being
undeclared. If this happens, try building like this:

	make USE_OPENSDK=yes

You can also edit the Makefile to change this more permanently.

After the compile process, flash the code happens in 2 steps. First the code itself gets flashed. Reset the module into bootloader
mode and enter 'make flash'.

The 2nd step is to pack the static files the webserver will serve and flash that. Reset the module into
bootloader mode again and enter `make htmlflash`.

You should have a working webserver now.

## WRITING CODE FOR THE WEBSERVER ##

Please see the README.md of the libesphttpd project for the programming manual.


