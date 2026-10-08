// Vault-Tec Stacker, a falling blocks holotape. Board is 10 x 20, one bit per cell. Only cells that
// change are redrawn: the piece and its ghost are diffed against where they were.
#pragma once

#define BOARD_X 170
#define BOARD_Y 20
#define CELL 14
#define COLUMNS 10
#define ROWS 20
#define NO_CELL 0xFF

// 4 x 4 masks per rotation, row 0 in the top nibble.
static const uint16_t PIECES[7][4] PROGMEM = {
  {0x0F00, 0x2222, 0x00F0, 0x4444},   // I
  {0x6600, 0x6600, 0x6600, 0x6600},   // O
  {0x4E00, 0x4640, 0x0E40, 0x4C40},   // T
  {0x6C00, 0x4620, 0x06C0, 0x8C40},   // S
  {0xC600, 0x2640, 0x0C60, 0x4C80},   // Z
  {0x8E00, 0x6440, 0x0E20, 0x44C0},   // J
  {0x2E00, 0x4460, 0x0E80, 0xC440},   // L
};

// Standard guideline colours, in PIECES order.
static const uint16_t PIECE_COLORS[7] PROGMEM = {
  RGB565(0, 240, 240), RGB565(240, 240, 0), RGB565(160, 0, 240), RGB565(0, 240, 0),
  RGB565(240, 0, 0), RGB565(0, 0, 240), RGB565(240, 160, 0),
};

static uint16_t board[ROWS];
static uint8_t cellPieces[ROWS * COLUMNS / 2];   // piece + 1 per cell, two cells per byte
static uint8_t piece, rotation, nextPiece, bag[7], bagLeft;
static int8_t pieceX, pieceY;
static uint8_t activeCells[4], ghostCells[4];
static uint16_t score, lines;
static uint8_t level;
static uint32_t lastFall, lastRepeat, flashUntil;
static int8_t flashButton = -1;
static bool gameOver;

enum { BUTTON_LEFT, BUTTON_RIGHT, BUTTON_ROTATE, BUTTON_DROP };
static const int16_t BUTTONS[4][4] PROGMEM = {
  {8, 196, 74, 100}, {88, 196, 74, 100}, {322, 150, 150, 66}, {322, 228, 150, 68},
};
static const char BUTTON_NAMES[] PROGMEM = "<\0>\0ROTATE\0DROP";

static uint16_t mask(uint8_t which, uint8_t turn) { return pgm_read_word(&PIECES[which][turn & 3]); }

static bool fits(uint8_t turn, int8_t x, int8_t y) {
  uint16_t m = mask(piece, turn);
  for (uint8_t i = 0; i < 16; i++) {
    if (!(m & (0x8000 >> i))) continue;
    int8_t column = x + (i & 3), row = y + (i >> 2);
    if (column < 0 || column >= COLUMNS || row >= ROWS) return false;
    if (row >= 0 && (board[row] >> column) & 1) return false;
  }
  return true;
}

static void cellsAt(uint8_t turn, int8_t x, int8_t y, uint8_t *out) {
  uint16_t m = mask(piece, turn);
  uint8_t count = 0;
  for (uint8_t i = 0; i < 16; i++) {
    if (!(m & (0x8000 >> i))) continue;
    int8_t row = y + (i >> 2);
    out[count++] = row < 0 ? NO_CELL : row * COLUMNS + x + (i & 3);
  }
}

static bool contains(const uint8_t *cells, uint8_t cell) {
  for (uint8_t i = 0; i < 4; i++) if (cells[i] == cell) return true;
  return false;
}

enum { CELL_EMPTY, CELL_ACTIVE, CELL_LOCKED, CELL_GHOST };

static uint16_t pieceColor(uint8_t which) { return pgm_read_word(&PIECE_COLORS[which]); }

static uint16_t darker(uint16_t color) {
  return (((color >> 11) * 9 >> 4) << 11) | ((((color >> 5) & 63) * 9 >> 4) << 5) | ((color & 31) * 9 >> 4);
}

static uint8_t lockedPiece(uint8_t cell) { return (cellPieces[cell >> 1] >> ((cell & 1) * 4)) & 15; }

static void setLockedPiece(uint8_t cell, uint8_t value) {
  uint8_t shift = (cell & 1) * 4;
  cellPieces[cell >> 1] = (cellPieces[cell >> 1] & ~(15 << shift)) | value << shift;
}

static void drawCell(uint8_t cell, uint8_t style) {
  if (cell == NO_CELL) return;
  int16_t x = BOARD_X + (cell % COLUMNS) * CELL, y = BOARD_Y + (cell / COLUMNS) * CELL;
  if (style == CELL_ACTIVE) { fillRect(x, y, CELL - 1, CELL - 1, pieceColor(piece)); return; }
  uint16_t color = style == CELL_LOCKED ? pieceColor(lockedPiece(cell) - 1) : 0;
  fillRect(x, y, CELL - 1, CELL - 1, style == CELL_LOCKED ? darker(color) : BG);
  if (style == CELL_LOCKED) { hline(x, y, CELL - 1, color); vline(x, y, CELL - 1, color); }
  if (style == CELL_GHOST) drawRect(x, y, CELL - 1, CELL - 1, colorFaint);
}

static int8_t dropRow() {
  int8_t y = pieceY;
  while (fits(rotation, pieceX, y + 1)) y++;
  return y;
}

// Clears only what the piece and ghost left behind, then draws them where they are now.
static void showPiece() {
  uint8_t active[4], ghost[4];
  cellsAt(rotation, pieceX, pieceY, active);
  cellsAt(rotation, pieceX, dropRow(), ghost);
  for (uint8_t i = 0; i < 4; i++) {
    if (!contains(active, activeCells[i]) && !contains(ghost, activeCells[i])) drawCell(activeCells[i], CELL_EMPTY);
    if (!contains(active, ghostCells[i]) && !contains(ghost, ghostCells[i])) drawCell(ghostCells[i], CELL_EMPTY);
  }
  for (uint8_t i = 0; i < 4; i++) {
    if (!contains(active, ghost[i]) && !contains(ghostCells, ghost[i])) {
      if (contains(activeCells, ghost[i])) drawCell(ghost[i], CELL_EMPTY);
      drawCell(ghost[i], CELL_GHOST);
    }
  }
  for (uint8_t i = 0; i < 4; i++) if (!contains(activeCells, active[i])) drawCell(active[i], CELL_ACTIVE);
  memcpy(activeCells, active, 4);
  memcpy(ghostCells, ghost, 4);
}

static void drawBoard() {
  for (uint8_t cell = 0; cell < ROWS * COLUMNS; cell++) {
    drawCell(cell, (board[cell / COLUMNS] >> (cell % COLUMNS)) & 1 ? CELL_LOCKED : CELL_EMPTY);
  }
}

static void drawNumber(int16_t x, int16_t y, uint16_t value) {
  char text[6];
  putNumber(text, value, 5, ' ');
  drawText(x, y, text, 2, colorBright, BG);
}

static void drawStats() {
  drawNumber(20, 78, score);
  drawNumber(20, 118, lines);
  drawNumber(20, 158, level);
  drawNumber(100, 118, max(score, settings.highScore));
}

static void drawNext() {
  clearRect(346, 30, 60, 48);
  uint16_t m = mask(nextPiece, 0);
  for (uint8_t i = 0; i < 16; i++) {
    if (m & (0x8000 >> i)) fillRect(350 + (i & 3) * 13, 34 + (i >> 2) * 13, 12, 12, pieceColor(nextPiece));
  }
}

static void drawButton(uint8_t button, bool lit) {
  int16_t b[4];
  memcpy_P(b, BUTTONS[button], sizeof(b));
  fillRect(b[0], b[1], b[2], b[3], lit ? colorDim : BG);
  drawRect(b[0], b[1], b[2], b[3], lit ? colorBright : colorDim);
  const char *name = BUTTON_NAMES;
  for (uint8_t i = 0; i < button; i++) name += strlen_P(name) + 1;
  uint8_t size = button < 2 ? 4 : 2;
  drawTextP(b[0] + b[2] / 2 - strlen_P(name) * 3 * size, b[1] + b[3] / 2 - 4 * size, name, size, colorBright);
}

static void pressButton(uint8_t button) {
  if (flashButton >= 0 && flashButton != button) drawButton(flashButton, false);
  if (flashButton != button) drawButton(button, true);
  flashButton = button;
  flashUntil = millis() + 90;
}

static uint8_t takeFromBag() {
  if (!bagLeft) {
    for (uint8_t i = 0; i < 7; i++) bag[i] = i;
    for (uint8_t i = 6; i > 0; i--) { uint8_t j = random(i + 1), t = bag[i]; bag[i] = bag[j]; bag[j] = t; }
    bagLeft = 7;
  }
  return bag[--bagLeft];
}

static void endGame() {
  gameOver = true;
  if (score > settings.highScore) { settings.highScore = score; saveSettings(); }
  fillRect(BOARD_X + 6, 120, COLUMNS * CELL - 12, 76, 0x0000);
  drawRect(BOARD_X + 6, 120, COLUMNS * CELL - 12, 76, colorBright);
  drawTextP(BOARD_X + 16, 132, PSTR("GAME OVER"), 2, colorBright);
  drawTextP(BOARD_X + 37, 162, PSTR("TAP TO"), 1, colorDim);
  drawTextP(BOARD_X + 25, 176, PSTR("PLAY AGAIN"), 1, colorDim);
}

static void spawn() {
  piece = nextPiece;
  nextPiece = takeFromBag();
  rotation = 0;
  pieceX = 3;
  pieceY = 0;
  memset(activeCells, NO_CELL, 4);
  memset(ghostCells, NO_CELL, 4);
  drawNext();
  if (!fits(rotation, pieceX, pieceY)) { endGame(); return; }
  showPiece();
  lastFall = millis();
}

static void lockPiece() {
  uint16_t m = mask(piece, rotation);
  for (uint8_t i = 0; i < 16; i++) {
    if (!(m & (0x8000 >> i))) continue;
    int8_t row = pieceY + (i >> 2);
    if (row < 0) { endGame(); return; }
    board[row] |= 1 << (pieceX + (i & 3));
    setLockedPiece(row * COLUMNS + pieceX + (i & 3), piece + 1);
  }
  for (uint8_t i = 0; i < 4; i++) {
    drawCell(activeCells[i], CELL_LOCKED);
    if (!contains(activeCells, ghostCells[i])) drawCell(ghostCells[i], CELL_EMPTY);
  }
  const uint16_t full = (1 << COLUMNS) - 1;
  uint8_t cleared = 0;
  for (uint8_t row = 0; row < ROWS; row++) cleared += board[row] == full;
  if (cleared) {
    // Full rows flash bright then dim before the stack falls.
    for (uint8_t pass = 0; pass < 2; pass++) {
      for (uint8_t row = 0; row < ROWS; row++) {
        if (board[row] == full) fillRect(BOARD_X, BOARD_Y + row * CELL, COLUMNS * CELL - 1, CELL - 1, pass ? colorDim : colorBright);
      }
      delay(70);
    }
    for (int8_t row = ROWS - 1; row >= 0;) {
      if (board[row] == full) {
        memmove(&board[1], &board[0], row * sizeof(board[0]));
        board[0] = 0;
        memmove(&cellPieces[COLUMNS / 2], &cellPieces[0], row * COLUMNS / 2);
        memset(cellPieces, 0, COLUMNS / 2);
      } else {
        row--;
      }
    }
    static const uint8_t POINTS[] PROGMEM = {0, 4, 10, 30, 120};
    score += pgm_read_byte(&POINTS[cleared]) * 10 * (level + 1);
    lines += cleared;
    level = lines / 10;
    drawBoard();
  }
  drawStats();
  spawn();
}

static void move(int8_t dx) {
  if (fits(rotation, pieceX + dx, pieceY)) { pieceX += dx; showPiece(); }
}

static void rotate() {
  // Simple wall kicks: in place, then one and two cells either side.
  static const int8_t KICKS[] PROGMEM = {0, -1, 1, -2, 2};
  for (uint8_t i = 0; i < sizeof(KICKS); i++) {
    int8_t dx = pgm_read_byte(&KICKS[i]);
    if (fits(rotation + 1, pieceX + dx, pieceY)) {
      rotation = (rotation + 1) & 3;
      pieceX += dx;
      showPiece();
      return;
    }
  }
}

static void hardDrop() {
  int8_t y = dropRow();
  score += (y - pieceY) * 2;
  pieceY = y;
  showPiece();
  lockPiece();
}

static void newGame() {
  memset(board, 0, sizeof(board));
  memset(cellPieces, 0, sizeof(cellPieces));
  score = lines = level = 0;
  bagLeft = 0;
  gameOver = false;
  flashButton = -1;
  clearRect(0, 0, LCD_W, LCD_H);
  drawTextP(10, 8, PSTR("VAULT-TEC"), 2, colorBright);
  drawTextP(10, 28, PSTR("STACKER"), 2, colorDim);
  drawTextP(20, 66, PSTR("SCORE"), 1, colorDim);
  drawTextP(20, 106, PSTR("LINES"), 1, colorDim);
  drawTextP(20, 146, PSTR("LEVEL"), 1, colorDim);
  drawTextP(100, 106, PSTR("BEST"), 1, colorDim);
  drawTextP(346, 14, PSTR("NEXT"), 1, colorDim);
  drawTextP(436, 4, PSTR("[EXIT]"), 1, colorBright);
  drawRect(BOARD_X - 3, BOARD_Y - 3, COLUMNS * CELL + 5, ROWS * CELL + 5, colorBright);
  for (uint8_t i = 0; i < 4; i++) drawButton(i, false);
  drawBoard();
  drawStats();
  nextPiece = takeFromBag();
  spawn();
}

static void startGame() {
  mode = MODE_GAME;
  glitch();
  newGame();
}

static void exitGame() {
  mode = MODE_PIPBOY;
  showPage(true);
}

static int8_t buttonAt(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < 4; i++) {
    int16_t b[4];
    memcpy_P(b, BUTTONS[i], sizeof(b));
    if (x >= b[0] - 4 && x < b[0] + b[2] + 4 && y >= b[1] - 4 && y < b[1] + b[3] + 4) return i;
  }
  return -1;
}

static void tapGame(int16_t x, int16_t y) {
  if (y < 26 && x > 410) { exitGame(); return; }
  if (gameOver) { newGame(); return; }
  int8_t button = buttonAt(x, y);
  // Tapping the board itself also rotates.
  if (button < 0 && x >= BOARD_X && x < BOARD_X + COLUMNS * CELL) button = BUTTON_ROTATE;
  if (button < 0) return;
  pressButton(button);
  if (button == BUTTON_LEFT) move(-1);
  else if (button == BUTTON_RIGHT) move(1);
  else if (button == BUTTON_ROTATE) rotate();
  else hardDrop();
  lastRepeat = millis();
}

static void tickGame() {
  if (flashButton >= 0 && millis() > flashUntil && !touching) { drawButton(flashButton, false); flashButton = -1; }
  if (gameOver) return;
  // Holding left or right repeats after a short pause.
  int8_t held = touching ? buttonAt(touchX, touchY) : -1;
  if ((held == BUTTON_LEFT || held == BUTTON_RIGHT) && millis() - touchStart > 220 && millis() - lastRepeat > 70) {
    lastRepeat = millis();
    move(held == BUTTON_LEFT ? -1 : 1);
  }
  uint16_t fallDelay = max(80, 700 - level * 60);
  if (millis() - lastFall < fallDelay) return;
  lastFall = millis();
  if (fits(rotation, pieceX, pieceY + 1)) { pieceY++; showPiece(); }
  else lockPiece();
}
