IMAGES=\
release/0x00000.bin \
release/0x40000.bin \
release/blank.bin

CP=cp

# this rule doesn't seem to work but running build followed by zip does
all:
	$(MAKE) build
	$(MAKE) zip

zip:	staged-files
	cd release; zip ../esp-httpd *

build:
	$(MAKE) -C .. STA_SSID= STA_PASS= clean
	$(MAKE) -C .. STA_SSID= STA_PASS=

staged-files:	release $(IMAGES)
	$(CP) flash.sh release

release/0x00000.bin:	../firmware/0x00000.bin patch
	./patch $< $@

release/0x40000.bin:	../firmware/0x40000.bin
	$(CP) $< $@

release/blank.bin:	../../esp_iot_sdk_v1.5.2/bin/blank.bin
	$(CP) $< $@

release:
	mkdir -p release

patch:	patch.c
	cc -o $@ $<

clean:
	rm -rf release patch esp-httpd.zip
