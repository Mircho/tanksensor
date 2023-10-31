# Water tank monitoring sensor

## Overview

A legacy water delivery system in the house I live in requires that a tank under the roof is to 
be filled for the house to have enough water pressure. Instead of replacing the pump 
and pressurizing the system I decided for retrofitting it with sensors.

HomeAssistant instance monitors the water level and turns on a pump by issueing a command to a Shelly Pro 1PM device. (btw Shellies have excellent support in HA)

An ESP32 based sesnsor node is used for monitoring tank water levels and overflow.

As at the place of use I have various infrastructure instabilities I implemented an http endpoint on the Shelly for the sensor to publish to. HomeAssistant would still use MQTT for entity data source. But if HA (or rather the RPI Zero2 it runs on) becomes unavailable the Shelly and the sensor would still keep running the water works.

## Sensor node

FireBeetle ESP32 based and has the following sensors:
- Analog pressure sensor [image](docs/analog-pressure-sensor.jpg). 15psi, 0.5-4.5V output for full input range. Usual listing at ebay would be: `1/8NPT Stainless Steel Pressure Transducer Sender 0-4.5V Oil Fuel Air stw B2AE`
- Fluid flow counter sensor [image](docs/flow-counter-sensor.jpg)
- BMP280 for environment information

### Pictures

- [BMP280 sensor node on a laser cut holder](./docs/sensor-box/bmp280-holder.jpg)
- [BMP280 in enclosure](./docs/sensor-box/bmp280-encolsure.jpg)
- [Sensor node open](./docs/sensor-box/node-open.jpg)
- [Sensor node closed](./docs/sensor-box/node-closed.jpg)

## Tank

The water tank is a [cylinder lying horizontally](docs/cylinder-tank.png), diameter - 500mm, length - 1000mm, volume: ~197 liters

Tank physical size is hardcoded in the main.c

Formula for calculating tank volume given depth of water is known:

V = L(R<sup>2</sup>arccos((R-D)/D)-(R-D)sqrt(2RD-D<sup>2</sup>))

Where:
- R - is the radius of the cylinder
- D - is the depth of water in the cylinder (calculated based on water pressure measurements)
- L - length of the cylinder (would be height if it was standing as in math books)

All of the above is explained at https://www.mathopenref.com/cylindervolpartial.html

## Firmware

Firmware is based on [Mongoose OS](https://mongoose-os.com).

### Frequency counter

For reporting frequency from the flow meter ESP32 RMT periphery and a sacrifical pin are used to control the input gate of the GPIO counter. There is no need for external periphery for this to run. Initialization and control happens in a separate FreeRTOS task for tighter timing.

Additionally for testing the logic of the above LEDC periphery is used by redirecting it's output via the device multiplexing control to the same pin on which finally the external sensor will be attached. This functionality is optional and can be turned off by changing this line in `mos.yml`

```
cflags:
  - "-DFREQUENCY_TEST_MODE=1"
```

### Pressure measurement

Provided that the pressure sensor can output from 0.5V to 4.5V for 0 to 5 psi [0 to 0.34 atm] and the maximum water column can be 0.5m e.g. 0.05atm of static pressure  (1 atm for every 10m of water) output of the sensor can not overshoot the maximum 3.3 V value for the ADC input. . When overflow occurs dynamic pressure will raise the readings but empirically established that will not overshoot.

Not much is provided for hardware filtering thus oversampling and exponential moving averaging is applied to stabilize readings.

## Reporting 
### Reporting channels

- MQTT (configurable) topic device-id/status
- Webhook (configurable) - http clinet on device will hit a webserver url (configurable)
- WebSocket
  - http://device-address/status - status for device
  - http://device-address/raw - raw values from adc pressure readings and counter

Polling data can be done using:

- HTTP endpoint http://device-address/status (configurable)
- Calling `Device.Status` RPC method
### Payload

JSON containing tank status data readings

```
{
  "timestamp": 1698212727,
  "air_temperature": 0.0,
  "air_pressure": 0.0,
  "air_humidity": 0.0,
  "tank_liters": 0.0,
  "tank_percentage": 0.0,
  "tank_status": "low",
  "tank_overflow": false
}
```

JSON containing raw tank status data readings from pressure and flow sensors

```
{
  "timestamp": 1698183816,
  "tank_pressure_adc": 0,
  "tank_overflow_count": 0,
  "tank_overflow_frequency": 0.0
}
```

### Reporting strategy

Any significant change in water volume or overflow status will cause a notification to be published on MQTT topic, over WebSocket and POST data to be sent to the HTTP WebHook URL.

### Configuration

Setting device config can be done over http using the mos tool:

```
mos call config.set '{"config":{"mqtt":{"server":"garagepi4:1883"}}}' --port http://tanksensor2/rpc
mos call config.save --port http://tanksensor2/rpc
```
## UI

Device configuration "app" is available when accessing the built in web server.

Single page app will display current device status as well will allow to configure threshold values. 

## Using make

- make build 
    - for building the project
- make localflash 
    - for flashing over a wire
- make flash
    - for OTA over http

To change device name pass a DEVICE_ID option to make:

DEVICE_ID=my_trinket make build

### Debug

According to the [docs](https://mongoose-os.com/docs/mongoose-os/userguide/debug.md) debug levels are respectively:

- 0 ERROR
- 1 WARNING
- 2 INFO
- 3 DEBUG
- 4 VERBOSE_DEBUG

There are two make targets - `debug_info` and `debug_debug` that will change the output in the logs 

You can use UDP debug option by setting the option `debug.udp_log_addr` to an ip:port of a receiver, where you can capture
the debug log using nc

File debug.json
```
{
  "config" : {
     "debug": {
       "udp_log_addr":"192.168.19.17:9966"
     }
  }
}
```

Set over http
```
curl http://tanksensor2/rpc/config.set -d @debug.json
```

Capture debug output over IP network
```
nc -ul 9966
```

## Some links

- [Mongoose OS Docs](https://mongoose-os.com/docs/)
- [RMT & PCNT sharing GPIO](https://www.esp32.com/viewtopic.php?f=13&t=4953)
- [PicoCSS](https://picocss.com)