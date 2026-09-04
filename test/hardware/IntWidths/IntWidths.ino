// Emits one message built from every integer spelling, so the host can check
// which OSC type tag each one produced ON TARGET. This is the half of the
// bug-1 fix that only runs where long is 32 bits and int is 16 (classic AVR).
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

void setup(){
  // See the note in OscEcho: these M5Stack S3 boards latch their own power
  // through GPIO46 and must hold it high before anything else.
#if defined(ARDUINO_M5STACK_CAPSULE) || defined(ARDUINO_M5STACK_DIAL) \
 || defined(ARDUINO_M5STACK_DINMETER)
  pinMode(46, OUTPUT);
  digitalWrite(46, HIGH);
#endif
  SLIPSerial.begin(115200);
}

void loop(){
  static OSCMessage in;                     // parsed only so /dfu is visible;
  while (!SLIPSerial.endofPacket()) {       // ANY other packet still triggers
    int a = SLIPSerial.available();         // the report, errors included
    if (a <= 0) return;
    while (a--) { int c = SLIPSerial.read(); if (c >= 0) in.fill((uint8_t) c); }
  }
#ifdef ARDUINO_ARCH_STM32
  if (!in.hasError() && in.fullMatch("/dfu")) jumpToBootloader();
#endif
  in.empty();
  OSCMessage m("/w");
  m.add((signed char)-1)
   .add((unsigned char)255)
   .add((short)-2)
   .add((unsigned short)65535)
   .add((int)-3)
   .add((unsigned int)65534u)
   .add((long)-100000L)
   .add((unsigned long)4000000000UL)
   .add((long long)-5000000000LL)
   .add((int32_t)123456)
   .add((int64_t)-9000000000LL);
  SLIPSerial.beginPacket(); m.send(SLIPSerial); SLIPSerial.endPacket();
  // report the compiler's view too
  OSCMessage s("/sizes");
  s.add((int32_t)sizeof(int)).add((int32_t)sizeof(long)).add((int32_t)sizeof(long long))
   .add((int32_t)sizeof(double));
  SLIPSerial.beginPacket(); s.send(SLIPSerial); SLIPSerial.endPacket();
}
