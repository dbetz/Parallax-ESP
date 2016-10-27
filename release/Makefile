#SDK=../../esp_iot_sdk_v1.5.2
SDK=../../esp_iot_sdk_v2.0.0.p1

ZIP=parallax-esp-$(shell date "+%Y-%m-%d-%H%M").zip
$(info ZIP $(ZIP))

IMAGES=\
release/httpd.user1.bin \
release/httpd.user2.bin \
release/Parallax-ESP-v010.ota \
release/blank.bin \
release/esp_init_data_default.bin

#IMAGES+=release/boot_v1.5.bin
IMAGES+=release/boot_v1.6.bin

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

staged-files:	release release/release-notes.txt release/tests release/samples $(IMAGES)
	$(CP) flash-all.sh update-fw.sh clear.sh clear-ffs.sh release

release/release-notes.txt:	release-notes.txt
	$(CP) release-notes.txt release

release/boot_v1.%.bin:	$(SDK)/bin/boot_v1.%.bin patch
	./patch $< $@ 2M

release/%:	../build/%
	$(CP) $< $@

release/Parallax-ESP-v010.ota:	../build/httpd.ota
	$(CP) $< $@

release/blank.bin:	$(SDK)/bin/blank.bin
	$(CP) $< $@

release/esp_init_data_default.bin:	$(SDK)/bin/esp_init_data_default.bin
	$(CP) $< $@

release:
	mkdir -p release

release/tests:
	mkdir -p release/tests
	cp ../tests/*.[ch] release/tests
	cp ../tests/*.html release/tests
	cp ../tests/*.side release/tests
	cp ../tests/Makefile release/tests

release/samples:
	mkdir -p release/samples
	cp ../samples/*.[ch] release/samples
	cp ../samples/*.html release/samples
	cp ../samples/*.side release/samples
	cp ../samples/Makefile release/samples

patch:	patch.c
	cc -o $@ $<

clean:
	rm -rf release patch esp-httpd.zip
