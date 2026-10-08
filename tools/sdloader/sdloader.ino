// Writes files received over serial into PIPART on the SD card, then echoes them back for checking.
// Only touches PIPART.
#include <SPI.h>
#include <SD.h>

static char line[40];

static bool readLine() {
  uint8_t length = 0;
  for (;;) {
    while (!Serial.available()) {}
    char c = Serial.read();
    if (c == '\n') { line[length] = 0; return true; }
    if (length < sizeof(line) - 1) line[length++] = c;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(10, OUTPUT);
  if (!SD.begin(10)) { Serial.println(F("SD FAIL")); return; }
  if (!SD.exists("PIPART")) SD.mkdir("PIPART");
  Serial.println(F("READY"));
  for (;;) {
    readLine();
    if (line[0] == 'W') {
      // W <name> <size>, then the bytes in 64 byte chunks, each answered with k
      char *name = strtok(line + 2, " ");
      long size = atol(strtok(nullptr, " "));
      char path[24] = "PIPART/";
      strcat(path, name);
      if (SD.exists(path)) SD.remove(path);
      File file = SD.open(path, FILE_WRITE);
      if (!file) { Serial.println(F("OPEN FAIL")); continue; }
      Serial.println(F("GO"));
      uint8_t buffer[64];
      while (size > 0) {
        uint8_t want = size > 64 ? 64 : size;
        for (uint8_t i = 0; i < want; i++) { while (!Serial.available()) {} buffer[i] = Serial.read(); }
        file.write(buffer, want);
        size -= want;
        Serial.write('k');
      }
      file.close();
      Serial.println(F("DONE"));
    } else if (line[0] == 'R') {
      char path[24] = "PIPART/";
      strcat(path, line + 2);
      File file = SD.open(path, FILE_READ);
      if (!file) { Serial.println(F("NOFILE")); continue; }
      Serial.print(F("SIZE ")); Serial.println(file.size());
      while (file.available()) Serial.write(file.read());
      file.close();
    } else if (line[0] == 'L') {
      File root = SD.open("/");
      for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
        Serial.print(entry.name()); Serial.print(' '); Serial.println(entry.size());
        entry.close();
      }
      root.close();
      Serial.println(F("END"));
    }
  }
}

void loop() {}
