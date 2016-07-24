ZIP=parallax-esp-$(shell date "+%Y-%m-%d-%H%M").zip
$(info ZIP $(ZIP))

IMAGES=\
release/boot_v1.5.bin \
release/httpd.user1.bin \
release/httpd.user2.bin \
release/Parallax-ESP-v010.ota \
release/blank.bin

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

staged-files:	release release/tests release/samples $(IMAGES)
	$(CP) flash.sh clear.sh clear-ffs.sh release

release/boot_v1.5.bin:	../../esp_iot_sdk_v1.5.2/bin/boot_v1.5.bin patch
	./patch $< $@

release/%:	../build/%
	$(CP) $< $@

release/Parallax-ESP-v010.ota:	../build/httpd.ota
	$(CP) $< $@

release/blank.bin:	../../esp_iot_sdk_v1.5.2/bin/blank.bin
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
