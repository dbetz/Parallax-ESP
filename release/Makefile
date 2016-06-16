ZIP=parallax-esp-$(shell date "+%Y-%m-%d-%H%M").zip
$(info ZIP $(ZIP))

IMAGES=\
release/boot_v1.5.bin \
release/httpd.user1.bin \
release/httpd.user2.bin \
release/Parallax-ESP-v010.ota \
release/blank.bin

SAMPLES=\
release/samples/Makefile \
release/samples/blink.c \
release/samples/blink.side \
release/samples/robot.c \
release/samples/robot.side \
release/samples/robot.html \
release/samples/robot.pbas \
release/samples/robot-ws.c \
release/samples/robot-ws.side \
release/samples/robot-ws.html \
release/samples/tcp-test.c \
release/samples/tcp-test.side \
release/samples/ifttt.c \
release/samples/ifttt.side \
release/samples/test.c \
release/samples/test.side \
release/samples/cmd.c \
release/samples/cmd.h

CP=cp

# this rule doesn't seem to work but running build followed by zip does
all:	clean
	$(MAKE) build
	$(MAKE) zip
	cp esp-httpd.zip ~/$(ZIP)

zip:	staged-files
	cd release; zip -r ../esp-httpd *

build:
	$(MAKE) -C .. STA_SSID= STA_PASS= clean
	$(MAKE) -C .. STA_SSID= STA_PASS=

staged-files:	release release/samples $(IMAGES) $(SAMPLES)
	$(CP) flash.sh clear.sh clear-ffs.sh release

release/boot_v1.5.bin:	../../esp_iot_sdk_v1.5.2/bin/boot_v1.5.bin patch
	./patch $< $@

release/%:	../build/%
	$(CP) $< $@

release/Parallax-ESP-v010.ota:	../build/httpd.ota
	$(CP) $< $@

release/samples/%:	../samples/%
	$(CP) $< $@

release/blank.bin:	../../esp_iot_sdk_v1.5.2/bin/blank.bin
	$(CP) $< $@

release:
	mkdir -p release

release/samples:
	mkdir -p release/samples

patch:	patch.c
	cc -o $@ $<

clean:
	rm -rf release patch esp-httpd.zip
