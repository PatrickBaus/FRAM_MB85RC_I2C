Arduino library for I2C FRAM - Fujitsu MB85RC & Cypress FM24, CY15B
==============

I2C Ferroelectric Random Access Memory (FRAM). Read/write endurance for each memory slot : 10^12 cycles and more.
9~16 bit adresses, 8 bits data slots.

Supports 4K, 16K, 64K, 128K, 256K & 512K devices. Works for 1M devices when considering each device as 2 differents 512K devices

For SPI chips, please have a look on [Christophe Persoz's repo](https://github.com/christophepersoz/FRAM_MB85RS_SPI)


## Features ##
- Device settings detection (if Device ID feature is available)
- Device manual setting
- Manage single bit (read, set, clear, toggle) from a byte
- Write one 8-bits, 16-bits or 32-bits value
- Write one array of bytes 
- Read one 8-bits, 16-bits or 32-bits value
- Read one array of bytes (up to 255 per call, limited by the buffer of the board's Wire lib - 32 bytes on AVR boards)
- Move a byte from an address to another
- Get device information
	- 1: Manufacturer ID
	- 2: Product ID
	- 3: Density code
	- 4: Density human readable
- Manage write protect pin
- Erase memory (set all chip to 0x00)
- Prevent cycling through memory map to avoid unwanted overwrites
- Write protect pin management and debug output selectable at compile time

## Usage ##

```cpp
#include <Wire.h>
#include <FRAM_MB85RC_I2C.h>

// Chip with Device ID feature at the default address 0x50, detected automatically
FRAM_MB85RC_I2C fram(&Wire);

void setup() {
	Serial.begin(115200);
	Wire.begin();        // The library does not start the I2C bus itself
	fram.begin();        // Detects the chip and sets up the memory map

	if (!fram.isReady()) {
		Serial.println("FRAM not found");
		return;
	}

	uint16_t value = 0;
	byte result = fram.writeWord(0x0025, 0xBEEF);
	if (result == ERROR_SUCCESS) {
		result = fram.readWord(0x0025, &value);
	}
	if (result == ERROR_SUCCESS) {
		Serial.println(value, HEX);
	}
}

void loop() {}
```

Chips without the Device ID feature (see the tables below) cannot be detected. Tell the library the density instead ("manual mode"):

```cpp
// I2C address 0x50, write protection off, WP pin 13, 16K chip
FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 13, 16);
```

More examples can be found in the [examples](examples) folder.

## Configuration ##

The following settings are evaluated at compile time. All of them have a default and can be overridden with compiler flags, so there is no need to edit the library.

| Flag | Default | Description |
| ------ | :------: | ------ |
| `MANAGE_WP` | `false` | `true` if the WP pin of the chip is connected to the MCU. The library then configures the pin as output and drives it. If `false`, no pin is touched and `enableWP()` / `disableWP()` return `ERROR_NOT_PERMITTED` |
| `DEFAULT_WP_PIN` | `13` | Pin connected to WP, used by the constructors that do not take a pin |
| `DEFAULT_WP_STATUS` | `false` | Write protection after start-up, used by the constructor that does not take a `wp` argument. `false`: the memory can be written |
| `SERIAL_DEBUG` | not set | Set to `1` to print debug information to `Serial`: the result of the detection in `begin()`, the calculated I2C address of each access and the start and result of `eraseDevice()`. If `Serial` is not open yet, `begin()` opens it with 9600 baud |

PlatformIO (`platformio.ini`):

```ini
build_flags =
	-DMANAGE_WP=true
	-DDEFAULT_WP_PIN=7
	-DSERIAL_DEBUG=1
```

Arduino CLI:

	arduino-cli compile --fqbn arduino:avr:uno --build-property "compiler.cpp.extra_flags=-DMANAGE_WP=true -DDEFAULT_WP_PIN=7" MySketch

The Arduino IDE has no setting for compiler flags. Either change the defaults in `FRAM_MB85RC_I2C.h` or set `compiler.cpp.extra_flags` in a `platform.local.txt` next to the `platform.txt` of the board package. A `#define` in the sketch has no effect, because the library is compiled separately.

## API ##

All functions returning a `byte` return an error code, see [Errors](#errors). `0` (`ERROR_SUCCESS`) means success.

### Constructors ###

| Constructor | Description |
| ------ | ------ |
| `FRAM_MB85RC_I2C(TwoWire* wire)` | Chip at the default address `0x50`, detected automatically. Write protection as set by `DEFAULT_WP_STATUS`, WP pin `DEFAULT_WP_PIN` |
| `FRAM_MB85RC_I2C(TwoWire* wire, uint8_t address, bool wp)` | Chip at `address`, detected automatically. `wp` is the write protection after start-up, WP pin `DEFAULT_WP_PIN` |
| `FRAM_MB85RC_I2C(TwoWire* wire, uint8_t address, bool wp, int pin)` | As above, with the WP pin `pin` |
| `FRAM_MB85RC_I2C(TwoWire* wire, uint8_t address, bool wp, int pin, uint16_t chipDensity)` | Manual mode for chips without Device ID feature. `chipDensity` is the density in kbit: `4`, `16`, `64`, `128`, `256`, `512` or `1024` |

- `wire` is the I2C interface the chip is connected to, e.g. `&Wire` or `&Wire1`. `begin()` of the interface must be called by the sketch.
- `address` is the 7 bit I2C address (`0x50` - `0x57`, see [Adresses](#adresses)). The constants `MB85RC_ADDRESS_A000` to `MB85RC_ADDRESS_A111` can be used.
- `wp` and `pin` are only used if `MANAGE_WP` is `true`. The pin is configured in the constructor.

### Initialisation and device information ###

| Function | Description |
| ------ | ------ |
| `void begin()` | Identifies the chip and sets up the memory map. Must be called once before the memory is accessed, after `wire->begin()` |
| `byte checkDevice()` | Runs the identification again. Returns `ERROR_SUCCESS` or `ERROR_CHIP_UNIDENTIFIED` and updates `isReady()` |
| `bool isReady()` | `true` if the chip was identified by the last `begin()` or `checkDevice()` |
| `byte getOneDeviceID(uint8_t idType, uint16_t *id)` | Reads one property of the chip. `idType`: `1` manufacturer ID, `2` product ID, `3` density code, `4` density in kbit. Returns `ERROR_DEVICE_ID` for any other type. In manual mode the three IDs are `0xF00` |

Until the chip is identified the memory map is empty and every read or write returns `ERROR_OUT_OF_RANGE`.

In manual mode the identification only validates the density, it does not talk to the chip. `isReady()` therefore does not tell whether a chip is actually connected.

### Reading and writing ###

`framAddr` is the address of the first byte within the memory map, starting at 0.

| Function | Description |
| ------ | ------ |
| `byte readByte(uint16_t framAddr, uint8_t *value)` | Reads one byte |
| `byte writeByte(uint16_t framAddr, uint8_t value)` | Writes one byte |
| `byte readWord(uint16_t framAddr, uint16_t *value)` | Reads a 16 bit value (2 bytes) |
| `byte writeWord(uint16_t framAddr, uint16_t value)` | Writes a 16 bit value (2 bytes) |
| `byte readLong(uint16_t framAddr, uint32_t *value)` | Reads a 32 bit value (4 bytes) |
| `byte writeLong(uint16_t framAddr, uint32_t value)` | Writes a 32 bit value (4 bytes) |
| `byte readArray(uint16_t framAddr, byte items, uint8_t values[])` | Reads `items` bytes into `values`. `items` must not be 0 (`ERROR_TOO_SHORT`) |
| `byte writeArray(uint16_t framAddr, byte items, uint8_t values[])` | Writes `items` bytes from `values` |
| `byte copyByte(uint16_t origAddr, uint16_t destAddr)` | Copies one byte from `origAddr` to `destAddr` |
| `byte eraseDevice()` | Overwrites the whole memory with `0x00`, byte by byte. Stops at the first error and returns it |

- **Range check:** an access that does not fit completely into the memory map returns `ERROR_OUT_OF_RANGE` and nothing is sent to the chip. The chip itself would roll over to address 0.
- **Failed reads:** if a read fails, the error is returned and the output variable or buffer is not modified.
- **Transfer size:** one call is one I2C transfer and is limited by the buffer of the board's Wire library. On AVR boards this is 32 bytes for a read and 32 bytes including the memory address for a write, i.e. 30 data bytes for chips of 64K and above and 31 data bytes for 4K and 16K chips. Larger transfers return `ERROR_TOO_LONG`. A write that is too long is still sent truncated, because the Wire library cannot abort a transmission, so split large arrays into several calls.
- **Byte order:** words and longs are stored in the byte order of the MCU (little endian on all common Arduino boards). Keep this in mind if the memory is read by another system.

### Bit operations ###

`bitNb` is the bit position within the byte, `0` (LSB) to `7` (MSB). Any other value returns `ERROR_INVALID_BIT_POS`.

| Function | Description |
| ------ | ------ |
| `byte readBit(uint16_t framAddr, uint8_t bitNb, byte *bit)` | Reads one bit, `bit` is set to `0` or `1` |
| `byte setOneBit(uint16_t framAddr, uint8_t bitNb)` | Sets one bit to `1` |
| `byte clearOneBit(uint16_t framAddr, uint8_t bitNb)` | Sets one bit to `0` |
| `byte toggleBit(uint16_t framAddr, uint8_t bitNb)` | Inverts one bit |

Setting, clearing and toggling read the byte, modify it and write it back. If the read fails, nothing is written.

### Write protection ###

| Function | Description |
| ------ | ------ |
| `byte enableWP()` | Drives the WP pin high, the memory is read-only |
| `byte disableWP()` | Drives the WP pin low, the memory can be written |
| `bool getWPStatus()` | `true` if the write protection is enabled. Always `false` if `MANAGE_WP` is `false` |

`enableWP()` and `disableWP()` return `ERROR_NOT_PERMITTED` if `MANAGE_WP` is `false`.

## Revision History ##


    v1.0 - First release
	v1.0.1 - Robustness enhancement
	v1.0.2 - fix constructor, introducing byte move in memory
	v1.0.3 - fix writeLong() function
	v1.0.4 - fix constructor call error
	v1.0.4.1 - Add example to help @porcao
	v1.0.5 - Enlarge density chip support by making check more flexible, Error codes not anymore hardcoded, add connect example, add Cypress FM24 & CY15B series comment.
	v1.1.0b - Adding support for devices without device IDs + 4K & 16 K devices support
	v1.1.0b1 - Fixing checkDevice() + end of range memory map check + better manual mode example
	v1.2.0 - Uses reinterpret_cast instead of bit shift / masking for performance. Breaks backward compatibility with previous code - See PR#6
	v1.2.1 - Fix issue #11, issue #13, issue #10, Updating tested chips table

## Devices ##

**Fujitsu FRAM** - manufacturer code 0x00A - [Fujitsu page](http://www.fujitsu.com/us/products/devices/semiconductor/memory/fram/lineup/index.html)

|  Model | Density (kB) | Device addressing | Device ID feature | Density code | Memory addressing | Tested |
|  ------ | :------: | :------: | :------: | :------: | :------: | :------: |
|  **MB85RC04V** | 4 | 6 bits | Yes | 0x00 | 9 bits [1] | Yes |
|  **MB85RC16V** | 16 | 4 bits | No | - | 11 bits [2] | Yes |
|  **MB85RC16** | 16 | 4 bits | No | - | 11 bits [2] | No |
|  **MB85RC64V** | 64 | 7 bits | No | - | 13 bits | No |
|  **MB85RC64A** | 64 | 7 bits | No | - | 13 bits | Yes |
|  **MB85RC64TA** | 64 | 7 bits | Yes | 0x03 | 13 bits | No |
|  **MB85RC128A** | 128 | 7 bits | No | - | 14 bits | No |
|  **MB85RC256V** | 256 | 7 bits | Yes | 0x05 | 15 bits | Yes |
|  **MB85RC512T** | 512 | 7 bits | Yes | 0x06 | 16 bits | No |
|  **MB85RC1MT** | 1024 | 6 bits | Yes | 0x07 | 17 bits [3] | Yes |

**Cypress FRAM** - manufacturer code 0x004 - [Cypress page](http://www.cypress.com/products/f-ram-serial)

|  Model | Density (kB) | Device addressing | Device ID feature | Density code | Memory addressing | Tested |
|  ------ | :------: | :------: | :------: | :------: | :------: | :------: |
|  **FM24CL04B** | 4 | 6 bits | No | - | 9 bits [1] | Yes |
|  **FM24C04B** | 4 | 6 bits | No | - | 9 bits [1] | Yes |
|  **FM24C16B** | 16 | 4 bits | No | - | 11 bits [2] | No |
|  **FM24CL16B** | 16 | 4 bits | No | - | 11 bits [2] | Yes |
|  **FM24C64B** | 64 | 7 bits | No | - | 13 bits | Yes |
|  **FM24CL64B** | 64 | 7 bits | No | - | 13 bits | Yes |
|  **CY15B128J** | 128 | 7 bits | Yes | 0x01 | 14 bits | No |
|  **FM24W256** | 256 | 7 bits | No | - | 15 bits | No |
|  **CY15B256J** | 256 | 7 bits | Yes | 0x02 | 15 bits | No |
|  **FM24V05** | 512 | 7 bits | Yes | 0x03 | 16 bits | No |
|  **FM24V10** | 1024 | 6 bits | Yes | 0x04 | 17 bits [3] | Yes |	


[1]: 4K devices have a 9 bits addressing memory map. The 9th bit is set in the device address byte

[2]: 16K devices a 11 bits addressing memory map. The 3 MSB are set in the device address byte in place of A2~A0

[3]: 1M a 17 bits addressing memory map. To manage this device, you need to consider it as 2 512K devices with 2 distincts adresses : 1010+A2+A1+0 and 1010+A2+A1+1. The library is set that way.


## Adresses ##
Devices address : b1010 + A2 + A1 + A0.

All devices are pulling down internaly A2, A1 & A0. Default address is b1010000 (0x50) - exception 1M chips which seems to be a double 512K devices in a single package. Please use 2 objects instances to deal with them.

4K devices have only A2 & A1 support. A0 is used for memory addressing. `i2c_addr = 0b1010xx0`

16K devices does not have A2, A1 nor A0 support. This is used for memory addressing `i2c_addr = 0b1010000`

An interesting [document](Docs/Fujitsu_FRAM_difference_addressing_scheme_over_I2C.pdf) from Fujitsu describes it quite well.



## Errors ##
Almost every function returns an error code as `byte`. Codes 1 to 3 are the status codes of `Wire.endTransmission()`, the others are generated by the library. Note that some Wire implementations also return 4 (other error) and 5 (timeout), which are passed on unchanged and overlap with the codes of the library. The constants are defined in `FRAM_defines.h`.

| Code | Constant | Description |
| :------: | ------ | ------ |
| 0 | `ERROR_SUCCESS` | Success |
| 1 | `ERROR_TOO_LONG` | Data too long to fit in the transmit or receive buffer of the Wire library (32 bytes including the memory address on AVR boards) |
| 2 | `ERROR_NACK_ON_ADDRESS` | Received NACK on transmit of the address, or the chip did not answer a read request |
| 3 | `ERROR_NACK_ON_DATA` | Received NACK on transmit of data |
| 4 | `ERROR_SERIAL_UNAVAILABLE` | Serial not accessible (debug output) |
| 5 | `ERROR_DEVICE_ID` | Not referenced device ID type |
| 7 | `ERROR_CHIP_UNIDENTIFIED` | FRAM chip unidentified |
| 8 | `ERROR_TOO_SHORT` | Number of bytes asked to read null |
| 9 | `ERROR_INVALID_BIT_POS` | Bit position out of range |
| 10 | `ERROR_NOT_PERMITTED` | Not permitted operation |
| 11 | `ERROR_OUT_OF_RANGE` | Out of memory range operation |

The former names `ERROR_0` to `ERROR_11` are still available.

## Testing ##
- Tested against MB85RC256V - breakout board from Adafruit http://www.adafruit.com/product/1895
- Tested on Arduino Mega with Arduino IDE 1.0.5 & 1.6.11
- Please comment about other devices (Memory & Arduino Boards) - A specific [thread](https://github.com/sosandroid/FRAM_MB85RC_I2C/issues/3) has been opened.

- While testing your device, please use the manual mode & the readIDs examples

## To do ##
- Test all devices - [Testing thread](https://github.com/sosandroid/FRAM_MB85RC_I2C/issues/3)
- Create a more robust error management (function to handle that with higher layer)
- Rework the debug mode

## Q&A ##
Here some quick answers to some interesting questions:

- **Does this lib suitable for some others devices not listed here ?** _It can. To understand the way the lib works, just have a look on the datasheets of MB85RC256V & MB85RC16V. They will let you know about the details. Some other chips, FRAM or not, may use the same logic : MB85RC series, FM24 series, CY15B series or even some 24LC memory chips_

- **What if I run manual mode declaring another density than the one really embeded ?** _There are several options :_
	- _You have a 128K chip and declaring 64K. There is no issue and you'll only use the first half of the memory map_
	- _You have a 128K chip and declaring 16K or 4K. The memory address mamangement is not done the same way. This cannot work_
	- _You have a 128K chip and declaring 256K. You'll be able to loop through the whole memory map and overwritting some data. The worst bugs ever_
	- _You have a 16K chip and declaring 256K. The memory address mamangement is not done the same way. This cannot work_
		
- **Your chip or the lib does not behave as expected** _First check the datasheet to check either this is a lib bug or not. Using the various exmaples and playing around with settings would help. Have a look on [issue #2](https://github.com/sosandroid/FRAM_MB85RC_I2C/issues/2) to have ideas about some possible misbehavior's origins._

- **My devices is not recognized automatically** _Please run the `ReadIDs.ino` example and `manual_mode.ino` example to find out your real chip capabilities._

- **Your chip has device's IDs but not recognized by the lib** _Please open an issue to add it in the lib. Provide also all required data such as device's manufacturer, name, IDs and the tests done._

- **Sleep mode & High speed mode are not supported** _Those features are not supported at the moment as they require a huge rework of the lib. At this time they seem to be out of scope._

## Credits ##
- [Kevin Townsend](https://github.com/microbuilder) wrote the very first [Adafruit Lib](https://github.com/adafruit/Adafruit_FRAM_I2C) of which this one is forked.
- All testers who helped to improve this library
- [@Palatis](https://github.com/Palatis) for performance optimisation and debugging PRs
