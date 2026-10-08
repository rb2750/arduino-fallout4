// Resistive touch and the 1 Mbaud serial link.
// The touch panel shares pins with the LCD: XP on D7, YM on D6, YP on A2 (LCD CD) and XM on A1
// (LCD WR). The original firmware used TouchScreen(7, A2, A1, 6, 300). The LCD is deselected
// while the panel is driven, or toggling WR would write junk pixels.
#pragma once
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>

// The first conversion after switching lines still carries charge from the previous line, which
// read as phantom pressure (idle sat at 158 to 176 against light taps of 210). It is discarded.
static uint16_t adcRead(uint8_t channel) {
  ADMUX = _BV(REFS0) | channel;
  for (uint8_t i = 0; i < 2; i++) {
    ADCSRA |= _BV(ADSC);
    while (ADCSRA & _BV(ADSC)) {}
  }
  return ADC;
}

struct RawTouch {
  int16_t x, y, z, z1, z2;
};

static RawTouch readTouchRaw() {
  RawTouch t;
  lcdDeselect();
  uint8_t savedC = PORTC, savedD = PORTD;
  // X: drive XP high and XM low, read YP.
  DDRC &= ~_BV(2); PORTC &= ~_BV(2);
  DDRD &= ~_BV(6); PORTD &= ~_BV(6);
  DDRD |= _BV(7); PORTD |= _BV(7);
  DDRC |= _BV(1); PORTC &= ~_BV(1);
  _delay_us(20);
  t.x = 1023 - adcRead(2);
  // Y: drive YP high and YM low, read XM.
  DDRD &= ~_BV(7); PORTD &= ~_BV(7);
  DDRC &= ~_BV(1); PORTC &= ~_BV(1);
  DDRC |= _BV(2); PORTC |= _BV(2);
  DDRD |= _BV(6); PORTD &= ~_BV(6);
  _delay_us(20);
  t.y = 1023 - adcRead(1);
  // Pressure: XP low and YM high, read XM and YP.
  DDRD |= _BV(7); PORTD &= ~_BV(7);
  PORTD |= _BV(6);
  PORTC &= ~_BV(2); DDRC &= ~_BV(2);
  _delay_us(20);
  t.z1 = adcRead(1);
  t.z2 = adcRead(2);
  t.z = 1023 - (t.z2 - t.z1);
  PORTC = savedC;
  PORTD = savedD;
  lcdPinsToOutput();
  lcdSelect();
  return t;
}

// Calibration maps raw readings to screen pixels. Captured once on first boot.
struct Settings {
  uint16_t magic;
  uint8_t theme, scanlines, flip, calibrationFlip;
  uint8_t swapAxes;
  int16_t rawLeft, rawRight, rawTop, rawBottom;
  uint8_t tab, subtab;
  uint16_t highScore;
  uint8_t standby;
};
#define SETTINGS_MAGIC 0x5034
static Settings settings;
static Settings EEMEM savedSettings;

static void saveSettings() { eeprom_update_block(&settings, &savedSettings, sizeof(settings)); }

// Idle pressure is steady to within a count or two but drifts between power ups (158 to 176), and
// light taps in the top left corner read only about 210. So a press is a rise over the tracked idle
// level rather than a fixed number.
#define PRESS_MARGIN 25
static int16_t idleLevel = -1;   // idle pressure times 8

static bool isPressed(const RawTouch &t) {
  if (idleLevel < 0) idleLevel = t.z * 8;
  int16_t idle = idleLevel / 8;
  if (t.z < idle + 12) idleLevel += (t.z * 8 - idleLevel) / 16;
  return t.z >= idle + PRESS_MARGIN && t.z < 1100;
}

// Screen coordinates in the calibrated orientation, or false when nothing is pressed.
static bool readTouch(int16_t &x, int16_t &y) {
  RawTouch a = readTouchRaw();
  if (!isPressed(a)) return false;
  RawTouch b = readTouchRaw();
  if (!isPressed(b) || abs(a.x - b.x) > 20 || abs(a.y - b.y) > 20) return false;
  int16_t rawX = (a.x + b.x) / 2, rawY = (a.y + b.y) / 2;
  if (settings.swapAxes) { int16_t t = rawX; rawX = rawY; rawY = t; }
  // Calibration targets sit 40 px in from each edge.
  x = 40 + (int32_t)(rawX - settings.rawLeft) * (LCD_W - 80) / (settings.rawRight - settings.rawLeft);
  y = 40 + (int32_t)(rawY - settings.rawTop) * (LCD_H - 80) / (settings.rawBottom - settings.rawTop);
  if (settings.flip != settings.calibrationFlip) { x = LCD_W - 1 - x; y = LCD_H - 1 - y; }
  x = constrain(x, 0, LCD_W - 1);
  y = constrain(y, 0, LCD_H - 1);
  return true;
}

static volatile uint8_t rxBuffer[16];
static volatile uint8_t rxHead, rxTail;

ISR(USART_RX_vect) {
  uint8_t value = UDR0;
  uint8_t next = (rxHead + 1) & 15;
  if (next != rxTail) { rxBuffer[rxHead] = value; rxHead = next; }
}

static void adcBegin() {
  ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS0);   // 500 kHz: 26 us per conversion, plenty for touch
}

static void uartBegin() {
  UCSR0A = _BV(U2X0);
  UBRR0 = 1;   // 1 Mbaud at 16 MHz
  UCSR0B = _BV(RXEN0) | _BV(TXEN0) | _BV(RXCIE0);
  UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
}

static int16_t uartRead() {
  if (rxHead == rxTail) return -1;
  uint8_t value = rxBuffer[rxTail];
  rxTail = (rxTail + 1) & 15;
  return value;
}

static int16_t uartWait() {
  uint32_t start = millis();
  int16_t value;
  while ((value = uartRead()) < 0) if (millis() - start > 200) return -1;
  return value;
}

static void uartWrite(uint8_t value) {
  while (!(UCSR0A & _BV(UDRE0))) {}
  UDR0 = value;
}

static void uartPrint(const char *text) { while (*text) uartWrite(*text++); }

static void uartNumber(int16_t value) {
  char buffer[8];
  itoa(value, buffer, 10);
  uartPrint(buffer);
}
