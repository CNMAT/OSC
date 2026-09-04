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
#include <BLEServer.h>

class BLEStream : public Stream {
 public:
  void attach(BLECharacteristic *tx) { _tx = tx; }
  void setConnected(bool c) { _connected = c; }
  bool connected() const { return _connected; }

  // The server, so write() can ask what MTU was actually negotiated instead
  // of assuming the 23-byte minimum. Set from onConnect().
  void setPeer(BLEServer *server) { _server = server; }

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

  // One notification per write where the negotiated MTU allows it.
  //
  // MEASURED, 2026-09-04, on a XIAO ESP32-C6 driven from Chrome: chunking a
  // 60-byte bundle into three 20-byte notifications 3 ms apart corrupted the
  // stream. Some frames arrived truncated, others with foreign bytes spliced
  // in between the bundle timetag and its first element. The cause is not
  // SLIP and not OSC -- the same bundles over USB were byte-perfect -- it is
  // that a peripheral cannot transmit faster than the CONNECTION INTERVAL,
  // typically 30-50 ms. setValue() overwrites the characteristic's buffer, so
  // a second setValue() 3 ms later replaces a payload the radio has not sent
  // yet: one chunk is lost and another goes out twice.
  //
  // The fix is to stop chunking unnecessarily. The sketch negotiates
  // BLEDevice::setMTU(247), so ask what the central actually granted and send
  // up to (MTU - 3) in a single notification -- an OSC state bundle is ~60
  // bytes and now leaves in one. When a payload genuinely exceeds the MTU the
  // chunk loop still runs, but it waits a full connection interval between
  // notifications rather than 3 ms, because correctness there costs latency
  // only on packets that are rare.
  size_t write(const uint8_t *buf, size_t n) override {
    if (!_tx || !_connected) return 0;
    size_t chunk = CHUNK_MIN;
    if (_server) {
      const uint16_t mtu = _server->getPeerMTU(_server->getConnId());
      if (mtu > 3) chunk = (size_t)(mtu - 3);        // 3 bytes of ATT header
      if (chunk < CHUNK_MIN) chunk = CHUNK_MIN;      // a central that lied
    }
    size_t sent = 0;
    while (sent < n) {
      size_t take = n - sent;
      if (take > chunk) take = chunk;
      _tx->setValue((uint8_t *)(buf + sent), take);
      _tx->notify();
      sent += take;
      if (sent < n) delay(CONN_INTERVAL_MS);   // only when more chunks follow
    }
    return sent;
  }

 private:
  static const size_t RX_SIZE = 512;
  static const size_t CHUNK_MIN = 20;      // the 23-byte default MTU, less ATT
  static const uint32_t CONN_INTERVAL_MS = 50;   // upper end of a typical one
  BLECharacteristic *_tx = nullptr;
  BLEServer *_server = nullptr;
  volatile bool _connected = false;
  uint8_t _rx[RX_SIZE];
  volatile size_t _head = 0, _tail = 0;
};
