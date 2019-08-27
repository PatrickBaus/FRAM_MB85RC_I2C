/**************************************************************************/
/*!
    @file     FRAM_MB85RC_I2C.h
    @author   SOSAndroid.fr (E. Ha.)

    @section  HISTORY

    v1.0 - First release
    v1.0.1 - Robustness enhancement
    v1.0.2 - fix constructor, introducing byte move in memory
    v1.0.3 - fix writeLong() function
    v1.0.4 - fix constructor call error
    v1.0.5 - Enlarge density chip support by making check more flexible, Error codes not anymore hardcoded, add connect example, add Cypress FM24 & CY15B series comment.
    v1.1.0b - adding support for devices without device IDs + 4K & 16 K devices support
    v1.2.0 - Uses reinterpret_cast instead of bit shift / masking for performance. Breaks backward compatibility with previous code - See PR#6
    v1.2.1 - Fix comment line #76 (issue #11), max address define statement for 512K & 1M chips (issue 13)
    v1.2.2 - Fix issue #16

    Driver for the MB85RC I2C FRAM from Fujitsu.

    @section LICENSE

    Software License Agreement (BSD License)

    Copyright (c) 2013, SOSAndroid.fr (E. Ha.)
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holders nor the
    names of its contributors may be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
    EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/**************************************************************************/
#ifndef _FRAM_MB85RC_I2C_H_
#define _FRAM_MB85RC_I2C_H_

#if ARDUINO >= 100
 #include <Arduino.h>
#else
 #include <WProgram.h>
#endif

#include <Wire.h>
#include "FRAM_defines.h"

// Enabling debug I2C - comment to disable / normal operations
#ifndef SERIAL_DEBUG
#define SERIAL_DEBUG 1
#endif

class FRAM_MB85RC_I2C {
 public:
  FRAM_MB85RC_I2C(void);
  FRAM_MB85RC_I2C(uint8_t address, boolean wp);
  FRAM_MB85RC_I2C(uint8_t address, boolean wp, int pin);
  FRAM_MB85RC_I2C(uint8_t address, boolean wp, int pin, uint16_t chipDensity);

  void  begin(void);
  byte checkDevice(void);
  byte readBit(uint16_t framAddr, uint8_t bitNb, byte *bit);
  byte setOneBit(uint16_t framAddr, uint8_t bitNb);
  byte clearOneBit(uint16_t framAddr, uint8_t bitNb);
  byte toggleBit(uint16_t framAddr, uint8_t bitNb);
  byte readArray (uint16_t framAddr, byte items, uint8_t value[]);
  byte writeArray (uint16_t framAddr, byte items, uint8_t value[]);
  byte readByte (uint16_t framAddr, uint8_t *value);
  byte writeByte (uint16_t framAddr, uint8_t value);
  byte copyByte (uint16_t origAddr, uint16_t destAddr);
  byte readWord(uint16_t framAddr, uint16_t *value);
  byte writeWord(uint16_t framAddr, uint16_t value);
  byte readLong(uint16_t framAddr, uint32_t *value);
  byte writeLong(uint16_t framAddr, uint32_t value);
  byte getOneDeviceID(uint8_t idType, uint16_t *id);
  boolean isReady(void);
  boolean getWPStatus(void);
  byte enableWP(void);
  byte disableWP(void);
  byte eraseDevice(void);

 private:
  uint8_t i2c_addr;
  boolean _framInitialised;
  boolean _manualMode;
  uint16_t manufacturer;
  uint16_t productid;
  uint16_t densitycode;
  uint16_t density;
  uint16_t maxaddress;

  int wpPin;
  boolean wpStatus;

  byte getDeviceIDs(void);
  byte setDeviceIDs(void);
  byte initWP(boolean wp);
  byte deviceIDs2Serial(void);
  void I2CAddressAdapt(uint16_t framAddr);
};

#endif
