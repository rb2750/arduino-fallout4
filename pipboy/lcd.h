// ILI9486 on the Uno 8-bit shield. Control lines on PORTC: RD A0, WR A1, CD A2, CS A3, RST A4.
// Data D0-D1 on PB0-PB1 and D2-D7 on PD2-PD7. Writing PORTB and PORTD must preserve the SPI
// and UART bits that share those ports.
#pragma once
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>
#include "font.h"

#define LCD_W 480
#define LCD_H 320

#define RD_BIT 0
#define WR_BIT 1
#define CD_BIT 2
#define CS_BIT 3
#define RST_BIT 4

// One cycle (62.5 ns) low, inside the ILI9486 write timing. Checked by reading the panel back.
#define WR_STROBE() do { PINC = _BV(WR_BIT); PINC = _BV(WR_BIT); } while (0)

static inline void lcdWrite8(uint8_t value) {
  PORTB = (PORTB & 0xFC) | (value & 0x03);
  PORTD = (PORTD & 0x03) | (value & 0xFC);
  WR_STROBE();
}

static inline void lcdCommand(uint8_t command) {
  PORTC &= ~_BV(CD_BIT);
  lcdWrite8(command);
  PORTC |= _BV(CD_BIT);
}

static void lcdSelect() { PORTC &= ~_BV(CS_BIT); }
static void lcdDeselect() { PORTC |= _BV(CS_BIT); }

static void lcdPinsToOutput() {
  DDRC |= 0x1F;
  DDRD |= 0xFC;
  DDRB |= 0x03;
  PORTC |= _BV(RD_BIT) | _BV(WR_BIT) | _BV(CD_BIT) | _BV(RST_BIT);
}

static const uint8_t LCD_INIT[] PROGMEM = {
  0x01, 0, 0xFF,                      // soft reset, then wait
  0x28, 0,
  0x3A, 1, 0x55,                      // 16 bit pixels
  0xC0, 2, 0x0D, 0x0D,
  0xC1, 2, 0x43, 0x00,
  0xC2, 1, 0x00,
  0xC5, 4, 0x00, 0x48, 0x00, 0x48,
  0xB4, 1, 0x00,
  0xB6, 3, 0x02, 0x02, 0x3B,
  0xE0, 15, 0x0F, 0x21, 0x1C, 0x0B, 0x0E, 0x08, 0x49, 0x98, 0x38, 0x09, 0x11, 0x03, 0x14, 0x10, 0x00,
  0xE1, 15, 0x0F, 0x2F, 0x2B, 0x0C, 0x0E, 0x06, 0x47, 0x76, 0x37, 0x07, 0x11, 0x04, 0x23, 0x1E, 0x00,
  0x20, 0,                            // inversion off
  0x11, 0, 0xFF,                      // sleep out, then wait
  0x29, 0,
};

static uint8_t lcdMadctl;

static void lcdSetFlip(bool flip) {
  // Values from MCUFRIEND_kbv setRotation(1) and setRotation(3) for the 9486.
  lcdMadctl = flip ? 0xF8 : 0x28;
  lcdCommand(0x36);
  lcdWrite8(lcdMadctl);
}

static void lcdBegin(bool flip) {
  lcdPinsToOutput();
  lcdDeselect();
  PORTC &= ~_BV(RST_BIT);
  _delay_ms(5);
  PORTC |= _BV(RST_BIT);
  _delay_ms(120);
  lcdSelect();
  const uint8_t *p = LCD_INIT;
  const uint8_t *end = LCD_INIT + sizeof(LCD_INIT);
  while (p < end) {
    uint8_t command = pgm_read_byte(p++);
    uint8_t count = pgm_read_byte(p++);
    lcdCommand(command);
    while (count--) lcdWrite8(pgm_read_byte(p++));
    if (p < end && pgm_read_byte(p) == 0xFF) {
      p++;
      _delay_ms(150);
    }
  }
  lcdSetFlip(flip);
  // Whole panel as the scroll area so the sync jitter effect can shift it.
  lcdCommand(0x33);
  lcdWrite8(0); lcdWrite8(0);
  lcdWrite8(LCD_W >> 8); lcdWrite8(LCD_W & 0xFF);
  lcdWrite8(0); lcdWrite8(0);
}

static void lcdScroll(uint16_t offset) {
  lcdCommand(0x37);
  lcdWrite8(offset >> 8);
  lcdWrite8(offset);
}

static void lcdWindow(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  lcdCommand(0x2A);
  lcdWrite8(x0 >> 8); lcdWrite8(x0); lcdWrite8(x1 >> 8); lcdWrite8(x1);
  lcdCommand(0x2B);
  lcdWrite8(y0 >> 8); lcdWrite8(y0); lcdWrite8(y1 >> 8); lcdWrite8(y1);
  lcdCommand(0x2C);
}

static void lcdPush(uint16_t color, uint32_t count) {
  uint8_t high = color >> 8, low = color;
  uint8_t portDHigh = (PORTD & 0x03) | (high & 0xFC), portBHigh = (PORTB & 0xFC) | (high & 0x03);
  if (high == low) {
    // Same byte twice per pixel: set the bus once and only strobe.
    PORTD = portDHigh;
    PORTB = portBHigh;
    // A full screen is 153600 pixels, so the block count fits 16 bits.
    uint16_t blocks = count >> 3;
    while (blocks--) {
      WR_STROBE(); WR_STROBE(); WR_STROBE(); WR_STROBE(); WR_STROBE(); WR_STROBE(); WR_STROBE(); WR_STROBE();
      WR_STROBE(); WR_STROBE(); WR_STROBE(); WR_STROBE(); WR_STROBE(); WR_STROBE(); WR_STROBE(); WR_STROBE();
    }
    uint8_t rest = count & 7;
    while (rest--) { WR_STROBE(); WR_STROBE(); }
    return;
  }
  uint8_t portDLow = (PORTD & 0x03) | (low & 0xFC), portBLow = (PORTB & 0xFC) | (low & 0x03);
  while (count--) {
    PORTD = portDHigh; PORTB = portBHigh; WR_STROBE();
    PORTD = portDLow; PORTB = portBLow; WR_STROBE();
  }
}

static inline void lcdPixel(int16_t x, int16_t y, uint16_t color) {
  if ((uint16_t)x >= LCD_W || (uint16_t)y >= LCD_H) return;
  lcdWindow(x, y, x, y);
  lcdWrite8(color >> 8);
  lcdWrite8(color);
}

static void lcdFill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;
  lcdWindow(x, y, x + w - 1, y + h - 1);
  lcdPush(color, (uint32_t)w * h);
}

// Reads back what the panel holds. Used by the serial screenshot command to verify drawing.
static uint8_t lcdReadByte() {
  PORTC &= ~_BV(RD_BIT);
  _delay_us(0.5);
  uint8_t value = (PIND & 0xFC) | (PINB & 0x03);
  PORTC |= _BV(RD_BIT);
  return value;
}

static void lcdBeginRead(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  lcdWindow(x0, y0, x1, y1);
  lcdCommand(0x2E);
  DDRD &= ~0xFC;
  DDRB &= ~0x03;
  lcdReadByte();
}

static void lcdEndRead() {
  DDRD |= 0xFC;
  DDRB |= 0x03;
}
