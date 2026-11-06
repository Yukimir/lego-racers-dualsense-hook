// LEGO Racers 1999 native DualSense hook. x86, USB DualSense.
// Internal offsets corroborated by isledecomp/racers (target SHA in README).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <mmsystem.h>
#include <dsound.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <share.h>
#include <mutex>
#include <vector>
#include "MinHook.h"

static FILE* logFile;
static std::mutex logMutex;
static void Log(const char* s) { std::lock_guard<std::mutex> l(logMutex); if(logFile){fprintf(logFile,"%lu %s\n",GetTickCount(),s);fflush(logFile);} }
template<class T> T& Field(void* p, size_t o) { return *reinterpret_cast<T*>((char*)p+o); }
struct Pad { float x=0,y=0,lt=0,rt=0; unsigned buttons=0; DWORD time=0; };
static Pad pad;
static std::mutex padMutex;
static std::atomic<float> speed{0};
static std::atomic<bool> powerupReady{false};
static std::atomic<float> roughSurface{0};
static std::atomic<float> tireSlip{0};
static std::atomic<float> lateralSlipSpeed{0};
static float TireSlipIntensity(unsigned contacts,bool skidSound,unsigned visualFlags,float actualSpeed){
 if(!contacts||actualSpeed<4.f||(!skidSound&&!(visualFlags&8)))return 0;
 return .65f+.35f*std::clamp(actualSpeed/110.f,0.f,1.f);
}
static std::atomic<DWORD> raceTick{0};
struct LandingEvent {float strength=0,duration=0;};
static float GroundLateralSpeed(const float* velocity,const float* normal){
 float n2=normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2];
 float v2=velocity[0]*velocity[0]+velocity[1]*velocity[1]+velocity[2]*velocity[2];
 float dot=velocity[0]*normal[0]+velocity[1]*normal[1]+velocity[2]*normal[2];
 float value=1000.f*sqrtf(std::max(0.f,v2-(n2>.01f?dot*dot/n2:0.f)));
 return std::isfinite(value)?value:0.f;
}
struct GripRecoveryTracker {
 unsigned previousTime=0,slidingMs=0,settledMs=0;float filtered=0,peak=0;bool armed=false;
 void Reset(){previousTime=slidingMs=settledMs=0;filtered=peak=0;armed=false;}
 LandingEvent Step(unsigned now,bool grounded,float actualSpeed,float lateral,bool slipping){
  if(!grounded||actualSpeed<8||!std::isfinite(lateral)){Reset();return {};}
  if(!previousTime){previousTime=now;filtered=lateral;return {};}
  unsigned dt=now-previousTime;previousTime=now;if(!dt)return {};
  if(dt>200){Reset();return {};}
  filtered+=(1-expf(-(float)dt/35.f))*(lateral-filtered);
  if(slipping&&filtered>=4){slidingMs+=dt;peak=std::max(peak,filtered);if(slidingMs>=100&&peak>=5)armed=true;}
  if(armed&&filtered<=std::max(2.f,peak*.25f)){
   settledMs+=dt;if(settledMs>=25){float severity=std::clamp(peak/50.f,0.f,1.f);LandingEvent event={.065f+.105f*severity,.07f+.045f*severity};Reset();previousTime=now;filtered=lateral;return event;}
  }else settledMs=0;
  if(!armed&&!slipping){slidingMs=0;peak=0;}
  return {};
 }
};
// Normal closing velocity measures the actual impact, including sloped landings.
static LandingEvent LandingImpact(unsigned airborneMs,const float* velocity,const float* normal){
 float length=sqrtf(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);
 if(airborneMs<80||length<.1f)return {};
 float impact=-1000.f*(velocity[0]*normal[0]+velocity[1]*normal[1]+velocity[2]*normal[2])/length;
 if(!std::isfinite(impact)||impact<2.f)return {};
 float severity=sqrtf(std::clamp((impact-2.f)/90.f,0.f,1.f));
 return {1.2f*(.025f+.115f*severity),.085f+.135f*severity};
}
static std::mutex landingMutex;
static LandingEvent pendingLanding;
static unsigned landingSequence=0;
static LandingEvent pendingRecovery;
static unsigned recoverySequence=0;
static GripRecoveryTracker gripRecovery;
static HANDLE hid=INVALID_HANDLE_VALUE;
static std::atomic<bool> shuttingDown{false};
static HANDLE hidWorker=nullptr;
static unsigned outputLength=48;
static bool Foreground() { DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid); return pid==GetCurrentProcessId(); }
static Pad State() { std::lock_guard<std::mutex> l(padMutex); Pad p=pad; if(GetTickCount()-p.time>250 || !Foreground()) return {};return p; }
static bool OpenPad() {
 GUID guid;HidD_GetHidGuid(&guid); HDEVINFO info=SetupDiGetClassDevs(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
 SP_DEVICE_INTERFACE_DATA iface={sizeof(iface)};
 for(DWORD i=0;SetupDiEnumDeviceInterfaces(info,nullptr,&guid,i,&iface);i++) {
  DWORD n=0;SetupDiGetDeviceInterfaceDetail(info,&iface,nullptr,0,&n,nullptr);
  std::vector<BYTE> storage(n);auto d=(SP_DEVICE_INTERFACE_DETAIL_DATA*)storage.data();d->cbSize=sizeof(*d);
  if(!SetupDiGetDeviceInterfaceDetail(info,&iface,d,n,nullptr,nullptr))continue;
  HANDLE h=CreateFile(d->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
  if(h==INVALID_HANDLE_VALUE)continue;
  HIDD_ATTRIBUTES a={sizeof(a)};PHIDP_PREPARSED_DATA prep=nullptr;HIDP_CAPS c={};
  bool ok=HidD_GetAttributes(h,&a)&&a.VendorID==0x054c&&(a.ProductID==0x0ce6||a.ProductID==0x0df2)&&HidD_GetPreparsedData(h,&prep);
  if(ok){ok=HidP_GetCaps(prep,&c)==HIDP_STATUS_SUCCESS&&c.UsagePage==1&&c.Usage==5&&c.InputReportByteLength==64;HidD_FreePreparsedData(prep);}
  if(ok){hid=h;outputLength=c.OutputReportByteLength;SetupDiDestroyDeviceInfoList(info);Log("USB HID connected");return true;}CloseHandle(h);
 }
 SetupDiDestroyDeviceInfoList(info);return false;
}
static bool WriteReport(BYTE* report) {
 OVERLAPPED ov={};ov.hEvent=CreateEvent(nullptr,TRUE,FALSE,nullptr);DWORD n=0;
 BOOL ok=WriteFile(hid,report,outputLength,&n,&ov);
 if(!ok&&GetLastError()==ERROR_IO_PENDING){if(WaitForSingleObject(ov.hEvent,100)==WAIT_OBJECT_0)ok=GetOverlappedResult(hid,&ov,&n,FALSE);else{CancelIoEx(hid,&ov);GetOverlappedResult(hid,&ov,&n,TRUE);}}
 CloseHandle(ov.hEvent);return ok&&n==outputLength;
}
static bool HasHeldPowerup(void* racer){unsigned color=Field<unsigned>(racer,0xccc);return color>=1&&color<=4;}
static void BuildTriggerReport(BYTE* r,bool active,bool hasPowerup) {
 memset(r,0,128);r[0]=2;r[1]=0x0c; // trigger enable bits; leave audio-haptics disable bits clear
 r[11]=r[22]=5; // OFF
 if(active&&hasPowerup) {
  // Weapon break-point effect: a short resistance wall followed by release.
  r[22]=0x25;r[23]=0x24;r[24]=0;r[25]=4; // zones 2 and 5, modest resistance
 }
}
static void TriggerReport(bool active) {
 BYTE r[128];BuildTriggerReport(r,active,powerupReady.load());bool ok=WriteReport(r);
 static DWORD lastLog=0;if(GetTickCount()-lastLog>2000){char s[160];sprintf_s(s,"HID output ok=%d active=%d held=%d L2mode=%02x R2mode=%02x slip=%.2f",ok,active,powerupReady.load(),r[22],r[11],tireSlip.load());Log(s);lastLog=GetTickCount();}
}
static DWORD WINAPI HidThread(void*) {
 while(!shuttingDown) {
  if(!OpenPad()){Sleep(1500);continue;}
  OVERLAPPED ov={};ov.hEvent=CreateEvent(nullptr,TRUE,FALSE,nullptr);BYTE data[64];DWORD lastOut=0;
  bool live=true;
  while(live&&!shuttingDown){ResetEvent(ov.hEvent);DWORD n=0;BOOL ok=ReadFile(hid,data,sizeof(data),&n,&ov);
   if(!ok&&GetLastError()!=ERROR_IO_PENDING)break;
   while(!ok){DWORD w=WaitForSingleObject(ov.hEvent,20);if(w==WAIT_OBJECT_0){ok=GetOverlappedResult(hid,&ov,&n,FALSE);if(!ok)live=false;break;}if(shuttingDown){CancelIoEx(hid,&ov);GetOverlappedResult(hid,&ov,&n,TRUE);live=false;break;}
    if(GetTickCount()-lastOut>40){TriggerReport(Foreground()&&GetTickCount()-raceTick.load()<200);lastOut=GetTickCount();}}
   if(ok&&n==64&&data[0]==1){Pad p;float x=(data[1]-127.5f)/127.5f;p.x=fabsf(x)<.10f?0:copysignf((fabsf(x)-.10f)/.90f,x);p.y=(data[2]-127.5f)/127.5f;p.lt=data[5]/255.f;p.rt=data[6]/255.f;p.buttons=data[8]|(data[9]<<8)|(data[10]<<16);p.time=GetTickCount();std::lock_guard<std::mutex> l(padMutex);pad=p;}
   if(GetTickCount()-lastOut>40){TriggerReport(Foreground()&&GetTickCount()-raceTick.load()<200);lastOut=GetTickCount();}
  }
  CancelIoEx(hid,&ov);TriggerReport(false);CloseHandle(ov.hEvent);CloseHandle(hid);hid=INVALID_HANDLE_VALUE;Log("USB HID disconnected");
 }
 return 0;
}
// Hook the game's controller update, not keyboard events. Retain the original drift/boost state machine.
using UpdateFn=void(__thiscall*)(void*,unsigned);
using AxisFn=void(__thiscall*)(void*,float);
static UpdateFn originalUpdate;
static AxisFn originalSteer,originalThrottle;
static thread_local bool nativeFrame=false;
static thread_local Pad framePad;
static void* player1=nullptr;
static bool fireHeld=false;
static void(__thiscall* powerup)(void*,int)=reinterpret_cast<void(__thiscall*)(void*,int)>(0x430620);
static void __fastcall UpdateHook(void* self,void*,unsigned dt) {
 if(!player1)player1=self;
 Pad p=State();bool connected=p.time!=0;
 if(self!=player1||!connected){if(self==player1&&fireHeld){powerup(self,0);fireHeld=false;}originalUpdate(self,dt);return;}
 framePad=p;nativeFrame=true;
 unsigned& flags=Field<unsigned>(self,0x5c);
 unsigned savedFlags=flags;
 flags&=~(1u|2u|4u|8u|0x80u);
 if(p.rt>.03f)flags|=4;
 if(p.buttons&0x40)flags|=8; // Circle = brake/reverse
 if(p.buttons&0x200)flags|=0x80; // R1
 bool fire=p.lt>.55f;
 if(fire!=fireHeld){powerup(self,fire);fireHeld=fire;}
 originalUpdate(self,dt);nativeFrame=false;
 flags=(flags&~(1u|2u|4u|8u|0x80u))|(savedFlags&(1u|2u|4u|8u|0x80u));
 void* racer=Field<void*>(self,0);float* v=(float*)((char*)racer+0x3e8+8);
 powerupReady=HasHeldPowerup(racer);
 // Physics integrates in milliseconds. RacerCarBody multiplies m_speed by 1000
 // before comparing to its max speed (default 110 game units / second).
 float s=1000.f*sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);if(std::isfinite(s))speed=s;
 if(!(Field<unsigned>(self,0x60)&3))raceTick=GetTickCount();
 static DWORD lastLog=0;if(GetTickCount()-lastLog>1000){char line[200];sprintf_s(line,"Drive R1=%d throttle=%.3f steering=%.3f state=%08x speed=%.3f drifting=%d",!!(p.buttons&0x200),p.rt,Field<float>(self,0x6c),Field<unsigned>(self,0x60),s,!!(Field<unsigned>(racer,0xd04)&0x80));Log(line);lastLog=GetTickCount();}
}
static void __fastcall SteerHook(void* self,void*,float value){if(nativeFrame)Field<float>(player1,0x6c)=-framePad.x;originalSteer(self,nativeFrame?-framePad.x:value);}
static void __fastcall ThrottleHook(void* self,void*,float value){originalThrottle(self,nativeFrame&&value>0?framePad.rt:value);}
using DestroyFn=void(__thiscall*)(void*);
static DestroyFn originalDestroy;
static void __fastcall DestroyHook(void* self,void*) {if(self==player1){player1=nullptr;fireHeld=false;speed=0;raceTick=0;powerupReady=false;roughSurface=0;tireSlip=0;lateralSlipSpeed=0;gripRecovery.Reset();}originalDestroy(self);}
using SnapFn=void(__thiscall*)(void*,void*);
static SnapFn originalSnap;
static void EmitLanding(void* self,unsigned airborne,const float* velocity,const float* normal){
 if(player1&&Foreground()&&GetTickCount()-raceTick.load()<200){
  void* racer=Field<void*>(player1,0);
  if(self==(char*)racer+0x3e8){
   auto event=LandingImpact(airborne,velocity,normal);
   static DWORD lastImpact=0;DWORD now=GetTickCount();
   if(event.strength>0&&now-lastImpact>160){
    {std::lock_guard<std::mutex> lock(landingMutex);pendingLanding=event;landingSequence++;}
    lastImpact=now;char line[160];sprintf_s(line,"Landing airborneMs=%u strength=%.4f durationMs=%.0f",airborne,event.strength,event.duration*1000);Log(line);
   }
  }
 }
}
static void __fastcall SnapHook(void* self,void*,void* probe){
 // After normal wheel-contact detection, before landing bounce modifies velocity.
 if(probe&&Field<unsigned>(self,0x36c)>0)EmitLanding(self,Field<unsigned>(self,0x6dc),(float*)((char*)self+8),(float*)probe);
 originalSnap(self,probe);
}
static UpdateFn originalSlideContacts;
static void __fastcall SlideContactsHook(void* self,void*,unsigned dt){
 float velocity[3];memcpy(velocity,(char*)self+8,sizeof(velocity));unsigned airborne=Field<unsigned>(self,0x6dc);
 originalSlideContacts(self,dt);
 // Drift uses a separate axle-contact path. Preserve the pre-response velocity.
 bool hit=false;for(unsigned i=0;i<2;i++)hit|=(Field<unsigned>(self,0x198+i*0x70+0x48)&2)!=0;
 float* normal=(float*)((char*)self+0x174);float* after=(float*)((char*)self+8);
 float response=(after[0]-velocity[0])*normal[0]+(after[1]-velocity[1])*normal[1]+(after[2]-velocity[2])*normal[2];
 if(Field<unsigned>(self,0x36c)>0||(hit&&response>.005f))EmitLanding(self,airborne,velocity,normal);
}
static bool NonRoadSurface(const char* material,const char* target,unsigned flags,float resistance){
 // Names are corroborated by the installed game's TMB surface tables. Ordinary
 // track/checkpoint materials have no surface target; do not treat unknown as dirt.
 const char* tags[]={"GRASS","GRAS","SAND","SNOW","ICE","SWAMP","DIRT","GRAVEL","NUBS","WATER","STREAM","WOOD","CURB","MMROCC","CROCK","MUD","LAVA"};
 char m[9]={},t[9]={};if(material)strncpy_s(m,material,8);if(target)strncpy_s(t,target,8);_strupr_s(m);_strupr_s(t);
 for(auto tag:tags)if(strstr(m,tag)||strstr(t,tag))return true;
 return ((flags&0x8000)&&std::isfinite(resistance)&&resistance>0)||((flags&0x400)!=0);
}
static DestroyFn originalWheelSurfaces;
static void __fastcall WheelSurfacesHook(void* self,void*){
 originalWheelSurfaces(self);
 if(!player1||self!=(char*)Field<void*>(player1,0)+0x3e8)return;
 unsigned touched=0,roughWheels=0;char selected[9]={};
 for(unsigned i=0;i<4;i++){char* probe=(char*)self+0x198+i*0x70;
  if(!(Field<unsigned>(probe,0x48)&1))continue;touched++;
  void* record=Field<void*>(probe,0x3c);if(!record)continue;
  void* target=Field<void*>(record,0x14);char material[9]={},surface[9]={};memcpy(material,(char*)record+0x18,8);
  unsigned flags=0;float resistance=0;if(target){memcpy(surface,target,8);flags=Field<unsigned>(target,8);resistance=Field<float>(target,0x58);}
  if(NonRoadSurface(material,surface,flags,resistance)){roughWheels++;memcpy(selected,*surface?surface:material,8);}
 }
 roughSurface=touched?roughWheels/4.f:0.f;
 void* racer=Field<void*>(player1,0);
 bool skidSound=Field<void*>(self,0x6e8)!=nullptr;unsigned visualFlags=Field<unsigned>(racer,0x18);
 float slip=TireSlipIntensity(touched,skidSound,visualFlags,speed.load());tireSlip=slip;
 float lateral=GroundLateralSpeed((float*)((char*)self+0x61c),(float*)((char*)self+0x174));lateralSlipSpeed=lateral;
 bool driving=Foreground()&&GetTickCount()-raceTick.load()<200;
 auto recovery=gripRecovery.Step(GetTickCount(),driving&&touched>0&&!Field<unsigned>(self,0x3ec),speed.load(),lateral,slip>0);
 if(recovery.strength>0){std::lock_guard<std::mutex> lock(landingMutex);pendingRecovery=recovery;recoverySequence++;char line[140];sprintf_s(line,"GripRecovery lateral=%.2f strength=%.4f durationMs=%.0f",lateral,recovery.strength,recovery.duration*1000);Log(line);}
 static bool wasSlipping=false;if((slip>0)!=wasSlipping){char line[140];sprintf_s(line,"TireSlip active=%d sound=%d skidMarks=%d contacts=%u intensity=%.2f",slip>0,skidSound,!!(visualFlags&8),touched,slip);Log(line);wasSlipping=slip>0;}
 static float previous=-1;static char lastName[9]={};if(previous!=roughSurface.load()||strcmp(lastName,selected)){
  char line[130];sprintf_s(line,"Terrain contacts=%u roughWheels=%u surface=%s roughness=%.2f",touched,roughWheels,selected,roughSurface.load());Log(line);previous=roughSurface;strcpy_s(lastName,selected);
 }
}
// Native game event dispatch, on the game thread. No OS keyboard injection.
struct NavState {unsigned held=0;DWORD repeatAt=0;void* owner=nullptr;};
static unsigned Navigation(const Pad& p){if(!p.time)return 0;unsigned mask=0,hat=p.buttons&15;
 if(hat==0||hat==1||hat==7||p.y<-.55f)mask|=1;
 if(hat>=3&&hat<=5||p.y>.55f)mask|=2;
 if(hat>=5&&hat<=7||p.x<-.55f)mask|=4;
 if(hat>=1&&hat<=3||p.x>.55f)mask|=8;
 if(p.buttons&0x40)mask|=16; // Circle: back
 if(p.buttons&0x2000)mask|=32; // Options: pause
 if(p.buttons&0x20)mask|=64; // Cross: confirm
 return mask;}
template<class Emit>static void DispatchNav(NavState& s,void* owner,Emit emit){unsigned now=Navigation(State());DWORD tick=GetTickCount();if(s.owner!=owner){s={0,0,owner};}
 for(unsigned bit=1;bit<=64;bit<<=1)if((now^s.held)&bit)emit(bit,(now&bit)!=0,false);
 if(now&15){if((now&15)!=(s.held&15))s.repeatAt=tick+400;else if((int)(tick-s.repeatAt)>=0){for(unsigned bit=1;bit<=8;bit<<=1)if(now&bit)emit(bit,true,true);s.repeatAt=tick+120;}}
 s.held=now;}
using MenuFn=int(__thiscall*)(void*,unsigned);
static MenuFn originalMenu;
static NavState menuNav,raceNav;
static int __fastcall MenuHook(void* self,void*,unsigned elapsed){
 void* queue=Field<void*>(self,0x0c);void* manager=Field<void*>(self,8);void* source=manager?Field<void*>(manager,0x9c):nullptr;
 DispatchNav(menuNav,Field<void*>(self,0x54),[&](unsigned bit,bool down,bool repeat){unsigned code=bit==1?0x40000003:bit==2?0x40000002:bit==4?0x40000000:bit==8?0x40000001:bit==64?0x30000004:0x10000001;
 if(!queue)return;auto e=reinterpret_cast<void*(__thiscall*)(void*,void*,unsigned,unsigned)>(0x44b820)(queue,source,code,GetTickCount());if(e){Field<BYTE>(e,0x0c)=down;Field<BYTE>(e,0x0d)=repeat;Log("Native menu event");}});
 return originalMenu(self,elapsed);
}
using RaceFn=void(__thiscall*)(void*);
static RaceFn originalRace;
static void __fastcall RaceHook(void* self,void*){
 DispatchNav(raceNav,self,[&](unsigned bit,bool down,bool){if(bit==16||bit==4||bit==8)return;unsigned code=bit==1?0x40000003:bit==2?0x40000002:bit==64?0x3000000d:0x10000001;
 reinterpret_cast<void(__thiscall*)(void*,unsigned)>(down?0x435c00:0x435cc0)(self,code);Log("Native race dialog event");});originalRace(self);
}
using PollFn=int(__thiscall*)(void*,int);
static PollFn originalPoll;
static int __fastcall PollHook(void* self,void*,int elapsed){const char* name=(char*)self+0x60;if(strstr(name,"DualSense")||strstr(name,"Wireless Controller"))return 0;return originalPoll(self,elapsed);}

// Snapshot only SoundBuffer PCM. DirectMusicStream does not pass through this hook.
struct Voice {IDirectSoundBuffer* ds;std::vector<BYTE> pcm;unsigned channels,bits,rate;};
static std::mutex voiceMutex;
static std::vector<Voice> voices;
using StartFn=void(__thiscall*)(void*);
static StartFn originalStart;
static void RegisterSound(void* self) {
 // m_streaming selects the spatial sound-instance owner, not music.
 // Include these buffers: engine, surfaces and positional effects use them.
 // Music is owned by DirectMusicStream and never reaches either SoundBuffer hook.
 auto ds=Field<IDirectSoundBuffer*>(self,0x3c);unsigned size=Field<unsigned>(self,0x44);
 if(!ds||!size||size>16*1024*1024)return;
 WAVEFORMATEX fmt={};if(FAILED(ds->GetFormat(&fmt,sizeof(fmt),nullptr))||fmt.wFormatTag!=WAVE_FORMAT_PCM||(fmt.wBitsPerSample!=8&&fmt.wBitsPerSample!=16)||fmt.nChannels>2||!fmt.nChannels)return;
 std::lock_guard<std::mutex> lock(voiceMutex);
  for(size_t i=0;i<voices.size();i++)if(voices[i].ds==ds){voices[i].ds->Release();voices.erase(voices.begin()+i);break;}
 if(voices.size()>=64)return;
 void *a,*b;DWORD na,nb;
 if(FAILED(ds->Lock(0,size,&a,&na,&b,&nb,DSBLOCK_ENTIREBUFFER)))return;
 Voice v={ds,{},fmt.nChannels,fmt.wBitsPerSample,fmt.nSamplesPerSec};v.pcm.resize(na+nb);memcpy(v.pcm.data(),a,na);if(nb)memcpy(v.pcm.data()+na,b,nb);ds->Unlock(a,na,b,nb);ds->AddRef();voices.push_back(std::move(v));Log("SFX PCM registered");
}
static void __fastcall StartHook(void* self,void*) {originalStart(self);RegisterSound(self);}
using PlayFn=int(__thiscall*)(void*,int);
static PlayFn originalPlay;
static int __fastcall PlayHook(void* self,void*,int loop){int result=originalPlay(self,loop);if(result)RegisterSound(self);return result;}
// A quiet continuous stereo engine bed, independent of the SFX volume and waveform.
// Peak at full speed is 3% PCM; SFX retain their separate 20% peak budget.
struct EngineHaptics {
 double phase=0;float amplitude=0;
 void Next(float actualSpeed,bool active,float& left,float& right){
  float normalized=std::clamp(actualSpeed/110.f,0.f,1.f);
  float target=active&&actualSpeed>1.f?.0048f+.0252f*normalized:0.f;
  amplitude+=.001f*(target-amplitude); // ~21 ms smoothing, avoids switching clicks
  phase+=6.283185307179586*(45.f+45.f*normalized)/48000.;
  if(phase>=6.283185307179586)phase-=6.283185307179586;
  left=amplitude*(float)sin(phase);right=amplitude*(float)sin(phase+.12);
 }
};
struct LandingHaptics {
 LandingEvent event;float elapsed=1;
 void Start(LandingEvent next){event=next;elapsed=0;}
 void Reset(){elapsed=1;}
 float Next(){
  if(elapsed>=event.duration)return 0;
  float t=elapsed;elapsed+=1.f/48000.f;
  // Fast contact tap followed by a lower-frequency chassis thump and short tail.
  float attack=std::min(t/.003f,1.f),tail=1.f-t/event.duration;
  float low=sinf(6.2831853f*55.f*t)*expf(-t*14.f);
  float tap=.35f*sinf(6.2831853f*180.f*t)*expf(-t*65.f);
  return event.strength*attack*tail*(low+tap)/1.35f;
 }
};
struct TerrainHaptics {
 double phase=0;float amplitude=0,noise[2]={};unsigned random=0x92a73b51;
 void Next(float actualSpeed,float roughness,bool active,float& left,float& right){
  float v=std::clamp(actualSpeed/110.f,0.f,1.f);
  float target=active&&actualSpeed>1.f?std::clamp(roughness,0.f,1.f)*(.0396f+.110f*v):0;
  amplitude+=.001f*(target-amplitude);phase+=6.283185307179586*(65+45*v)/48000.;if(phase>=6.283185307179586)phase-=6.283185307179586;
  float* output[]={&left,&right};for(unsigned c=0;c<2;c++){
   random^=random<<13;random^=random>>17;random^=random<<5;
   float sample=(random&65535)/32767.5f-1;noise[c]+=.12f*(sample-noise[c]);
   *output[c]=amplitude*(.55f*(float)sin(phase+c*.3)+.45f*noise[c]);
  }
 }
};
struct TireSlipHaptics {
 double phase=0,grain=0,slow=0;float amplitude=0,noise[2]={};unsigned random=0x58a431eb;
 void Next(float actualSpeed,float slip,float lateral,bool active,float& left,float& right){
  float v=std::clamp(actualSpeed/110.f,0.f,1.f);
  float sideways=std::clamp(lateral/40.f,0.f,1.f);
  float target=active?std::clamp(slip,0.f,1.f)*(.20f+.12f*v):0;
  amplitude+=.0015f*(target-amplitude);
  phase+=6.283185307179586*(130+40*v)/48000.;if(phase>=6.283185307179586)phase-=6.283185307179586;
  slow+=6.283185307179586*2.3/48000.;if(slow>=6.283185307179586)slow-=6.283185307179586;
  grain+=6.283185307179586*(9+16*sideways)*(1+.12*sin(slow))/48000.;if(grain>=6.283185307179586)grain-=6.283185307179586;
  float modulation=.55f+.45f*(float)(sin(grain)*sin(grain));
  float* output[]={&left,&right};for(unsigned c=0;c<2;c++){
   random^=random<<13;random^=random>>17;random^=random<<5;
   noise[c]+=.18f*((random&65535)/32767.5f-1-noise[c]);
   *output[c]=amplitude*modulation*(.86f*(float)sin(phase+c*.25)+.14f*noise[c]);
  }
 }
};
static float CombineHaptics(float sound,float engine){return std::clamp(sound+engine,-.24f,.24f);}
static void Mix(short* out,unsigned frames) {
 std::vector<float> mix(frames*2,0.f);
 {std::lock_guard<std::mutex> lock(voiceMutex);
 for(size_t j=0;j<voices.size();) {
  Voice& v=voices[j];DWORD status=0,cursor=0,freq=v.rate;LONG volume=0,pan=0;
  if(FAILED(v.ds->GetStatus(&status))||!(status&DSBSTATUS_PLAYING)){v.ds->Release();voices.erase(voices.begin()+j);continue;}
  v.ds->GetCurrentPosition(&cursor,nullptr);v.ds->GetFrequency(&freq);v.ds->GetVolume(&volume);v.ds->GetPan(&pan);
  unsigned stride=v.channels*(v.bits/8),count=(unsigned)v.pcm.size()/stride;
  float gain=powf(10.f,volume/2000.f),gl=gain*powf(10.f,-std::max(pan,0L)/2000.f),gr=gain*powf(10.f,std::min(pan,0L)/2000.f);
  for(unsigned i=0;i<frames;i++){unsigned k=cursor/stride+(unsigned)(i*freq/48000.);if(k>=count){if(status&DSBSTATUS_LOOPING)k%=count;else break;}
   auto sample=[&](unsigned c){const BYTE* b=v.pcm.data()+k*stride+c*(v.bits/8);return v.bits==16?*reinterpret_cast<const short*>(b)/32768.f:(*b-128)/128.f;};
   mix[2*i]+=sample(0)*gl;mix[2*i+1]+=sample(v.channels-1)*gr;
  }j++;
 }}
 // DC removal and low-pass; bounded stereo waveforms, front speaker channels silent.
 static float low[2]={},dc[2]={};static EngineHaptics engine;static TerrainHaptics terrain;static TireSlipHaptics tires;bool active=Foreground();
 bool engineActive=active&&!shuttingDown&&GetTickCount()-raceTick.load()<200;
 float actualSpeed=speed.load();
 static LandingHaptics landing;static unsigned observedLanding=0;
 static LandingHaptics regrip;static unsigned observedRecovery=0;
 {std::lock_guard<std::mutex> lock(landingMutex);if(observedLanding!=landingSequence){observedLanding=landingSequence;if(engineActive)landing.Start(pendingLanding);}}
 {std::lock_guard<std::mutex> lock(landingMutex);if(observedRecovery!=recoverySequence){observedRecovery=recoverySequence;if(engineActive)regrip.Start(pendingRecovery);}}
 if(!engineActive){landing.Reset();regrip.Reset();}
 float roughness=roughSurface.load();
 float slip=tireSlip.load();
 float lateral=lateralSlipSpeed.load();
 for(unsigned i=0;i<frames;i++){float bed[2],road[2],skid[2];engine.Next(actualSpeed,engineActive,bed[0],bed[1]);terrain.Next(actualSpeed,roughness,engineActive,road[0],road[1]);tires.Next(actualSpeed,slip,lateral,engineActive,skid[0],skid[1]);float impact=landing.Next()+regrip.Next();
  for(unsigned c=0;c<2;c++){float x=active?mix[2*i+c]:0;dc[c]+=.003f*(x-dc[c]);low[c]+=.035f*(x-dc[c]-low[c]);float y=CombineHaptics(.20f*tanhf(low[c]*2.f),bed[c]+impact+road[c]+skid[c]);out[4*i+c]=0;out[4*i+2+c]=(short)(y*32767);}}
 static DWORD lastLog=0;if(GetTickCount()-lastLog>2000){int peak=0;for(unsigned i=0;i<frames;i++)peak=std::max(peak,std::max(abs((int)out[4*i+2]),abs((int)out[4*i+3])));char s[160];sprintf_s(s,"PCM frames=%u peak=%d foreground=%d engineActive=%d speed=%.2f engineGain=%.4f",frames,peak,active,engineActive,actualSpeed,engine.amplitude);Log(s);lastLog=GetTickCount();}
}
static IMMDevice* AudioDevice() {
 IMMDeviceEnumerator* e=nullptr;IMMDeviceCollection* devices=nullptr;IMMDevice* found=nullptr;
 if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&e))))return nullptr;
 if(SUCCEEDED(e->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices))){UINT n=0;devices->GetCount(&n);
  for(UINT i=0;i<n;i++){IMMDevice* d=nullptr;IPropertyStore* props=nullptr;PROPVARIANT name;PropVariantInit(&name);devices->Item(i,&d);
   if(SUCCEEDED(d->OpenPropertyStore(STGM_READ,&props))){props->GetValue(PKEY_Device_FriendlyName,&name);if(name.vt==VT_LPWSTR&&wcsstr(name.pwszVal,L"DualSense")){found=d;d=nullptr;}PropVariantClear(&name);props->Release();}
   if(d)d->Release();if(found)break;
  }devices->Release();
 }e->Release();return found;
}
static WAVEFORMATEXTENSIBLE HapticFormat(){WAVEFORMATEXTENSIBLE f={};f.Format={WAVE_FORMAT_EXTENSIBLE,4,48000,384000,8,16,22};f.Samples.wValidBitsPerSample=16;f.dwChannelMask=0x33;f.SubFormat=KSDATAFORMAT_SUBTYPE_PCM;return f;}
static DWORD WINAPI AudioThread(void*) {
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);IMMDevice* device=AudioDevice();if(!device){Log("ERROR: no DualSense audio endpoint");return 1;}
 IAudioClient* client=nullptr;HRESULT hr=device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,(void**)&client);
 if(FAILED(hr)){device->Release();return 1;}auto f=HapticFormat();
 HANDLE event=CreateEvent(nullptr,FALSE,FALSE,nullptr);
 hr=client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,100000,100000,&f.Format,nullptr);
 if(hr==AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED){UINT aligned=0;client->GetBufferSize(&aligned);client->Release();device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,(void**)&client);REFERENCE_TIME t=(REFERENCE_TIME)(10000000.0*aligned/48000+.5);hr=client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE,AUDCLNT_STREAMFLAGS_EVENTCALLBACK,t,t,&f.Format,nullptr);}
 device->Release();
 if(FAILED(hr)){char s[100];sprintf_s(s,"ERROR: 4-channel WASAPI exclusive init HRESULT=%08lx",hr);Log(s);client->Release();CloseHandle(event);return 1;}
 client->SetEventHandle(event);UINT frames=0;client->GetBufferSize(&frames);IAudioRenderClient* render=nullptr;
 if(FAILED(client->GetService(IID_PPV_ARGS(&render)))){client->Release();CloseHandle(event);return 1;}
 BYTE* data=nullptr;render->GetBuffer(frames,&data);render->ReleaseBuffer(frames,AUDCLNT_BUFFERFLAGS_SILENT);client->Start();Log("4-channel WASAPI PCM endpoint open");
 while(!shuttingDown&&WaitForSingleObject(event,2000)==WAIT_OBJECT_0){if(FAILED(render->GetBuffer(frames,&data)))break;Mix((short*)data,frames);if(FAILED(render->ReleaseBuffer(frames,0)))break;}
 client->Stop();render->Release();client->Release();CloseHandle(event);Log("PCM endpoint stopped");return 0;
}
static bool Hook(void* target,void* replacement,void** original){return MH_CreateHook(target,replacement,original)==MH_OK;}
static decltype(&ExitProcess) originalExit;
static void WINAPI ExitHook(UINT code){shuttingDown=true;if(hidWorker)WaitForSingleObject(hidWorker,700);originalExit(code);}
static void Initialize() {
 logFile=_fsopen("ds5-native.log","a",_SH_DENYNO);
 if(GetModuleHandle(nullptr)!=(HMODULE)0x400000){Log("Unsupported module base; hooks refused");return;}
 // Version gate: generated signatures cover every patched function's original prologue.
 extern bool CheckSignatures();if(!CheckSignatures()){Log("Unsupported EXE signatures; hooks refused");return;}
 if(MH_Initialize()!=MH_OK)return;
 bool ok=Hook((void*)0x430530,UpdateHook,(void**)&originalUpdate)&&Hook((void*)0x41fe60,SteerHook,(void**)&originalSteer)&&Hook((void*)0x420130,ThrottleHook,(void**)&originalThrottle)&&Hook((void*)0x41bc60,StartHook,(void**)&originalStart)&&Hook((void*)0x4300a0,DestroyHook,(void**)&originalDestroy)&&Hook((void*)0x4694b0,MenuHook,(void**)&originalMenu)&&Hook((void*)0x435220,RaceHook,(void**)&originalRace)&&Hook((void*)0x44efc0,PollHook,(void**)&originalPoll);
 ok=ok&&Hook((void*)0x41bb30,PlayHook,(void**)&originalPlay);
 ok=ok&&Hook((void*)0x448430,SnapHook,(void**)&originalSnap);
 ok=ok&&Hook((void*)0x4465c0,SlideContactsHook,(void**)&originalSlideContacts);
 ok=ok&&Hook((void*)0x42a900,WheelSurfacesHook,(void**)&originalWheelSurfaces);
 if(!ok){MH_Uninitialize();Log("Hook preparation failed; no hooks enabled");return;}
 if(!Hook((void*)ExitProcess,ExitHook,(void**)&originalExit)){MH_Uninitialize();Log("Exit cleanup hook failed");return;}
 if(MH_EnableHook(MH_ALL_HOOKS)!=MH_OK){Log("Hook enable failed");return;}
 Log("Native input + SFX hooks installed");hidWorker=CreateThread(nullptr,0,HidThread,nullptr,0,nullptr);CloseHandle(CreateThread(nullptr,0,AudioThread,nullptr,0,nullptr));
}
static HMODULE realInput;
extern "C" HRESULT WINAPI NativeDirectInputCreateA(HINSTANCE h,DWORD v,void** out,void* u) {
 static std::once_flag once;std::call_once(once,[]{char path[MAX_PATH];GetSystemDirectoryA(path,MAX_PATH);strcat_s(path,"\\dinput.dll");realInput=LoadLibraryA(path);Initialize();});
 auto fn=(HRESULT(WINAPI*)(HINSTANCE,DWORD,void**,void*))GetProcAddress(realInput,"DirectInputCreateA");return fn?fn(h,v,out,u):E_FAIL;
}
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,void*){if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(h);return TRUE;}
