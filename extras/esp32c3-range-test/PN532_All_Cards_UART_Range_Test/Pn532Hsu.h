#pragma once
#include <Arduino.h>

class Pn532Hsu {
 public:
  enum class Protocol : uint8_t { TypeA = 0, Felica212 = 1, Felica424 = 2,
                                TypeB = 3, Jewel = 4, StSr = 5 };
  struct Card {
    Protocol protocol = Protocol::TypeA;
    uint8_t target = 0, sak = 0, uidLength = 0;
    uint8_t uid[10] = {}, atqa[2] = {};
    uint8_t metadata[64] = {}, metadataLength = 0;
  };
  explicit Pn532Hsu(HardwareSerial &uart) : uart_(uart) {}
  bool begin(int rx, int tx, uint32_t baud);
  bool initialize(uint8_t firmware[4]);
  bool setRf(bool enabled);
  int findCard(Card &card, Protocol protocol = Protocol::TypeA);
  bool authenticate(const Card &card, uint8_t block, const uint8_t key[6]);
  bool readBlock(const Card &card, uint8_t block, uint8_t data[16]);
  bool readType2(const Card &card, uint8_t page, uint8_t data[16]);
  int type2Version(const Card &card, uint8_t version[8]);
  static const char *protocolName(Protocol protocol);
  static const char *cardHint(const Card &card);
  static const char *versionHint(const uint8_t version[8]);
  bool transportFailed() const { return transportFailed_; }
  const char *error() const { return error_; }
  uint8_t status() const { return status_; }
  uint8_t lastCommand() const { return lastCommand_; }
  const char *lastPhase() const { return lastPhase_; }
  uint16_t receivedBytes() const { return receivedBytes_; }
  uint8_t support() const { return support_; }
 private:
  // Bound normal frames; extended frames are rejected, never overrun a buffer.
  static constexpr size_t MAX_FRAME = 254;
  HardwareSerial &uart_;
  const char *error_ = "OK", *lastPhase_ = "IDLE";
  bool transportFailed_ = false;
  uint8_t status_ = 0, lastCommand_ = 0, support_ = 0;
  uint16_t receivedBytes_ = 0;
  void discardInput();
  void abortCommand();
  int fail(const char *message);
  int readFrame(uint8_t *data, size_t &length, uint32_t timeoutMs);
  int command(const uint8_t *data, size_t length, uint8_t *reply,
              size_t capacity, uint32_t timeoutMs = 700);
  bool exchange(const uint8_t *data, size_t length, uint8_t *reply, size_t expectedLength);
  int findStSr(Card &card);
  int rawExchange(const uint8_t *data, size_t length, uint8_t *reply, size_t capacity);
  bool updateRegister(uint16_t address, uint8_t mask, uint8_t bits);
};
