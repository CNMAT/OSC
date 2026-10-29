// A Wii extension controller through NintendoExtensionCtrl alone, no OSC: the
// library's own port, connect() and update(), as its MultipleTypes example
// uses them, on whatever board Wire is. It prints, once a second, how many
// connect() and update() calls succeeded, the controller type the library
// identified and its identity bytes, so a bus problem can be told apart from
// a problem in the code that sits on top of the library.
//
// Plain text at 115200. Send 's' for Wire at 100 kHz (the default here),
// 'f' for 400 kHz, 'q' to scan 0x08..0x77 two ways: an empty write (what
// most scanners send) and a one-byte read, printing who answered each. 'i'
// asks, ten times for each gap of 0, 1 and 2 ms after connect(), whether the
// library's requestIdentity() and then update() succeed, and prints the last
// identity read.
#include <NintendoExtensionCtrl.h>

ExtensionPort port;   // on Wire
Nunchuk::Shared nchuk(port);
ClassicController::Shared classic(port);

static uint32_t connects = 0, connectsOk = 0, updates = 0, updatesOk = 0, lastPrint = 0;
static bool connected = false;
static uint32_t clockHz = 100000;

static const char *typeName(ExtensionType t) {
  switch (t) {
  case ExtensionType::NoController: return "NoController";
  case ExtensionType::UnknownController: return "UnknownController";
  case ExtensionType::Nunchuk: return "Nunchuk";
  case ExtensionType::ClassicController: return "ClassicController";
  case ExtensionType::GuitarController: return "GuitarController";
  case ExtensionType::DrumController: return "DrumController";
  case ExtensionType::DJTurntableController: return "DJTurntableController";
  case ExtensionType::uDrawTablet: return "uDrawTablet";
  case ExtensionType::DrawsomeTablet: return "DrawsomeTablet";
  default: return "?";
  }
}

void setup() {
  Serial.begin(115200);
  port.begin();          // Wire.begin()
}

void loop() {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == 'q') {
      Serial.print("scan by empty write:");
      for (uint8_t a = 0x08; a <= 0x77; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) { Serial.print(" 0x"); Serial.print(a, HEX); }
      }
      Serial.println();
      Serial.print("scan by one-byte read:");
      for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (Wire.requestFrom(a, (uint8_t) 1) == 1) { Serial.print(" 0x"); Serial.print(a, HEX); }
        while (Wire.available()) Wire.read();
      }
      Serial.println();
      // 0x52 alone, ten of each kind of probe, 1 ms apart.
      uint8_t empty = 0, read1 = 0, pointer = 0;
      for (uint8_t i = 0; i < 10; i++) {
        Wire.beginTransmission(0x52);
        if (Wire.endTransmission() == 0) empty++;
        delay(1);
        if (Wire.requestFrom((uint8_t) 0x52, (uint8_t) 1) == 1) read1++;
        while (Wire.available()) Wire.read();
        delay(1);
        Wire.beginTransmission(0x52);
        Wire.write((uint8_t) 0x00);
        if (Wire.endTransmission() == 0) pointer++;
        delay(1);
      }
      Serial.print("0x52 of 10: empty write ");
      Serial.print(empty);
      Serial.print(", one-byte read ");
      Serial.print(read1);
      Serial.print(", pointer write ");
      Serial.println(pointer);
      connected = false;
    }
    if (c == 'i') {
      static const uint8_t GAPS_MS[] = { 0, 1, 2 };
      uint8_t id[6] = { 0 };
      for (uint8_t g = 0; g < sizeof GAPS_MS; g++) {
        uint8_t connects = 0, idOk = 0, updOk = 0;
        for (uint8_t i = 0; i < 10; i++) {
          delay(20);
          if (!port.connect()) continue;
          connects++;
          delay(GAPS_MS[g]);
          if (port.requestIdentity(id)) idOk++;
          delay(GAPS_MS[g]);
          if (port.update()) updOk++;
        }
        Serial.print("gap ");
        Serial.print(GAPS_MS[g]);
        Serial.print(" ms: connect ");
        Serial.print(connects);
        Serial.print("/10, then requestIdentity ");
        Serial.print(idOk);
        Serial.print(", then update ");
        Serial.print(updOk);
        Serial.print("; last id");
        for (uint8_t b = 0; b < 6; b++) {
          Serial.print(' ');
          Serial.print(id[b], HEX);
        }
        Serial.println();
      }
      connected = false;
    }
    if (c == 's' || c == 'f') {
      clockHz = c == 's' ? 100000 : 400000;
      Wire.setClock(clockHz);
      connected = false;
      Serial.print("clock ");
      Serial.println(clockHz);
    }
  }
  if (!connected) {
    connects++;
    connected = port.connect();
    if (connected) connectsOk++;
  } else {
    updates++;
    if (port.update()) updatesOk++;
    else connected = false;
  }
  if (millis() - lastPrint >= 1000) {
    lastPrint = millis();
    Serial.print("clock ");
    Serial.print(clockHz);
    Serial.print(" connect ");
    Serial.print(connectsOk);
    Serial.print("/");
    Serial.print(connects);
    Serial.print(" update ");
    Serial.print(updatesOk);
    Serial.print("/");
    Serial.print(updates);
    Serial.print(" type ");
    Serial.print(typeName(port.getControllerType()));
    Serial.print(" data");
    for (uint8_t i = 0; i < 6; i++) {
      Serial.print(' ');
      Serial.print(port.getControlData(i), HEX);
    }
    Serial.println();
    connects = connectsOk = updates = updatesOk = 0;
  }
  delay(10);
}
