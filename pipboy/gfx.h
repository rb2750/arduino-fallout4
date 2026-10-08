#pragma once
#include "lcd.h"

#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

// The four display colours Fallout 3 offers. Green is the default Pip-Boy colour, amber is the
// FalloutPrefs.ini "Default Amber" value (0xFFB642).
// The scanline byte makes a dark tint whose two RGB565 bytes match, which keeps clears on the
// fast fill path.
struct Theme {
  uint8_t r, g, b, scan;
};
static const Theme THEMES[] PROGMEM = {
  {26, 255, 128, 0x01},
  {255, 182, 66, 0x20},
  {46, 207, 255, 0x09},
  {192, 255, 255, 0x09},
};
static const char THEME_NAMES[] PROGMEM = "GREEN\0AMBER\0BLUE \0WHITE";
#define THEME_COUNT 4

static uint16_t colorBright, colorDim, colorFaint, colorScan;
static bool scanlines = true;

static uint16_t shade(const Theme &t, uint8_t percent) {
  return RGB565(t.r * percent / 100, t.g * percent / 100, t.b * percent / 100);
}

static void setTheme(uint8_t index) {
  Theme t;
  memcpy_P(&t, &THEMES[index % THEME_COUNT], sizeof(t));
  colorBright = shade(t, 100);
  colorDim = shade(t, 45);
  colorFaint = shade(t, 22);
  colorScan = t.scan << 8 | t.scan;
}

// Sentinel colour meaning "the background at this row", ie black or a scanline.
#define BG 0x0001

static inline uint16_t backgroundAt(int16_t y) {
  return (scanlines && (y & 1)) ? colorScan : 0x0000;
}

static void clearRect(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (!scanlines) { lcdFill(x, y, w, h, 0); return; }
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > LCD_W) w = LCD_W - x;
  if (y + h > LCD_H) h = LCD_H - y;
  if (w <= 0 || h <= 0) return;
  lcdWindow(x, y, x + w - 1, y + h - 1);
  for (int16_t row = 0; row < h; row++) lcdPush(backgroundAt(y + row), w);
}

static void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (color == BG) clearRect(x, y, w, h);
  else lcdFill(x, y, w, h, color);
}

static void hline(int16_t x, int16_t y, int16_t w, uint16_t color) {
  if (color == BG) color = backgroundAt(y);
  lcdFill(x, y, w, 1, color);
}

static void vline(int16_t x, int16_t y, int16_t h, uint16_t color) {
  fillRect(x, y, 1, h, color);
}

static void pixel(int16_t x, int16_t y, uint16_t color) {
  lcdPixel(x, y, color == BG ? backgroundAt(y) : color);
}

static void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  hline(x, y, w, color);
  hline(x, y + h - 1, w, color);
  vline(x, y, h, color);
  vline(x + w - 1, y, h, color);
}

static void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  if (y0 == y1) { if (x1 < x0) { int16_t t = x0; x0 = x1; x1 = t; } hline(x0, y0, x1 - x0 + 1, color); return; }
  if (x0 == x1) { if (y1 < y0) { int16_t t = y0; y0 = y1; y1 = t; } vline(x0, y0, y1 - y0 + 1, color); return; }
  // Bresenham, but emit horizontal or vertical runs instead of single pixels.
  bool steep = abs(y1 - y0) > abs(x1 - x0);
  if (steep) { int16_t t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
  if (x0 > x1) { int16_t t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
  int16_t dx = x1 - x0, dy = abs(y1 - y0), err = dx / 2, step = y0 < y1 ? 1 : -1;
  int16_t runStart = x0;
  for (int16_t x = x0; x <= x1; x++) {
    err -= dy;
    if (err < 0 || x == x1) {
      int16_t length = x - runStart + 1;
      if (steep) vline(y0, runStart, length, color);
      else hline(runStart, y0, length, color);
      y0 += step;
      err += dx;
      runStart = x + 1;
    }
  }
}

// Upward pointing filled triangle, for map markers and the player arrow.
static void fillArrow(int16_t cx, int16_t top, uint8_t height, uint16_t color) {
  for (uint8_t row = 0; row < height; row++) hline(cx - row / 2, top + row, (row / 2) * 2 + 1, color);
}

static const uint8_t SINE[65] PROGMEM = {
  0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46, 49, 51, 54, 57, 60, 63, 65, 68, 71, 73,
  76, 78, 81, 83, 85, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
  117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127,
};

// Angle 0..255 is a full turn. Returns -127..127.
static int8_t isin(uint8_t angle) {
  uint8_t quarter = angle & 63;
  int8_t value = pgm_read_byte(&SINE[(angle & 64) ? 64 - quarter : quarter]);
  return (angle & 128) ? -value : value;
}
static int8_t icos(uint8_t angle) { return isin(angle + 64); }

// 64 steps per quarter. Large rings come out dotted, which suits the radar rings. Radius up to 250.
static void drawCircle(int16_t cx, int16_t cy, int16_t r, uint16_t color) {
  for (uint8_t angle = 0; angle < 64; angle++) {
    int16_t dx = (icos(angle) * r) >> 7, dy = (isin(angle) * r) >> 7;
    pixel(cx + dx, cy + dy, color); pixel(cx - dx, cy + dy, color);
    pixel(cx + dx, cy - dy, color); pixel(cx - dx, cy - dy, color);
  }
}

static inline uint8_t glyphColumn(char c, uint8_t column) {
  if (c < 0x20 || c > 0x7E || column > 4) return 0;
  return pgm_read_byte(&FONT[(c - 0x20) * 5 + column]);
}

// Only the inked pixels, as vertical runs, so text can sit on filled boxes.
static void drawCharClear(int16_t x, int16_t y, char c, uint8_t size, uint16_t color) {
  for (uint8_t column = 0; column < 5; column++) {
    uint8_t bits = glyphColumn(c, column);
    uint8_t row = 0;
    while (bits) {
      while (!(bits & 1)) { bits >>= 1; row++; }
      uint8_t start = row;
      while (bits & 1) { bits >>= 1; row++; }
      fillRect(x + column * size, y + start * size, size, (row - start) * size, color);
    }
  }
}

#define PUSH_BYTES(d1, b1, d2, b2) do { PORTD = d1; PORTB = b1; WR_STROBE(); PORTD = d2; PORTB = b2; WR_STROBE(); } while (0)

// Opaque cell, one window per glyph with the bus bytes worked out once. Several times faster than
// drawing runs. BG as the background follows the scanlines.
static void drawCharSolid(int16_t x, int16_t y, char c, uint8_t size, uint16_t color, uint16_t background) {
  uint8_t columns[6];
  for (uint8_t i = 0; i < 6; i++) columns[i] = glyphColumn(c, i);
  lcdWindow(x, y, x + 6 * size - 1, y + 8 * size - 1);
  uint8_t keepD = PORTD & 0x03, keepB = PORTB & 0xFC;
  uint8_t fd1 = keepD | ((color >> 8) & 0xFC), fb1 = keepB | ((color >> 8) & 0x03);
  uint8_t fd2 = keepD | (color & 0xFC), fb2 = keepB | (color & 0x03);
  for (uint8_t row = 0; row < 8 * size; row++) {
    uint16_t back = background == BG ? backgroundAt(y + row) : background;
    uint8_t bd1 = keepD | ((back >> 8) & 0xFC), bb1 = keepB | ((back >> 8) & 0x03);
    uint8_t bd2 = keepD | (back & 0xFC), bb2 = keepB | (back & 0x03);
    if (size == 1) {
      uint8_t bit = 1 << row;
      for (uint8_t column = 0; column < 6; column++) {
        if (columns[column] & bit) PUSH_BYTES(fd1, fb1, fd2, fb2);
        else PUSH_BYTES(bd1, bb1, bd2, bb2);
      }
      continue;
    }
    uint8_t bit = 1 << (row / size);
    for (uint8_t column = 0; column < 6; column++) {
      bool on = columns[column] & bit;
      for (uint8_t repeat = 0; repeat < size; repeat++) {
        if (on) PUSH_BYTES(fd1, fb1, fd2, fb2);
        else PUSH_BYTES(bd1, bb1, bd2, bb2);
      }
    }
  }
}

static int16_t drawText(int16_t x, int16_t y, const char *text, uint8_t size, uint16_t color, uint16_t background = 0xFFFF) {
  for (; *text; text++, x += 6 * size) {
    if (background == 0xFFFF) drawCharClear(x, y, *text, size, color);
    else drawCharSolid(x, y, *text, size, color, background);
  }
  return x;
}

static int16_t drawTextP(int16_t x, int16_t y, const char *text, uint8_t size, uint16_t color, uint16_t background = 0xFFFF) {
  char c;
  while ((c = pgm_read_byte(text++))) {
    if (background == 0xFFFF) drawCharClear(x, y, c, size, color);
    else drawCharSolid(x, y, c, size, color, background);
    x += 6 * size;
  }
  return x;
}
