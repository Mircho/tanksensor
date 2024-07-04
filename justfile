set dotenv-load
@default: build

DEPS_DIR := "deps"
BUILD_DIR := "build"
UDP_PORT := "9966"
ADDR := `hostname -I | awk '{print $1}'`
UDP_DEBUG_ADDR := ADDR + ":" + UDP_PORT

build: secrets
  mos build --local

flash:
  mos flash

ota:
  curl -v -F file=@build/fw.zip http://$DEVICE_ID/update

@secrets:
  #!/usr/bin/env bash
  @echo "mos.yml add names and passwords" 
  export $(xargs < .env) && yq 'with (.config_schema.[] | .[1] | select(tag == "!secret"); .|parent line_comment = . | . tag = "" | . = "${"+ . + "}") | (.. | select(tag == "!!str")) |= envsubst | (...) tag=""' -i mos.yml

@clean-secrets:
  @echo "Clean mos.yml from sensitive data"
  yq 'with(.config_schema.[] | select(line_comment != ""); .[1] = line_comment | .[1] type = "!secret")' -i mos.yml

clean:
  rm -rf {{DEPS_DIR}}
  rm -rf {{BUILD_DIR}}

asmgen:
  xtensa-esp32-elf-objdump -S --disassemble build/objs/${DEVICE_ID}.elf > ${DEVICE_ID}.dump

set-webhook webhook:
  @echo "Setting webhook to {{webhook}}"
  mos call config.set '{"config":{"webhook":{"url":"{{webhook}}"}}}' --port http://$DEVICE_ID/rpc
  mos call config.save --port http://$DEVICE_ID/rpc

setup-debug-udp:
  mos --port http://$DEVICE_ID/rpc config-set debug.udp_log_addr={{UDP_DEBUG_ADDR}}

udp-debug:
  nc -ul {{UDP_PORT}}

debug-info:
  mos --port=http://$DEVICE_ID/rpc call config.set '{"config":{"debug":{"level":2}}}'

debug-debug:
  mos --port=http://$DEVICE_ID/rpc call config.set '{"config":{"debug":{"level":3}}}'

reboot:         
  mos --port=http://$DEVICE_ID/rpc call Sys.Reboot

