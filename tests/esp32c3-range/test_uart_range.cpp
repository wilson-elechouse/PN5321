#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#include "../../extras/esp32c3-range-test/PN532_All_Cards_UART_Range_Test/PN532_All_Cards_UART_Range_Test.ino"

using Bytes = std::vector<uint8_t>;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
Bytes response(uint8_t command, const Bytes &payload) {
  Bytes wire = {0,0,0xFF,0,0xFF,0,0,0,0xFF};
  const uint8_t size = static_cast<uint8_t>(payload.size() + 2);
  wire.push_back(size); wire.push_back(static_cast<uint8_t>(-size));
  wire.push_back(0xD5); wire.push_back(static_cast<uint8_t>(command + 1));
  wire.insert(wire.end(), payload.begin(), payload.end());
  uint8_t sum = 0;
  for (size_t i = 11; i < wire.size(); ++i) sum += wire[i];
  wire.push_back(static_cast<uint8_t>(-sum)); wire.push_back(0);
  return wire;
}
struct Fixture {
  Protocol present = Protocol::TypeA;
  bool absent = false, authFail = false, badChecksum = false, shortPoll = false;
  bool srAbsent = false, versionUnavailable = false;
  uint8_t sak = 8, timeout = 0, support = 7, storage = 0x0F;
  Bytes uid = {4,1,2,3};
  std::map<int,int> registers = {{0x6302,0x80},{0x6303,0x80},{0x6305,0x40},
    {0x6318,0x25},{0x6319,0x16},{0x633C,0},{0x633D,0}};
  std::vector<Bytes> requests;
  Bytes reply(const Bytes &wire) {
    if (wire == Bytes{0x55,0x55,0,0,0} || wire == Bytes(32,0) ||
        wire == Bytes{0,0,0xFF,0,0xFF,0}) return {};
    check(wire.size() >= 9 && wire.size() == size_t(wire[3]) + 7, "TX length");
    uint8_t sum = 0;
    for (size_t i = 5; i + 1 < wire.size(); ++i) sum += wire[i];
    check(sum == 0 && uint8_t(wire[3] + wire[4]) == 0, "TX checksums");
    const Bytes cmd(wire.begin() + 6, wire.end() - 2);
    requests.push_back(cmd);
    if (cmd[0] == timeout) return {};
    Bytes payload;
    switch (cmd[0]) {
      case 0x14: break;
      case 0x02: payload = {0x32,1,6,support}; break;
      case 0x32: break;
      case 0x06:
        for (size_t i = 1; i < cmd.size(); i += 2)
          payload.push_back(static_cast<uint8_t>(registers[uint16_t((cmd[i] << 8) | cmd[i+1])]));
        break;
      case 0x08:
        for (size_t i = 1; i < cmd.size(); i += 3)
          registers[uint16_t((cmd[i] << 8) | cmd[i+1])] = cmd[i+2];
        break;
      case 0x4A: {
        const Protocol protocol = static_cast<Protocol>(cmd[2]);
        if (protocol == Protocol::TypeB) check(cmd == Bytes({0x4A,1,3,0}), "Type B mandatory AFI");
        if (protocol == Protocol::Felica212 || protocol == Protocol::Felica424)
          check(cmd == Bytes({0x4A,1,cmd[2],0,0xFF,0xFF,1,0}), "FeliCa polling data");
        payload = {0};
        if (absent || protocol != present) break;
        if (protocol == Protocol::TypeA) {
          payload = {1,1,0,4,sak,static_cast<uint8_t>(uid.size())};
          payload.insert(payload.end(),uid.begin(),uid.end());
          if (sak & 0x20) payload.insert(payload.end(), {4,0x75,0x77,0x81});
        } else if (protocol == Protocol::TypeB) {
          payload = {1,1,0x50,0x12,0x34,0x56,0x78,0,0,0,0,0,0,1,1,0};
        } else if (protocol == Protocol::Jewel) payload = {1,1,0x0C,0,1,2,3,4};
        else {
          payload = {1,1,20,1,1,2,3,4,5,6,7,8};
          payload.insert(payload.end(), {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0x12,0xFC});
        }
        if (shortPoll) payload.pop_back();
        break;
      }
      case 0x40:
        check(cmd[2] != 0xA0 && cmd[2] != 0xA2, "Never write cards");
        if (cmd[2] == 0x60 && cmd.size() == 3) {
          payload = versionUnavailable ? Bytes{1} : Bytes{0,0,4,4,2,1,0,storage,3};
        } else if (cmd[2] == 0x60) {
          check(cmd.size() == 14 && std::equal(cmd.end()-4,cmd.end(),uid.end()-4), "Auth last four UID bytes");
          payload = {static_cast<uint8_t>(authFail ? 0x14 : 0)};
        } else if (cmd[2] == 0x30) { payload = Bytes(17,0xAB); payload[0] = 0; }
        else throw std::runtime_error("Unexpected card command");
        break;
      case 0x42:
        if (present != Protocol::StSr || absent || srAbsent) { payload = {1}; break; }
        if (cmd == Bytes({0x42,6,0})) payload = {0,0x42};
        else if (cmd == Bytes({0x42,0x0E,0x42})) payload = {0,0x42};
        else if (cmd == Bytes({0x42,0x0B})) payload = {0,1,2,3,4,5,6,2,0xD0};
        else throw std::runtime_error("Unexpected ST SR command");
        break;
      default: throw std::runtime_error("Unexpected host command");
    }
    Bytes result = response(cmd[0],payload);
    if (badChecksum && cmd[0] == 0x4A) result[result.size()-2] ^= 1;
    return result;
  }
  void reset() {
    fakeMillis = 0; readerReady = false; nextInitAt = 0; runNumber = 1;
    attempts = passCount = noCardCount = errorCount = streak = 0;
    windowCount = windowPass = windowMiss = windowError = 0;
    selectedProtocol = Protocol::TypeA; operation = Operation::Id; autoScan = true;
    distanceMm = -1; memcpy(sampleLabel,"unlabelled",sizeof("unlabelled")); commandLength = 0; commandOverflow = false;
    Serial.log.clear(); Serial.incoming.clear(); pn532Serial.incoming.clear();
    pn532Serial.responder = [this](const Bytes &wire) { return reply(wire); };
    setup();
  }
  void input(const std::string &s) {
    for (const auto ch : s) Serial.incoming.push_back(static_cast<uint8_t>(ch));
    handleSerial();
  }
};
int main() {
  int tests = 0;
  auto run = [&](const char *name,const std::function<void(Fixture &)> &test) {
    Fixture f; f.reset(); test(f); ++tests; std::cout << "PASS " << name << '\n';
  };
  try {
    run("default UID distance accepts Classic without authentication",[](Fixture &f) {
      f.authFail = true; loop(); check(passCount == 1 && gpio[12] == HIGH,"ID pass");
      check(std::none_of(f.requests.begin(),f.requests.end(),[](const Bytes &c){return c[0]==0x40;}),"No data commands in ID mode");
      check(pn532Serial.rxPin==4 && pn532Serial.txPin==5 && gpio[13]==LOW,"C3 pins");
    });
    run("all Type A families can identify",[](Fixture &f) {
      for (auto s : {0,8,9,0x18,0x10,0x11,0x20,0x88,0x7F}) {f.sak=static_cast<uint8_t>(s); loop();}
      check(passCount==9,"Unknown compatible Type A also passes");
    });
    run("seven and ten byte IDs plus ISO-DEP ATS",[](Fixture &f) {
      f.uid={4,1,2,3,4,5,6}; loop();
      f.uid={4,1,2,3,4,5,6,7,8,9}; f.sak=0x20; loop();
      check(passCount==2 && Serial.log.find("meta=04757781")!=std::string::npos,"UID/ATS");
    });
    run("Type B AFI and PUPI parsed",[](Fixture &f) {
      f.present=Protocol::TypeB; f.input("b\n"); loop();
      check(passCount==1 && Serial.log.find("B106,PUPI,12345678")!=std::string::npos,"PUPI");
    });
    run("FeliCa 212/424 IDm PMm system code",[](Fixture &f) {
      for (auto p : {Protocol::Felica212,Protocol::Felica424}) {
        f.present=p; f.input(p==Protocol::Felica212 ? "f\n":"g\n"); loop();
        check(passCount==1 && Serial.log.find("IDm,0102030405060708")!=std::string::npos,"IDm");
      }
    });
    run("Jewel/Topaz four byte ID",[](Fixture &f) {
      f.present=Protocol::Jewel; f.input("j\n"); loop();
      check(passCount==1 && Serial.log.find("JEWEL106,UID,01020304")!=std::string::npos,"Jewel");
    });
    run("ST SR initiate select UID and register restoration",[](Fixture &f) {
      f.present=Protocol::StSr; const auto original=f.registers; f.input("k\n"); loop();
      check(passCount==1 && f.registers==original,"ST SR and register restoration");
      check(Serial.log.find("ST_SR106,UID,01020304050602D0")!=std::string::npos,"ST UID");
    });
    run("ST SR RF timeout is no-card and restores configuration",[](Fixture &f) {
      f.present=Protocol::StSr; f.srAbsent=true; const auto original=f.registers; f.input("k\n"); loop();
      check(noCardCount==1 && errorCount==0 && f.registers==original && gpio[12]==LOW,"ST no-card");
    });
    run("auto finds every protocol without false misses",[](Fixture &f) {
      for (auto p : ALL_PROTOCOLS) {
        f.present=p; f.input("x\n"); loop(); check(passCount==1 && noCardCount==0,"Auto detection");
      }
    });
    run("no-card clears LED after success",[](Fixture &f) {
      loop(); f.absent=true; loop(); check(passCount==1 && noCardCount==1 && gpio[12]==LOW,"Removal");
    });
    run("Classic data criterion auth failure differs from detection",[](Fixture &f) {
      f.authFail=true; f.input("c\n"); loop(); check(errorCount==1 && passCount==0 && gpio[12]==LOW,"Auth fail");
      f.authFail=false; loop(); check(passCount==1 && gpio[12]==HIGH,"Data recovered");
    });
    run("Type2 pages 4..7 read",[](Fixture &f) {
      f.sak=0; f.input("t\n"); loop(); check(passCount==1 && Serial.log.find("pages4..7")!=std::string::npos,"Type2 read");
    });
    run("NTAG213/215/216 version signatures",[](Fixture &f) {
      f.sak=0; for (auto storage : {0x0F,0x11,0x13}) {
        f.storage=static_cast<uint8_t>(storage); f.input("v\n"); loop(); check(passCount==1,"Version read");
      }
      check(Serial.log.find("NTAG213-reported")!=std::string::npos && Serial.log.find("NTAG215-reported")!=std::string::npos && Serial.log.find("NTAG216-reported")!=std::string::npos,"Version models");
    });
    run("GET_VERSION unsupported still allows ID distance testing",[](Fixture &f) {
      f.sak=0; f.versionUnavailable=true; f.input("v\n"); loop(); check(errorCount==1,"No version");
      f.input("u\n"); loop(); check(passCount==1,"ID fallback explicit");
    });
    run("truncated replies rejected for all native poll formats",[](Fixture &f) {
      f.shortPoll=true;
      for (auto p : {Protocol::TypeA,Protocol::TypeB,Protocol::Felica212,Protocol::Jewel}) {
        f.present=p; f.input("x\n"); fakeMillis+=1000; loop(); check(passCount==0 && errorCount==1 && gpio[12]==LOW,"Short poll");
      }
    });
    run("checksum error and missing module never light D4",[](Fixture &f) {
      f.badChecksum=true; loop(); check(errorCount==1 && !readerReady && gpio[12]==LOW,"CRC fail");
      f.timeout=0x14; fakeMillis=nextInitAt; loop(); check(!readerReady && gpio[12]==LOW,"Missing module");
    });
    run("firmware without Type B skips B and ST SR",[](Fixture &f) {
      f.support=1; f.present=Protocol::Felica212; loop(); check(passCount==1,"Capability filtering");
      check(std::none_of(f.requests.begin(),f.requests.end(),[](const Bytes &c){return c[0]==0x4A && c[2]==3;}),"No unsupported Type B");
    });
    run("manual sample distance and 20 sample window",[](Fixture &f) {
      f.input("a\nn NTAG213_sample\nd 35\n");
      for (int i=0;i<20;++i) loop();
      check(attempts==20 && passCount==20 && distanceMm==35 && windowCount==0,"Window counts");
      check(Serial.log.find("[WINDOW20] pass=20 no_card=0 errors=0 source=MANUAL distance_mm=35")!=std::string::npos,"Window report");
      f.input("d 40\n"); check(attempts==0 && distanceMm==40,"Distance starts fresh statistics");
    });
    run("invalid distance/oversized input rejected without scenario change",[](Fixture &f) {
      f.input("d 25\nd 8oops\n"+std::string(100,'a')+"\n");
      check(distanceMm==25 && Serial.log.find("line too long")!=std::string::npos,"Input bounds");
    });
    run("invalid command preserves selected profile and criterion",[](Fixture &f) {
      f.input("t\n"); const auto previousRun=runNumber; f.input("z\n");
      check(operation==Operation::Type2Read && !autoScan && runNumber==previousRun,"Invalid command is non-mutating");
    });
    std::cout << tests << " tests passed\n";
  } catch (const std::exception &e) {std::cerr << "FAIL " << e.what() << '\n'; return 1;}
  return 0;
}
