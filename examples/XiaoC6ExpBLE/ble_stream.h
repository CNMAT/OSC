/*
 * BLEStream — an Arduino Stream over the Nordic UART Service, for ESP32.
 *
 * This exists to make one point. The library's SLIP transport is the
 * template `_SLIPSerial<T>`, and T only has to behave like a Stream. On the
 * nRF52840 that came free: Bluefruit's BLEUart already IS a Stream, so
 * `_SLIPSerial<BLEUart>` was the whole BLE transport. The ESP32 BLE library
 * offers no such wrapper, so the adapter is written here — and it is small,
 * which is the interesting part: ~60 lines of buffering is all that stands
 * between a GATT characteristic and the same OSC/SLIP code that runs over
 * USB, TCP and BLE on another chip family entirely.
 *
 * Lives in a header rather than the .ino because the Arduino build hoists
 * generated prototypes above the file's own declarations, which breaks any
 * template or class used before its definition.
 */
#pragma once
#include <Arduino.h>
#include <BLECharacteristic.h>

class BLEStream : public Stream {
 public:
  void attach(BLECharacteristic *tx) { _tx = tx; }
  void setConnected(bool c) { _connected = c; }
  bool connected() const { return _connected; }

  // Called from the RX characteristic's write callback.
  void feed(const uint8_t *d, size_t n) {
    for (size_t i = 0; i < n; i++) {
      size_t next = (_head + 1) % RX_SIZE;
      if (next == _tail) break;            // full: drop, SLIP will resync
      _rx[_head] = d[i];
      _head = next;
    }
  }

  int available() override { return (int)((_head + RX_SIZE - _tail) % RX_SIZE); }
  int peek() override { return available() ? _rx[_tail] : -1; }
  int read() override {
    if (!available()) return -1;
    uint8_t b = _rx[_tail];
    _tail = (_tail + 1) % RX_SIZE;
    return b;
  }
  void flush() override {}

  size_t write(uint8_t b) override { return write(&b, 1); }

  // Chunked to CHUNK bytes per notification. The nRF52840 twin taught this
  // the hard way: a BLE stack that is handed more than one notification's
  // worth may TRUNCATE it silently rather than fail, and every OSC bundle
  // then arrives unterminated. SLIP does not care where a frame is split,
  // so chunking conservatively costs nothing and cannot be got wrong.
  size_t write(const uint8_t *buf, size_t n) override {
    if (!_tx || !_connected) return 0;
    size_t sent = 0;
    while (sent < n) {
      size_t take = n - sent;
      if (take > CHUNK) take = CHUNK;
      _tx->setValue((uint8_t *)(buf + sent), take);
      _tx->notify();
      sent += take;
      delay(3);                            // let the stack drain its queue
    }
    return sent;
  }

 private:
  static const size_t RX_SIZE = 512;
  static const size_t CHUNK = 20;          // safe below any negotiated MTU
  BLECharacteristic *_tx = nullptr;
  volatile bool _connected = false;
  uint8_t _rx[RX_SIZE];
  volatile size_t _head = 0, _tail = 0;
};
