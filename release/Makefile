SDK=../ESP8266_NONOS_SDK

ZIP=parallax-esp-$(shell date "+%Y-%m-%d-%H%M").zip
$(info ZIP $(ZIP))

IMAGES=\
release/httpd.user1.bin \
release/httpd.user2.bin \
release/Parallax-ESP.ota \
release/blank.bin \
release/esp_init_data_default_v08.bin

TOOLS=\
release/esptool

BOOTBIN=boot_v1.7.bin
IMAGES+=release/$(BOOTBIN)

CP=cp

# this rule doesn't seem to work but running build followed by zip does
all:	clean
	$(MAKE) build
	$(MAKE) zip

zip:	staged-files
	cd release; zip -r ../$(ZIP) *

build:
	$(MAKE) -C .. STA_SSID= STA_PASS= clean
	$(MAKE) -C .. STA_SSID= STA_PASS=

../esptool-ck/esptool:
	$(MAKE) -C ../esptool-ck
	
staged-files:	release release/release-notes.txt $(IMAGES) $(TOOLS)
	$(CP) flash-all.sh update-fw.sh clear.sh clear-ffs.sh release

release/release-notes.txt:	release-notes.txt
	$(CP) release-notes.txt release

release/$(BOOTBIN):	$(SDK)/bin/$(BOOTBIN) patch
	./patch $< $@ 2M

release/%:	../build/%
	$(CP) $< $@

release/Parallax-ESP.ota:	../build/httpd.ota
	$(CP) $< $@

release/blank.bin:	$(SDK)/bin/blank.bin
	$(CP) $< $@

release/esp_init_data_default_v08.bin:	$(SDK)/bin/esp_init_data_default_v08.bin
	$(CP) $< $@

release/esptool:	../esptool-ck/esptool
	$(CP) $< $@

release:
	mkdir -p release

patch:	patch.c
	cc -o $@ $<

clean:
	rm -rf release patch ../esptool-ck/esptool
