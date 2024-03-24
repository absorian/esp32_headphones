# ESP32 headphones

The headphones are able to connect to a WIFI hotspot and transmit/receive audio 
from the specified server via **UDP** socket.

The project uses ESP32 on the client along with an external DAC and ADC with I2S.

## Tech stack
- Server: PortAudio, Pthread.
- Client: ESP-IDF, BTStack.

MinGW, Cmake

## How to configure
### Wifi
wifi_util.cpp: set SSID and password of the wifi network.

net_transport.cpp: configure server address.
### Pin config
stream_bridge.cpp: set ADC and DAC pins and their corresponding configs.

ctl_periph.cpp: configure the button pin and thresholds.

## How to build
### Server
Set PortAudio directory in CMakeLists and build
```
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -S . -B ./build
cmake --build ./build --target server
```
### Client
Install esp-idf
```
git clone https://github.com/espressif/esp-idf --recursive
cd esp-idf
install.bat
```

Install port of BTStack (https://github.com/bluekitchen/btstack/blob/master/port/esp32/README.md) 

Run export.bat from idf path, build, and flash
```
/PATH/TO/IDF/export.bat
idf.py all
idf.py flash
```
### Known issues
1. No auth in udp connection, server will not check for ip consistency
2. No security in udp connection, dtls instead of udp could solve the problem

## Future extensions:
- Return volume control capability
- Add encoder knob for multimedia tasks
- Create configuring ability via MQTT or Bluetooth + app.
- Deal with battery drain
  - Battery voltage measurement.
  - FLAC can be added for udp connection.
  - Code optimisation.
