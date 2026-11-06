#ifdef NDEBUG
#undef NDEBUG
#endif
#include "native.cpp"
#include <cassert>
static float observed;
static void __fastcall CaptureAxis(void*,void*,float v){observed=v;}
int main(int argc,char** argv){
 originalSteer=(AxisFn)CaptureAxis;originalThrottle=(AxisFn)CaptureAxis;
 unsigned char controls[0x74]={};player1=controls;
 nativeFrame=true;framePad.x=.37f;framePad.rt=.62f;
 SteerHook(nullptr,nullptr,1);assert(fabsf(observed+.37f)<.0001f);
 assert(fabsf(Field<float>(controls,0x6c)+.37f)<.0001f); // drift observes same analog steering
 ThrottleHook(nullptr,nullptr,1);assert(fabsf(observed-.62f)<.0001f);
 ThrottleHook(nullptr,nullptr,-1);assert(observed==-1); // preserve reverse
 nativeFrame=false;SteerHook(nullptr,nullptr,.8f);assert(observed==.8f);
 short samples[480*4];Mix(samples,480);for(auto x:samples)assert(x==0);
 printf("PASS: analog steering, proportional R2, reverse, keyboard fallback, silent PCM\n");
 BYTE report[128];BuildTriggerReport(report,true,true);assert(report[11]==5&&report[22]==0x25);assert((report[1]&3)==0);
 BuildTriggerReport(report,true,false);assert(report[11]==5&&report[22]==5);
 BuildTriggerReport(report,false,true);assert(report[11]==5&&report[22]==5);
 unsigned char racer[0xccc+4]={};assert(!HasHeldPowerup(racer));for(unsigned c=1;c<=4;c++){Field<unsigned>(racer,0xccc)=c;assert(HasHeldPowerup(racer));}Field<unsigned>(racer,0xccc)=0;assert(!HasHeldPowerup(racer));
 auto energy=[](float s,bool active){EngineHaptics engine;double sum=0;float left,right;for(int i=0;i<48000;i++){engine.Next(s,active,left,right);assert(fabsf(left)<=.03001f&&fabsf(right)<=.03001f);if(i>24000)sum+=left*left+right*right;}return sum;};
 assert(energy(0,true)==0);assert(energy(110,false)==0);assert(energy(110,true)>energy(30,true)*2);
 assert(fabsf(CombineHaptics(.1f,.02f)-.12f)<.00001f);assert(CombineHaptics(1.f,1.f)==.24f);
 tireSlip=1;BuildTriggerReport(report,true,true);assert(report[11]==5&&report[20]==0&&report[22]==0x25);
 BuildTriggerReport(report,true,false);assert(report[11]==5&&report[22]==5);BuildTriggerReport(report,false,true);assert(report[11]==5&&report[22]==5);tireSlip=0;
 assert(TireSlipIntensity(4,false,0,100)==0);assert(TireSlipIntensity(0,true,8,100)==0);assert(TireSlipIntensity(4,true,0,0)==0);
 assert(TireSlipIntensity(4,true,0,100)>.9f);assert(TireSlipIntensity(4,false,8,100)>.9f);
 TireSlipHaptics tires;float skidLeft,skidRight,skidPeak=0;for(int i=0;i<48000;i++){tires.Next(110,1,30,true,skidLeft,skidRight);assert(fabsf(skidLeft)<=.32001f&&fabsf(skidRight)<=.32001f);skidPeak=std::max(skidPeak,fabsf(skidLeft));}assert(skidPeak>.24f);
 for(int i=0;i<24000;i++)tires.Next(110,0,0,true,skidLeft,skidRight);assert(fabsf(skidLeft)<.0001f);
 float slipV[]={.03f,0,.08f},up[]={0,0,1};assert(fabsf(GroundLateralSpeed(slipV,up)-30.f)<.0001f);
 GripRecoveryTracker recovery;unsigned kicks=0;LandingEvent kick;
 for(unsigned t=1000;t<1300;t+=10)assert(recovery.Step(t,true,80,30,true).strength==0);
 for(unsigned t=1300;t<1700;t+=10){auto event=recovery.Step(t,true,80,0,false);if(event.strength>0){kicks++;kick=event;}}
 assert(kicks==1&&kick.strength>.12f);
 for(unsigned t=2000;t<2040;t+=10)recovery.Step(t,true,80,30,true);
 for(unsigned t=2040;t<2300;t+=10)assert(recovery.Step(t,true,80,0,false).strength==0);
 for(unsigned t=3000;t<3300;t+=10)recovery.Step(t,true,80,30,true);
 assert(recovery.Step(3300,false,80,0,false).strength==0);for(unsigned t=3310;t<3600;t+=10)assert(recovery.Step(t,true,80,0,false).strength==0);
 puts("PASS: ground-tangent lateral speed; one regrip kick after sustained slide; no kick from brief slip or airborne transition; modulated bounded chatter");
 puts("PASS: native slip sound/mark detection; R2 always OFF even in slip; airborne/stationary/normal-steering rejection; increased bounded skid waveform and release");
 float normal[]={0,0,1},smallFall[]={.12f,0,-.01f},bigFall[]={.12f,0,-.08f},flat[]={.12f,0,0},ascending[]={0,0,.08f};
 auto lightLanding=LandingImpact(150,smallFall,normal),heavyLanding=LandingImpact(700,bigFall,normal);
 assert(lightLanding.strength>0&&heavyLanding.strength>lightLanding.strength&&heavyLanding.duration>lightLanding.duration);
 assert(LandingImpact(10,bigFall,normal).strength==0);assert(LandingImpact(700,flat,normal).strength==0);assert(LandingImpact(700,ascending,normal).strength==0);
 LandingHaptics pulse;pulse.Start(heavyLanding);float peak=0;for(int i=0;i<24000;i++){float x=pulse.Next();assert(std::isfinite(x)&&fabsf(x)<=heavyLanding.strength);peak=std::max(peak,fabsf(x));}assert(peak>0&&pulse.Next()==0);pulse.Start(heavyLanding);pulse.Reset();assert(pulse.Next()==0);
 puts("PASS: impact strength/duration increase with falling speed; no flat-road/upward/brief-contact false positives; bounded finite pulse and pause reset");
 assert(NonRoadSurface("grassc",nullptr,0,0));assert(NonRoadSurface("mmsand",nullptr,0,0));assert(NonRoadSurface("ice",nullptr,0,0));assert(NonRoadSurface("default","gravel",0,0));
 assert(!NonRoadSurface("road",nullptr,0,0));assert(!NonRoadSurface("startfin","startfin",0x40001,0));assert(NonRoadSurface("unknown","unknown",0x8000,15));
 auto terrainEnergy=[](float coverage,bool active){TerrainHaptics terrain;double energy=0;float a,b;for(int i=0;i<48000;i++){terrain.Next(110,coverage,active,a,b);assert(fabsf(a)<=.14961f&&fabsf(b)<=.14961f);energy+=a*a+b*b;}return energy;};
 assert(terrainEnergy(0,true)==0&&terrainEnergy(1,false)==0);assert(terrainEnergy(1,true)>terrainEnergy(.25f,true)*10);
 puts("PASS: game surface names and physical resistance classify non-road; plain track/checkpoints excluded; bounded rough-terrain bed scales with wheel coverage");
 if(argc>1&&strcmp(argv[1],"--logic-only")==0)return 0;
 CoInitializeEx(nullptr,COINIT_MULTITHREADED);IMMDevice* d=AudioDevice();if(d){IAudioClient* a=nullptr;d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,(void**)&a);if(a){auto f=HapticFormat();printf("4ch exclusive format support HRESULT=%08lx\n",a->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,&f.Format,nullptr));a->Release();}d->Release();}
 for(UINT i=0;i<waveOutGetNumDevs();i++){WAVEOUTCAPSA c={};waveOutGetDevCapsA(i,&c,sizeof(c));printf("audio %u: %s channels=%u\n",i,c.szPname,c.wChannels);}
 if(OpenPad()) {printf("USB DualSense accessible, output bytes=%u\n",outputLength);CloseHandle(hid);return 0;}
 printf("USB DualSense not accessible (not connected or another program hides/owns HID)\n");return 2;
}
