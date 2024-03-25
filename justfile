@default: build

DEPS_DIR := "deps"
BUILD_DIR := "build"

build: secrets
  mos build --local

flash:
  mos flash

ota:
  curl -v -F file=@build/fw.zip http://$(DEVICE_ID)/update

@secrets:
  #!/usr/bin/env bash
  echo "mos.yml add names and passwords" 
  export $(xargs < .env) && yq 'with (.config_schema.[] | .[1] | select(tag == "!secret"); .|parent line_comment = . | . tag = "" | . = "${"+ . + "}") | (.. | select(tag == "!!str")) |= envsubst | (...) tag=""' -i mos.yml

@clean-secrets:
  echo "Clean mos.yml from sensitive data"
  yq 'with(.config_schema.[] | select(line_comment != ""); .[1] = line_comment | .[1] type = "!secret")' -i mos.yml

clean:
  rm -rf {{DEPS_DIR}}
  rm -rf {{BUILD_DIR}}

asmgen:
  xtensa-esp32-elf-objdump -S --disassemble build/objs/${DEVICE_ID}.elf > ${DEVICE_ID}.dump

debug-info:
  mos --port=http://$(DEVICE_ID)/rpc call config.set '{"config":{"debug":{"level":2}}}'

debug-debug:
  mos --port=http://$(DEVICE_ID)/rpc call config.set '{"config":{"debug":{"level":3}}}'

reboot:         
  mos --port=http://$(DEVICE_ID)/rpc call Sys.Reboot

