// Unit tests for the FRAM_MB85RC_I2C library.
//
// The tests run on the host against a simulated I2C bus with a behavioural model of the FRAM chips
// (see mocks/). The file is compiled once per library configuration, see CMakeLists.txt.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "FRAM_MB85RC_I2C.h"

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;

namespace {

#if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)
const bool kDebug = true;
#else
const bool kDebug = false;
#endif

const uint8_t ID_MANUFACTURER = 1;
const uint8_t ID_PRODUCT = 2;
const uint8_t ID_DENSITY_CODE = 3;
const uint8_t ID_DENSITY = 4;

uint32_t memorySize(uint16_t densityKbit) { return densityKbit >= 512 ? 65536UL : densityKbit * 128UL; }

uint16_t getId(FRAM_MB85RC_I2C& fram, uint8_t type) {
    uint16_t id = 0xDEAD;
    EXPECT_EQ(fram.getOneDeviceID(type, &id), ERROR_SUCCESS);
    return id;
}

class FramTest : public ::testing::Test {
   protected:
    void SetUp() override {
        Wire.reset();
        Serial.reset();
        gpio.reset();
    }
    void TearDown() override { Wire.reset(); }

    // Forgets all bus traffic seen so far
    void clearBusLog() {
        Wire.transmissions.clear();
        Wire.requests.clear();
    }
};

// =================================================================================================
// Automatic device detection
// =================================================================================================
struct IdCase {
    uint16_t manufacturer;
    uint16_t product;
    uint16_t densityKbit;
};

class AutoDetectTest : public FramTest, public ::testing::WithParamInterface<IdCase> {};

TEST_P(AutoDetectTest, IdentifiesChipAndMemorySize) {
    const IdCase& param = GetParam();
    FakeFram chip(param.densityKbit);
    chip.setDeviceId(param.manufacturer, param.product);
    Wire.device = &chip;

    FRAM_MB85RC_I2C fram(&Wire);
    EXPECT_FALSE(fram.isReady());
    fram.begin();

    EXPECT_TRUE(fram.isReady());
    EXPECT_EQ(fram.checkDevice(), ERROR_SUCCESS);
    EXPECT_EQ(getId(fram, ID_MANUFACTURER), param.manufacturer);
    EXPECT_EQ(getId(fram, ID_PRODUCT), param.product);
    EXPECT_EQ(getId(fram, ID_DENSITY_CODE), param.product >> 8);
    EXPECT_EQ(getId(fram, ID_DENSITY), param.densityKbit);

    // The whole memory map is usable, but nothing beyond
    const uint32_t size = memorySize(param.densityKbit);
    uint8_t value = 0;
    EXPECT_EQ(fram.writeByte(static_cast<uint16_t>(size - 1), 0xA5), ERROR_SUCCESS);
    EXPECT_EQ(fram.readByte(static_cast<uint16_t>(size - 1), &value), ERROR_SUCCESS);
    EXPECT_EQ(value, 0xA5);
    EXPECT_EQ(chip.memory[size - 1], 0xA5);
    if (size < 65536UL) {
        EXPECT_EQ(fram.writeByte(static_cast<uint16_t>(size), 0x5A), ERROR_OUT_OF_RANGE);
    }
    EXPECT_FALSE(chip.wrapped);
}

INSTANTIATE_TEST_SUITE_P(Chips, AutoDetectTest,
                         ::testing::Values(IdCase{FUJITSU_MANUFACT_ID, 0x010, 4},      // MB85RC04V
                                           IdCase{FUJITSU_MANUFACT_ID, 0x358, 64},     // MB85RC64TA
                                           IdCase{FUJITSU_MANUFACT_ID, 0x510, 256},    // MB85RC256V
                                           IdCase{FUJITSU_MANUFACT_ID, 0x658, 512},    // MB85RC512T
                                           IdCase{FUJITSU_MANUFACT_ID, 0x758, 1024},   // MB85RC1MT
                                           IdCase{CYPRESS_MANUFACT_ID, 0x100, 128},    // CY15B128J
                                           IdCase{CYPRESS_MANUFACT_ID, 0x200, 256},    // CY15B256J
                                           IdCase{CYPRESS_MANUFACT_ID, 0x300, 512},    // FM24V05
                                           IdCase{CYPRESS_MANUFACT_ID, 0x400, 1024})); // FM24V10

TEST_F(FramTest, DeviceIdRequestFollowsTheDatasheetSequence) {
    FakeFram chip(256, 0x53);
    chip.setDeviceId(FUJITSU_MANUFACT_ID, 0x510);
    Wire.device = &chip;

    FRAM_MB85RC_I2C fram(&Wire, 0x53, false);
    EXPECT_EQ(fram.checkDevice(), ERROR_SUCCESS);

    // Reserved slave ID 0xF8 followed by the device address, repeated start, then 3 bytes from 0xF9
    ASSERT_EQ(Wire.transmissions.size(), 1u);
    EXPECT_EQ(Wire.transmissions[0].address, 0xF8 >> 1);
    EXPECT_THAT(Wire.transmissions[0].payload, ElementsAre(0x53 << 1));
    EXPECT_FALSE(Wire.transmissions[0].sendStop);
    ASSERT_EQ(Wire.requests.size(), 1u);
    EXPECT_EQ(Wire.requests[0].address, 0xF8 >> 1);
    EXPECT_EQ(Wire.requests[0].quantity, 3);
}

// Runs the detection against a chip reporting the given IDs and expects it to be rejected
class DetectionFailureTest : public FramTest {
   protected:
    void expectRejected(FRAM_MB85RC_I2C& fram) {
        fram.begin();
        EXPECT_FALSE(fram.isReady());
        EXPECT_EQ(fram.checkDevice(), ERROR_CHIP_UNIDENTIFIED);
        EXPECT_EQ(getId(fram, ID_DENSITY), 0);
        // An unidentified chip has no memory map
        uint8_t value = 0x42;
        EXPECT_EQ(fram.writeByte(0, 0x00), ERROR_OUT_OF_RANGE);
        EXPECT_EQ(fram.readByte(0, &value), ERROR_OUT_OF_RANGE);
        EXPECT_EQ(value, 0x42);
    }
};

TEST_F(DetectionFailureTest, EmptyBus) {
    FRAM_MB85RC_I2C fram(&Wire);
    expectRejected(fram);
    EXPECT_EQ(Wire.available(), 0);
}

TEST_F(DetectionFailureTest, ChipWithoutDeviceIdFeature) {
    FakeFram chip(64);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire);
    expectRejected(fram);
    EXPECT_EQ(chip.bytesWritten, 0u);
}

TEST_F(DetectionFailureTest, ChipAtDifferentAddress) {
    FakeFram chip(256, 0x52);
    chip.setDeviceId(FUJITSU_MANUFACT_ID, 0x510);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire);
    expectRejected(fram);
}

TEST_F(DetectionFailureTest, UnknownFujitsuDensity) {
    FakeFram chip(256);
    chip.setDeviceId(FUJITSU_MANUFACT_ID, 0x210);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire);
    expectRejected(fram);
    EXPECT_EQ(getId(fram, ID_MANUFACTURER), FUJITSU_MANUFACT_ID);
}

TEST_F(DetectionFailureTest, UnknownCypressDensity) {
    FakeFram chip(256);
    chip.setDeviceId(CYPRESS_MANUFACT_ID, 0x700);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire);
    expectRejected(fram);
    EXPECT_EQ(getId(fram, ID_MANUFACTURER), CYPRESS_MANUFACT_ID);
}

TEST_F(DetectionFailureTest, UnknownManufacturer) {
    FakeFram chip(256);
    chip.setDeviceId(0x123, 0x510);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire);
    expectRejected(fram);
}

TEST_F(DetectionFailureTest, IncompleteDeviceIdAnswerIsNotInterpreted) {
    FakeFram chip(256);
    chip.setDeviceId(FUJITSU_MANUFACT_ID, 0x510);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire);

    Wire.forcedRxCounts = {2, 2, 2};
    expectRejected(fram);
    EXPECT_EQ(getId(fram, ID_MANUFACTURER), 0);
    EXPECT_EQ(Wire.available(), 0) << "Stale bytes must not be left in the receive buffer";
}

TEST_F(DetectionFailureTest, BusErrorDuringDetection) {
    FakeFram chip(256);
    chip.setDeviceId(FUJITSU_MANUFACT_ID, 0x510);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire);
    fram.begin();
    ASSERT_TRUE(fram.isReady());

    // A failed detection must not leave the memory map of the previous detection behind
    Wire.forcedTxResults = {ERROR_NACK_ON_DATA, ERROR_NACK_ON_DATA};  // begin() + checkDevice()
    expectRejected(fram);
    EXPECT_EQ(getId(fram, ID_MANUFACTURER), 0);
    EXPECT_EQ(getId(fram, ID_PRODUCT), 0);
    EXPECT_EQ(getId(fram, ID_DENSITY_CODE), 0);

    // ...and the chip is usable again once the bus has recovered
    EXPECT_EQ(fram.checkDevice(), ERROR_SUCCESS);
    EXPECT_TRUE(fram.isReady());
    EXPECT_EQ(fram.writeByte(0, 0x00), ERROR_SUCCESS);
}

TEST_F(FramTest, ChipDisappearingMakesTheObjectNotReady) {
    FakeFram chip(256);
    chip.setDeviceId(FUJITSU_MANUFACT_ID, 0x510);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire);
    fram.begin();
    ASSERT_TRUE(fram.isReady());

    Wire.device = nullptr;
    EXPECT_EQ(fram.checkDevice(), ERROR_CHIP_UNIDENTIFIED);
    EXPECT_FALSE(fram.isReady());
}

// =================================================================================================
// Manual mode
// =================================================================================================
class ManualModeTest : public FramTest, public ::testing::WithParamInterface<uint16_t> {};

TEST_P(ManualModeTest, ConfiguresMemoryMapWithoutBusAccess) {
    const uint16_t density = GetParam();
    const uint32_t size = memorySize(density);
    FakeFram chip(density);
    Wire.device = &chip;

    FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 13, density);
    EXPECT_FALSE(fram.isReady());
    fram.begin();

    EXPECT_TRUE(fram.isReady());
    EXPECT_THAT(Wire.transmissions, IsEmpty()) << "Manual mode must not query the device IDs";
    EXPECT_THAT(Wire.requests, IsEmpty());
    EXPECT_EQ(getId(fram, ID_MANUFACTURER), MANUALMODE_MANUFACT_ID);
    EXPECT_EQ(getId(fram, ID_PRODUCT), MANUALMODE_PRODUCT_ID);
    EXPECT_EQ(getId(fram, ID_DENSITY_CODE), MANUALMODE_DENSITY_ID);
    EXPECT_EQ(getId(fram, ID_DENSITY), density);

    // Every address of the memory map ends up in the right cell
    const std::vector<uint32_t> addresses = {0, 0x25, 0xFF, 0x100, size / 2 - 1, size / 2, size - 80, size - 1};
    for (const uint32_t address : addresses) {
        const uint8_t written = static_cast<uint8_t>(address * 7 + 3);
        uint8_t value = static_cast<uint8_t>(~written);
        ASSERT_EQ(fram.writeByte(static_cast<uint16_t>(address), written), ERROR_SUCCESS) << address;
        ASSERT_EQ(chip.memory[address], written) << address;
        ASSERT_EQ(fram.readByte(static_cast<uint16_t>(address), &value), ERROR_SUCCESS) << address;
        ASSERT_EQ(value, written) << address;
    }

    // ...and nothing beyond it
    if (size < 65536UL) {
        const size_t writtenBefore = chip.bytesWritten;
        uint8_t value = 0x42;
        EXPECT_EQ(fram.writeByte(static_cast<uint16_t>(size), 0x00), ERROR_OUT_OF_RANGE);
        EXPECT_EQ(fram.readByte(static_cast<uint16_t>(size), &value), ERROR_OUT_OF_RANGE);
        EXPECT_EQ(fram.writeByte(0xFFFF, 0x00), ERROR_OUT_OF_RANGE);
        EXPECT_EQ(value, 0x42);
        EXPECT_EQ(chip.bytesWritten, writtenBefore);
    }
    EXPECT_FALSE(chip.wrapped);
}

INSTANTIATE_TEST_SUITE_P(Densities, ManualModeTest, ::testing::Values(4, 16, 64, 128, 256, 512, 1024));

TEST_F(FramTest, ManualModeRejectsUnknownDensity) {
    FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 13, 32);
    fram.begin();

    EXPECT_FALSE(fram.isReady());
    EXPECT_EQ(fram.checkDevice(), ERROR_CHIP_UNIDENTIFIED);
    EXPECT_EQ(fram.writeByte(0, 0x00), ERROR_OUT_OF_RANGE);
}

// =================================================================================================
// Addressing schemes
// =================================================================================================
TEST_F(FramTest, Addressing4kUsesOnePageBit) {
    FakeFram chip(4, 0x52);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire, 0x52, false, 13, 4);
    fram.begin();

    EXPECT_EQ(fram.writeByte(0x125, 0xAB), ERROR_SUCCESS);
    EXPECT_EQ(fram.writeByte(0x025, 0xCD), ERROR_SUCCESS);

    ASSERT_EQ(Wire.transmissions.size(), 2u);
    EXPECT_EQ(Wire.transmissions[0].address, 0x53);
    EXPECT_THAT(Wire.transmissions[0].payload, ElementsAre(0x25, 0xAB));
    EXPECT_EQ(Wire.transmissions[1].address, 0x52) << "The page bit must be cleared again";
    EXPECT_THAT(Wire.transmissions[1].payload, ElementsAre(0x25, 0xCD));
}

TEST_F(FramTest, Addressing16kUsesThreePageBits) {
    FakeFram chip(16);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 13, 16);
    fram.begin();

    uint8_t value = 0;
    EXPECT_EQ(fram.writeByte(0x750, 0xAB), ERROR_SUCCESS);
    EXPECT_EQ(fram.readByte(0x750, &value), ERROR_SUCCESS);
    EXPECT_EQ(value, 0xAB);
    EXPECT_EQ(fram.writeByte(0x025, 0xCD), ERROR_SUCCESS);

    ASSERT_EQ(Wire.transmissions.size(), 3u);
    EXPECT_EQ(Wire.transmissions[0].address, 0x57);
    EXPECT_THAT(Wire.transmissions[0].payload, ElementsAre(0x50, 0xAB));
    EXPECT_EQ(Wire.transmissions[1].address, 0x57);
    EXPECT_THAT(Wire.transmissions[1].payload, ElementsAre(0x50));
    ASSERT_EQ(Wire.requests.size(), 1u);
    EXPECT_EQ(Wire.requests[0].address, 0x57);
    EXPECT_EQ(Wire.transmissions[2].address, 0x50) << "The page bits must be cleared again";
    EXPECT_THAT(Wire.transmissions[2].payload, ElementsAre(0x25, 0xCD));
}

TEST_F(FramTest, AddressingLargeChipsUsesTwoAddressBytes) {
    FakeFram chip(256, 0x53);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire, 0x53, false, 13, 256);
    fram.begin();

    uint8_t value = 0;
    EXPECT_EQ(fram.writeByte(0x1234, 0xAB), ERROR_SUCCESS);
    EXPECT_EQ(fram.readByte(0x1234, &value), ERROR_SUCCESS);
    EXPECT_EQ(value, 0xAB);

    ASSERT_EQ(Wire.transmissions.size(), 2u);
    EXPECT_EQ(Wire.transmissions[0].address, 0x53);
    EXPECT_THAT(Wire.transmissions[0].payload, ElementsAre(0x12, 0x34, 0xAB));
    EXPECT_EQ(Wire.transmissions[1].address, 0x53);
    EXPECT_THAT(Wire.transmissions[1].payload, ElementsAre(0x12, 0x34));
    ASSERT_EQ(Wire.requests.size(), 1u);
    EXPECT_EQ(Wire.requests[0].address, 0x53);
    EXPECT_EQ(Wire.requests[0].quantity, 1);
}

// =================================================================================================
// Reading and writing
// =================================================================================================
// A 64 kbit chip in manual mode, ready to use
class ReadWriteTest : public FramTest {
   protected:
    ReadWriteTest() : chip(64), fram(&Wire, 0x50, false, 13, 64) {}

    void SetUp() override {
        FramTest::SetUp();
        Wire.device = &chip;
        fram.begin();
        clearBusLog();
    }

    FakeFram chip;
    FRAM_MB85RC_I2C fram;
    static const uint16_t LAST = 8191;
};
const uint16_t ReadWriteTest::LAST;

TEST_F(FramTest, NothingIsAccessibleBeforeBegin) {
    FakeFram chip(256);
    chip.setDeviceId(FUJITSU_MANUFACT_ID, 0x510);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire);

    uint8_t value = 0x42;
    uint16_t id = 0xFFFF;
    EXPECT_FALSE(fram.isReady());
    EXPECT_EQ(fram.writeByte(0, 0x00), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.readByte(0, &value), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.eraseDevice(), ERROR_SUCCESS);
    EXPECT_EQ(fram.getOneDeviceID(ID_DENSITY, &id), ERROR_SUCCESS);
    EXPECT_EQ(id, 0);
    EXPECT_EQ(value, 0x42);
    EXPECT_EQ(chip.bytesWritten, 0u);
    EXPECT_THAT(Wire.transmissions, IsEmpty());
}

TEST_F(ReadWriteTest, ArrayRoundTrip) {
    uint8_t written[30];
    uint8_t read[30] = {0};
    for (size_t i = 0; i < sizeof(written); i++) {
        written[i] = static_cast<uint8_t>(0xC0 + i);
    }

    EXPECT_EQ(fram.writeArray(0x0FF0, sizeof(written), written), ERROR_SUCCESS);
    EXPECT_EQ(fram.readArray(0x0FF0, sizeof(read), read), ERROR_SUCCESS);

    EXPECT_THAT(read, ElementsAreArray(written));
    EXPECT_EQ(chip.memory[0x0FF0 - 1], 0xFF);
    EXPECT_EQ(chip.memory[0x0FF0], 0xC0);
    EXPECT_EQ(chip.memory[0x0FF0 + 29], 0xC0 + 29);
    EXPECT_EQ(chip.memory[0x0FF0 + 30], 0xFF);
    EXPECT_EQ(chip.bytesWritten, 30u);
}

TEST_F(ReadWriteTest, ArrayMayEndOnTheLastByte) {
    uint8_t written[4] = {1, 2, 3, 4};
    uint8_t read[4] = {0};

    EXPECT_EQ(fram.writeArray(LAST - 3, 4, written), ERROR_SUCCESS);
    EXPECT_EQ(fram.readArray(LAST - 3, 4, read), ERROR_SUCCESS);

    EXPECT_THAT(read, ElementsAreArray(written));
    EXPECT_EQ(chip.memory[LAST], 4);
    EXPECT_FALSE(chip.wrapped);
}

TEST_F(ReadWriteTest, ArrayCrossingTheEndIsRejected) {
    uint8_t buffer[4] = {1, 2, 3, 4};

    // One byte too many would silently roll over to address 0 on the chip
    EXPECT_EQ(fram.writeArray(LAST - 2, 4, buffer), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.writeArray(LAST, 2, buffer), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.writeArray(LAST + 1, 1, buffer), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.writeArray(0xFFFF, 255, buffer), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.readArray(LAST - 2, 4, buffer), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.readArray(LAST, 2, buffer), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.readArray(LAST + 1, 1, buffer), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.readArray(0xFFFF, 255, buffer), ERROR_OUT_OF_RANGE);

    EXPECT_THAT(buffer, ElementsAre(1, 2, 3, 4));
    EXPECT_THAT(Wire.transmissions, IsEmpty());
    EXPECT_THAT(Wire.requests, IsEmpty());
    EXPECT_EQ(chip.bytesWritten, 0u);
}

TEST_F(FramTest, LastByteOf512kChipIsAccessible) {
    FakeFram chip(512);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 13, 512);
    fram.begin();

    uint16_t word = 0;
    EXPECT_EQ(fram.writeWord(0xFFFE, 0xBEEF), ERROR_SUCCESS);
    EXPECT_EQ(fram.readWord(0xFFFE, &word), ERROR_SUCCESS);
    EXPECT_EQ(word, 0xBEEF);
    EXPECT_EQ(fram.writeWord(0xFFFF, 0xDEAD), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.readWord(0xFFFF, &word), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(word, 0xBEEF);
    EXPECT_FALSE(chip.wrapped);
    EXPECT_EQ(chip.memory[0], 0xFF);
}

TEST_F(ReadWriteTest, ReadingZeroBytesIsAnError) {
    uint8_t buffer[1] = {0x42};
    EXPECT_EQ(fram.readArray(0, 0, buffer), ERROR_TOO_SHORT);
    EXPECT_EQ(fram.readArray(0xFFFF, 0, buffer), ERROR_TOO_SHORT);
    EXPECT_EQ(buffer[0], 0x42);
    EXPECT_THAT(Wire.transmissions, IsEmpty());
}

TEST_F(ReadWriteTest, WritingZeroBytesOnlySetsTheAddress) {
    uint8_t buffer[1] = {0x42};
    EXPECT_EQ(fram.writeArray(0x0123, 0, buffer), ERROR_SUCCESS);
    EXPECT_EQ(fram.writeArray(LAST + 1, 0, buffer), ERROR_OUT_OF_RANGE);

    ASSERT_EQ(Wire.transmissions.size(), 1u);
    EXPECT_THAT(Wire.transmissions[0].payload, ElementsAre(0x01, 0x23));
    EXPECT_EQ(chip.bytesWritten, 0u);
}

TEST_F(ReadWriteTest, WriteLargerThanTheWireBufferIsReported) {
    uint8_t buffer[64] = {0};

    // 2 address bytes + 30 data bytes fill the 32 byte buffer
    EXPECT_EQ(fram.writeArray(0, 30, buffer), ERROR_SUCCESS);
    EXPECT_EQ(fram.writeArray(0, 31, buffer), ERROR_TOO_LONG);
    EXPECT_EQ(fram.writeArray(0, 64, buffer), ERROR_TOO_LONG);

    Wire.bufferLength = 128;
    EXPECT_EQ(fram.writeArray(0, 64, buffer), ERROR_SUCCESS);
}

TEST_F(ReadWriteTest, WriteReportsBusErrorsFirst) {
    uint8_t buffer[64] = {0};
    Wire.forcedTxResults = {ERROR_NACK_ON_ADDRESS, ERROR_NACK_ON_DATA};

    EXPECT_EQ(fram.writeArray(0, 4, buffer), ERROR_NACK_ON_ADDRESS);
    EXPECT_EQ(fram.writeArray(0, 64, buffer), ERROR_NACK_ON_DATA);
    EXPECT_EQ(chip.bytesWritten, 0u);
}

TEST_F(ReadWriteTest, ReadLargerThanTheWireBufferIsReported) {
    uint8_t buffer[64];
    memset(buffer, 0x42, sizeof(buffer));

    EXPECT_EQ(fram.readArray(0, 32, buffer), ERROR_SUCCESS);
    EXPECT_EQ(buffer[0], 0xFF);
    memset(buffer, 0x42, sizeof(buffer));
    EXPECT_EQ(fram.readArray(0, 33, buffer), ERROR_TOO_LONG);

    EXPECT_EQ(buffer[0], 0x42) << "A failed read must not modify the output buffer";
    EXPECT_EQ(Wire.available(), 0) << "Stale bytes must not be left in the receive buffer";
}

TEST_F(ReadWriteTest, ReadReportsBusErrors) {
    uint8_t buffer[4] = {1, 2, 3, 4};

    // The chip does not acknowledge its address when the memory address is sent
    Wire.forcedTxResults = {ERROR_NACK_ON_ADDRESS};
    EXPECT_EQ(fram.readArray(0, 4, buffer), ERROR_NACK_ON_ADDRESS);
    EXPECT_THAT(Wire.requests, IsEmpty());

    // The chip does not answer the read request
    Wire.forcedRxCounts = {0};
    EXPECT_EQ(fram.readArray(0, 4, buffer), ERROR_NACK_ON_ADDRESS);

    // The chip vanished
    Wire.device = nullptr;
    EXPECT_EQ(fram.readArray(0, 4, buffer), ERROR_NACK_ON_ADDRESS);
    EXPECT_EQ(fram.writeArray(0, 4, buffer), ERROR_NACK_ON_ADDRESS);

    EXPECT_THAT(buffer, ElementsAre(1, 2, 3, 4));
}

TEST_F(ReadWriteTest, ByteRoundTrip) {
    uint8_t value = 0;
    EXPECT_EQ(fram.writeByte(0x0025, 0xBE), ERROR_SUCCESS);
    EXPECT_EQ(fram.readByte(0x0025, &value), ERROR_SUCCESS);
    EXPECT_EQ(value, 0xBE);
    EXPECT_EQ(chip.memory[0x0025], 0xBE);
}

TEST_F(ReadWriteTest, WordRoundTrip) {
    uint16_t value = 0;
    EXPECT_EQ(fram.writeWord(0x0101, 0xBEEF), ERROR_SUCCESS);  // odd address on purpose
    EXPECT_EQ(fram.readWord(0x0101, &value), ERROR_SUCCESS);
    EXPECT_EQ(value, 0xBEEF);
    EXPECT_EQ(chip.bytesWritten, 2u);

    // The word is stored in the byte order of the MCU
    const uint16_t reference = 0xBEEF;
    EXPECT_EQ(memcmp(&chip.memory[0x0101], &reference, sizeof(reference)), 0);
}

TEST_F(ReadWriteTest, LongRoundTrip) {
    uint32_t value = 0;
    EXPECT_EQ(fram.writeLong(0x0201, 0xDEADBEEFUL), ERROR_SUCCESS);  // odd address on purpose
    EXPECT_EQ(fram.readLong(0x0201, &value), ERROR_SUCCESS);
    EXPECT_EQ(value, 0xDEADBEEFUL);
    EXPECT_EQ(chip.bytesWritten, 4u);

    const uint32_t reference = 0xDEADBEEFUL;
    EXPECT_EQ(memcmp(&chip.memory[0x0201], &reference, sizeof(reference)), 0);
}

TEST_F(ReadWriteTest, FailedReadsLeaveTheOutputUntouched) {
    uint8_t byteValue = 0x11;
    uint16_t wordValue = 0x2222;
    uint32_t longValue = 0x33333333UL;
    byte bitValue = 0x44;

    EXPECT_EQ(fram.readByte(LAST + 1, &byteValue), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.readWord(LAST, &wordValue), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.readLong(LAST - 2, &longValue), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.readBit(LAST + 1, 0, &bitValue), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.writeWord(LAST, 0), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.writeLong(LAST - 2, 0), ERROR_OUT_OF_RANGE);

    Wire.device = nullptr;
    EXPECT_EQ(fram.readByte(0, &byteValue), ERROR_NACK_ON_ADDRESS);
    EXPECT_EQ(fram.readWord(0, &wordValue), ERROR_NACK_ON_ADDRESS);
    EXPECT_EQ(fram.readLong(0, &longValue), ERROR_NACK_ON_ADDRESS);
    EXPECT_EQ(fram.readBit(0, 0, &bitValue), ERROR_NACK_ON_ADDRESS);

    EXPECT_EQ(byteValue, 0x11);
    EXPECT_EQ(wordValue, 0x2222);
    EXPECT_EQ(longValue, 0x33333333UL);
    EXPECT_EQ(bitValue, 0x44);
    EXPECT_EQ(chip.bytesWritten, 0u);
}

TEST_F(ReadWriteTest, CopyByte) {
    chip.memory[0x0010] = 0x5A;
    EXPECT_EQ(fram.copyByte(0x0010, LAST), ERROR_SUCCESS);
    EXPECT_EQ(chip.memory[LAST], 0x5A);
    EXPECT_EQ(chip.memory[0x0010], 0x5A);
}

TEST_F(ReadWriteTest, CopyByteDoesNotWriteAfterAFailedRead) {
    chip.memory[0x0020] = 0x77;

    EXPECT_EQ(fram.copyByte(LAST + 1, 0x0020), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.copyByte(0x0010, LAST + 1), ERROR_OUT_OF_RANGE);
    Wire.forcedTxResults = {ERROR_NACK_ON_ADDRESS};
    EXPECT_EQ(fram.copyByte(0x0010, 0x0020), ERROR_NACK_ON_ADDRESS);

    EXPECT_EQ(chip.memory[0x0020], 0x77);
    EXPECT_EQ(chip.bytesWritten, 0u);
}

// =================================================================================================
// Bit operations
// =================================================================================================
TEST_F(ReadWriteTest, ReadBit) {
    chip.memory[0x0030] = 0xA5;  // 0b10100101
    for (uint8_t bit = 0; bit < 8; bit++) {
        byte value = 0xFF;
        EXPECT_EQ(fram.readBit(0x0030, bit, &value), ERROR_SUCCESS);
        EXPECT_EQ(value, (0xA5 >> bit) & 1) << "bit " << int(bit);
    }
}

TEST_F(ReadWriteTest, SetClearAndToggleBit) {
    chip.memory[0x0030] = 0x00;
    for (uint8_t bit = 0; bit < 8; bit++) {
        EXPECT_EQ(fram.setOneBit(0x0030, bit), ERROR_SUCCESS);
        EXPECT_EQ(chip.memory[0x0030], 1 << bit);
        EXPECT_EQ(fram.setOneBit(0x0030, bit), ERROR_SUCCESS);
        EXPECT_EQ(chip.memory[0x0030], 1 << bit) << "Setting a set bit must not change it";
        EXPECT_EQ(fram.clearOneBit(0x0030, bit), ERROR_SUCCESS);
        EXPECT_EQ(chip.memory[0x0030], 0x00);
        EXPECT_EQ(fram.toggleBit(0x0030, bit), ERROR_SUCCESS);
        EXPECT_EQ(chip.memory[0x0030], 1 << bit);
        EXPECT_EQ(fram.toggleBit(0x0030, bit), ERROR_SUCCESS);
        EXPECT_EQ(chip.memory[0x0030], 0x00);
    }

    // The other bits of the byte are preserved
    chip.memory[0x0030] = 0xA5;
    EXPECT_EQ(fram.setOneBit(0x0030, 1), ERROR_SUCCESS);
    EXPECT_EQ(chip.memory[0x0030], 0xA7);
    EXPECT_EQ(fram.clearOneBit(0x0030, 7), ERROR_SUCCESS);
    EXPECT_EQ(chip.memory[0x0030], 0x27);
    EXPECT_EQ(fram.toggleBit(0x0030, 0), ERROR_SUCCESS);
    EXPECT_EQ(chip.memory[0x0030], 0x26);
    EXPECT_EQ(chip.memory[0x002F], 0xFF);
    EXPECT_EQ(chip.memory[0x0031], 0xFF);
}

TEST_F(ReadWriteTest, BitPositionIsValidated) {
    byte value = 0x42;
    EXPECT_EQ(fram.readBit(0x0030, 8, &value), ERROR_INVALID_BIT_POS);
    EXPECT_EQ(fram.setOneBit(0x0030, 8), ERROR_INVALID_BIT_POS);
    EXPECT_EQ(fram.clearOneBit(0x0030, 8), ERROR_INVALID_BIT_POS);
    EXPECT_EQ(fram.toggleBit(0x0030, 255), ERROR_INVALID_BIT_POS);

    EXPECT_EQ(value, 0x42);
    EXPECT_THAT(Wire.transmissions, IsEmpty());
}

TEST_F(ReadWriteTest, BitOperationsDoNotWriteAfterAFailedRead) {
    chip.memory[0x0030] = 0xA5;

    EXPECT_EQ(fram.setOneBit(LAST + 1, 0), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.clearOneBit(LAST + 1, 0), ERROR_OUT_OF_RANGE);
    EXPECT_EQ(fram.toggleBit(LAST + 1, 0), ERROR_OUT_OF_RANGE);

    // The read fails, writing back would store garbage
    Wire.forcedRxCounts = {0, 0, 0};
    EXPECT_EQ(fram.setOneBit(0x0030, 1), ERROR_NACK_ON_ADDRESS);
    EXPECT_EQ(fram.clearOneBit(0x0030, 0), ERROR_NACK_ON_ADDRESS);
    EXPECT_EQ(fram.toggleBit(0x0030, 0), ERROR_NACK_ON_ADDRESS);

    EXPECT_EQ(chip.memory[0x0030], 0xA5);
    EXPECT_EQ(chip.bytesWritten, 0u);
}

// =================================================================================================
// Device information
// =================================================================================================
TEST_F(ReadWriteTest, UnknownDeviceIdTypeIsRejected) {
    for (const uint8_t type : {0, 5, 255}) {
        uint16_t id = 0xDEAD;
        EXPECT_EQ(fram.getOneDeviceID(type, &id), ERROR_DEVICE_ID);
        EXPECT_EQ(id, 0);
    }
}

// =================================================================================================
// Erasing
// =================================================================================================
class EraseTest : public FramTest, public ::testing::WithParamInterface<uint16_t> {};

TEST_P(EraseTest, ClearsTheWholeMemoryMap) {
    const uint16_t density = GetParam();
    FakeFram chip(density);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 13, density);
    fram.begin();

    EXPECT_EQ(fram.eraseDevice(), ERROR_SUCCESS);

    EXPECT_EQ(chip.bytesWritten, memorySize(density));
    EXPECT_EQ(chip.memory, std::vector<uint8_t>(memorySize(density), 0x00));
    if (kDebug) {
        EXPECT_THAT(Serial.output, HasSubstr("Start erasing device\n"));
        EXPECT_THAT(Serial.output, HasSubstr("device erased\n"));
    }
}

INSTANTIATE_TEST_SUITE_P(Densities, EraseTest, ::testing::Values(4, 16, 64, 512));

TEST_F(ReadWriteTest, EraseStopsAtTheFirstError) {
    Wire.forcedTxResults = {0, 0, ERROR_NACK_ON_DATA};

    EXPECT_EQ(fram.eraseDevice(), ERROR_NACK_ON_DATA);

    EXPECT_EQ(chip.bytesWritten, 2u);
    EXPECT_EQ(Wire.transmissions.size(), 3u);
    if (kDebug) {
        EXPECT_THAT(Serial.output, HasSubstr("ERROR: device erasing stopped at position 2\n"));
    }
}

// =================================================================================================
// Write protection
// =================================================================================================
#if MANAGE_WP

TEST_F(FramTest, WriteProtectPinIsInitialisedOnce) {
    {
        FRAM_MB85RC_I2C fram(&Wire);
        EXPECT_FALSE(fram.getWPStatus());
        ASSERT_EQ(gpio.events.size(), 2u);
        EXPECT_EQ(gpio.modes[DEFAULT_WP_PIN], OUTPUT);
        EXPECT_EQ(gpio.levels[DEFAULT_WP_PIN], LOW);
    }
    gpio.reset();
    {
        FRAM_MB85RC_I2C fram(&Wire, 0x50, true);
        EXPECT_TRUE(fram.getWPStatus());
        ASSERT_EQ(gpio.events.size(), 2u);
        EXPECT_EQ(gpio.levels[DEFAULT_WP_PIN], HIGH);
    }
    gpio.reset();
    {
        // Only the configured pin may be touched, not the default one
        FRAM_MB85RC_I2C fram(&Wire, 0x50, true, 7);
        EXPECT_TRUE(fram.getWPStatus());
        ASSERT_EQ(gpio.events.size(), 2u);
        EXPECT_EQ(gpio.events[0].type, GpioState::Event::PIN_MODE);
        EXPECT_EQ(gpio.events[0].pin, 7);
        EXPECT_EQ(gpio.events[0].value, OUTPUT);
        EXPECT_EQ(gpio.events[1].type, GpioState::Event::DIGITAL_WRITE);
        EXPECT_EQ(gpio.events[1].pin, 7);
        EXPECT_EQ(gpio.events[1].value, HIGH);
    }
    gpio.reset();
    {
        FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 8, 64);
        EXPECT_FALSE(fram.getWPStatus());
        ASSERT_EQ(gpio.events.size(), 2u);
        EXPECT_EQ(gpio.modes[8], OUTPUT);
        EXPECT_EQ(gpio.levels[8], LOW);
    }
}

TEST_F(FramTest, WriteProtectPinFollowsEnableAndDisable) {
    FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 7);

    EXPECT_EQ(fram.enableWP(), ERROR_SUCCESS);
    EXPECT_TRUE(fram.getWPStatus());
    EXPECT_EQ(gpio.levels[7], HIGH);

    EXPECT_EQ(fram.disableWP(), ERROR_SUCCESS);
    EXPECT_FALSE(fram.getWPStatus());
    EXPECT_EQ(gpio.levels[7], LOW);
}

#else

TEST_F(FramTest, WriteProtectPinIsNotTouchedWhenUnmanaged) {
    FRAM_MB85RC_I2C automatic(&Wire, 0x50, true, 7);
    FRAM_MB85RC_I2C manual(&Wire, 0x50, true, 7, 64);

    EXPECT_FALSE(automatic.getWPStatus());
    EXPECT_FALSE(manual.getWPStatus());
    EXPECT_EQ(automatic.enableWP(), ERROR_NOT_PERMITTED);
    EXPECT_EQ(automatic.disableWP(), ERROR_NOT_PERMITTED);
    EXPECT_FALSE(automatic.getWPStatus());
    EXPECT_THAT(gpio.events, IsEmpty());
}

#endif

// =================================================================================================
// Debug output
// =================================================================================================
#if defined(SERIAL_DEBUG) && (SERIAL_DEBUG == 1)

TEST_F(FramTest, DebugOutputForDetectedChip) {
    FakeFram chip(256);
    chip.setDeviceId(FUJITSU_MANUFACT_ID, 0x510);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 7);
    fram.begin();

    EXPECT_EQ(Serial.output,
              "FRAM_MB85RC_I2C object created\n"
              "I2C device address 0x50\n"
              "WP pin number 7\n"
              "Write protect management: true\n"
              "Memory Chip initialized\n"
              "FRAM Device IDs\n"
              "Manufacturer 0xA\n"
              "ProductID 0x510\n"
              "Density code 0x5\n"
              "Density 256K\n"
              "Device identfied automatically\n"
              "...... ...... ......\n"
              "...... ...... ......\n");
    EXPECT_EQ(Serial.baud, 0u) << "An open serial port must not be reconfigured";

    Serial.output.clear();
    EXPECT_EQ(fram.writeByte(0, 0), ERROR_SUCCESS);
    EXPECT_EQ(Serial.output, "Calculated address 0x50\n");
}

TEST_F(FramTest, DebugOutputForManualMode) {
    FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 7, 16);
    fram.begin();

    EXPECT_THAT(Serial.output, HasSubstr("Memory Chip initialized\n"));
    EXPECT_THAT(Serial.output, HasSubstr("Manufacturer 0xF00\n"));
    EXPECT_THAT(Serial.output, HasSubstr("Density 16K\n"));
    EXPECT_THAT(Serial.output, HasSubstr("Device properties set\n"));

    Serial.output.clear();
    EXPECT_EQ(fram.writeByte(0x750, 0), ERROR_NACK_ON_ADDRESS);  // empty bus
    EXPECT_EQ(Serial.output, "Calculated address 0x57\n");
}

TEST_F(FramTest, DebugOutputForMissingChip) {
    FRAM_MB85RC_I2C fram(&Wire);
    fram.begin();

    EXPECT_THAT(Serial.output, HasSubstr("Memory Chip NOT FOUND\n"));
    EXPECT_THAT(Serial.output, Not(HasSubstr("FRAM Device IDs")));
}

TEST_F(FramTest, DebugOpensTheSerialPortIfRequired) {
    Serial.ready = false;
    FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 7, 64);
    fram.begin();

    EXPECT_EQ(Serial.baud, 9600u);
    EXPECT_THAT(Serial.output, HasSubstr("Memory Chip initialized\n"));
}

TEST_F(FramTest, DebugIsSilentWithoutSerialPort) {
    FakeFram chip(64);
    Wire.device = &chip;
    Serial.ready = false;
    Serial.canBegin = false;
    FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 7, 64);

    fram.begin();
    EXPECT_TRUE(fram.isReady());
    EXPECT_EQ(fram.writeByte(0, 0x12), ERROR_SUCCESS);
    EXPECT_EQ(fram.eraseDevice(), ERROR_SUCCESS);

    EXPECT_EQ(Serial.baud, 9600u);
    EXPECT_EQ(Serial.output, "");
    EXPECT_EQ(chip.memory[0], 0x00);
}

TEST_F(FramTest, DebugCopesWithSerialPortDisappearing) {
    // The port passes the checks in begin(), but is gone when the IDs are about to be printed
    Serial.readyChecks = 2;
    FRAM_MB85RC_I2C fram(&Wire, 0x50, false, 7, 64);
    fram.begin();

    EXPECT_TRUE(fram.isReady());
    EXPECT_THAT(Serial.output, HasSubstr("Memory Chip initialized\n"));
    EXPECT_THAT(Serial.output, Not(HasSubstr("FRAM Device IDs")));
}

#else

TEST_F(FramTest, NoDebugOutputByDefault) {
    FakeFram chip(256);
    chip.setDeviceId(FUJITSU_MANUFACT_ID, 0x510);
    Wire.device = &chip;
    FRAM_MB85RC_I2C fram(&Wire);
    fram.begin();

    EXPECT_EQ(fram.writeByte(0, 0), ERROR_SUCCESS);
    EXPECT_EQ(fram.eraseDevice(), ERROR_SUCCESS);
    EXPECT_EQ(Serial.output, "");
    EXPECT_EQ(Serial.baud, 0u);
}

#endif

}  // namespace
