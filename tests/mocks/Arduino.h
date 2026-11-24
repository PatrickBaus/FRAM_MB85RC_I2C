// Minimal host-side stand-in for the Arduino core, used by the unit tests only.
#ifndef FRAM_TEST_ARDUINO_H_
#define FRAM_TEST_ARDUINO_H_

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef ARDUINO
#define ARDUINO 10819
#endif

typedef uint8_t byte;
typedef bool boolean;

#define HIGH 0x1
#define LOW 0x0
#define INPUT 0x0
#define OUTPUT 0x1

#define DEC 10
#define HEX 16

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))

// Records everything the code under test does to the GPIOs
struct GpioState {
    struct Event {
        enum Type { PIN_MODE, DIGITAL_WRITE } type;
        int pin;
        int value;
    };
    std::vector<Event> events;
    std::map<int, int> modes;
    std::map<int, int> levels;

    void reset() {
        events.clear();
        modes.clear();
        levels.clear();
    }
};
extern GpioState gpio;

void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);

// Captures everything printed by the code under test
class FakeSerial {
   public:
    bool ready = true;        // What `if (Serial)` evaluates to
    mutable int readyChecks = -1;  // If >= 0: number of `if (Serial)` checks until the port disappears
    bool canBegin = true;     // Whether begin() brings the port up
    unsigned long baud = 0;
    std::string output;

    void reset() {
        ready = true;
        readyChecks = -1;
        canBegin = true;
        baud = 0;
        output.clear();
    }

    void begin(unsigned long baudrate) {
        baud = baudrate;
        ready = canBegin;
    }
    explicit operator bool() const {
        if (readyChecks == 0) {
            return false;
        }
        if (readyChecks > 0) {
            readyChecks--;
        }
        return ready;
    }

    void print(const char* text) { output += text; }
    void print(long value, int base = DEC) {
        std::ostringstream stream;
        if (base == HEX) {
            stream << std::uppercase << std::hex;
        }
        stream << value;
        output += stream.str();
    }
    template <typename T>
    void println(T value) {
        print(value);
        output += "\n";
    }
    template <typename T>
    void println(T value, int base) {
        print(value, base);
        output += "\n";
    }
};
extern FakeSerial Serial;

#endif
