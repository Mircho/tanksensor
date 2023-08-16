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

### Reporting 
### Reporting channels

- WebSocket
- MQTT topic device-id/status (configurable)
- Webhook - http clinet on device will hit a webserver url (configurable)

Polling data can be done using:

- HTTP endpoint http://device-ip/status (configurable)
- Calling `Device.Status` RPC method
### Payload

JSON containing tank status data plus raw readings from pressure and flow sensors

### Reporting strategy

Any significant change in water volume or overflow frequency will cause a notification to be published on MQTT topic, over WebSocket and POST data to be sent to the HTTP WebHook URL.

## UI

Current status and configuration can be done by accessing the HTTP server via a web browser

## Some links

- [Mongoose OS Docs](https://mongoose-os.com/docs/)
- [RMT & PCNT sharing GPIO](https://www.esp32.com/viewtopic.php?f=13&t=4953)
- [PicoCSS](https://picocss.com)