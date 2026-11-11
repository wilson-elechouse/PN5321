#include "Pn532Hsu.h"
#include <string.h>
// Define PN532_HSU_TRACE in build flags to log raw UART frames for diagnosis.
#if defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>
#endif

namespace {
constexpr uint8_t ACK[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
}

bool Pn532Hsu::begin(int rx, int tx, uint32_t baud) {
  // Hold an undriven RX line at the UART idle level instead of receiving noise.
  pinMode(rx, INPUT_PULLUP);
  uart_.begin(baud, SERIAL_8N1, rx, tx);
#if defined(ARDUINO_ARCH_ESP32)
  gpio_set_pull_mode(static_cast<gpio_num_t>(rx), GPIO_PULLUP_ONLY);
#endif
  delay(100);
  return true;
}

void Pn532Hsu::discardInput() {
  // Bound draining even when the RX wire is noisy.
  const int available = uart_.available();
  for (int i = 0; i < available; ++i) uart_.read();
}

void Pn532Hsu::abortCommand() {
  // PN532 host ACK aborts an unfinished command (UM0701-02, section 6.2.2.2).
  uart_.write(ACK, sizeof(ACK));
  uart_.flush();
  delay(25);
  discardInput();
}

int Pn532Hsu::fail(const char *message) {
  error_ = message;
  transportFailed_ = true;
  return -1;
}

int Pn532Hsu::readFrame(uint8_t *data, size_t &length, uint32_t timeoutMs) {
  const uint32_t started = millis();
  auto nextByte = [&]() -> int {
    while (millis() - started < timeoutMs) {
      const int value = uart_.read();
      if (value >= 0) {
        ++receivedBytes_;
#ifdef PN532_HSU_TRACE
        Serial.printf("%02X ", value);
#endif
        return value;
      }
      delay(1);
    }
#ifdef PN532_HSU_TRACE
    Serial.print("<timeout> ");
#endif
    return -1;
  };

  // Search the start code; tolerate extra zero bytes and line noise before it.
  uint8_t zeros = 0;
  for (;;) {
    const int value = nextByte();
    if (value < 0) return fail("FRAME_TIMEOUT");
    if (value == 0) {
      if (zeros < 2) ++zeros;
    } else if (value == 0xFF && zeros == 2) {
      break;
    } else {
      zeros = 0;
    }
  }
  const int len = nextByte();
  const int lcs = nextByte();
  if (len < 0 || lcs < 0) return fail("HEADER_TIMEOUT");
  if (len == 0 && lcs == 0xFF) {
    if (nextByte() != 0) return fail("BAD_ACK");
    length = 0;
    return 0; // ACK
  }
  if (len == 0 || len > static_cast<int>(MAX_FRAME) ||
      static_cast<uint8_t>(len + lcs) != 0) return fail("BAD_LENGTH");
  uint8_t sum = 0;
  for (int i = 0; i < len; ++i) {
    const int value = nextByte();
    if (value < 0) return fail("DATA_TIMEOUT");
    data[i] = static_cast<uint8_t>(value);
    sum += data[i];
  }
  const int dcs = nextByte();
  const int postamble = nextByte();
  if (dcs < 0 || postamble < 0) return fail("TAIL_TIMEOUT");
  if (static_cast<uint8_t>(sum + dcs) != 0 || postamble != 0)
    return fail("BAD_CHECKSUM");
  length = static_cast<size_t>(len);
  return 1; // Data frame
}

int Pn532Hsu::command(const uint8_t *data, size_t length, uint8_t *reply,
                      size_t capacity, uint32_t timeoutMs) {
  transportFailed_ = false;
  status_ = 0;
  error_ = "OK";
  receivedBytes_ = 0;
  if (length == 0 || length + 1 > MAX_FRAME) return fail("COMMAND_LENGTH");
  lastCommand_ = data[0];
  discardInput();
  uint8_t frame[MAX_FRAME + 7] = {0x00, 0x00, 0xFF};
  frame[3] = static_cast<uint8_t>(length + 1);
  frame[4] = static_cast<uint8_t>(-frame[3]);
  frame[5] = 0xD4;
  memcpy(frame + 6, data, length);
  uint8_t sum = 0xD4;
  for (size_t i = 0; i < length; ++i) sum += data[i];
  frame[6 + length] = static_cast<uint8_t>(-sum);
  frame[7 + length] = 0x00;
#ifdef PN532_HSU_TRACE
  Serial.print("[HSU TX] ");
  for (size_t i = 0; i < length + 8; ++i) Serial.printf("%02X ", frame[i]);
  Serial.println("");
  Serial.print("[HSU ACK] ");
#endif
  uart_.write(frame, length + 8);
  uart_.flush();

  uint8_t response[MAX_FRAME];
  size_t responseLength = 0;
  lastPhase_ = "ACK";
  if (readFrame(response, responseLength, 250) != 0) {
    if (!transportFailed_) fail("EXPECTED_ACK");
    abortCommand();
    return -1;
  }
#ifdef PN532_HSU_TRACE
  Serial.println("");
  Serial.print("[HSU RESPONSE] ");
#endif
  lastPhase_ = "RESPONSE";
  if (readFrame(response, responseLength, timeoutMs) != 1) {
    if (!transportFailed_) fail("EXPECTED_RESPONSE");
    abortCommand();
    return -1;
  }
#ifdef PN532_HSU_TRACE
  Serial.println("");
#endif
  if (responseLength < 2 || response[0] != 0xD5 ||
      response[1] != static_cast<uint8_t>(data[0] + 1) ||
      responseLength - 2 > capacity) {
    fail("BAD_RESPONSE");
    abortCommand();
    return -1;
  }
  const size_t payloadLength = responseLength - 2;
  if (payloadLength > 0) memcpy(reply, response + 2, payloadLength);
  lastPhase_ = "COMPLETE";
  return static_cast<int>(payloadLength);
}

bool Pn532Hsu::initialize(uint8_t firmware[4]) {
  const uint8_t wake[] = {0x55, 0x55, 0x00, 0x00, 0x00};
  uart_.write(wake, sizeof(wake));
  uart_.flush();
  delay(10); // Exceeds PN532 oscillator wake-up delay (UM0701-02, Fig. 65).
  discardInput();
  const uint8_t longPreamble[32] = {};
  uart_.write(longPreamble, sizeof(longPreamble));
  // Enter normal mode first, including cold power-up in LowVbat mode.
  // UM0701-02 sections 3.1.3.8 and 6.3.2.1.
  uint8_t reply[MAX_FRAME];
  const uint8_t sam[] = {0x14, 0x01, 0x14, 0x01}; // Standard normal-mode configuration
  if (command(sam, sizeof(sam), reply, sizeof(reply), 1000) != 0) return false;
  const uint8_t version[] = {0x02};
  if (command(version, sizeof(version), firmware, 4, 1000) != 4) return false;
  if (firmware[0] != 0x32 || (firmware[3] & 0x01) == 0) {
    fail("NOT_PN532_TYPE_A");
    return false;
  }
  support_ = firmware[3];
  const uint8_t retries[] = {0x32, 0x05, 0xFF, 0x01, 0x00};
  if (command(retries, sizeof(retries), reply, sizeof(reply)) != 0) return false;
  const uint8_t timings[] = {0x32, 0x02, 0x00, 0x0B, 0x0A};
  if (command(timings, sizeof(timings), reply, sizeof(reply)) != 0) return false;
  return setRf(false);
}

bool Pn532Hsu::setRf(bool enabled) {
  const uint8_t cmd[] = {0x32, 0x01, static_cast<uint8_t>(enabled ? 0x01 : 0x00)};
  uint8_t reply[MAX_FRAME];
  return command(cmd, sizeof(cmd), reply, sizeof(reply)) == 0;
}

int Pn532Hsu::findCard(Card &card, Protocol protocol) {
  card = Card{};
  card.protocol = protocol;
  if (protocol == Protocol::StSr) return findStSr(card);
  if (static_cast<uint8_t>(protocol) > 4) return fail("BAD_PROTOCOL");
  uint8_t cmd[8] = {0x4A, 1, static_cast<uint8_t>(protocol)};
  size_t cmdLength = 3;
  if (protocol == Protocol::TypeB) {
    cmd[3] = 0; // Mandatory AFI, all application families; default timeslot polling.
    cmdLength = 4;
  } else if (protocol == Protocol::Felica212 || protocol == Protocol::Felica424) {
    // POL_REQ: command 00, system FFFF, request system code, one time slot.
    const uint8_t polling[] = {0x00, 0xFF, 0xFF, 0x01, 0x00};
    memcpy(cmd + 3, polling, sizeof(polling));
    cmdLength = sizeof(cmd);
  }
  uint8_t reply[MAX_FRAME];
  const int length = command(cmd, cmdLength, reply, sizeof(reply));
  if (length < 0) return -1;
  if (length == 1 && reply[0] == 0) return 0;
  if (length < 2 || reply[0] != 1 || reply[1] != 1) return fail("BAD_TARGET");
  card.target = 1;
  if (protocol == Protocol::TypeA) {
    if (length < 6 || (reply[5] != 4 && reply[5] != 7 && reply[5] != 10) ||
        length < 6 + reply[5]) return fail("BAD_TYPE_A");
    card.atqa[0] = reply[2]; card.atqa[1] = reply[3];
    card.sak = reply[4]; card.uidLength = reply[5];
    memcpy(card.uid, reply + 6, card.uidLength);
    const size_t offset = 6 + card.uidLength;
    if (static_cast<size_t>(length) > offset) {
      const size_t atsLength = reply[offset];
      if (atsLength == 0 || offset + atsLength != static_cast<size_t>(length) ||
          atsLength > sizeof(card.metadata)) return fail("BAD_ATS");
      card.metadataLength = static_cast<uint8_t>(atsLength);
      memcpy(card.metadata, reply + offset, atsLength);
    }
  } else if (protocol == Protocol::TypeB) {
    // NbTg, Tg, ATQB[12] (50,PUPI[4],app[4],protocol[3]), ATTRIB_LEN, ATTRIB.
    if (length < 15 || reply[2] != 0x50 || length != 15 + reply[14])
      return fail("BAD_TYPE_B");
    card.uidLength = 4;
    memcpy(card.uid, reply + 3, 4); // PUPI is not necessarily a permanent UID.
    card.metadataLength = 12;
    memcpy(card.metadata, reply + 2, 12);
  } else if (protocol == Protocol::Felica212 || protocol == Protocol::Felica424) {
    if (length < 20 || (reply[2] != 18 && reply[2] != 20) ||
        length != 2 + reply[2] || reply[3] != 1) return fail("BAD_FELICA");
    card.uidLength = 8;
    memcpy(card.uid, reply + 4, 8); // IDm / NFCID2t
    card.metadataLength = static_cast<uint8_t>(length - 12);
    memcpy(card.metadata, reply + 12, card.metadataLength); // PMm [, system code]
  } else {
    if (length != 8) return fail("BAD_JEWEL");
    card.atqa[0] = reply[2]; card.atqa[1] = reply[3];
    card.uidLength = 4;
    memcpy(card.uid, reply + 4, 4);
  }
  return 1;
}

bool Pn532Hsu::exchange(const uint8_t *data, size_t length, uint8_t *reply,
                       size_t expectedLength) {
  const int received = command(data, length, reply, MAX_FRAME);
  if (received < 0) return false;
  if (received < 1) {
    fail("MISSING_STATUS");
    return false;
  }
  status_ = reply[0];
  if (status_ != 0) {
    error_ = "CARD_STATUS"; // Includes MI/NAD bits; only a complete response passes.
    return false;
  }
  if (received != static_cast<int>(expectedLength)) {
    fail("BAD_DATA_LENGTH");
    return false;
  }
  return true;
}

bool Pn532Hsu::authenticate(const Card &card, uint8_t block, const uint8_t key[6]) {
  if (card.uidLength != 4 && card.uidLength != 7) {
    fail("AUTH_UID_LENGTH");
    return false;
  }
  uint8_t cmd[14] = {0x40, card.target, 0x60, block}; // MIFARE Key A
  memcpy(cmd + 4, key, 6);
  memcpy(cmd + 10, card.uid + card.uidLength - 4, 4);
  uint8_t reply[MAX_FRAME];
  return exchange(cmd, sizeof(cmd), reply, 1);
}

bool Pn532Hsu::readBlock(const Card &card, uint8_t block, uint8_t data[16]) {
  const uint8_t cmd[] = {0x40, card.target, 0x30, block};
  uint8_t reply[MAX_FRAME];
  if (!exchange(cmd, sizeof(cmd), reply, 17)) return false;
  memcpy(data, reply + 1, 16);
  return true;
}


bool Pn532Hsu::readType2(const Card &card, uint8_t page, uint8_t data[16]) {
  const uint8_t cmd[] = {0x40, card.target, 0x30, page};
  uint8_t reply[MAX_FRAME];
  if (!exchange(cmd, sizeof(cmd), reply, 17)) return false;
  memcpy(data, reply + 1, 16);
  return true;
}

int Pn532Hsu::type2Version(const Card &card, uint8_t version[8]) {
  const uint8_t cmd[] = {0x40, card.target, 0x60};
  uint8_t reply[MAX_FRAME];
  const int size = command(cmd, sizeof(cmd), reply, sizeof(reply));
  if (size < 0) return -1;
  if (size < 1) return fail("MISSING_STATUS");
  status_ = reply[0];
  if (status_ != 0) { error_ = "VERSION_UNAVAILABLE"; return 0; }
  if (size != 9) return fail("BAD_VERSION_LENGTH");
  memcpy(version, reply + 1, 8);
  return 1;
}

const char *Pn532Hsu::protocolName(Protocol protocol) {
  switch (protocol) {
    case Protocol::TypeA: return "A106";
    case Protocol::TypeB: return "B106";
    case Protocol::Felica212: return "F212";
    case Protocol::Felica424: return "F424";
    case Protocol::Jewel: return "JEWEL106";
    case Protocol::StSr: return "ST_SR106";
  }
  return "UNKNOWN";
}
const char *Pn532Hsu::cardHint(const Card &card) {
  if (card.protocol == Protocol::TypeB) return "ISO14443B/Type4B-compatible";
  if (card.protocol == Protocol::StSr) return "ST-SR/SRI/SRT/ST25TB-family";
  if (card.protocol == Protocol::Felica212 || card.protocol == Protocol::Felica424)
    return "FeliCa/Type3-family";
  if (card.protocol == Protocol::Jewel) return "Jewel/Topaz/Type1-family";
  const uint8_t sak = card.sak & 0x7F;
  if (sak & 0x20) return "ISO-DEP-A/DESFire/Plus/Type4A-family";
  switch (sak) {
    case 0x00: return "Type2/NTAG/Ultralight-compatible";
    case 0x09: return "Classic-Mini/S20-compatible";
    case 0x08: return "Classic1K/S50/Plus-SL1-compatible";
    case 0x18: return "Classic4K/S70/Plus-SL1-compatible";
    case 0x10: case 0x11: return "MIFARE-Plus-SL2-compatible";
    default: return "ISO14443A-unknown-compatible";
  }
}
const char *Pn532Hsu::versionHint(const uint8_t v[8]) {
  if (v[0] == 0 && v[1] == 4 && v[3] == 2 && v[4] == 1 && v[5] == 0 && v[7] == 3 && v[2] == 4) {
    if (v[6] == 0x0F) return "NTAG213-reported";
    if (v[6] == 0x11) return "NTAG215-reported";
    if (v[6] == 0x13) return "NTAG216-reported";
  }
  if (v[0] == 0 && v[1] == 4 && v[2] == 3 && v[3] == 1 && v[4] == 1 && v[5] == 0 && v[7] == 3) {
    if (v[6] == 0x0B) return "Ultralight-EV1-48B-reported";
    if (v[6] == 0x0E) return "Ultralight-EV1-128B-reported";
  }
  return "Type2-version-other";
}

bool Pn532Hsu::updateRegister(uint16_t address, uint8_t mask, uint8_t bits) {
  const uint8_t read[] = {0x06, static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address)};
  uint8_t value;
  if (command(read, sizeof(read), &value, 1) != 1) return false;
  const uint8_t write[] = {0x08, read[1], read[2],
                          static_cast<uint8_t>((value & ~mask) | (bits & mask))};
  uint8_t reply[MAX_FRAME];
  return command(write, sizeof(write), reply, sizeof(reply)) == 0;
}
int Pn532Hsu::rawExchange(const uint8_t *data, size_t length, uint8_t *reply, size_t capacity) {
  if (length + 1 > MAX_FRAME - 1) return fail("RAW_COMMAND_LENGTH");
  uint8_t cmd[MAX_FRAME] = {0x42}; // InCommunicateThru, PN532 manages RF CRC.
  memcpy(cmd + 1, data, length);
  uint8_t response[MAX_FRAME];
  const int size = command(cmd, length + 1, response, sizeof(response));
  if (size < 0) return -1;
  if (size < 1) return fail("MISSING_STATUS");
  status_ = response[0];
  if (status_ == 1) { error_ = "NO_RESPONSE"; return 0; } // PN532 RF timeout
  if (status_ != 0) { error_ = "RF_STATUS"; return -1; }
  if (static_cast<size_t>(size - 1) > capacity) return fail("RAW_REPLY_LENGTH");
  memcpy(reply, response + 1, size - 1);
  return size - 1;
}
int Pn532Hsu::findStSr(Card &card) {
  // ST's INITIATE/SELECT/GET_UID sequence is distinct from standard REQB/ATTRIB.
  Card ignored;
  if (findCard(ignored, Protocol::TypeB) < 0) return -1;
  if (!setRf(true)) return -1;
  delay(10);
  constexpr uint16_t registers[] = {0x6302, 0x6303, 0x6305, 0x6318, 0x6319, 0x633C, 0x633D};
  uint8_t original[sizeof(registers) / sizeof(registers[0])];
  uint8_t read[1 + sizeof(registers)] = {0x06};
  for (size_t i = 0; i < sizeof(original); ++i) {
    read[1 + i * 2] = static_cast<uint8_t>(registers[i] >> 8);
    read[2 + i * 2] = static_cast<uint8_t>(registers[i]);
  }
  if (command(read, sizeof(read), original, sizeof(original)) != static_cast<int>(sizeof(original)))
    return fail("SR_REGISTER_READ");
  auto finish = [&](int result) {
    const char *savedError = error_, *savedPhase = lastPhase_;
    const uint8_t savedStatus = status_, savedCommand = lastCommand_;
    const bool savedTransport = transportFailed_;
    const uint16_t savedReceived = receivedBytes_;
    uint8_t restore[1 + sizeof(original) * 3] = {0x08};
    for (size_t i = 0; i < sizeof(original); ++i) {
      restore[1 + i * 3] = read[1 + i * 2];
      restore[2 + i * 3] = read[2 + i * 2];
      restore[3 + i * 3] = original[i];
    }
    uint8_t reply[MAX_FRAME];
    if (command(restore, sizeof(restore), reply, sizeof(reply)) != 0) return fail("SR_REGISTER_RESTORE");
    error_ = savedError; lastPhase_ = savedPhase; status_ = savedStatus;
    lastCommand_ = savedCommand; transportFailed_ = savedTransport; receivedBytes_ = savedReceived;
    return result;
  };
  if (!updateRegister(0x6302, 0xF3, 0x83) || !updateRegister(0x6303, 0xF3, 0x83) ||
      !updateRegister(0x6305, 0xEF, 0x07) || !updateRegister(0x6318, 0x3F, 0x3F) ||
      !updateRegister(0x6319, 0x3F, 0x12) || !updateRegister(0x633C, 0x10, 0x10) ||
      !updateRegister(0x633D, 0x77, 0)) return finish(-1);
  const uint8_t initiate[] = {0x06, 0x00};
  uint8_t chipId = 0;
  int size = rawExchange(initiate, sizeof(initiate), &chipId, 1);
  if (size <= 0) return finish(size);
  const uint8_t select[] = {0x0E, chipId};
  uint8_t selectedId = 0;
  size = rawExchange(select, sizeof(select), &selectedId, 1);
  if (size <= 0) return finish(size);
  if (selectedId != chipId) return finish(fail("SR_SELECT_ID"));
  const uint8_t getUid[] = {0x0B};
  size = rawExchange(getUid, sizeof(getUid), card.uid, 8);
  if (size <= 0) return finish(size);
  if (size != 8) return finish(fail("SR_UID_LENGTH"));
  card.uidLength = 8;
  return finish(1);
}
