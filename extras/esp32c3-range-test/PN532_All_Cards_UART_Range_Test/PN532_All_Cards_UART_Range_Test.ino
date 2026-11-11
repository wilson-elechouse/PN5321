#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "Pn532Hsu.h"

using Protocol = Pn532Hsu::Protocol;
constexpr int RX_PIN = 4, TX_PIN = 5, D4_PIN = 12, D5_PIN = 13;
constexpr uint8_t KEY_A[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
constexpr Protocol ALL_PROTOCOLS[] = {Protocol::TypeA, Protocol::TypeB,
    Protocol::Felica212, Protocol::Felica424, Protocol::Jewel, Protocol::StSr};
enum class Operation : uint8_t { Id, ClassicRead, Type2Read, Type2Version };
HardwareSerial pn532Serial(1);
Pn532Hsu nfc(pn532Serial);
Protocol selectedProtocol = Protocol::TypeA;
Operation operation = Operation::Id;
bool autoScan = true, readerReady = false;
uint32_t nextInitAt = 0, runNumber = 1;
uint32_t attempts = 0, passCount = 0, noCardCount = 0, errorCount = 0, streak = 0;
uint32_t windowCount = 0, windowPass = 0, windowMiss = 0, windowError = 0;
int distanceMm = -1; // Manually entered; PN532 does not measure physical distance.
char sampleLabel[40] = "unlabelled";
char commandLine[80] = {};
size_t commandLength = 0;
bool commandOverflow = false;

void setResult(bool success) { digitalWrite(D4_PIN, success ? HIGH : LOW); }
void printHex(const uint8_t *data, size_t size) {
  for (size_t i = 0; i < size; ++i) Serial.printf("%02X", data[i]);
}
const char *operationName() {
  switch (operation) {
    case Operation::ClassicRead: return "CLASSIC_READ4";
    case Operation::Type2Read: return "TYPE2_READ_PAGE4";
    case Operation::Type2Version: return "TYPE2_GET_VERSION";
    default: return "ID_ONLY";
  }
}
void printStats() {
  Serial.printf("[STATS] run=%lu label=%s distance_mm=%d source=MANUAL profile=%s op=%s attempts=%lu pass=%lu no_card=%lu errors=%lu success_pct=%.1f streak=%lu\n",
      static_cast<unsigned long>(runNumber), sampleLabel, distanceMm,
      autoScan ? "AUTO" : Pn532Hsu::protocolName(selectedProtocol), operationName(),
      static_cast<unsigned long>(attempts), static_cast<unsigned long>(passCount),
      static_cast<unsigned long>(noCardCount), static_cast<unsigned long>(errorCount),
      attempts ? 100.0 * passCount / attempts : 0.0, static_cast<unsigned long>(streak));
}
void resetStatistics() {
  ++runNumber;
  attempts = passCount = noCardCount = errorCount = streak = 0;
  windowCount = windowPass = windowMiss = windowError = 0;
  setResult(false);
  printStats();
}
void printHelp() {
  Serial.println("[HELP] Enter a command followed by CR/LF:");
  Serial.println(" x=AUTO all; a=TypeA; b=TypeB; f=Felica212; g=Felica424; j=Jewel/Topaz; k=ST SR/SRI/SRT/ST25TB");
  Serial.println(" u=ID distance criterion; c=Classic block4; t=Type2 pages4..7; v=Type2 GET_VERSION (not an ID range test)");
  Serial.println(" n <sample_name>=manual chip/sample label; d <mm>=manual ruler distance (-1=unknown)");
  Serial.println(" r=clear/reinitialize; s=statistics; h=help; use a fixed protocol for repeatable distance tests");
}
void executeCommand(char *line) {
  while (*line == ' ') ++line;
  size_t len = strlen(line);
  while (len && line[len - 1] == ' ') line[--len] = 0;
  if (!len) return;
  const char ch = line[0];
  if (len == 1 && ch == 's') { printStats(); return; }
  if (len == 1 && (ch == 'h' || ch == '?')) { printHelp(); return; }
  if (len > 2 && ch == 'n' && line[1] == ' ') {
    size_t i = 0;
    for (const char *p = line + 2; *p && i + 1 < sizeof(sampleLabel); ++p) {
      const char v = *p;
      sampleLabel[i++] = ((v >= 'a' && v <= 'z') || (v >= 'A' && v <= 'Z') ||
          (v >= '0' && v <= '9') || v == '-' || v == '_' || v == '.') ? v : '_';
    }
    sampleLabel[i] = 0;
    distanceMm = -1;
    resetStatistics(); return;
  }
  if (len > 2 && ch == 'd' && line[1] == ' ') {
    char *end = nullptr;
    const long value = strtol(line + 2, &end, 10);
    if (end == line + 2 || *end || value < -1 || value > 10000) {
      Serial.println("[COMMAND_ERROR] distance must be an integer from -1 to 10000 mm"); return;
    }
    distanceMm = static_cast<int>(value);
    resetStatistics(); return;
  }
  if (len != 1) { Serial.println("[COMMAND_ERROR] use h for help"); return; }
  if (ch == 'r') {
    readerReady = false; nextInitAt = millis(); resetStatistics(); return;
  }
  if (ch == 'u') operation = Operation::Id;
  else if (ch == 'c' || ch == 't' || ch == 'v') {
    operation = ch == 'c' ? Operation::ClassicRead : ch == 't' ? Operation::Type2Read : Operation::Type2Version;
    autoScan = false; selectedProtocol = Protocol::TypeA;
  } else {
    if (!strchr("xabfgjk", ch)) { Serial.println("[COMMAND_ERROR] use h for help"); return; }
    operation = Operation::Id;
    autoScan = ch == 'x';
    switch (ch) {
      case 'x': case 'a': selectedProtocol = Protocol::TypeA; break;
      case 'b': selectedProtocol = Protocol::TypeB; break;
      case 'f': selectedProtocol = Protocol::Felica212; break;
      case 'g': selectedProtocol = Protocol::Felica424; break;
      case 'j': selectedProtocol = Protocol::Jewel; break;
      case 'k': selectedProtocol = Protocol::StSr; break;
      default: Serial.println("[COMMAND_ERROR] use h for help"); return;
    }
  }
  distanceMm = -1;
  resetStatistics();
}
void handleSerial() {
  while (Serial.available()) {
    const int ch = Serial.read();
    if (ch == '\r' || ch == '\n') {
      if (commandOverflow) Serial.println("[COMMAND_ERROR] line too long; discarded");
      else { commandLine[commandLength] = 0; executeCommand(commandLine); }
      commandLength = 0; commandOverflow = false;
    } else if (ch >= 32 && ch < 127) {
      if (commandLength + 1 < sizeof(commandLine)) commandLine[commandLength++] = static_cast<char>(ch);
      else commandOverflow = true;
    }
  }
}
bool protocolSupported(Protocol protocol) {
  return (nfc.support() & ((protocol == Protocol::TypeB || protocol == Protocol::StSr) ? 2 : 1)) != 0;
}
int pollOne(Protocol protocol, Pn532Hsu::Card &card) {
  if (!nfc.setRf(false)) return -1;
  delay(20);
  if (!nfc.setRf(true)) return -1;
  delay(10);
  return nfc.findCard(card, protocol);
}
void csvResult(const char *result, const Pn532Hsu::Card *card, uint32_t elapsed) {
  Serial.printf("RESULT,%lu,%s,%d,%s,%s,", static_cast<unsigned long>(runNumber), sampleLabel,
                distanceMm, autoScan ? "AUTO" : Pn532Hsu::protocolName(selectedProtocol), operationName());
  if (card) {
    const char *idKind = card->protocol == Protocol::TypeB ? "PUPI" :
        (card->protocol == Protocol::Felica212 || card->protocol == Protocol::Felica424) ? "IDm" : "UID";
    Serial.printf("%s,%s,", Pn532Hsu::protocolName(card->protocol), idKind);
    printHex(card->uid, card->uidLength);
    Serial.printf(",%s", Pn532Hsu::cardHint(*card));
  } else Serial.print("NONE,NONE,,unknown");
  Serial.printf(",%s,%lu,%lu\n", result, static_cast<unsigned long>(elapsed), static_cast<unsigned long>(streak));
}
void reportError(const char *stage, const Pn532Hsu::Card *card, uint32_t started) {
  ++errorCount; ++windowError; streak = 0; setResult(false);
  Serial.printf("[ERROR] stage=%s error=%s status=0x%02X cmd=0x%02X phase=%s D4=OFF\n",
                stage, nfc.error(), nfc.status(), nfc.lastCommand(), nfc.lastPhase());
  csvResult("ERROR", card, millis() - started);
  if (nfc.transportFailed()) { readerReady = false; nextInitAt = millis() + 1000; }
}
void runSample() {
  const uint32_t started = millis();
  ++attempts; ++windowCount;
  Pn532Hsu::Card card;
  int found = 0;
  if (autoScan) {
    for (const auto protocol : ALL_PROTOCOLS) {
      if (!protocolSupported(protocol)) continue;
      found = pollOne(protocol, card);
      if (found != 0) break;
    }
  } else {
    if (!protocolSupported(selectedProtocol)) {
      reportError("UNSUPPORTED_BY_READER_FIRMWARE", nullptr, started); return;
    }
    found = pollOne(selectedProtocol, card);
  }
  if (found < 0) { reportError("POLL", nullptr, started); return; }
  if (found == 0) {
    ++noCardCount; ++windowMiss; streak = 0; setResult(false);
    Serial.println("[NO_CARD] D4=OFF"); csvResult("NO_CARD", nullptr, millis() - started); return;
  }
  Serial.printf("[CARD] protocol=%s ID=", Pn532Hsu::protocolName(card.protocol));
  printHex(card.uid, card.uidLength);
  Serial.printf(" SAK=0x%02X ATQA=%02X%02X hint=%s hint_only=1 meta=",
                card.sak, card.atqa[0], card.atqa[1], Pn532Hsu::cardHint(card));
  printHex(card.metadata, card.metadataLength); Serial.println("");
  uint8_t data[16] = {};
  if (operation == Operation::ClassicRead) {
    const uint8_t sak = card.sak & 0x7F;
    if (sak != 0x08 && sak != 0x18 && sak != 0x09) { reportError("NOT_CLASSIC_COMPATIBLE", &card, started); return; }
    if (!nfc.authenticate(card, 4, KEY_A)) { reportError("CLASSIC_AUTH", &card, started); return; }
    if (!nfc.readBlock(card, 4, data)) { reportError("CLASSIC_READ", &card, started); return; }
    Serial.print("[DATA] Classic block4="); printHex(data, 16); Serial.println("");
  } else if (operation == Operation::Type2Read || operation == Operation::Type2Version) {
    if ((card.sak & 0x7F) != 0) { reportError("NOT_TYPE2_COMPATIBLE", &card, started); return; }
    if (operation == Operation::Type2Read) {
      if (!nfc.readType2(card, 4, data)) { reportError("TYPE2_READ", &card, started); return; }
      Serial.print("[DATA] Type2 pages4..7="); printHex(data, 16); Serial.println("");
    } else {
      const int result = nfc.type2Version(card, data);
      if (result != 1) { reportError("VERSION_UNAVAILABLE_NOT_A_RANGE_FAILURE", &card, started); return; }
      Serial.print("[VERSION] raw="); printHex(data, 8);
      Serial.printf(" reported=%s\n", Pn532Hsu::versionHint(data));
    }
  }
  ++passCount; ++windowPass; ++streak; setResult(true);
  Serial.printf("[PASS] criterion=%s elapsed_ms=%lu D4=ON\n", operationName(), static_cast<unsigned long>(millis() - started));
  csvResult("PASS", &card, millis() - started);
}
void setup() {
  pinMode(D4_PIN, OUTPUT); pinMode(D5_PIN, OUTPUT);
  setResult(false); digitalWrite(D5_PIN, LOW);
  Serial.begin(115200); delay(2000);
  Serial.println("\n[BOOT] PN532 all-card UART range test v2 / LuatOS ESP32-C3");
  Serial.println("[WIRING] TX->GPIO4 RX->GPIO5 VCC->3V3 GND->GND; HSU J1/J2 OPEN; UART/PC=115200 8N1");
  Serial.println("[RANGE] default=ID_ONLY; distances/labels entered manually; one card at a time; no card writes");
  Serial.println("RESULT,run,label,distance_mm,profile,criterion,protocol,id_kind,id,card_hint,result,elapsed_ms,streak");
  printHelp(); nfc.begin(RX_PIN, TX_PIN, 115200); nextInitAt = millis();
}
void loop() {
  handleSerial();
  if (!readerReady) {
    setResult(false);
    if (static_cast<int32_t>(millis() - nextInitAt) < 0) { delay(10); return; }
    uint8_t firmware[4] = {};
    if (!nfc.initialize(firmware)) {
      Serial.printf("[RETRY] error=%s phase=%s cmd=0x%02X\n", nfc.error(), nfc.lastPhase(), nfc.lastCommand());
      nextInitAt = millis() + 1000; return;
    }
    readerReady = true;
    Serial.printf("[READY] PN532=%u.%u support=0x%02X\n", firmware[1], firmware[2], nfc.support());
  }
  runSample();
  if (windowCount == 20) {
    Serial.printf("[WINDOW20] pass=%lu no_card=%lu errors=%lu source=MANUAL distance_mm=%d\n",
                  static_cast<unsigned long>(windowPass), static_cast<unsigned long>(windowMiss),
                  static_cast<unsigned long>(windowError), distanceMm);
    printStats(); windowCount = windowPass = windowMiss = windowError = 0;
  }
  const uint32_t pauseStarted = millis();
  while (millis() - pauseStarted < 150) { handleSerial(); delay(10); }
}
