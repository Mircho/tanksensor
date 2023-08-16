DEPS	= deps
BUILD	= build
DEVICE_ID ?= tanksensor2

WIFI_SSID := $(shell yq e '.ssid.["wifi"]' secrets.yml)
WIFI1_SSID := $(shell yq e '.ssid.["wifi1"]' secrets.yml)

AP_PASS := $(shell yq e '.passwords.["ap"]' secrets.yml)
WIFI_PASS := $(shell yq e '.passwords.["wifi"]' secrets.yml)
WIFI1_PASS := $(shell yq e '.passwords.["wifi1"]' secrets.yml)

.SILENT: prepare_yml clean_yml
.PHONY: build

build: prepare_yml mos_build

prepare_yml:
	# replace secrets
	yq e '(.config_schema.[] | select(contains(["wifi.sta.ssid"])) | .[1]) = "'$(WIFI_SSID)'"' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.sta1.ssid"])) | .[1]) = "'$(WIFI1_SSID)'"' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.ap.pass"])) | .[1]) = "'$(AP_PASS)'"' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.sta.pass"])) | .[1]) = "'$(WIFI_PASS)'"' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.sta1.pass"])) | .[1]) = "'$(WIFI1_PASS)'"' -i mos.yml
	# replace device id
	yq e '(.config_schema.[] | select(contains(["dns_sd.host_name"])) | .[1]) = "'$(DEVICE_ID)'"' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.sta.dhcp_hostname"])) | .[1]) = "'$(DEVICE_ID)'"' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.sta1.dhcp_hostname"])) | .[1]) = "'$(DEVICE_ID)'"' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.ap.ssid"])) | .[1]) = "'$(DEVICE_ID)'_??????"' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["mqtt.topic"])) | .[2]) = "'$(DEVICE_ID)'/status"' -i mos.yml

clean_yml:
	yq e '(.config_schema.[] | select(contains(["wifi.sta.ssid"])) | .[1]) = ""' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.sta1.ssid"])) | .[1]) = ""' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.ap.pass"])) | .[1]) = ""' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.sta.pass"])) | .[1]) = ""' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.sta1.pass"])) | .[1]) = ""' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.sta.dhcp_hostname"])) | .[1]) = ""' -i mos.yml
	yq e '(.config_schema.[] | select(contains(["wifi.sta1.dhcp_hostname"])) | .[1]) = ""' -i mos.yml


clean_build:
	rm -rf $(DEPS)
	rm -rf $(BUILD)

mos_build:
	mos build --local

clean: clean_yml clean_build

localflash:
	mos flash

flash: 		
	curl -v -F file=@build/fw.zip http://$(DEVICE_ID)/update

reboot:		
	mos --port=http://$(DEVICE_ID)/rpc call Sys.Reboot
