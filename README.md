# ESP32 headphones

The headphones are able to connect to a WIFI hotspot and receive audio 
from the specified server via **UDP** socket.

The project uses ESP32 on the client along with an external DAC.

## Tech stack
- Server: ESP-IDF, ESP-ADF, ASIO.
- Client: SFML::Network, SFML::Audio(OpenAL).

## How to build
### Server
A standard cmake project, my version is also building SFML from source files. You can specify either "SFML" 
environment variable or edit the "SFML_SOURCE_DIR" in CMakeLists.txt.

Chosen cmake options:

    -DBUILD_SHARED_LIBS=FALSE -DSFML_BUILD_WINDOW=FALSE -DSFML_BUILD_GRAPHICS=FALSE -DSFML_USE_STATIC_STD_LIBS=TRUE

In the main.cpp top macros allow to edit **port**, **channel count**, and **sample rate**.

To be able to extract sound from OS, Virtual Audio Cable can be used on Windows, 
I believe on Linux there is an equivalent. After all, in device selection menu, find VBA, 
and choose system audio output that corresponds to this VBA. 

### Client

A standard esp-idf cmake based project. WIFI hotspot auth can be configured via example connection component in

    idf.py menuconfig

In the main/client.cpp top macros allow to edit **host address**, 
**port**, **channel count**, and **sample rate**, as well as pinout for DAC (pin_config_spk).

Support for more than 2 channels is not there, and probably won't be in the future. 
Host address can be written once and locked in the router settings. 

## Future extensions:

- Deal with packet loss
    - Packet ordering with bufferization and resend requests.
    - Disable checksum and apply FEC on metadata block.
    - FLAC can be added.
- Microphone
    - Configure in client.
    - Find a way to select playback device with OpenAL dug under SFML.
    - See how it goes.
- Add bluetooth connectivity
    - Use it for initial/configuring via smartphone.
    - Make Bluetooth playback (somewhere ready).
    - Add physical switch between WIFI and Bluetooth playback.
- Move to a dual-band MCU, i.e. ESP32-C5.
