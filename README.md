<h3 align="center">PSA VAN bus reader for Arduino-ESP8266 / ESP32</h3>

---

<p align="center">Reading packets from PSA vehicles' VAN bus.<br></p>

## 📝 Table of Contents
- [Description](#description)
- [Schematics](#schematics)
- [Usage](#usage)
  - [General](#general)
  - [Functions](#functions)
- [License](#license)

## 🎈 Description <a name = "description"></a>

This module allows you to receive packets on the "VAN" bus of your Peugeot or Citroen vehicle.

VAN bus is pretty similar to CAN bus. It was used in many cars (Peugeot, Citroen) made by PSA.

In the beginning of 2000's the PSA group (Peugeot and Citroen) used VAN bus as a communication protocol
between the various comfort-related equipment. Later, around 2005, they started to replace this protocol
in their newer cars with the CAN bus protocol, however some models had VAN bus inside them until 2009.

## 🔌 Schematics <a name = "schematics"></a>

You can usually find the VAN bus on pins 2 and 3 of ISO block "A" of your head unit (car radio). See 
https://en.wikipedia.org/wiki/Connectors_for_car_audio and https://github.com/morcibacsi/esp32_rmt_van_rx .

There are various possibilities to hook up a ESP8266 based board to your vehicle's VAN bus:

1. Use a [MCP2551] transceiver, connected with its CANH and CANL pins to the vehicle's VAN bus.
   As the MCP2551 has 5V logic, a 5V <-> 3.3V [level converter] is needed to connect the RXD pin of the transceiver to
   a GPIO of your ESP8266 board.

2. Use a [SN65HVD230] transceiver, connected with its CANH and CANL pins to the vehicle's VAN bus.
   The SN65HVD230 transceiver already has 3.3V logic, so it is possible to directly connect the CRX pin of the
   transceiver to a GPIO ping of your ESP8266 board.

3. The simplest schematic is not to use a transceiver at all, but connect the VAN DATA line to GrouND using
   two 4.7 kOhm resistors. Connect the GPIO pin of your ESP8266 board to the 1:2 [voltage divider] that is thus
   formed by the two resistors.
   --> Note: I used this schematic during many long debugging hours, but I cannot guarantee that it won't ultimately
       cause your car to explode! (or anything less catastrofic)

## 🚀 Usage<a name = "usage"></a>

### General <a name = "general"></a>

Add the following line to your ```.ino``` sketch:
```
#include <VanBus.h>
```

Add the following lines to your initialisation block ```void setup()```:
```
int RECV_PIN = 2; // Set to GPIO pin connected to VAN bus transciever output
VanBus.Setup(RECV_PIN);
```

Add the following line at the beginning of your main loop ```void loop()```:
```
TVanPacketRxDesc pkt;
if (VanBus.Receive(pkt)) pkt.DumpRaw(Serial);
```

### ```VanBus``` object

The following methods are available for the ```VanBus``` object:<a name = "functions"></a>

1. [```void Setup(uint8_t rxPin)```](#Setup)
2. [```bool Available()```](#Available)
3. [```bool Receive(TVanPacketRxDesc& pkt)```](#Receive)
4. [```uint32_t GetCount()```](#GetCount)
5. [```void DumpStats(Stream& s)```](#DumpStats)

---

### 1. ```void Setup(uint8_t rxPin)``` <a name = "Setup"></a>

Start the receiver listening on GPIO pin ```rxPin```

### 2. ```bool Available()``` <a name = "Available"></a>

Returns ```true``` if a VAN packet is available in the receive queue.

### 3. ```bool Receive(TVanPacketRxDesc& pkt)``` <a name = "Available"></a>

Copy a VAN packet out of the receive queue, if available. Otherwise, returns ```false```.

### 4. ```uint32_t GetCount()``` <a name = "GetCount"></a>

Returns the number of received VAN packets since power-on. Counter may roll over.

### 5. ```void DumpStats(Stream& s)``` <a name = "DumpStats"></a>

Dumps a few packet statistics on the passed stream.

### VAN packets

On a VAN bus, the electrical signals are the same as CAN. However, CAN and VAN use different data encodings on the
line which makes it impossible to use CAN interfaces on a VAN bus.

For background reading:
- https://en.wikipedia.org/wiki/Vehicle_Area_Network
- http://graham.auld.me.uk/projects/vanbus/lineprotocol.html
- http://graham.auld.me.uk/projects/vanbus/datasheets/
- http://www.i3s.unice.fr/~map/Cours/MASTER_STIC_SE/COURS32007.pdf
- http://ebajic.free.fr/Ecole%20Printemps%20Reseau%20Mars%202006/Supports/J%20MERCKLE%20CANopen.pdf
- http://igm.univ-mlv.fr/~duris/NTREZO/20042005/Guerrin-Guers-Guinchard-VAN-CAN-rapport.pdf
- http://www.educauto.org/sites/www.educauto.org/files/file_fields/2013/11/18/mux3.pdf
- https://www.amazon.com/bus-VAN-vehicle-area-network/dp/2100031600
- http://milajda22.sweb.cz/Manual_k_ridici_jednotce.pdf#page=17
- https://github.com/morcibacsi/VanAnalyzer/

The following methods are available for ```TVanPacketRxDesc``` packet objects as obtained from
```VanBus.Receive(...)```:

1. [```uint16_t Iden()```](#Iden)
2. [```uint16_t Flags()```](#Flags)
3. [```const uint8_t* Data()```](#Data)
4. [```int DataLen()```](#DataLen)
5. [```uint16_t Crc()```](#Crc)
6. [```bool CheckCrc()```](#CheckCrc)
7. [```bool CheckCrcAndRepair()```](#CheckCrcAndRepair)
8. [```void DumpRaw(Stream& s)```](#DumpRaw)
9. [```const TIsrDebugPacket& getIsrDebugPacket()```](#getIsrDebugPacket)
10. [```const char* FlagsStr()```](#FlagsStr)
11. [```const char* AckStr()```](#AckStr)
12. [```const char* ResultStr()```](#ResultStr)

---

### 1. ```uint16_t Iden()``` <a name = "Iden"></a>

Returns the IDEN field of the VAN packet.

An overview of known IDEN values can be found e.g. at:

- http://pinterpeti.hu/psavanbus/PSA-VAN.html
- http://graham.auld.me.uk/projects/vanbus/protocol.html

### 2. ```uint16_t Flags()``` <a name = "Flags"></a>

Returns the "command" FLAGS field of the VAN packet. Each VAN packet has 4 "command" flags:
- EXT : always 1
- RAK : 1 = Request AcKnowledge
- R/W : 1 = Read, 0 = Write
- RTR : 1 = Remote Transmit Request

### 3. ```const uint8_t* Data()``` <a name = "Data"></a>

Returns the data field (bytes) of the VAN packet.

### 4. ```int DataLen()``` <a name = "DataLen"></a>

Returns the number of data bytes in the VAN packet. There can be at most 28 data bytes in a VAN packet.

### 5. ```uint16_t Crc()``` <a name = "Crc"></a>

Returns the 15-bit CRC value of the VAN packet.

### 6. ```bool CheckCrc()``` <a name = "CheckCrc"></a>

Checks the CRC value of the VAN packet.

### 7. ```bool CheckCrcAndRepair()``` <a name = "CheckCrcAndRepair"></a>

Checks the CRC value of the VAN packet. If not, tries to repair it by flipping each bit. Returns ```true``` if the
packet is OK (either before or after the repair).

### 8. ```void DumpRaw(Stream& s)``` <a name = "DumpRaw"></a>

Dumps the raw packet bytes to a stream.

### 9. ```const TIsrDebugPacket& getIsrDebugPacket()``` <a name = "getIsrDebugPacket"></a>

Retrieves a debug structure that can be used to analyse (observed) bit timings.

### 10. ```const char* FlagsStr()``` <a name = "FlagsStr"></a>

Returns the "command" FLAGS field of the VAN packet as a string

Note: uses a statically allocated buffer, so don't call this method twice within the same printf invocation.

### 11. ```const char* AckStr()``` <a name = "AckStr"></a>

Returns the ACK field of the VAN packet as a string, either "ACK" or "NO_ACK".

### 12. ```const char* ResultStr()``` <a name = "ResultStr"></a>

Returns the RESULT field of the VAN packet as a string, either "OK" or a string starting with "ERROR_".

## 👷 Work to be done

Currently I am writing a VAN packet parser that will be able to send WebSocket messages to a browser, for real-time
updates on a "virtual dashboard" web page that is served by the ESP8266 based board. I am looking for volunteers who
can draw a nice skin for this "virtual dashboard" 😁. Inspiration? Have a look e.g. at
https://realdash.net/gallery.php.

## 📖 License <a name = "license"></a>

This library is open-source and licensed under the [MIT license](http://opensource.org/licenses/MIT).

Do whatever you like with it, but contributions are appreciated!

[MCP2551]: http://ww1.microchip.com/downloads/en/devicedoc/21667d.pdf
[level converter]: https://www.tinytronics.nl/shop/en/dc-dc-converters/level-converters/i2c-uart-bi-directional-logic-level-converter-5v-3.3v-2-channel-with-supply
[SN65HVD230]: https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf?ts=1592992149874&ref_url=https%253A%252F%252Fwww.google.nl%252F
[voltage divider]: https://www.quora.com/How-many-pins-on-Arduino-Uno-give-a3-3v-pin-output