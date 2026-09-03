/*
 * BLEStream — an Arduino Stream over a GATT characteristic pair, for the
 * Silicon Labs stack (sl_bt_*).
 *
 * Third BLE stack, same story. `_SLIPSerial<T>` only needs T to look like a
 * Stream. Bluefruit's BLEUart on nRF52840 already was one, so BLE cost that
 * sketch nothing. ESP32's Bluedroid was not, so examples/XiaoC6ExpBLE wraps
 * it in ~60 lines. Silicon Labs is the same shape as ESP32: the stack is an
 * event/callback API, so the adapter below buffers receives and turns
 * writes into notifications. The transport line in the .ino is then
 * identical on all three chip families.
 *
 * The core does ship ezBLE, which IS a Stream and would have needed no
 * adapter at all — but its data characteristic is READ|WRITE with no
 * NOTIFY, so a peripheral cannot push to a central and a browser would
 * have to poll. This adapter uses NOTIFY, which is what lets the same Web
 * Bluetooth page drive this board as the other two.
 */
#pragma once
#include <Arduino.h>

class BLEStream : public Stream {
 public:
  // The sketch supplies these once the stack reports a connection.
  void attach(uint8_t conn, uint16_t tx_char) { _conn = conn; _tx = tx_char; }
  void setConnected(bool c) { _connected = c; }
  bool connected() const { return _connected; }

  // Called from the GATT write event.
  void feed(const uint8_t *d, size_t n) {
    for (size_t i = 0; i < n; i++) {
      size_t next = (_head + 1) % RX_SIZE;
      if (next == _tail) break;              // full: drop, SLIP resyncs
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

  // Chunked, for the reason the nRF52840 taught: a stack handed more than
  // one notification's worth may silently truncate rather than fail, and
  // every OSC bundle then arrives unterminated. SLIP does not care where a
  // frame is split, so chunking conservatively cannot be got wrong.
  size_t write(const uint8_t *buf, size_t n) override {
    if (!_connected) return 0;
    size_t sent = 0;
    while (sent < n) {
      size_t take = n - sent;
      if (take > CHUNK) take = CHUNK;
      if (sl_bt_gatt_server_send_notification(_conn, _tx, (uint8_t)take,
                                              buf + sent) != SL_STATUS_OK)
        break;                               // queue full: stop, do not spin
      sent += take;
    }
    return sent;
  }

 private:
  static const size_t RX_SIZE = 512;
  static const size_t CHUNK = 20;            // safe below any negotiated MTU
  uint8_t _conn = 0xFF;
  uint16_t _tx = 0;
  volatile bool _connected = false;
  uint8_t _rx[RX_SIZE];
  volatile size_t _head = 0, _tail = 0;
};
