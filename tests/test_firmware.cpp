// Tests execute production Firmware.cpp with fake hardware, not a rewritten FSM.
#include <cassert>
#include <iostream>
#include "../Firmware.cpp"

static std::string request(const std::string &method, const std::string &path,
    const std::string &body="") {
  client=EthernetClient(); client.live=true;
  client.request=method+" "+path+" HTTP/1.1\r\nHost: test\r\n";
  if(method=="POST") {
    client.request+="Content-Type: application/x-www-form-urlencoded\r\nContent-Length: "+std::to_string(body.size())+"\r\n";
  }
  client.request+="\r\n"+body;
  netPhase=0;used=bodyLength=headerBytes=0;connectedAt=fakeNow;
  contentType=lengthSeen=post=false;route=NONE;
  for(int i=0;i<1000&&client.live;++i) { networkTick(fakeNow); ++fakeNow; }
  assert(!client.live);
  return client.response;
}
static std::string mutate(const char *path, const std::string &args) {
  return request("POST",path,"r="+std::to_string(revision)+args);
}
static void has(const std::string &s,const std::string &part) { assert(s.find(part)!=std::string::npos); }
static void sample(uint16_t t,uint16_t h) {
  uint8_t a[]={uint8_t(t>>8),uint8_t(t)},b[]={uint8_t(h>>8),uint8_t(h)};
  Wire.data={a[0],a[1],sensorCrc(a),b[0],b[1],sensorCrc(b)};
  converting=true;conversionAt=fakeNow-10;sensorAt=fakeNow;
  sensorTick(fakeNow);
}
int main() {
  loadSettings();
  assert(settings.target==45 && !settings.autoWanted && power==POWER_UNKNOWN && mode==PAUSED);
  // Hysteresis, protection precedence, unknown state, timers and rollover.
  ControlInput x={1000000,0,0,4200,0,45,1,1,OFF,true,true,false,true,true,true};
  assert(decide(x)==POWER_COMMAND);
  x.now=899999;assert(decide(x)==NO_COMMAND);
  x.now=1000000;x.power=ON;x.humidity=4800;assert(decide(x)==POWER_COMMAND);
  x.powerAt=x.now;x.high=true;assert(decide(x)==POWER_COMMAND);
  x.power=POWER_UNKNOWN;assert(decide(x)==NO_COMMAND);
  x.power=ON;x.automatic=false;assert(decide(x)==NO_COMMAND);
  x.automatic=true;x.ready=false;assert(decide(x)==NO_COMMAND);
  x.ready=true;x.high=false;x.powerAt=0;x.humidity=4000;assert(decide(x)==LEVEL_COMMAND);
  x.trend=1;assert(decide(x)==NO_COMMAND);
  x.level=2;x.humidity=4300;assert(decide(x)==LEVEL_COMMAND);
  x.sleepOff=false;assert(decide(x)==NO_COMMAND);
  x.sleepOff=true;x.levelCode=false;assert(decide(x)==NO_COMMAND);
  x.power=OFF;x.humidity=4200;x.now=900100;x.powerAt=UINT32_MAX-100;
  assert(decide(x)==POWER_COMMAND);
  x.rememberedLevel=0;assert(decide(x)==NO_COMMAND);


  // Settings recovery: newest good slot survives a torn write in the other slot.
  settings.target=46;persist();settings.target=47;persist();
  EEPROM.update(settingsSlot*sizeof(Settings),0);loadSettings();assert(settings.target==46);
  fakeNow=1000000;
  uint8_t crcData[]={0xBE,0xEF};assert(sensorCrc(crcData)==0x92);
  sample(26214,28835);assert(sampleOK && temperature==2500 && humidity>=4890 && humidity<=4910);
  for(int i=0;i<10;++i){fakeNow+=1000;sample(26214,28835);}
  assert(sensorReady(fakeNow));
  mode=AUTO;fakeNow+=30001;sensorTick(fakeNow);assert(sensorFault && mode==PAUSED);
  for(int i=0;i<10;++i){fakeNow+=1000;sample(26214,28835);}
  assert(!sensorFault && mode==PAUSED);
  Wire.data={0,0,0,0,0,0};converting=true;conversionAt=fakeNow-10;sensorAt=fakeNow;
  sensorTick(fakeNow);assert(!sampleOK);
  for(int i=0;i<10;++i){fakeNow+=1000;sample(26214,28835);}

  memset(&ir,0,sizeof(ir));ir.schema=SCHEMA;ir.protocol=NEC;ir.khz=38;
  writeRecord(IR_BASE,ir);saved[0]=true;
  mode=AUTO;const auto oldRev=revision;
  has(mutate("/api/state/sync","&a=2&b=2&c=1&d=0"),"200 OK");
  assert(power==ON && level==2 && mode==PAUSED && source==1 && fakeSends==0);
  has(request("POST","/api/state/sync","r="+std::to_string(oldRev)+"&a=1&b=0&c=1&d=0"),"409");
  assert(power==ON && fakeSends==0);
  has(mutate("/api/state/sync","&a=1&b=2"),"400");assert(power==ON);
  has(mutate("/api/ir/send","&a=0"),"200 OK");assert(fakeSends==1 && power==OFF);
  has(mutate("/api/ir/send","&a=0"),"409");assert(fakeSends==1); // Rate limit.
  auto revBefore=revision;
  has(mutate("/api/target","&a=44"),"200 OK"); // No authentication header needed.
  assert(revision==revBefore+1 && settings.target==44 && fakeSends==1);
  has(mutate("/api/target","&a=99"),"400");
  has(mutate("/api/target","&a=45&a=46"),"400");
  has(request("GET","/api/ir/send"),"405");
  has(request("GET","/api/status"),"\"p\":1");
  has(request("GET","/"),"Content-Encoding: gzip");
  has(request("GET","/missing"),"404");
  // Learning/test invalidate physical state. No implicit command during sync.
  has(mutate("/api/ir/learn","&a=0"),"200 OK");
  assert(mode==PAUSED && power==POWER_UNKNOWN);
  IrReceiver.pending=true;IrReceiver.decodedIRData.protocol=NEC;learnTick(fakeNow);
  assert(learnState==2 && !tested);
  has(mutate("/api/ir/save","&a=1"),"409");
  fakeNow+=2000;
  has(mutate("/api/ir/test","&a=0&b=38"),"200 OK");assert(tested && fakeSends==2);
  has(mutate("/api/ir/save","&a=1"),"200 OK");assert(saved[0] && learnState==0);
  has(mutate("/api/state/sync","&a=2&b=1&c=1&d=0"),"200 OK");assert(fakeSends==2);
  has(mutate("/api/mode","&a=1"),"200 OK");assert(mode==AUTO);
  has(mutate("/api/ir/learn","&a=1"),"200 OK");fakeNow+=15001;learnTick(fakeNow);
  assert(learnState==3 && mode==PAUSED);
  // A stale sensor or unverified restart profile must prevent AUTO entry.
  has(mutate("/api/state/sync","&a=1&b=0&c=1&d=0"),"200 OK");
  rememberedLevel=0;
  has(mutate("/api/mode","&a=1"),"409");assert(mode==PAUSED);
  // RAW capacity/duration and receiver overflow are explicitly rejected.
  memset(&ir,0,sizeof(ir));ir.schema=SCHEMA;ir.khz=38;ir.length=101;
  assert(!validIr());ir.length=100;
  for(auto &us:ir.raw)us=3000;
  assert(!validIr());ir.length=10;assert(validIr());
  has(mutate("/api/ir/learn","&a=1"),"200 OK");
  IrReceiver.decodedIRData.flags=IRDATA_FLAGS_WAS_OVERFLOW;
  IrReceiver.pending=true;learnTick(fakeNow);assert(learnState==4);
  // Incomplete clients time out without blocking the independent controller.
  client=EthernetClient();client.live=true;netPhase=0;used=0;connectedAt=fakeNow;
  mode=AUTO;power=ON;sleepState=1;highCount=3;sampleOK=hasSample=true;sampleCount=10;sampleAt=fakeNow;
  learnState=0;lastIrAt=fakeNow-2000;controlAt=fakeNow-5000;
  saved[0]=true;memset(&ir,0,sizeof(ir));ir.schema=SCHEMA;ir.protocol=NEC;ir.khz=38;
  writeRecord(IR_BASE,ir);
  const int sendsBefore=fakeSends;controlTick(fakeNow);
  assert(fakeSends==sendsBefore+1 && power==OFF);
  fakeNow+=4001;networkTick(fakeNow);assert(!client.live);
  // Real device semantics: retain the last level over a normal OFF/ON cycle.
  const int physicalSends=fakeSends;
  for(uint8_t remembered: {1,2}) {
    has(mutate("/api/state/sync", "&a=2&b="+std::to_string(remembered)+"&c=1&d=0"),"200 OK");
    infer(POWER_COMMAND,fakeNow);assert(power==OFF && level==0 && rememberedLevel==remembered);
    infer(POWER_COMMAND,fakeNow);assert(power==ON && level==remembered && sleepState==1);
  }
  assert(fakeSends==physicalSends); // Inference and calibration never emit IR.
  // Sleep is a toggle: 2 -> sleep/1 -> normal/2, also 1 -> sleep/1 -> normal/1.
  for(uint8_t before: {1,2}) {
    has(mutate("/api/state/sync", "&a=2&b="+std::to_string(before)+"&c=1&d=0"),"200 OK");
    infer(SLEEP_COMMAND,fakeNow);assert(sleepState==2 && level==1 && resumeLevel==before);
    has(mutate("/api/mode","&a=1"),"409");
    infer(SLEEP_COMMAND,fakeNow);assert(sleepState==1 && level==before && rememberedLevel==before);
  }
  // Correcting OFF can supply the remembered level without any IR transmission.
  has(mutate("/api/state/sync","&a=1&b=2&c=1&d=0"),"200 OK");
  assert(power==OFF && level==0 && rememberedLevel==2 && fakeSends==physicalSends);
  infer(POWER_COMMAND,fakeNow);assert(level==2);
  has(mutate("/api/state/sync","&a=2&b=1&c=2&d=0"),"200 OK");
  infer(SLEEP_COMMAND,fakeNow);assert(sleepState==1 && level==0); // Unknown resume stays unknown.
  has(mutate("/api/state/sync","&a=2&b=2&c=2&d=2"),"400");
  has(mutate("/api/state/sync","&a=2&b=1&c=2&d=2"),"200 OK");
  const auto originalPowerAt=powerAt;
  infer(POWER_COMMAND,fakeNow);assert(power==ON && level==2 && sleepState==1);
  assert(powerAt==originalPowerAt); // Exiting sleep must not restart the minimum ON timer.
  infer(POWER_COMMAND,fakeNow);assert(power==OFF && rememberedLevel==2);
  infer(POWER_COMMAND,fakeNow);assert(power==ON && level==2);
  has(mutate("/api/state/sync","&a=2&b=1&c=0&d=0"),"200 OK");
  infer(POWER_COMMAND,fakeNow);assert(power==POWER_UNKNOWN);
  has(mutate("/api/state/sync","&a=2&b=1&c=2&d=2"),"200 OK");
  infer(LEVEL_COMMAND,fakeNow);assert(level==0 && sleepState==0);
  has(mutate("/api/profile","&a=1&b=1"),"410");
  has(mutate("/api/state/sync","&a=2&b=1"),"400"); // Old UI cannot confirm new fields.
  forgetState();loadSettings();assert(power==POWER_UNKNOWN && rememberedLevel==0 && sleepState==0);
  uint32_t n=0;assert(!number("4294967296",n));assert(number("4294967295",n));
  std::cout<<"All firmware tests passed\n";
}
