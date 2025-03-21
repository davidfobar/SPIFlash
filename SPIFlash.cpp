// Copyright (c) 2013-2015 by Felix Rusu, LowPowerLab.com
// SPI Flash memory library for arduino/moteino.
// This works with 256byte/page SPI flash memory
// For instance a 4MBit (512Kbyte) flash chip will have 2048 pages: 256*2048 = 524288 bytes (512Kbytes)
// Minimal modifications should allow chips that have different page size but modifications
// DEPENDS ON: Arduino SPI library
// > Updated Jan. 5, 2015, TomWS1, modified writeBytes to allow blocks > 256 bytes and handle page misalignment.
// > Updated Feb. 26, 2015 TomWS1, added support for SPI Transactions (Arduino 1.5.8 and above)
// > Selective merge by Felix after testing in IDE 1.0.6, 1.6.4
// **********************************************************************************
// License
// **********************************************************************************
// This program is free software; you can redistribute it 
// and/or modify it under the terms of the GNU General    
// Public License as published by the Free Software       
// Foundation; either version 3 of the License, or        
// (at your option) any later version.                    
//                                                        
// This program is distributed in the hope that it will   
// be useful, but WITHOUT ANY WARRANTY; without even the  
// implied warranty of MERCHANTABILITY or FITNESS FOR A   
// PARTICULAR PURPOSE. See the GNU General Public        
// License for more details.                              
//                                                        
// You should have received a copy of the GNU General    
// Public License along with this program.
// If not, see <http://www.gnu.org/licenses/>.
//                                                        
// Licence can be viewed at                               
// http://www.gnu.org/licenses/gpl-3.0.txt
//
// Please maintain this license information along with authorship
// and copyright notices in any redistribution of this code

#include <SPIFlash.h>

uint8_t SPIFlash::UNIQUEID[8];

/// IMPORTANT: NAND FLASH memory requires erase before write, because
///            it can only transition from 1s to 0s and only the erase command can reset all 0s to 1s
///            See http://en.wikipedia.org/wiki/Flash_memory
///            The smallest range that can be erased is a sector (4K, 32K, 64K); there is also a chip erase command

/// IMPORTANT: When flash chip is powered down, aka sleeping, the only command it will respond to is 
///            Release Power-down / Device ID (ABh), per section 8.2.19 of the W25X40CL datasheet.
///            This means after using the sleep() function of this library, wake() must be the first
///            function called. If other commands are used, the flash chip will ignore the commands. 

/// Constructor. JedecID is optional but recommended, since this will ensure that the device is present and has a valid response
/// get this from the datasheet of your flash chip
/// Example for Atmel-Adesto 4Mbit AT25DF041A: 0x1F44 (page 27: http://www.adestotech.com/sites/default/files/datasheets/doc3668.pdf)
/// Example for Winbond 4Mbit W25X40CL: 0xEF30 (page 14: http://www.winbond.com/NR/rdonlyres/6E25084C-0BFE-4B25-903D-AE10221A0929/0/W25X40CL.pdf)
SPIFlash::SPIFlash(uint8_t slaveSelectPin, SPIClass *spi, SPISettings settings, uint16_t jedecID) {
  _slaveSelectPin = slaveSelectPin;
  _jedecID = jedecID;
  _spi = spi;
  _settings = settings;
}

/// Select the flash chip
void SPIFlash::select() {
  _spi->beginTransaction(_settings);
  digitalWrite(_slaveSelectPin, LOW);
}

/// UNselect the flash chip
void SPIFlash::unselect() {
  digitalWrite(_slaveSelectPin, HIGH);
_spi->endTransaction();
}

/// setup SPI, read device ID etc...
bool SPIFlash::initialize() {

  pinMode(_slaveSelectPin, OUTPUT);
  _spi->begin();

  unselect();
  wakeup();
  
  if (_jedecID == 0 || readDeviceId() == _jedecID) {
    command(SPIFLASH_STATUSWRITE, true); // Write Status Register
    _spi->transfer(0);                     // Global Unprotect
    unselect();
    return true;
  }
  return false;
}

/// Get the manufacturer and device ID bytes (as a short word)
uint16_t SPIFlash::readDeviceId() {
  select();
  _spi->transfer(SPIFLASH_IDREAD);
  uint16_t jedecid = _spi->transfer(0) << 8;
  jedecid |= _spi->transfer(0);
  unselect();
  return jedecid;
}

/// Get the 64 bit unique identifier, stores it in UNIQUEID[8]. Only needs to be called once, ie after initialize
/// Returns the byte pointer to the UNIQUEID byte array
/// Read UNIQUEID like this:
/// flash.readUniqueId(); for (uint8_t i=0;i<8;i++) { Serial.print(flash.UNIQUEID[i], HEX); Serial.print(' '); }
/// or like this:
/// flash.readUniqueId(); uint8_t* MAC = flash.readUniqueId(); for (uint8_t i=0;i<8;i++) { Serial.print(MAC[i], HEX); Serial.print(' '); }
uint8_t* SPIFlash::readUniqueId()
{
  command(SPIFLASH_MACREAD);
  _spi->transfer(0);
  _spi->transfer(0);
  _spi->transfer(0);
  _spi->transfer(0);
  for (uint8_t i=0;i<8;i++)
    UNIQUEID[i] = _spi->transfer(0);
  unselect();
  return UNIQUEID;
}

/// read 1 byte from flash memory
uint8_t SPIFlash::readByte(uint32_t addr) {
  command(SPIFLASH_ARRAYREADLOWFREQ);
  _spi->transfer(addr >> 16);
  _spi->transfer(addr >> 8);
  _spi->transfer(addr);
  uint8_t result = _spi->transfer(0);
  unselect();
  return result;
}

/// read unlimited # of bytes
void SPIFlash::readBytes(uint32_t addr, void* buf, uint16_t len) {
  command(SPIFLASH_ARRAYREAD);
  _spi->transfer(addr >> 16);
  _spi->transfer(addr >> 8);
  _spi->transfer(addr);
  _spi->transfer(0); //"dont care"
  for (uint16_t i = 0; i < len; ++i)
    ((uint8_t*) buf)[i] = _spi->transfer(0);
  unselect();
}

/// Send a command to the flash chip, pass TRUE for isWrite when its a write command
void SPIFlash::command(uint8_t cmd, bool isWrite) {

  if (isWrite) {
    command(SPIFLASH_WRITEENABLE); // Write Enable
    unselect();
  }
  //  wait for any write/erase to complete
  //  a time limit cannot really be added here without it being a very large safe limit
  //  that is because some chips can take several seconds to carry out a chip erase or other similar multi block or entire-chip operations
  //  
  //  Note: If the MISO line is high, busy() will return true. 
  //        This can be a problem and cause the code to hang when there is noise/static on MISO data line when:
  //        1) There is no flash chip connected
  //        2) The flash chip connected is powered down, aka sleeping. 
  if (cmd != SPIFLASH_WAKE) while(busy());
  select();
  _spi->transfer(cmd);
}

/// check if the chip is busy erasing/writing
bool SPIFlash::busy() {
  /*
  select();
  SPI.transfer(SPIFLASH_STATUSREAD);
  uint8_t status = SPI.transfer(0);
  unselect();
  return status & 1;
  */
  return readStatus() & 1;
}

/// return the STATUS register
uint8_t SPIFlash::readStatus() {
  select();
  _spi->transfer(SPIFLASH_STATUSREAD);
  uint8_t status = _spi->transfer(0);
  unselect();
  return status;
}


/// Write 1 byte to flash memory
/// WARNING: you can only write to previously erased memory locations (see datasheet)
///          use the block erase commands to first clear memory (write 0xFFs)
void SPIFlash::writeByte(uint32_t addr, uint8_t byt) {
  command(SPIFLASH_BYTEPAGEPROGRAM, true);  // Byte/Page Program
  _spi->transfer(addr >> 16);
  _spi->transfer(addr >> 8);
  _spi->transfer(addr);
  _spi->transfer(byt);
  unselect();
}

/// write multiple bytes to flash memory (up to 64K)
/// WARNING: you can only write to previously erased memory locations (see datasheet)
///          use the block erase commands to first clear memory (write 0xFFs)
/// This version handles both page alignment and data blocks larger than 256 bytes.
///
void SPIFlash::writeBytes(uint32_t addr, const void* buf, uint16_t len) {
  uint16_t n;
  uint16_t maxBytes = 256-(addr%256);  // force the first set of bytes to stay within the first page
  uint16_t offset = 0;
  while (len>0)
  {
    n = (len<=maxBytes) ? len : maxBytes;
    command(SPIFLASH_BYTEPAGEPROGRAM, true);  // Byte/Page Program
    _spi->transfer(addr >> 16);
    _spi->transfer(addr >> 8);
    _spi->transfer(addr);
    
    for (uint16_t i = 0; i < n; i++) {
      _spi->transfer(((uint8_t*) buf)[offset + i]);
    }
    unselect();
    
    addr+=n;  // adjust the addresses and remaining bytes by what we've just transferred.
    offset +=n;
    len -= n;
    maxBytes = 256;   // now we can do up to 256 bytes per loop
  }
}

/// erase entire flash memory array
/// may take several seconds depending on size, but is non blocking
/// so you may wait for this to complete using busy() or continue doing
/// other things and later check if the chip is done with busy()
/// note that any command will first wait for chip to become available using busy()
/// so no need to do that twice
void SPIFlash::chipErase() {
  command(SPIFLASH_CHIPERASE, true);
  unselect();
}

/// erase a 4Kbyte block
void SPIFlash::blockErase4K(uint32_t addr) {
  command(SPIFLASH_BLOCKERASE_4K, true); // Block Erase
  _spi->transfer(addr >> 16);
  _spi->transfer(addr >> 8);
  _spi->transfer(addr);
  unselect();
}

/// erase a 32Kbyte block
void SPIFlash::blockErase32K(uint32_t addr) {
  command(SPIFLASH_BLOCKERASE_32K, true); // Block Erase
  _spi->transfer(addr >> 16);
  _spi->transfer(addr >> 8);
  _spi->transfer(addr);
  unselect();
}

/// erase a 64Kbyte block
void SPIFlash::blockErase64K(uint32_t addr) {
  command(SPIFLASH_BLOCKERASE_64K, true); // Block Erase
  _spi->transfer(addr >> 16);
  _spi->transfer(addr >> 8);
  _spi->transfer(addr);
  unselect();
}

/// found() - checks there is a FLASH chip by checking the deviceID repeatedly - should be a consistent value
uint8_t SPIFlash::found() {
  uint16_t deviceID=0;
  wakeup(); //if sleep() was previously called, wakeup() is required or it's non responsive
  for (uint8_t i=0;i<10;i++) {
    uint16_t idNow = readDeviceId();
    if (idNow==0 || idNow==0xffff || (i>0 && idNow != deviceID)) {
      deviceID=0;
      break;
    }
    deviceID=idNow;
  }
  if (deviceID==0) { //NO FLASH CHIP FOUND, ABORTING
    return false;
  }
  return true;
}

///regionIsEmpty() - check a random flashmem byte array is all clear and can be written to (ie. it's all 0xff)
uint8_t SPIFlash::regionIsEmpty(uint32_t startAddress, uint8_t length) {
  uint8_t flashBuf[length];
  readBytes(startAddress, flashBuf, length);
  for (uint8_t i=0;i<length;i++) if (flashBuf[i]!=0xff) return false;
  return true;
}

/// Put flash memory chip into power down mode
/// WARNING: after this command, only the WAKEUP and DEVICE_ID commands are recognized
/// hence a wakeup() command should be invoked first before further operations
/// If a MCU soft restart is possible with flash chip left in sleep(), then a wakeup() command
///   should always be invoked before any other commands to ensure the flash chip was not left in sleep
void SPIFlash::sleep() {
  command(SPIFLASH_SLEEP);
  unselect();
}

/// Wake flash memory from power down mode
/// NOTE: this command is required after a sleep() command is used, or no other commands will be recognized
/// If a MCU soft restart is possible with flash chip left in sleep(), then a wakeup() command
///   should always be invoked before any other commands to ensure the flash chip was not left in sleep
void SPIFlash::wakeup() {
  command(SPIFLASH_WAKE);
  unselect();
}

/// cleanup
void SPIFlash::end() {
  _spi->end();
}