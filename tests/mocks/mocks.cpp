#include "Arduino.h"
#include "Wire.h"

GpioState gpio;
FakeSerial Serial;
TwoWire Wire;

void pinMode(int pin, int mode) {
    gpio.events.push_back({GpioState::Event::PIN_MODE, pin, mode});
    gpio.modes[pin] = mode;
}

void digitalWrite(int pin, int value) {
    gpio.events.push_back({GpioState::Event::DIGITAL_WRITE, pin, value});
    gpio.levels[pin] = value;
}

// ---------------------------------------------------------------------------------------------
// FakeFram
// ---------------------------------------------------------------------------------------------
FakeFram::FakeFram(uint16_t densityKbit, uint8_t address) : baseAddress(address) {
    // A 1 Mbit chip is seen as two 512 kbit chips, only one half is modelled
    memory.assign(densityKbit >= 512 ? 65536 : densityKbit * 128, 0xFF);
}

void FakeFram::setDeviceId(uint16_t manufacturer, uint16_t product) {
    hasDeviceId = true;
    deviceId[0] = static_cast<uint8_t>(manufacturer >> 4);
    deviceId[1] = static_cast<uint8_t>(((manufacturer & 0x0F) << 4) | ((product >> 8) & 0x0F));
    deviceId[2] = static_cast<uint8_t>(product & 0xFF);
}

uint8_t FakeFram::pageBits() const {
    if (memory.size() == 512) {
        return 1;
    }
    if (memory.size() == 2048) {
        return 3;
    }
    return 0;
}

bool FakeFram::respondsTo(uint8_t address) const {
    const uint8_t mask = static_cast<uint8_t>(0xFF << pageBits());
    return (address & mask) == (baseAddress & mask);
}

void FakeFram::write(uint8_t address, const std::vector<uint8_t>& payload) {
    const uint8_t bits = pageBits();
    const size_t addressBytes = (bits > 0) ? 1 : 2;
    if (payload.size() < addressBytes) {
        return;  // Incomplete address, the latch is not updated
    }
    if (bits > 0) {
        const uint32_t page = address & ((1u << bits) - 1);
        pointer = (page << 8) | payload[0];
    } else {
        pointer = (static_cast<uint32_t>(payload[0]) << 8) | payload[1];
    }
    pointer %= memory.size();
    for (size_t i = addressBytes; i < payload.size(); i++) {
        memory[pointer] = payload[i];
        bytesWritten++;
        pointer++;
        if (pointer == memory.size()) {
            pointer = 0;
            wrapped = (i + 1 < payload.size()) || wrapped;
        }
    }
}

uint8_t FakeFram::read(uint8_t address) {
    const uint8_t bits = pageBits();
    if (bits > 0) {
        // The page bits of the device address replace the upper bits of the address latch
        const uint32_t page = address & ((1u << bits) - 1);
        pointer = (page << 8) | (pointer & 0xFF);
    }
    const uint8_t value = memory[pointer];
    pointer = (pointer + 1) % memory.size();
    return value;
}

// ---------------------------------------------------------------------------------------------
// TwoWire
// ---------------------------------------------------------------------------------------------
void TwoWire::reset() {
    device = nullptr;
    bufferLength = 32;
    forcedTxResults.clear();
    forcedRxCounts.clear();
    transmissions.clear();
    requests.clear();
    txAddress = 0;
    txBuffer.clear();
    rxBuffer.clear();
    deviceIdSelected = false;
}

void TwoWire::beginTransmission(uint8_t address) {
    txAddress = address;
    txBuffer.clear();
}

size_t TwoWire::write(uint8_t data) {
    if (txBuffer.size() >= bufferLength) {
        return 0;  // Like the real library: bytes that do not fit the buffer are dropped
    }
    txBuffer.push_back(data);
    return 1;
}

uint8_t TwoWire::endTransmission(bool sendStop) {
    transmissions.push_back({txAddress, txBuffer, sendStop});
    const std::vector<uint8_t> payload = txBuffer;
    txBuffer.clear();
    deviceIdSelected = false;

    if (!forcedTxResults.empty()) {
        const uint8_t forced = forcedTxResults.front();
        forcedTxResults.pop_front();
        if (forced != 0) {
            return forced;
        }
    }
    if (device == nullptr) {
        return 2;  // NACK on address
    }
    if (txAddress == DEVICE_ID_ADDRESS) {
        if (!device->hasDeviceId) {
            return 2;
        }
        if (payload.size() != 1 || !device->respondsTo(payload[0] >> 1)) {
            return 3;  // NACK on data
        }
        deviceIdSelected = true;
        return 0;
    }
    if (!device->respondsTo(txAddress)) {
        return 2;
    }
    device->write(txAddress, payload);
    return 0;
}

uint8_t TwoWire::requestFrom(uint8_t address, uint8_t quantity) {
    requests.push_back({address, quantity});
    rxBuffer.clear();

    size_t count = quantity;
    if (count > bufferLength) {
        count = bufferLength;  // Like the real library: the request is truncated
    }
    if (!forcedRxCounts.empty()) {
        const int forced = forcedRxCounts.front();
        forcedRxCounts.pop_front();
        if (forced >= 0) {
            count = static_cast<size_t>(forced);
        }
    }

    if (device != nullptr && address == DEVICE_ID_ADDRESS && deviceIdSelected) {
        for (size_t i = 0; i < count; i++) {
            rxBuffer.push_back(device->deviceId[i % 3]);
        }
    } else if (device != nullptr && address != DEVICE_ID_ADDRESS && device->respondsTo(address)) {
        for (size_t i = 0; i < count; i++) {
            rxBuffer.push_back(device->read(address));
        }
    }
    deviceIdSelected = false;
    return static_cast<uint8_t>(rxBuffer.size());
}

int TwoWire::available() { return static_cast<int>(rxBuffer.size()); }

int TwoWire::read() {
    if (rxBuffer.empty()) {
        return -1;
    }
    const uint8_t value = rxBuffer.front();
    rxBuffer.pop_front();
    return value;
}
