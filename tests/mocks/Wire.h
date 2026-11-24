// Host-side stand-in for the Arduino Wire library with a simulated FRAM chip attached to the bus.
// Used by the unit tests only.
#ifndef FRAM_TEST_WIRE_H_
#define FRAM_TEST_WIRE_H_

#include <stddef.h>
#include <stdint.h>

#include <deque>
#include <vector>

// Behavioural model of an I2C FRAM chip. It implements the three addressing schemes used by the
// MB85RC / FM24 families:
//   -  4 kbit: 1 page bit in the device address + 1 address byte
//   - 16 kbit: 3 page bits in the device address + 1 address byte
//   - >= 64 kbit: 2 address bytes
// A 1 Mbit device looks like two 512 kbit devices and is modelled as such.
class FakeFram {
   public:
    FakeFram(uint16_t densityKbit, uint8_t baseAddress = 0x50);

    // Enables the "Device ID" feature and sets the 3 bytes returned by the chip
    void setDeviceId(uint16_t manufacturer, uint16_t product);

    uint8_t pageBits() const;
    bool respondsTo(uint8_t address) const;

    uint8_t baseAddress;
    bool hasDeviceId = false;
    uint8_t deviceId[3] = {0, 0, 0};
    std::vector<uint8_t> memory;
    uint32_t pointer = 0;      // Internal address latch
    size_t bytesWritten = 0;   // Total number of data bytes written to the memory
    bool wrapped = false;      // Set if the address latch rolled over during a write

    void write(uint8_t address, const std::vector<uint8_t>& payload);
    uint8_t read(uint8_t address);
};

class TwoWire {
   public:
    // ---- The part of the Arduino API used by the library ----
    void begin() {}
    void beginTransmission(uint8_t address);
    void beginTransmission(int address) { beginTransmission(static_cast<uint8_t>(address)); }
    size_t write(uint8_t data);
    size_t write(int data) { return write(static_cast<uint8_t>(data)); }
    uint8_t endTransmission(bool sendStop = true);
    uint8_t requestFrom(uint8_t address, uint8_t quantity);
    uint8_t requestFrom(int address, int quantity) {
        return requestFrom(static_cast<uint8_t>(address), static_cast<uint8_t>(quantity));
    }
    int available();
    int read();

    // ---- Test controls ----
    static const uint8_t DEVICE_ID_ADDRESS = 0xF8 >> 1;

    struct Transmission {
        uint8_t address;
        std::vector<uint8_t> payload;
        bool sendStop;
    };
    struct Request {
        uint8_t address;
        uint8_t quantity;
    };

    void reset();

    FakeFram* device = nullptr;                // The chip on the bus, nullptr for an empty bus
    size_t bufferLength = 32;                  // Size of the TX/RX buffers, 32 bytes on AVR
    std::deque<uint8_t> forcedTxResults;       // Results of the next endTransmission() calls. 0 = pass through
    std::deque<int> forcedRxCounts;            // Byte count of the next requestFrom() calls. < 0 = pass through
    std::vector<Transmission> transmissions;   // Log of all write transfers
    std::vector<Request> requests;             // Log of all read transfers

   private:
    uint8_t txAddress = 0;
    std::vector<uint8_t> txBuffer;
    std::deque<uint8_t> rxBuffer;
    bool deviceIdSelected = false;
};

extern TwoWire Wire;

#endif
