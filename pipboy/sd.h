// Read-only SD access over SPI with a minimal FAT16/FAT32 reader. Nothing here can write to the
// card. Only the root directory is indexed because that is where the screen files live.
#pragma once
#include <avr/io.h>

#define SD_CS_BIT 2

static uint8_t sector[512];
static bool sdHighCapacity;
static uint32_t fatStart, dataStart, rootStart, rootCluster;
static uint16_t rootEntries;
static uint8_t clusterSectors;
static bool fat32;

static uint8_t spi(uint8_t value) {
  SPDR = value;
  while (!(SPSR & _BV(SPIF))) {}
  return SPDR;
}

static void sdDeselect() { PORTB |= _BV(SD_CS_BIT); spi(0xFF); }

static uint8_t sdCommand(uint8_t command, uint32_t argument) {
  PORTB &= ~_BV(SD_CS_BIT);
  spi(0xFF);
  spi(0x40 | command);
  spi(argument >> 24); spi(argument >> 16); spi(argument >> 8); spi(argument);
  spi(command == 0 ? 0x95 : command == 8 ? 0x87 : 0x01);
  uint8_t response;
  for (uint8_t i = 0; i < 10 && ((response = spi(0xFF)) & 0x80); i++) {}
  return response;
}

static bool sdReadSector(uint32_t lba) {
  if (sdCommand(17, sdHighCapacity ? lba : lba << 9)) { sdDeselect(); return false; }
  uint16_t timeout = 0;
  uint8_t token;
  while ((token = spi(0xFF)) == 0xFF && ++timeout) {}
  if (token != 0xFE) { sdDeselect(); return false; }
  for (uint16_t i = 0; i < 512; i++) sector[i] = spi(0xFF);
  spi(0xFF); spi(0xFF);
  sdDeselect();
  return true;
}

static bool sdBegin() {
  DDRB |= _BV(SD_CS_BIT) | _BV(3) | _BV(5);
  DDRB &= ~_BV(4);
  PORTB |= _BV(SD_CS_BIT);
  SPCR = _BV(SPE) | _BV(MSTR) | _BV(SPR1) | _BV(SPR0);   // 125 kHz while initialising
  SPSR = 0;
  for (uint8_t i = 0; i < 10; i++) spi(0xFF);
  uint16_t tries = 0;
  while (sdCommand(0, 0) != 0x01) { if (++tries > 200) { sdDeselect(); return false; } }
  bool version2 = false;
  if (sdCommand(8, 0x1AA) == 0x01) {
    for (uint8_t i = 0; i < 4; i++) spi(0xFF);
    version2 = true;
  }
  tries = 0;
  for (;;) {
    sdCommand(55, 0);
    if (sdCommand(41, version2 ? 0x40000000UL : 0) == 0) break;
    if (++tries > 2000) { sdDeselect(); return false; }
    _delay_ms(1);
  }
  sdHighCapacity = false;
  if (version2 && sdCommand(58, 0) == 0) {
    sdHighCapacity = spi(0xFF) & 0x40;
    spi(0xFF); spi(0xFF); spi(0xFF);
  }
  sdCommand(16, 512);
  sdDeselect();
  SPCR = _BV(SPE) | _BV(MSTR);   // full speed, 8 MHz
  SPSR = _BV(SPI2X);
  return true;
}

static inline uint16_t le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static inline uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }

static bool fatBegin() {
  if (!sdReadSector(0)) return false;
  uint32_t volume = 0;
  if (sector[510] != 0x55 || sector[511] != 0xAA) return false;
  if (!(sector[0] == 0xEB || sector[0] == 0xE9)) {
    volume = le32(&sector[0x1C6]);
    if (!sdReadSector(volume)) return false;
  }
  if (le16(&sector[11]) != 512) return false;
  clusterSectors = sector[13];
  uint16_t reserved = le16(&sector[14]);
  uint8_t fats = sector[16];
  rootEntries = le16(&sector[17]);
  uint32_t fatSize = le16(&sector[22]);
  if (!fatSize) fatSize = le32(&sector[36]);
  fat32 = rootEntries == 0;
  rootCluster = le32(&sector[44]);
  fatStart = volume + reserved;
  rootStart = fatStart + fats * fatSize;
  dataStart = rootStart + (rootEntries * 32 + 511) / 512;
  return clusterSectors != 0;
}

static uint32_t clusterSector(uint32_t cluster) { return dataStart + (cluster - 2) * clusterSectors; }

static uint32_t nextCluster(uint32_t cluster) {
  uint32_t offset = fat32 ? cluster * 4 : cluster * 2;
  if (!sdReadSector(fatStart + offset / 512)) return 0x0FFFFFFF;
  uint16_t at = offset % 512;
  if (fat32) return le32(&sector[at]) & 0x0FFFFFFF;
  uint16_t value = le16(&sector[at]);
  return value >= 0xFFF8 ? 0x0FFFFFFF : value;
}

// PIPART/ARTnn.PIP pictures go in slot nn, PIPART/LAYnn.PIP layouts in slot LAYOUT_SLOT + nn.
#define LAYOUT_SLOT 15
#define FILE_SLOTS 33
struct FileEntry {
  uint32_t cluster;
  uint16_t size;
};
static FileEntry files[FILE_SLOTS];
static uint32_t artFolder;

// Returns false at the end of the directory.
static bool scanDirectorySector(bool root) {
  for (uint16_t at = 0; at < 512; at += 32) {
    uint8_t *entry = &sector[at];
    if (entry[0] == 0) return false;
    if (entry[0] == 0xE5 || (entry[11] & 0x0F) == 0x0F) continue;
    uint32_t cluster = le16(&entry[26]) | (uint32_t)le16(&entry[20]) << 16;
    if (root) {
      if ((entry[11] & 0x10) && !memcmp_P(entry, PSTR("PIPART     "), 11)) artFolder = cluster;
      continue;
    }
    if (memcmp_P(entry + 5, PSTR("   PIP"), 6)) continue;
    uint8_t slot = (entry[3] - '0') * 10 + entry[4] - '0';
    if (!memcmp_P(entry, PSTR("LAY"), 3)) slot += LAYOUT_SLOT;
    else if (memcmp_P(entry, PSTR("ART"), 3)) continue;
    if (slot >= FILE_SLOTS) continue;
    files[slot].cluster = cluster;
    files[slot].size = le32(&entry[28]);
  }
  return true;
}

static void scanCluster(uint32_t cluster, bool root) {
  for (; cluster >= 2 && cluster < 0x0FFFFFF8; cluster = nextCluster(cluster)) {
    for (uint8_t s = 0; s < clusterSectors; s++) {
      if (!sdReadSector(clusterSector(cluster) + s) || !scanDirectorySector(root)) return;
    }
  }
}

static uint8_t indexFiles() {
  memset(files, 0, sizeof(files));
  artFolder = 0;
  if (fat32) {
    scanCluster(rootCluster, true);
  } else {
    for (uint16_t s = 0; s < (rootEntries * 32 + 511) / 512; s++) {
      if (!sdReadSector(rootStart + s) || !scanDirectorySector(true)) break;
    }
  }
  if (artFolder) scanCluster(artFolder, false);
  uint8_t found = 0;
  for (uint8_t i = 0; i < FILE_SLOTS; i++) found += files[i].size != 0;
  return found;
}

// Streaming reader over one file.
static uint32_t streamCluster;
static uint16_t streamLeft, streamAt;
static uint8_t streamSectorInCluster;

static bool streamOpen(uint8_t slot) {
  if (slot >= FILE_SLOTS || !files[slot].size) return false;
  streamCluster = files[slot].cluster;
  streamLeft = files[slot].size;
  streamSectorInCluster = 0;
  streamAt = 512;
  return true;
}

static int16_t streamByte() {
  if (!streamLeft) return -1;
  if (streamAt == 512) {
    if (streamSectorInCluster == clusterSectors) {
      streamCluster = nextCluster(streamCluster);
      streamSectorInCluster = 0;
      if (streamCluster < 2 || streamCluster >= 0x0FFFFFF8) { streamLeft = 0; return -1; }
    }
    if (!sdReadSector(clusterSector(streamCluster) + streamSectorInCluster++)) { streamLeft = 0; return -1; }
    streamAt = 0;
  }
  streamLeft--;
  return sector[streamAt++];
}
