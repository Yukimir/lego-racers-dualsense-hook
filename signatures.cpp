#include <cstring>
bool CheckSignatures() {
  { const unsigned char s[] = {86,139,241,139,76,36,8,139,70,100,59,193}; if(memcmp((void*)0x430530,s,sizeof(s))) return false; }
  { const unsigned char s[] = {138,65,20,217,68,36,4,168,16,116,2,217}; if(memcmp((void*)0x41fe60,s,sizeof(s))) return false; }
  { const unsigned char s[] = {138,65,20,217,68,36,4,168,16,116,49,216}; if(memcmp((void*)0x420130,s,sizeof(s))) return false; }
  { const unsigned char s[] = {86,139,241,139,70,60,133,192,116,41,232,97}; if(memcmp((void*)0x41bc60,s,sizeof(s))) return false; }
  { const unsigned char s[] = {86,139,241,141,78,4,232,245,7,0,0,51}; if(memcmp((void*)0x4300a0,s,sizeof(s))) return false; }
  { const unsigned char s[] = {83,139,92,36,8,86,139,241,87,139,126,84}; if(memcmp((void*)0x4694b0,s,sizeof(s))) return false; }
  { const unsigned char s[] = {131,236,12,86,139,241,87,139,70,8,139,78}; if(memcmp((void*)0x435220,s,sizeof(s))) return false; }
  { const unsigned char s[] = {129,236,16,1,0,0,83,139,217,86,139,67}; if(memcmp((void*)0x44efc0,s,sizeof(s))) return false; }
  { const unsigned char s[] = {86,87,139,241,51,255,232,165,254,255,255,133}; if(memcmp((void*)0x41bb30,s,sizeof(s))) return false; }
  { const unsigned char s[] = {131,236,48,86,139,241,87,139,134,108,3,0}; if(memcmp((void*)0x448430,s,sizeof(s))) return false; }
  { const unsigned char s[] = {129,236,132,0,0,0,83,85,86,139,241,87}; if(memcmp((void*)0x4465c0,s,sizeof(s))) return false; }
  { const unsigned char s[] = {131,236,16,83,139,217,85,86,141,179,152,1}; if(memcmp((void*)0x42a900,s,sizeof(s))) return false; }
return true; }