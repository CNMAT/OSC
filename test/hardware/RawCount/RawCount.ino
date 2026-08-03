// No SLIP, no OSC. Just drain Serial as fast as possible and report the byte
// count, to separate the USB CDC layer from anything this library does.
void setup() { Serial.begin(115200); }
unsigned long total = 0, last = 0;
void loop() {
  int a = Serial.available();
  while (a-- > 0) { Serial.read(); total++; }
  unsigned long now = millis();
  if (now - last >= 500) { last = now; Serial.print("RX "); Serial.println(total); }
}
