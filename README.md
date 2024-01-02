# ESP32 headphones

The headphones are able to connect to a WIFI hotspot and transmit/receive audio 
from the specified server via **UDP** socket.

The project uses ESP32 on the client along with an external DAC and ADC with I2S.

## Tech stack
- Server: ASIO, PortAudio, pthread.
- Client: ESP-ADF, ASIO.

## How to build
TBA

## Future extensions:

- Create configuring ability via MQTT or Bluetooth + app.
- Make Bluetooth playback.
  - Add physical switch between WIFI and Bluetooth playback.
- Deal with packet loss
  - Packet ordering with bufferization and resend requests.
  - Disable checksum and apply FEC on metadata block.
  - FLAC can be added.
- Move to a dual-band MCU, i.e. ESP32-C5.
