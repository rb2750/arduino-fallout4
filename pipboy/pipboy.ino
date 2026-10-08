// Pip-Boy 3000 HUD for an Uno with a 3.5" ILI9486 touch shield. Page layouts and Vault Boy pictures
// are compiled from the original screen designs by tools/build_assets.py into PIPART on the SD
// card. They are kept there because flash is full. Everything that moves is drawn here.
#include "gfx.h"
#include "sd.h"
#include "io.h"
#include "layout.h"

#define CONTENT_Y 34
#define CONTENT_H 246
#define LABEL_Y 303

enum { TAB_STATS, TAB_ITEMS, TAB_DATA };
enum { MODE_PIPBOY, MODE_SETTINGS, MODE_GAME, MODE_STANDBY };

static uint8_t tab, subtab, mode;
static bool menuOpen;

// Touch state, read by the game for held buttons.
static bool touching;
static int16_t touchX, touchY;
static uint32_t touchStart;
static uint8_t statusSide;        // CND, RAD, EFF on the Status page
static uint8_t radioStation = 1;  // Galaxy News Radio is selected in the screen file
static uint8_t effectSelected;
static int8_t mapSelected = -1;
static bool sdReady;
static uint8_t filesFound;

static char *putNumber(char *p, uint16_t value, uint8_t width, char pad) {
  char digits[5];
  uint8_t count = 0;
  do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
  while (width-- > count) *p++ = pad;
  while (count) *p++ = digits[--count];
  *p = 0;
  return p;
}

static const uint8_t *bitsAt;
static uint8_t bitIndex;

static uint16_t takeBits(uint8_t width) {
  uint16_t value = 0;
  while (width--) {
    value = value << 1 | ((*bitsAt >> (7 - bitIndex)) & 1);
    if (++bitIndex == 8) { bitIndex = 0; bitsAt++; }
  }
  return value;
}

// List rows found while drawing a page, for tap selection. A row can span two lines.
#define MAX_ROWS 12
static int16_t rowY[MAX_ROWS];
static uint8_t rowLines[MAX_ROWS], rowCount;
static int16_t rowRight;
static int8_t selectedRow = -1;

// A layout fits one sector, so loading it is a single read into the sector buffer.
static bool loadLayout(uint8_t slot) {
  if (!sdReady || !streamOpen(slot) || streamByte() < 0) return false;
  bitsAt = sector;
  bitIndex = 0;
  return true;
}

// Opcodes from tools/build_assets.py: 1 hline, 2 vline, 3 fill, 4 line, 6 text, 7 rect.
// With onlyTop set, draws just the list text between onlyTop and onlyBottom, to repaint a row.
static void drawLayout(uint8_t slot, int16_t onlyTop = -1, int16_t onlyBottom = 0) {
  if (!loadLayout(slot)) return;
  static const uint8_t FIELD_COUNTS[] PROGMEM = {0, 3, 3, 4, 4, 0, 2, 4};
  if (onlyTop < 0) { rowCount = 0; rowRight = 0; }
  for (;;) {
    uint8_t op = takeBits(3);
    if (!op) return;
    uint16_t color = takeBits(1) ? colorBright : colorDim;
    bool clear = op == 6 && takeBits(1);
    int16_t a[4];
    uint8_t count = pgm_read_byte(&FIELD_COUNTS[op]);
    for (uint8_t i = 0; i < count; i++) a[i] = takeBits(9);
    if (op != 6) {
      if (onlyTop >= 0) continue;
      switch (op) {
        case 1: hline(a[0], a[1], a[2], color); break;
        case 2: vline(a[0], a[1], a[2], color); break;
        case 3: fillRect(a[0], a[1], a[2], a[3], color); break;
        case 4: drawLine(a[0], a[1], a[2], a[3], color); break;
        case 7: drawRect(a[0], a[1], a[2], a[3], color); break;
      }
      continue;
    }
    uint8_t size = takeBits(1) + 1, length = takeBits(6);
    bool listText = a[0] >= 40 && a[0] <= 60 && a[1] > 40 && a[1] < 270;
    bool draw = onlyTop < 0 || (listText && a[1] >= onlyTop && a[1] <= onlyBottom);
    if (onlyTop < 0 && listText) {
      if (rowCount && a[1] - (rowY[rowCount - 1] + (rowLines[rowCount - 1] - 1) * 15) == 15) rowLines[rowCount - 1]++;
      else if (rowCount < MAX_ROWS) { rowY[rowCount] = a[1]; rowLines[rowCount++] = 1; }
      rowRight = max(rowRight, a[0] + length * 6);
    }
    while (length--) {
      char c = takeBits(7);
      // The background was just cleared, so blank cells need no drawing.
      if (draw && c != ' ') {
        // Repainted rows sit on the selection box, so they are always drawn clear.
        if (clear || onlyTop >= 0) drawCharClear(a[0], a[1], c, size, color);
        else drawCharSolid(a[0], a[1], c, size, color, BG);
      }
      a[0] += 6 * size;
    }
  }
}

static int16_t readWord() {
  int16_t low = streamByte(), high = streamByte();
  return low | high << 8;
}

// Packed picture: x, y, w, h, then alternating off and on run lengths. 255 then 0 continues a run.
static void drawArt(uint8_t page) {
  if (!sdReady || !streamOpen(page)) return;
  int16_t x = readWord(), y = readWord(), w = readWord(), h = readWord();
  lcdWindow(x, y, x + w - 1, y + h - 1);
  int16_t column = 0, row = 0;
  bool on = false;
  for (int16_t run; (run = streamByte()) >= 0; on = !on) {
    while (run && row < h) {
      int16_t count = min(run, w - column);
      lcdPush(on ? colorBright : backgroundAt(y + row), count);
      run -= count;
      column += count;
      if (column == w) { column = 0; row++; }
    }
  }
}

// ---- Clock in the DATA header. Starts at the time printed in the screen file.

static void drawClock(bool force) {
  static uint16_t lastMinute = 0xFFFF;
  uint16_t minutes = 4 * 60 + 15 + (uint16_t)(millis() / 60000UL);
  if (!force && minutes == lastMinute) return;
  lastMinute = minutes;
  uint8_t day = 23 + (minutes / 1440) % 7;
  uint8_t hour = (minutes / 60) % 12;
  if (!hour) hour = 12;
  char text[17] = "08.";
  char *p = putNumber(text + 3, day, 2, '0');
  strcpy_P(p, PSTR(".77, "));
  p = putNumber(p + 5, hour, 1, ' ');
  *p++ = ':';
  p = putNumber(p, minutes % 60, 2, '0');
  strcpy_P(p, PSTR(" "));
  drawText(370, 20, text, 1, colorBright, BG);
}

// ---- Glitch: shifts the whole panel a few pixels using hardware scrolling, like a CRT losing sync.

static void glitch() {
  for (uint8_t i = 0; i < 5; i++) {
    lcdScroll(random(2) ? random(1, 7) : LCD_W - random(1, 7));
    delay(18);
  }
  lcdScroll(0);
}

// ---- Bottom labels: the current sub tab is bright, the rest dim, as in the game.

static const uint16_t *const LABEL_XS[] = {LABEL_X_0, LABEL_X_1, LABEL_X_2};
static const char *const LABEL_TEXTS[] = {LABEL_TEXT_0, LABEL_TEXT_1, LABEL_TEXT_2};

static const char *labelText(uint8_t index) {
  const char *p = LABEL_TEXTS[tab];
  while (index--) p += strlen_P(p) + 1;
  return p;
}

static int16_t labelMiddle(uint8_t index) {
  return pgm_read_word(&LABEL_XS[tab][index]) + strlen_P(labelText(index)) * 3;
}

// The bar over the current bottom label slides to the new one.
#define SLIDER_Y 296
static int16_t sliderLeft, sliderRight;

static void moveSlider(int16_t left, int16_t right) {
  hline(left, SLIDER_Y, right - left, colorBright);
  hline(left, SLIDER_Y + 1, right - left, colorBright);
  if (sliderLeft < left) fillRect(sliderLeft, SLIDER_Y, min(left, sliderRight) - sliderLeft, 2, BG);
  if (sliderRight > right) fillRect(max(right, sliderLeft), SLIDER_Y, sliderRight - max(right, sliderLeft), 2, BG);
  sliderLeft = left;
  sliderRight = right;
}

static int16_t slideFromLeft, slideFromRight, slideToLeft, slideToRight;
static uint8_t slideStep = 6;

// Starts a slide. The page wipe advances it, so both animations run together.
static void slideTo(uint8_t index, bool animate) {
  slideToLeft = pgm_read_word(&LABEL_XS[tab][index]) - 2;
  slideToRight = slideToLeft + strlen_P(labelText(index)) * 6 + 3;
  if (!animate) { sliderLeft = sliderRight = slideToLeft; moveSlider(slideToLeft, slideToRight); return; }
  slideFromLeft = sliderLeft;
  slideFromRight = sliderRight;
  slideStep = 0;
}

static void advanceSlide() {
  if (slideStep >= 6) return;
  slideStep++;
  moveSlider(slideFromLeft + (slideToLeft - slideFromLeft) * slideStep / 6, slideFromRight + (slideToRight - slideFromRight) * slideStep / 6);
}

static void drawLabels() {
  for (uint8_t i = 0; i < 5; i++) {
    int16_t x = pgm_read_word(&LABEL_XS[tab][i]);
    const char *text = labelText(i);
    char c;
    for (uint8_t at = 0; (c = pgm_read_byte(text + at)); at++) {
      drawCharSolid(x + at * 6, LABEL_Y, c, 1, i == subtab ? colorBright : colorDim, BG);
    }
  }
}

static const char SIDE_TABS[] PROGMEM = "CND\0RAD\0EFF";

static void drawSideTabs(int16_t boxY) {
  fillRect(18, boxY, 40, 22, colorDim);
  drawRect(18, boxY, 40, 22, colorBright);
  for (uint8_t i = 0; i < 3; i++) drawTextP(29, 50 + i * 25, SIDE_TABS + i * 4, 1, colorBright);
}

static void slideSideTab(uint8_t from, uint8_t to) {
  for (uint8_t step = 1; step <= 5; step++) {
    clearRect(16, 42, 46, 76);
    drawSideTabs(43 + (from * 25 * (5 - step) + to * 25 * step) / 5);
    delay(10);
  }
}

// ---- CND: the limb condition bars fill in when the page opens.

static const uint16_t LIMB_BARS[] PROGMEM = {220, 49, 123, 103, 314, 103, 220, 262, 123, 210, 314, 210};
static uint8_t barStep;

static void tickBars() {
  static uint32_t last;
  if (barStep > 30 || millis() - last < 16) return;
  last = millis();
  for (uint8_t i = 0; i < 6; i++) {
    int16_t from = constrain((barStep - i * 2) * 4, 0, 43), to = constrain((barStep + 1 - i * 2) * 4, 0, 43);
    if (to > from) fillRect(pgm_read_word(&LIMB_BARS[i * 2]) + from, pgm_read_word(&LIMB_BARS[i * 2 + 1]), to - from, 8, colorBright);
  }
  barStep++;
}

// ---- RAD: a Geiger gauge. The needle is drawn at its new angle first, then only the old pixels
// it no longer covers are cleared, so it never blinks.

#define GAUGE_X 322
#define GAUGE_Y 234
#define GAUGE_R 104
#define NEEDLE_R 86
static int16_t rads = 112;
static uint8_t needleAngle;

static uint8_t angleForRads(int16_t value) { return 128 - (uint32_t)value * 128 / 1000; }
static int16_t needleX(uint8_t angle) { return GAUGE_X + ((icos(angle) * NEEDLE_R) >> 7); }
static int16_t needleY(uint8_t angle) { return GAUGE_Y - ((isin(angle) * NEEDLE_R) >> 7); }

// Walks the needle from the hub. With keep set, skips pixels within 1.5 px of that needle. Every
// needle is NEEDLE_R long, so distance from it is the cross product over NEEDLE_R.
static void walkNeedle(uint8_t angle, uint16_t color, int16_t keep) {
  int16_t x1 = needleX(angle), y1 = needleY(angle);
  int16_t kx = keep >= 0 ? needleX(keep) - GAUGE_X : 0, ky = keep >= 0 ? needleY(keep) - GAUGE_Y : 0;
  int16_t dx = abs(x1 - GAUGE_X), dy = -abs(y1 - GAUGE_Y), sx = GAUGE_X < x1 ? 1 : -1, sy = GAUGE_Y < y1 ? 1 : -1;
  int16_t err = dx + dy, x = GAUGE_X, y = GAUGE_Y;
  for (;;) {
    for (int8_t offset = 0; offset < 2; offset++) {
      int16_t px = x + offset;
      if (keep >= 0) {
        int16_t cross = (px - GAUGE_X) * ky - (y - GAUGE_Y) * kx;
        if (abs(cross) <= NEEDLE_R * 3 / 2) continue;
      }
      pixel(px, y, color);
    }
    if (x == x1 && y == y1) break;
    int16_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x += sx; }
    if (e2 <= dx) { err += dx; y += sy; }
  }
}

static void drawRadPage() {
  drawSideTabs(43 + 25);
  for (uint8_t angle = 0; angle <= 128; angle++) {
    pixel(GAUGE_X + ((icos(angle) * GAUGE_R) >> 7), GAUGE_Y - ((isin(angle) * GAUGE_R) >> 7), colorBright);
  }
  for (uint8_t step = 0; step <= 10; step++) {
    uint8_t angle = 128 - step * 128 / 10;
    int8_t c = icos(angle), sn = isin(angle);
    int16_t inner = step % 5 ? GAUGE_R - 7 : GAUGE_R - 13;
    drawLine(GAUGE_X + ((c * inner) >> 7), GAUGE_Y - ((sn * inner) >> 7),
             GAUGE_X + ((c * GAUGE_R) >> 7), GAUGE_Y - ((sn * GAUGE_R) >> 7), step >= 6 ? colorBright : colorDim);
    if (step % 5 == 0) {
      char text[5];
      putNumber(text, step * 100, 1, ' ');
      drawText(GAUGE_X + ((c * (GAUGE_R + 14)) >> 7) - strlen(text) * 3, GAUGE_Y - ((sn * (GAUGE_R + 14)) >> 7) - 4, text, 1, colorBright);
    }
  }
  drawTextP(80, 130, PSTR("RADS"), 1, colorDim);
  drawTextP(80, 186, PSTR("RAD RESIST     24%"), 1, colorBright);
  drawTextP(80, 206, PSTR("RADAWAY        (5)"), 1, colorBright);
  drawTextP(80, 226, PSTR("RAD-X          (8)"), 1, colorBright);
  drawTextP(GAUGE_X - 42, GAUGE_Y + 22, PSTR("GEIGER COUNTER"), 1, colorDim);
  needleAngle = angleForRads(rads);
  walkNeedle(needleAngle, colorBright, -1);
}

static void tickRad() {
  static uint32_t last;
  if (millis() - last < 50) return;
  last = millis();
  int16_t next = rads + random(-3, 4);
  if (!random(40)) next += random(6, 18);   // a hot pocket of radiation
  rads = constrain(next, 90, 180);
  uint8_t angle = angleForRads(rads);
  if (angle != needleAngle) {
    walkNeedle(angle, colorBright, -1);
    walkNeedle(needleAngle, BG, angle);
    needleAngle = angle;
  }
  fillRect(GAUGE_X - 4, GAUGE_Y - 4, 10, 9, colorBright);
  char text[4];
  putNumber(text, rads, 3, ' ');
  drawText(80, 144, text, 3, colorBright, BG);
}

// ---- EFF: active effects with detail on the right.

static const char EFFECTS[] PROGMEM =
  "Well Rested\0XP +10%\0"
  "Ant Might\0STR +1, Fire Res. +25%\0"
  "Bobblehead - Medicine\0Medicine +10\0"
  "Lady Killer\0Damage +10% vs. women\0"
  "Rad-X\0Rad. Resistance +50%\0";
#define EFFECT_COUNT 5

static const char *effectText(uint8_t index, uint8_t part) {
  const char *p = EFFECTS;
  for (uint8_t i = 0; i < index * 2 + part; i++) p += strlen_P(p) + 1;
  return p;
}

static void drawEffects() {
  for (uint8_t i = 0; i < EFFECT_COUNT; i++) {
    int16_t y = 50 + i * 25;
    clearRect(76, y - 2, 6, 10);
    if (i == effectSelected) fillRect(76, y + 1, 6, 6, colorBright);
    drawTextP(88, y, effectText(i, 0), 1, i == effectSelected ? colorBright : colorDim, BG);
  }
  clearRect(250, 190, 214, 30);
  hline(250, 190, 212, colorBright);
  vline(461, 190, 10, colorBright);
  drawTextP(256, 202, effectText(effectSelected, 1), 1, colorBright);
}

// ---- Radio: a live oscilloscope on the page's axes. Each 2 px column is written once per frame
// with its final pixels, never cleared first, which is what removes the flicker.

#define WAVE_X 277
#define WAVE_COLUMNS 88
#define WAVE_TOP 68
#define WAVE_MIDDLE 87
static uint8_t waveSpans[WAVE_COLUMNS * 2];
static uint8_t wavePhase;

static int8_t sampleWave(uint16_t column) {
  uint8_t t = wavePhase;
  column *= 2;
  switch (radioStation) {
    case 0:   // Enclave Radio: a march, square edged
      return (isin((column * 9 >> 2) + t * 3) > 0 ? 46 : -46) + (isin((column * 23 >> 2) - t * 5) >> 3);
    case 1:   // Galaxy News Radio: swing band
      return (isin((column * 11 >> 2) + t * 4) >> 1) + (isin((column * 29 >> 2) - t * 7) >> 3) - (isin((column * 5 >> 2) + t) >> 4);
    default:  // Vault 101 PA: an idle hum
      return (isin((column * 17 >> 2) + t * 9) >> 3) + (random(5) - 2);
  }
}

static void startRadio() {
  memset(waveSpans, 0xFF, sizeof(waveSpans));
}

static void tickRadio() {
  static uint32_t last;
  if (millis() - last < 30) return;
  last = millis();
  wavePhase++;
  int16_t previous = WAVE_MIDDLE - sampleWave(0);
  for (uint8_t column = 0; column < WAVE_COLUMNS; column++) {
    int16_t next = WAVE_MIDDLE - sampleWave(column + 1);
    uint8_t top = min(previous, next), bottom = max(previous, next);
    if (bottom > top) bottom--;   // joins to the next column without doubling up
    previous = next;
    uint8_t oldTop = waveSpans[column * 2], oldBottom = waveSpans[column * 2 + 1];
    if (oldTop == top && oldBottom == bottom) continue;
    uint8_t from = oldTop == 0xFF ? top : min(top, oldTop), to = oldTop == 0xFF ? bottom : max(bottom, oldBottom);
    lcdWindow(WAVE_X + column * 2, WAVE_TOP + from, WAVE_X + column * 2 + 1, WAVE_TOP + to);
    for (uint8_t row = from; row < top; row++) lcdPush(backgroundAt(WAVE_TOP + row), 2);
    lcdPush(colorBright, (bottom - top + 1) * 2);
    for (uint8_t row = bottom + 1; row <= to; row++) lcdPush(backgroundAt(WAVE_TOP + row), 2);
    waveSpans[column * 2] = top;
    waveSpans[column * 2 + 1] = bottom;
  }
}

// Now playing, scrolled a character at a time under the station list.
static const char SONGS_0[] PROGMEM = "BATTLE HYMN OF THE REPUBLIC  *  HAIL COLUMBIA  *  PRESIDENT EDEN: A FIRESIDE CHAT  *  AMERICA THE BEAUTIFUL  *  ";
static const char SONGS_1[] PROGMEM = "BUTCHER PETE - ROY BROWN  *  MAYBE - THE INK SPOTS  *  ANYTHING GOES - COLE PORTER  *  CIVILIZATION - DANNY KAYE  *  I DON'T WANT TO SET THE WORLD ON FIRE - THE INK SPOTS  *  ";
static const char SONGS_2[] PROGMEM = "ATTENTION RESIDENTS: THE OVERSEER REMINDS YOU THAT VAULT 101 IS YOUR HOME  *  ";
static const char *const SONGS[] = {SONGS_0, SONGS_1, SONGS_2};
#define TICKER_CHARS 33
static uint8_t tickerOffset;

static void tickTicker() {
  static uint32_t last;
  if (millis() - last < 160) return;
  last = millis();
  const char *text = SONGS[radioStation];
  uint8_t length = strlen_P(text);
  tickerOffset %= length;
  for (uint8_t i = 0; i < TICKER_CHARS; i++) {
    drawCharSolid(50 + i * 6, 166, pgm_read_byte(text + (tickerOffset + i) % length), 1, colorBright, BG);
  }
  tickerOffset++;
}

static void drawStations() {
  for (uint8_t i = 0; i < 3; i++) fillRect(38, 51 + i * 25, 6, 6, i == radioStation ? colorBright : BG);
}

// ---- Maps. Positions are approximate, scaled to the map box. Kinds: 0 vault, 1 town, 2 landmark.

struct Place {
  uint8_t x, y, kind;
  const char *name;
};
static const char N0[] PROGMEM = "Vault 101";
static const char N1[] PROGMEM = "Megaton";
static const char N2[] PROGMEM = "Springvale";
static const char N3[] PROGMEM = "Vault 108";
static const char N4[] PROGMEM = "Big Town";
static const char N5[] PROGMEM = "Paradise Falls";
static const char N6[] PROGMEM = "Little Lamplight";
static const char N7[] PROGMEM = "Raven Rock";
static const char N8[] PROGMEM = "Oasis";
static const char N9[] PROGMEM = "Canterbury Commons";
static const char N10[] PROGMEM = "Arefu";
static const char N11[] PROGMEM = "Galaxy News Radio";
static const char N12[] PROGMEM = "The Citadel";
static const char N13[] PROGMEM = "Rivet City";
static const char N14[] PROGMEM = "Jefferson Memorial";
static const char N15[] PROGMEM = "Tenpenny Tower";
static const char N16[] PROGMEM = "Vault 112";
static const char N17[] PROGMEM = "Grayditch";
static const Place PLACES[] PROGMEM = {
  {55, 155, 0, N0}, {75, 172, 1, N1}, {68, 150, 1, N2}, {88, 62, 0, N3}, {122, 66, 1, N4},
  {140, 38, 1, N5}, {20, 64, 1, N6}, {12, 16, 2, N7}, {46, 30, 2, N8}, {216, 46, 1, N9},
  {172, 90, 1, N10}, {204, 140, 2, N11}, {190, 172, 2, N12}, {224, 214, 1, N13}, {212, 196, 2, N14},
  {30, 196, 2, N15}, {38, 224, 0, N16}, {140, 164, 1, N17},
};
#define PLACE_COUNT 18
#define PLAYER_PLACE 3
#define MAP_X 22
#define MAP_Y 40
#define MAP_SCALE 17   // map units to pixels, x16

static const uint8_t RIVER[] PROGMEM = {176, 0, 180, 40, 184, 80, 196, 110, 206, 150, 208, 170, 218, 190, 232, 210, 242, 236, 252, 236};

static int16_t mapX(uint8_t x) { return MAP_X + x * MAP_SCALE / 10; }
static int16_t mapY(uint8_t y) { return MAP_Y + y * 9 / 10; }

static void drawPlaceIcon(uint8_t index, uint16_t color) {
  Place place;
  memcpy_P(&place, &PLACES[index], sizeof(place));
  int16_t x = mapX(place.x), y = mapY(place.y);
  if (place.kind == 0) { drawCircle(x, y, 4, color); pixel(x, y, color); }
  else if (place.kind == 1) drawRect(x - 3, y - 3, 7, 7, color);
  else fillArrow(x, y - 4, 8, color);
}

static void drawPlayerArrow(bool on) {
  Place place;
  memcpy_P(&place, &PLACES[PLAYER_PLACE], sizeof(place));
  int16_t x = mapX(place.x) + 10, y = mapY(place.y) - 10;
  fillArrow(x, y - 6, 12, on ? colorBright : BG);
}

static void drawMapCaption() {
  clearRect(30, 262, 420, 12);
  if (mapSelected < 0) {
    drawTextP(30, 264, PSTR("Tap a marker to identify it."), 1, colorDim);
    return;
  }
  Place place;
  memcpy_P(&place, &PLACES[mapSelected], sizeof(place));
  int16_t x = drawTextP(30, 264, place.name, 1, colorBright);
  drawTextP(x + 12, 264, mapSelected == PLAYER_PLACE ? PSTR("- You are here") : PSTR("- Discovered"), 1, colorDim);
}

static void drawWorldMap() {
  for (int16_t x = MAP_X; x <= 462; x += 40) vline(x, MAP_Y, 216, colorFaint);
  for (int16_t y = MAP_Y; y <= 256; y += 36) hline(MAP_X, y, 440, colorFaint);
  for (uint8_t i = 0; i + 3 < sizeof(RIVER); i += 2) {
    int16_t x0 = mapX(pgm_read_byte(&RIVER[i])), y0 = mapY(pgm_read_byte(&RIVER[i + 1]));
    int16_t x1 = mapX(pgm_read_byte(&RIVER[i + 2])), y1 = mapY(pgm_read_byte(&RIVER[i + 3]));
    drawLine(x0, y0, x1, y1, colorDim);
    drawLine(x0 + 3, y0, x1 + 3, y1, colorDim);
  }
  drawTextP(MAP_X + 380, MAP_Y + 10, PSTR("N"), 2, colorBright);
  vline(MAP_X + 385, MAP_Y + 28, 14, colorBright);
  for (uint8_t i = 0; i < PLACE_COUNT; i++) drawPlaceIcon(i, i == mapSelected ? colorBright : colorDim);
  drawPlayerArrow(true);
  drawMapCaption();
}

static void tapWorldMap(int16_t x, int16_t y) {
  int8_t best = -1;
  int32_t bestDistance = 18 * 18;
  for (uint8_t i = 0; i < PLACE_COUNT; i++) {
    Place place;
    memcpy_P(&place, &PLACES[i], sizeof(place));
    int32_t dx = mapX(place.x) - x, dy = mapY(place.y) - y;
    int32_t distance = dx * dx + dy * dy;
    if (distance < bestDistance) { bestDistance = distance; best = i; }
  }
  if (best < 0) return;
  if (mapSelected >= 0) drawPlaceIcon(mapSelected, colorDim);
  mapSelected = best;
  drawPlaceIcon(mapSelected, colorBright);
  drawMapCaption();
}

// Vault 108 living quarters: rooms as x, y, w, h in 4 px units.
static const uint8_t ROOMS[] PROGMEM = {
  10, 14, 22, 14,   // atrium
  38, 10, 18, 10,   // living quarters
  38, 24, 18, 10,   // cafeteria
  62, 12, 22, 22,   // cloning lab
  14, 34, 14, 12,   // overseer
  62, 40, 22, 10,   // reactor
};
static const char ROOM_NAMES[] PROGMEM = "ATRIUM\0LIVING QTRS\0CAFETERIA\0CLONING LAB\0OVERSEER\0REACTOR";

static void drawLocalMap() {
  for (int16_t x = 30; x <= 450; x += 20) vline(x, 44, 210, colorFaint);
  for (int16_t y = 44; y <= 254; y += 20) hline(30, y, 420, colorFaint);
  const char *name = ROOM_NAMES;
  for (uint8_t i = 0; i < sizeof(ROOMS); i += 4) {
    int16_t x = 30 + pgm_read_byte(&ROOMS[i]) * 5, y = pgm_read_byte(&ROOMS[i + 1]) * 5;
    int16_t w = pgm_read_byte(&ROOMS[i + 2]) * 5, h = pgm_read_byte(&ROOMS[i + 3]) * 5;
    drawRect(x, y, w, h, colorBright);
    drawRect(x + 2, y + 2, w - 4, h - 4, colorDim);
    drawTextP(x + 6, y + 6, name, 1, colorDim);
    name += strlen_P(name) + 1;
  }
  // Corridors joining the rooms.
  hline(190, 100, 30, colorBright); hline(190, 112, 30, colorBright);
  hline(310, 72, 30, colorBright); hline(310, 84, 30, colorBright);
  hline(310, 140, 30, colorBright); hline(310, 152, 30, colorBright);
  vline(122, 140, 30, colorBright); vline(134, 140, 30, colorBright);
  vline(360, 170, 30, colorBright); vline(380, 170, 30, colorBright);
  drawTextP(30, 264, PSTR("Vault 108 - Living Quarters"), 1, colorBright);
}

static void tickMap() {
  static uint32_t last;
  static bool on = true;
  if (millis() - last < 450) return;
  last = millis();
  on = !on;
  if (subtab == 1) {
    drawPlayerArrow(on);
  } else {
    int16_t x = 30 + 46 * 5, y = 14 * 5 + 22;
    fillArrow(x, y - 6, 12, on ? colorBright : BG);
  }
}

// ---- CRT roll. Every few seconds a faint bright band rolls down the screen, like a tube. With no
// frame buffer, each row is read back from the panel, lit, and restored a few frames later. Pages
// use only five theme colours, so each lit colour maps back to exactly one original, and anything
// drawn over the band in the meantime is left alone.

#define ROLL_COLORS 5
#define ROLL_HEIGHT 3
static uint16_t rollFrom[ROLL_COLORS], rollTo[ROLL_COLORS];
static int16_t rollRow = -1;
static uint32_t rollNext, rollLast;

static uint16_t lighten(uint16_t color, uint16_t add) {
  uint16_t r = min(31, (color >> 11) + (add >> 11)), g = min(63, ((color >> 5) & 63) + ((add >> 5) & 63)),
           b = min(31, (color & 31) + (add & 31));
  return r << 11 | g << 5 | b;
}

static void setupRoll() {
  uint16_t add = colorFaint;
  rollFrom[0] = 0; rollFrom[1] = colorScan; rollFrom[2] = colorFaint; rollFrom[3] = colorDim; rollFrom[4] = colorBright;
  for (uint8_t i = 0; i < ROLL_COLORS; i++) {
    rollTo[i] = lighten(rollFrom[i], add);
    // A lit colour must never equal a real one, or restoring would change it.
    for (uint8_t j = 0; j < ROLL_COLORS; j++) if (rollTo[i] == rollFrom[j]) rollTo[i] ^= 1;
  }
}

static void rollPass(int16_t row, bool light) {
  if (row < 0 || row >= LCD_H) return;
  uint16_t pixels[40];
  for (int16_t x = 0; x < LCD_W; x += 40) {
    lcdBeginRead(x, row, x + 39, row);
    for (uint8_t i = 0; i < 40; i++) { uint8_t high = lcdReadByte(); pixels[i] = high << 8 | lcdReadByte(); }
    lcdEndRead();
    lcdWindow(x, row, x + 39, row);
    for (uint8_t i = 0; i < 40; i++) {
      for (uint8_t c = 0; c < ROLL_COLORS; c++) {
        if (pixels[i] == (light ? rollFrom[c] : rollTo[c])) { pixels[i] = light ? rollTo[c] : rollFrom[c]; break; }
      }
      lcdWrite8(pixels[i] >> 8);
      lcdWrite8(pixels[i]);
    }
  }
}

static void tickRoll() {
  if (rollRow < 0) {
    if (millis() < rollNext) return;
    rollRow = 0;
  }
  if (millis() - rollLast < 18) return;
  rollLast = millis();
  rollPass(rollRow - ROLL_HEIGHT, false);
  rollPass(rollRow - ROLL_HEIGHT + 1, false);
  rollPass(rollRow, true);
  rollPass(rollRow + 1, true);
  rollRow += 2;
  if (rollRow > LCD_H + ROLL_HEIGHT) {
    rollRow = -1;
    rollNext = millis() + 6000 + random(6000);
  }
}

// ---- Page composition.

static uint8_t page() { return tab * 5 + subtab; }

// A scan bar sweeps across the content in the direction of travel, clearing as it goes.
static void wipeContent(bool forward, int16_t left) {
  for (int16_t i = 0; i < LCD_W - left; i += 40) {
    int16_t x = forward ? left + i : LCD_W - 40 - i;
    clearRect(x, CONTENT_Y, 40, CONTENT_H);
    int16_t edge = forward ? x + 40 : x - 1;
    if (edge >= left && edge < LCD_W) vline(edge, CONTENT_Y, CONTENT_H, colorBright);
    if (i % 80 == 0) advanceSlide();
  }
  while (slideStep < 6) advanceSlide();
}

static void drawContent(bool forward = true, int16_t left = 0) {
  wipeContent(forward, left);
  rowCount = 0;
  selectedRow = -1;
  if (page() == 0 && statusSide) {
    if (statusSide == 1) drawRadPage();
    else { drawSideTabs(43 + 50); drawEffects(); }
    return;
  }
  if (page() == 10) { drawLocalMap(); return; }
  if (page() == 11) { drawWorldMap(); return; }
  drawLayout(LAYOUT_SLOT + page());
  drawArt(page());
  if (page() == 0) { drawSideTabs(43); barStep = 0; }
  if (page() == 14) {
    drawStations();
    startRadio();
    drawTextP(50, 150, PSTR("NOW PLAYING"), 1, colorDim);
    tickerOffset = 0;
  }
}

static void drawFrame() {
  clearRect(0, 0, LCD_W, LCD_H);
  drawLayout(LAYOUT_SLOT + 15 + tab);
  drawLabels();
  slideTo(subtab, false);
  if (tab == TAB_DATA) drawClock(true);
}

static void showPage(bool frame, bool forward = true) {
  if (frame) { glitch(); drawFrame(); }
  drawContent(forward);
}

// ---- List selection. The box grows out from the tapped point, in the style of the CND tab box.

static int16_t rowTop(uint8_t row) { return rowY[row] - 5; }
static int16_t rowHeight(uint8_t row) { return 17 + (rowLines[row] - 1) * 15; }
static int16_t rowBottomText(uint8_t row) { return rowY[row] + (rowLines[row] - 1) * 15; }
#define ROW_LEFT 46

static void deselectRow() {
  if (selectedRow < 0) return;
  uint8_t row = selectedRow;
  selectedRow = -1;
  clearRect(ROW_LEFT, rowTop(row), rowRight + 6 - ROW_LEFT, rowHeight(row));
  drawLayout(LAYOUT_SLOT + page(), rowY[row], rowBottomText(row));
}

static void selectRow(uint8_t row, int16_t fromX) {
  deselectRow();
  selectedRow = row;
  int16_t right = rowRight + 6, top = rowTop(row), height = rowHeight(row);
  fromX = constrain(fromX, ROW_LEFT, right);
  int16_t left = fromX, end = fromX;
  for (uint8_t step = 1; step <= 4; step++) {
    int16_t newLeft = fromX - (fromX - ROW_LEFT) * step / 4, newEnd = fromX + (right - fromX) * step / 4;
    fillRect(newLeft, top, left - newLeft, height, colorDim);
    fillRect(end, top, newEnd - end, height, colorDim);
    left = newLeft;
    end = newEnd;
    delay(8);
  }
  drawRect(ROW_LEFT, top, right - ROW_LEFT, height, colorBright);
  drawLayout(LAYOUT_SLOT + page(), rowY[row], rowBottomText(row));
  if (page() == 14) { radioStation = row; drawStations(); tickerOffset = 0; }
}

static void tickPage() {
  if (tab == TAB_DATA) {
    drawClock(false);
    if (subtab == 4) { tickRadio(); tickTicker(); }
    if (subtab <= 1) tickMap();
  }
  if (page() == 0) {
    if (statusSide == 0) tickBars();
    if (statusSide == 1) tickRad();
  }
}

// ---- Boot. The text is the original firmware's boot sequence.

static bool skipRequested() {
  int16_t x, y;
  return readTouch(x, y);
}

static bool typeText(int16_t x, int16_t y, const char *text, uint8_t size, uint16_t color, uint8_t pace) {
  char c;
  while ((c = pgm_read_byte(text++))) {
    drawCharSolid(x, y, c, size, color, BG);
    fillRect(x + 6 * size, y, 5 * size, 7 * size, colorBright);
    delay(pace);
    clearRect(x + 6 * size, y, 6 * size, 8 * size);
    x += 6 * size;
    if (skipRequested()) return false;
  }
  return true;
}

static void crtPowerOn() {
  lcdFill(0, 0, LCD_W, LCD_H, 0);
  for (uint8_t width = 8; width < 240; width += 24) {
    hline(240 - width, 159, width * 2, colorBright);
    hline(240 - width, 160, width * 2, colorBright);
    delay(8);
  }
  for (uint8_t h = 2; h < 12; h += 2) {
    lcdFill(0, 160 - h, LCD_W, 2 * h, colorDim);
    delay(15);
  }
  clearRect(0, 0, LCD_W, LCD_H);
}

static const char BOOT_1[] PROGMEM = "VAULT-TEC PERSONAL INFO PROCESSOR";
static const char BOOT_2[] PROGMEM = "PIP-BOY 3000 MK V  //  FIRMWARE 6.0.0";
static const char CHECKS[] PROGMEM =
  "MEMORY CHECK.........\0" "2048 BYTES OK\0"
  "SD STORAGE...........\0" "MOUNTED\0"
  "DISPLAY 480x320......\0" "ONLINE\0"
  "GEIGER COUNTER.......\0" "CALIBRATED\0"
  "BIOMETRIC SCANNER....\0" "ONLINE\0"
  "SECURITY CLEARANCE...\0" "LEVEL OMEGA\0"
  "RADIO RECEIVER.......\0" "STANDBY\0";

static void boot() {
  crtPowerOn();
  bool full = typeText(12, 14, BOOT_1, 2, colorBright, 14) && typeText(12, 36, BOOT_2, 2, colorDim, 8);
  hline(12, 58, 456, colorDim);
  const char *p = CHECKS;
  for (uint8_t i = 0; i < 7 && full; i++) {
    int16_t y = 70 + i * 22;
    full = typeText(12, y, p, 2, colorBright, 5);
    p += strlen_P(p) + 1;
    delay(90);
    bool failed = i == 1 && !sdReady;
    drawTextP(276, y, failed ? PSTR("NOT FOUND") : p, 2, failed ? colorBright : colorDim);
    p += strlen_P(p) + 1;
  }
  if (full) {
    drawTextP(12, 236, PSTR("LOADING EXECUTIVE PROFILE"), 2, colorBright);
    drawRect(12, 258, 456, 14, colorDim);
    for (int16_t w = 0; w < 452 && full; w += 8) {
      fillRect(14 + w, 260, 8, 10, colorBright);
      delay(6);
      full = !skipRequested();
    }
  }
  clearRect(0, 0, LCD_W, LCD_H);
  if (full) {
    drawTextP(240 - 14 * 9, 96, PSTR("WELCOME, PETER"), 3, colorBright);
    full = typeText(240 - 27 * 6, 150, PSTR("EXECUTIVE CLEARANCE GRANTED"), 2, colorBright, 18) &&
           typeText(240 - 28 * 6, 176, PSTR("HEAD OF ADVANCED WEAPONS R&D"), 2, colorDim, 10) &&
           typeText(240 - 27 * 6, 220, PSTR("HAVE A PRODUCTIVE DAY, SIR."), 2, colorBright, 18);
    for (uint16_t i = 0; i < 90 && full; i++) { delay(10); full = !skipRequested(); }
  }
  int16_t x, y;
  while (readTouch(x, y)) {}
}

// ---- Touch calibration. Three targets 40 px in from the corners.

static bool calibrationSkipped;

static RawTouch waitForPress() {
  RawTouch sum = {0, 0, 0, 0, 0};
  uint8_t samples = 0;
  for (;;) {
    if (uartRead() == 'K') { calibrationSkipped = true; return sum; }
    RawTouch t = readTouchRaw();
    if (isPressed(t)) {
      sum.x += t.x; sum.y += t.y; samples++;
      if (samples == 16) break;
    } else {
      sum.x = sum.y = samples = 0;
    }
    delay(10);
  }
  sum.x /= 16; sum.y /= 16;
  uint8_t released = 0;
  while (released < 10) {
    if (uartRead() == 'K') { calibrationSkipped = true; return sum; }
    released = isPressed(readTouchRaw()) ? 0 : released + 1;
    delay(10);
  }
  return sum;
}

static void drawTarget(int16_t x, int16_t y, uint16_t color) {
  hline(x - 12, y, 25, color);
  vline(x, y - 12, 25, color);
  drawCircle(x, y, 6, color);
}

static void calibrate() {
  clearRect(0, 0, LCD_W, LCD_H);
  drawTextP(240 - 27 * 6, 120, PSTR("VAULT-TEC TOUCH CALIBRATION"), 2, colorBright);
  drawTextP(240 - 28 * 3, 150, PSTR("TAP THE CENTRE OF EACH TARGET"), 1, colorDim);
  static const int16_t TARGETS[3][2] = {{40, 40}, {LCD_W - 40, 40}, {40, LCD_H - 40}};
  RawTouch raw[3];
  for (uint8_t i = 0; i < 3; i++) {
    drawTarget(TARGETS[i][0], TARGETS[i][1], colorBright);
    raw[i] = waitForPress();
    drawTarget(TARGETS[i][0], TARGETS[i][1], BG);
    if (calibrationSkipped) {
      // Serial testing only. Rough values, and the next boot asks again.
      calibrationSkipped = false;
      settings.magic = 0;
      settings.rawLeft = 100; settings.rawRight = 900; settings.rawTop = 100; settings.rawBottom = 900;
      clearRect(0, 0, LCD_W, LCD_H);
      return;
    }
  }
  settings.magic = SETTINGS_MAGIC;
  settings.swapAxes = abs(raw[1].y - raw[0].y) > abs(raw[1].x - raw[0].x);
  if (settings.swapAxes) for (uint8_t i = 0; i < 3; i++) { int16_t t = raw[i].x; raw[i].x = raw[i].y; raw[i].y = t; }
  settings.rawLeft = raw[0].x;
  settings.rawRight = raw[1].x;
  settings.rawTop = (raw[0].y + raw[1].y) / 2;
  settings.rawBottom = raw[2].y;
  settings.calibrationFlip = settings.flip;
  saveSettings();
  uartPrint("CAL "); uartNumber(settings.swapAxes); uartWrite(' '); uartNumber(settings.rawLeft); uartWrite(' ');
  uartNumber(settings.rawRight); uartWrite(' '); uartNumber(settings.rawTop); uartWrite(' '); uartNumber(settings.rawBottom); uartWrite('\n');
  clearRect(0, 0, LCD_W, LCD_H);
  drawTextP(240 - 20 * 6, 150, PSTR("CALIBRATION COMPLETE"), 2, colorBright);
  delay(700);
}

// ---- Settings, opened by holding the tab title.

static const char SETTING_NAMES[] PROGMEM = "DISPLAY COLOR\0SCANLINES\0ORIENTATION\0STANDBY\0CALIBRATE TOUCH\0RETURN";
static const char STANDBY_NAMES[] PROGMEM = "1 MIN \0005 MIN \0NEVER ";
#define SETTING_ROW 40

static void drawSettings() {
  clearRect(0, 0, LCD_W, LCD_H);
  drawTextP(24, 14, PSTR("PIP-BOY 3000 CONFIGURATION"), 2, colorBright);
  hline(24, 36, 432, colorDim);
  const char *name = SETTING_NAMES;
  for (uint8_t i = 0; i < 6; i++) {
    int16_t y = 52 + i * SETTING_ROW;
    drawRect(24, y, 432, 32, colorDim);
    drawTextP(40, y + 9, name, 2, colorBright);
    name += strlen_P(name) + 1;
    const char *value = nullptr;
    if (i == 0) value = THEME_NAMES + settings.theme * 6;
    if (i == 1) value = settings.scanlines ? PSTR("ON ") : PSTR("OFF");
    if (i == 2) value = settings.flip ? PSTR("FLIPPED") : PSTR("NORMAL ");
    if (i == 3) value = STANDBY_NAMES + settings.standby * 7;
    if (value) drawTextP(300, y + 9, value, 2, colorBright);
  }
}

static void tapSettings(int16_t x, int16_t y) {
  if (y < 52) return;
  uint8_t row = (y - 52) / SETTING_ROW;
  if (row == 0) { settings.theme = (settings.theme + 1) % THEME_COUNT; setTheme(settings.theme); setupRoll(); }
  else if (row == 1) { settings.scanlines = !settings.scanlines; scanlines = settings.scanlines; }
  else if (row == 2) { settings.flip = !settings.flip; lcdSetFlip(settings.flip); }
  else if (row == 3) settings.standby = (settings.standby + 1) % 3;
  else if (row == 4) calibrate();
  else { saveSettings(); mode = MODE_PIPBOY; showPage(true); return; }
  saveSettings();
  drawSettings();
}

#include "game.h"

// ---- Standby. Collapses like a CRT switching off, wakes on touch or on movement at A5.
// A5 is the only free pin. With the pull-up and nothing attached it never changes, so a tilt or
// vibration switch from A5 to GND is optional and acts as "picked up".

static uint32_t lastActivity;

static void crtPowerDown() {
  for (int16_t h = 160; h > 2; h -= 16) {
    lcdFill(0, 160 - h, LCD_W, 16, 0);
    lcdFill(0, 144 + h, LCD_W, 16, 0);
    hline(0, 160 - h + 16, LCD_W, colorDim);
    hline(0, 143 + h, LCD_W, colorDim);
  }
  lcdFill(0, 0, LCD_W, LCD_H, 0);
  for (int16_t w = 240; w > 4; w -= 20) {
    lcdFill(240 - w, 159, w * 2, 2, colorBright);
    delay(12);
    lcdFill(240 - w, 159, w * 2, 2, 0);
  }
  for (uint8_t i = 0; i < 3; i++) { fillRect(237, 157, 6, 6, i == 0 ? colorBright : colorDim); delay(120); }
  lcdFill(237, 157, 6, 6, 0);
}

static void enterStandby() {
  menuOpen = false;
  // The page is only saved here, so browsing does not wear the EEPROM.
  settings.tab = tab;
  settings.subtab = subtab;
  saveSettings();
  mode = MODE_STANDBY;
  crtPowerDown();
}

static const char HELLO[] PROGMEM = "HELLO, PETER";

static void helloAnimation() {
  crtPowerOn();
  for (uint8_t r = 10; r < 150; r += 14) {
    drawCircle(240, 120, r, colorDim);
    if (r > 24) drawCircle(240, 120, r - 14, BG);
  }
  drawCircle(240, 120, 136, BG);
  // Letters decrypt from noise, left to right.
  int16_t left = 240 - 12 * 12;
  for (uint8_t frame = 0; frame <= 24; frame++) {
    for (uint8_t i = 0; i < 12; i++) {
      char c = pgm_read_byte(&HELLO[i]);
      if (frame < i * 2 + 2 && c != ' ') c = 'A' + random(26);
      drawCharSolid(left + i * 24, 104, c, 4, frame >= i * 2 + 2 ? colorBright : colorDim, BG);
    }
  }
  hline(left, 144, 288, colorDim);
  typeText(240 - 25 * 6, 160, PSTR("PIP-BOY 3000 RESUMING..."), 2, colorDim, 16);
  delay(500);
}

static void wake() {
  clearRect(0, 0, LCD_W, LCD_H);
  helloAnimation();
  mode = MODE_PIPBOY;
  lastActivity = millis();
  showPage(true);
}

// ---- Lock screen. A touch on a sleeping Pip-Boy shows it, and the knob has to be dragged across,
// so brushing a sleeve against it does not wake it.

#define TRACK_X 60
#define TRACK_Y 180
#define TRACK_W 360
#define KNOB_W 44
#define KNOB_MIN (TRACK_X + 3)
#define KNOB_MAX (TRACK_X + TRACK_W - KNOB_W - 3)
static int16_t knobX;

// Fills the strip the knob moves into and clears the strip it leaves, so it never blinks.
static void moveKnob(int16_t x) {
  if (x > knobX) {
    fillRect(knobX + KNOB_W, TRACK_Y + 3, x - knobX, 38, colorBright);
    fillRect(knobX, TRACK_Y + 3, x - knobX, 38, BG);
  } else if (x < knobX) {
    fillRect(x, TRACK_Y + 3, knobX - x, 38, colorBright);
    fillRect(x + KNOB_W, TRACK_Y + 3, knobX - x, 38, BG);
  }
  knobX = x;
}

static bool slideToUnlock() {
  crtPowerOn();
  drawTextP(240 - 19 * 6, 100, PSTR("PIP-BOY 3000 LOCKED"), 2, colorBright);
  drawRect(TRACK_X, TRACK_Y, TRACK_W, 44, colorDim);
  drawTextP(240 - 19 * 3, 240, PSTR("SLIDE TO UNLOCK >>>"), 1, colorDim);
  knobX = KNOB_MIN;
  fillRect(knobX, TRACK_Y + 3, KNOB_W, 38, colorBright);
  bool dragging = false;
  uint8_t missed = 0;
  for (uint32_t lastTouch = millis(); millis() - lastTouch < 6000;) {
    int16_t x, y;
    if (readTouch(x, y)) {
      lastTouch = millis();
      missed = 0;
      if (!dragging && y > TRACK_Y - 30 && y < TRACK_Y + 74 && x < knobX + KNOB_W + 40) dragging = true;
      if (dragging) {
        moveKnob(constrain(x - KNOB_W / 2, KNOB_MIN, KNOB_MAX));
        if (knobX >= KNOB_MAX - 6) return true;
      }
    } else if (dragging && ++missed > 4) {
      dragging = false;
      while (knobX > KNOB_MIN) { moveKnob(max(KNOB_MIN, knobX - 30)); delay(8); }
    }
    delay(8);
  }
  return false;
}

static void pollStandby() {
  if (mode == MODE_STANDBY) {
    int16_t x, y;
    if (readTouch(x, y)) {
      if (slideToUnlock()) wake();
      else crtPowerDown();
      while (readTouch(x, y)) {}
    }
    return;
  }
  static const uint16_t TIMEOUT_SECONDS[] = {60, 300};
  if (settings.standby < 2 && mode != MODE_SETTINGS && millis() - lastActivity > TIMEOUT_SECONDS[settings.standby] * 1000UL) enterStandby();
}

// ---- Pip-Boy menu, dropped down from the tab title.

#define MENU_X 17
#define MENU_Y 36
#define MENU_W 164
#define MENU_ROW 30
#define MENU_ITEMS 5
static const char MENU_NAMES[] PROGMEM = "STATS\0ITEMS\0DATA\0SETTINGS\0SCREEN OFF";

static void drawMenuRow(uint8_t row, bool lit) {
  int16_t y = MENU_Y + 3 + row * MENU_ROW;
  fillRect(MENU_X + 3, y, MENU_W - 6, MENU_ROW - 3, lit ? colorDim : BG);
  if (lit) drawRect(MENU_X + 3, y, MENU_W - 6, MENU_ROW - 3, colorBright);
  if (row == tab) fillRect(MENU_X + 11, y + 10, 6, 6, colorBright);
  const char *name = MENU_NAMES;
  for (uint8_t i = 0; i < row; i++) name += strlen_P(name) + 1;
  drawTextP(MENU_X + 26, y + 7, name, 2, row < 3 ? colorBright : colorDim);
}

static void openMenu() {
  menuOpen = true;
  for (uint8_t row = 0; row < MENU_ITEMS; row++) {
    int16_t height = (row + 1) * MENU_ROW + 3;
    clearRect(MENU_X, MENU_Y + row * MENU_ROW, MENU_W, MENU_ROW + 3);
    drawMenuRow(row, false);
    vline(MENU_X, MENU_Y, height, colorBright);
    vline(MENU_X + MENU_W - 1, MENU_Y, height, colorBright);
    hline(MENU_X, MENU_Y + height - 1, MENU_W, colorBright);
    delay(14);
  }
  hline(MENU_X, MENU_Y, MENU_W, colorBright);
  hline(MENU_X + 8, MENU_Y + 3 * MENU_ROW + 1, MENU_W - 16, colorFaint);
}

static void closeMenu() {
  menuOpen = false;
  drawContent();
}

static void tapMenu(int16_t x, int16_t y) {
  int16_t row = (y - MENU_Y) / MENU_ROW;
  if (x < MENU_X || x >= MENU_X + MENU_W || y < MENU_Y || row >= MENU_ITEMS) { closeMenu(); return; }
  drawMenuRow(row, true);
  delay(90);
  menuOpen = false;
  if (row < 3) {
    tab = row;
    subtab = 0;
    statusSide = 0;
    showPage(true);
  } else if (row == 3) {
    mode = MODE_SETTINGS;
    glitch();
    drawSettings();
  } else {
    enterStandby();
  }
}

// ---- Input.

static uint8_t secretTaps;
static uint32_t secretStart;

static void drawCharsForLabel(uint8_t index) {
  int16_t x = pgm_read_word(&LABEL_XS[tab][index]);
  drawTextP(x, LABEL_Y, labelText(index), 1, index == subtab ? colorBright : colorDim, BG);
}

static void onTap(int16_t x, int16_t y) {
  if (mode == MODE_STANDBY) return;
  if (mode == MODE_SETTINGS) { tapSettings(x, y); return; }
  if (mode == MODE_GAME) { tapGame(x, y); return; }
  if (y < 42 && x < 140) {
    if (menuOpen) closeMenu();
    else openMenu();
    return;
  }
  if (menuOpen) { tapMenu(x, y); return; }
  if (y >= LABEL_Y - 18) {
    int8_t best = -1;
    int16_t bestDistance = 60;
    for (uint8_t i = 0; i < 5; i++) {
      int16_t distance = abs(x - labelMiddle(i));
      if (distance < bestDistance) { bestDistance = distance; best = i; }
    }
    if (best >= 0 && best != subtab) {
      bool forward = best > subtab;
      uint8_t previous = subtab;
      subtab = best;
      statusSide = 0;
      drawCharsForLabel(previous);
      drawCharsForLabel(subtab);
      slideTo(subtab, true);
      showPage(false, forward);
    }
    return;
  }
  if (page() == 0 && x < 64 && y >= 43 && y < 118) {
    uint8_t side = (y - 43) / 25;
    if (side != statusSide) {
      slideSideTab(statusSide, side);
      statusSide = side;
      drawContent(true, 64);
    }
    return;
  }
  if (tab == TAB_STATS && subtab == 0 && statusSide == 2 && y >= 45 && y < 45 + EFFECT_COUNT * 25 && x > 70 && x < 250) {
    effectSelected = (y - 45) / 25;
    drawEffects();
    return;
  }
  if (tab == TAB_STATS && subtab == 4 && x > 280 && x < 400 && y > 60 && y < 230) {
    if (millis() - secretStart > 3000) { secretTaps = 0; secretStart = millis(); }
    if (++secretTaps == 5) { secretTaps = 0; startGame(); }
    return;
  }
  if (tab == TAB_DATA && subtab == 1) { tapWorldMap(x, y); return; }
  if (x < rowRight + 10) {
    for (uint8_t row = 0; row < rowCount; row++) {
      if (y >= rowTop(row) && y < rowTop(row) + rowHeight(row)) {
        if (row != selectedRow) selectRow(row, x);
        return;
      }
    }
  }
}

static void onLongPress(int16_t x, int16_t y) {
  uartPrint("HOLD "); uartNumber(x); uartWrite(' '); uartNumber(y); uartWrite('\n');
  if (mode == MODE_GAME) exitGame();
}

static bool longFired;
static uint8_t releaseCount;

static void pollTouch() {
  int16_t x, y;
  bool pressed = readTouch(x, y);
  if (pressed) {
    lastActivity = millis();
    releaseCount = 0;
    if (!touching) {
      touching = true;
      longFired = false;
      touchStart = millis();
      touchX = x;
      touchY = y;
      uartPrint("TAP "); uartNumber(x); uartWrite(' '); uartNumber(y); uartWrite('\n');
      onTap(x, y);
    } else if (!longFired && millis() - touchStart > 900) {
      longFired = true;
      onLongPress(touchX, touchY);
    }
  } else if (touching && ++releaseCount >= 3) {
    // About 36 ms without contact. Longer merges quick taps into one press.
    touching = false;
  }
}

// ---- Serial: S screenshot, T tap, H hold, R raw touch, G game. Coordinates are 16 bit little endian.

static void screenshot() {
  uartPrint("SHOT\n");
  lcdBeginRead(0, 0, LCD_W - 1, LCD_H - 1);
  // The host acknowledges every chunk, since the USB bridge drops bytes at this rate otherwise.
  for (uint32_t i = 0; i < (uint32_t)LCD_W * LCD_H * 2; i++) {
    if (i % 60 == 0 && uartWait() < 0) break;
    uartWrite(lcdReadByte());
  }
  lcdEndRead();
}

static void pollSerial() {
  int16_t command = uartRead();
  if (command < 0) return;
  if (command == 'S') { screenshot(); return; }
  if (command == 'R') {
    RawTouch t = readTouchRaw();
    uartNumber(t.x); uartWrite(' '); uartNumber(t.y); uartWrite(' '); uartNumber(t.z); uartWrite(' ');
    uartNumber(t.z1); uartWrite(' '); uartNumber(t.z2); uartWrite('\n');
    return;
  }
  if (command == 'G') { startGame(); uartPrint("OK\n"); return; }
  if (command == 'C') { calibrate(); showPage(true); return; }
  if (command == 'Y') { helloAnimation(); uartPrint("OK\n"); return; }
  if (command == 'Y') { helloAnimation(); uartPrint("OK\n"); return; }
  if (command == 'Q') { uartNumber(rollRow); uartWrite('\n'); return; }
  if (command == 'Z') { enterStandby(); uartPrint("OK\n"); return; }
  if (command == 'W') { wake(); uartPrint("OK\n"); return; }
  if (command == 'T' || command == 'H') {
    lastActivity = millis();
    int16_t x = uartWait(); x |= uartWait() << 8;
    int16_t y = uartWait(); y |= uartWait() << 8;
    if (command == 'T') onTap(x, y);
    else onLongPress(x, y);
    uartPrint("OK "); uartNumber(mode); uartWrite(' '); uartNumber(tab); uartWrite(' '); uartNumber(subtab); uartWrite('\n');
  }
}

void setup() {
  uartBegin();
  adcBegin();
  eeprom_read_block(&settings, &savedSettings, sizeof(settings));
  bool configured = settings.magic == SETTINGS_MAGIC;
  if (!configured) {
    memset(&settings, 0, sizeof(settings));
    settings.magic = SETTINGS_MAGIC;
    settings.scanlines = 1;
  }
  setTheme(settings.theme);
  setupRoll();
  scanlines = settings.scanlines;
  tab = settings.tab % 3;
  subtab = settings.subtab % 5;
  lcdBegin(settings.flip);
  sdReady = sdBegin() && fatBegin();
  if (sdReady) filesFound = indexFiles();
  sdReady = sdReady && filesFound;
  randomSeed(analogRead(5) ^ micros());
  uartPrint("PIPBOY "); uartNumber(sdReady); uartWrite(' '); uartNumber(filesFound); uartWrite('\n');
  boot();
  if (!configured) calibrate();
  showPage(true);
  lastActivity = millis();
}

void loop() {
  static uint32_t lastTouch;
  pollSerial();
  if (millis() - lastTouch >= 12) {
    lastTouch = millis();
    pollStandby();
    if (mode == MODE_STANDBY) return;
    pollTouch();
  }
  if (mode == MODE_PIPBOY && !menuOpen) { tickPage(); tickRoll(); }
  else if (mode == MODE_GAME) tickGame();
}
