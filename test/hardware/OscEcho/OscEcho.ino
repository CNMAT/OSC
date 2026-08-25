// Pure OSC echo: decode an inbound packet with the library, re-encode it with
// the library, send it straight back. A byte-identical reply means decode and
// encode agree on the wire format for whatever was sent -- on the target, not
// on a host.
#include <OSCBundle.h>
#include <OSCMessage.h>
#include <SLIPEncodedSerial.h>

#ifdef BOARD_HAS_USB_SERIAL
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
#else
// Boards with no SLIPEncodedUSBSerial land here and bind to Serial through
// HardwareSerial. That is the right port either way on the UNO R4: the
// Minima does #define Serial SerialUSB (native USB, derives from
// HardwareSerial), and the WiFi builds with -DNO_USB so its Serial is a real
// UART that the on-board ESP32-S3 bridges to the host.
SLIPEncodedSerial SLIPSerial(Serial);
#endif

static uint8_t buf[600];
static size_t  n = 0;          // MUST persist across loop() iterations

void setup() {
  SLIPSerial.begin(115200);
}

#ifdef ARDUINO_ARCH_STM32
// Strapless reflash: /dfu jumps to the ROM bootloader, exactly as if BOOT0
// had been strapped high at reset. Earned on the bench: the F407 core
// board's BOOT0 jumper made contact exactly once, and a USB transition log
// showed every later "strapped" reset coming back in CDC -- the strap was
// never electrically there. Software has no loose dupont pins. F4 system
// memory is at 0x1FFF0000 (F1 would be 0x1FFF0000 too but with a different
// layout; this route only compiles where stm32duino compiles).
static void jumpToBootloader() {
  delay(50);
  HAL_RCC_DeInit();
  HAL_DeInit();
  SysTick->CTRL = 0; SysTick->LOAD = 0; SysTick->VAL = 0;
  __disable_irq();
  __HAL_SYSCFG_REMAPMEMORY_SYSTEMFLASH();
  const uint32_t base = 0x1FFF0000UL;
  void (*boot)(void) = *(void (**)(void))(base + 4);
  __set_MSP(*(uint32_t *) base);
  __enable_irq();
  boot();
  for (;;) {}
}
#endif

void loop() {
  while (!SLIPSerial.endofPacket()) {
    int avail = SLIPSerial.available();
    if (avail <= 0) return;                 // come back next loop()
    while (avail--) {
      int c = SLIPSerial.read();
      if (c >= 0 && n < sizeof buf) buf[n++] = (uint8_t) c;
    }
  }
  if (n == 0) return;

  if (buf[0] == '#') {
    OSCBundle b;
    b.fill(buf, n);
    if (!b.hasError()) { SLIPSerial.beginPacket(); b.send(SLIPSerial); SLIPSerial.endPacket(); }
  } else {
    OSCMessage m;
    m.fill(buf, n);
#ifdef ARDUINO_ARCH_STM32
    if (!m.hasError() && m.fullMatch("/dfu")) jumpToBootloader();
#endif
    if (!m.hasError()) { SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket(); }
  }
  n = 0;
}
