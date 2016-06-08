ZIP=parallax-esp-$(shell date "+%Y-%m-%d-%H%M").zip
$(info ZIP $(ZIP))

IMAGES=\
release/boot_v1.5.bin \
release/httpd.user1.bin \
release/httpd.user2.bin \
release/httpd.ota \
release/blank.bin

SAMPLES=\
release/robot.c \
release/robot.side \
release/robot.html \
release/robot.pbas \
release/robot-ws.c \
release/robot-ws.side \
release/robot-ws.html \
release/tcp-test.c \
release/tcp-test.side \
release/ifttt.c \
release/ifttt.side \
release/sscp-client.c \
release/sscp-client.h

CP=cp

# this rule doesn't seem to work but running build followed by zip does
all:
	$(MAKE) build
	$(MAKE) zip
	cp esp-httpd.zip ~/$(ZIP)

zip:	staged-files
	cd release; zip ../esp-httpd *

build:
	$(MAKE) -C .. STA_SSID= STA_PASS= clean
	$(MAKE) -C .. STA_SSID= STA_PASS=

staged-files:	release $(IMAGES) $(SAMPLES)
	$(CP) flash.sh clear.sh clear-ffs.sh release

release/boot_v1.5.bin:	../../esp_iot_sdk_v1.5.2/bin/boot_v1.5.bin patch
	./patch $< $@

release/%:	../build/%
	$(CP) $< $@

release/%:	../samples/%
	$(CP) $< $@

release/blank.bin:	../../esp_iot_sdk_v1.5.2/bin/blank.bin
	$(CP) $< $@

release:
	mkdir -p release

patch:	patch.c
	cc -o $@ $<

clean:
	rm -rf release patch esp-httpd.zip
