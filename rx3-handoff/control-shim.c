/* RX3 v1.19 input adapter: use the firmware's message-queued key entry. */
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
extern int close(int);
extern char *program_invocation_short_name;
extern int pthread_create(unsigned long*,const void*,void *(*)(void*),void*);
struct command {int key,operation,channel,value;float analog;int extra;};
/* State query (key 0xFFFF on the control FIFO): dump engine state via the firmware's own DjEngineIF getters to /tmp/rx3-query.txt. */
static char *putnum(char *p,long v){char t[16];int n=0;if(v<0){*p++='-';v=-v;}do{t[n++]='0'+v%10;v/=10;}while(v);while(n)*p++=t[--n];return p;}
static char *putf(char *p,float f){long m=(long)(f*1000+(f<0?-0.5f:0.5f));p=putnum(p,m/1000);*p++='.';long r=m<0?-m%1000:m%1000;*p++='0'+r/100;*p++='0'+(r/10)%10;*p++='0'+r%10;return p;}
static void query_state(void){
 int (*route)(void*,int)=(void*)0x50708,(*xfa)(void*,int)=(void*)0x4ccc4,(*cfxt)(void*,int)=(void*)0x4e37c,(*playing)(void*,int)=(void*)0x45984,(*realmix)(void*)=(void*)0x4e7f8;
 float (*fader)(void*,int)=(void*)0x4c744,(*trim)(void*,int)=(void*)0x4c51c,(*cfxc)(void*,int)=(void*)0x4e4e4,(*tempo)(void*,int)=(void*)0x45f24;
 void *eng=*(void **)0x011492d8;if(!eng)return;   /* DjEngineIF singleton (same one allinone_debug::mixeron uses) */
 char buf[512],*p=buf;
 for(int i=0;i<2;i++){const char *l="input";while(*l)*p++=*l++;*p++='0'+i;const char *k=" route=";while(*k)*p++=*k++;p=putnum(p,route(eng,i));k=" xfassign=";while(*k)*p++=*k++;p=putnum(p,xfa(eng,i));k=" fader=";while(*k)*p++=*k++;p=putf(p,fader(eng,i));k=" trim=";while(*k)*p++=*k++;p=putf(p,trim(eng,i));k=" cfxtype=";while(*k)*p++=*k++;p=putnum(p,cfxt(eng,i));k=" cfxcolor=";while(*k)*p++=*k++;p=putf(p,cfxc(eng,i));*p++='\n';}
 for(int i=0;i<2;i++){const char *l="player";while(*l)*p++=*l++;*p++='0'+i;const char *k=" playing=";while(*k)*p++=*k++;p=putnum(p,playing(eng,i));k=" tempo=";while(*k)*p++=*k++;p=putf(p,tempo(eng,i));*p++='\n';}
 const char *k="realmixer=";while(*k)*p++=*k++;p=putnum(p,realmix(eng));*p++='\n';
 int fd=open("/tmp/rx3-query.txt",01|0100|01000,0644);if(fd>=0){write(fd,buf,p-buf);close(fd);}
}
/* ---- Panel LEDs ----
   On the real unit the firmware drives its button lights through panel microcontrollers on SPI; here nothing answers,
   so it never sends them. The state is still computed: every 20 ms ui::PanelComController::timerCallback (UiMain)
   rebuilds the uif::LedStat held at LedManager+48. Find that LedManager once (through the PanelComController, by its
   vtable) and publish the LEDs to /tmp/rx3-leds for the controller bridge.
   LedStat: +4 u16 used, +8 Led[44 bytes], +12 u16 ids, +14 u16 per-id slots (one per deck), +16 u16 index[id*slots+deck-1]
   (0 = unlit, else 1-based Led). Led: +16 state (1 lit, 2 blinking), +20 brightness (0 full, 1 dim), +28 blink period
   in ms, +40..42 RGB. Published per id and deck, 8 bytes: present, state, brightness, r, g, b, period lo, hi.
   After the table: the two mixer channels' levels as int32 (djengine::DjEngineIF::getInputChLevelMono, the value
   ui::Mixer::MonoLvMeter turns into the RX3's channel meter), for the controller's VU meters. */
extern int sscanf(const char*,const char*,...);
#define VT_PANELCOM (0x4cfb08+8)
#define VT_LEDMGR (0x4d5e60+8)
static struct {unsigned lo,hi,rwp;} regions[1024];static int nregions,scanned,hits;
static int readable(unsigned a,unsigned len){for(int i=0;i<nregions;i++)if(a>=regions[i].lo&&a+len<=regions[i].hi)return 1;return 0;}
static int plausible_mgr(unsigned m){
 if(m&3||!readable(m,128)||*(const unsigned*)m!=VT_LEDMGR)return 0;const unsigned char *st=(const unsigned char*)m+48;
 unsigned ids=*(const unsigned short*)(st+12),slots=*(const unsigned short*)(st+14),leds=*(const unsigned*)(st+8),idx=*(const unsigned*)(st+16);
 return ids>0&&ids<=256&&slots>=1&&slots<=4&&readable(leds,44)&&readable(idx,ids*slots*2);}
/* Readable mappings from /hostproc/self/maps; the PanelComController lives on the heap and holds its LedManager at +88. */
static const unsigned char *find_led_manager(void){
 static char buf[131072];int fd=open("/hostproc/self/maps",O_RDONLY);if(fd<0)return 0;   /* the chroot's /proc is the firmware's fake one; mount-rx3.sh mounts a real one here */
 int n=0,r;while(n<(int)sizeof(buf)-1&&(r=read(fd,buf+n,sizeof(buf)-1-n))>0)n+=r;close(fd);buf[n]=0;
 nregions=0;
 for(char *line=buf;*line&&nregions<1024;){unsigned lo,hi;char perm[5];
  if(sscanf(line,"%x-%x %4s",&lo,&hi,perm)==3&&perm[0]=='r'){regions[nregions].lo=lo;regions[nregions].hi=hi;regions[nregions].rwp=perm[1]=='w'&&perm[3]=='p';nregions++;}
  char *nl=strchr(line,'\n');if(!nl)break;line=nl+1;}
 scanned=hits=0;
 for(int i=0;i<nregions;i++){if(!regions[i].rwp||regions[i].hi-regions[i].lo>(256u<<20))continue;scanned++;
  for(const unsigned *w=(const unsigned*)regions[i].lo;w<(const unsigned*)regions[i].hi;w++)
   if(*w==VT_PANELCOM&&readable((unsigned)w,92)){unsigned m=w[22];hits++;if(plausible_mgr(m))return (const unsigned char*)m;}}
 return 0;
}
#define LED_IDS 64
static void snapshot_leds(const unsigned char *mgr,unsigned char *out){
 const unsigned char *st=mgr+48;unsigned ids=*(const unsigned short*)(st+12),slots=*(const unsigned short*)(st+14);
 const unsigned char *leds=*(const unsigned char*const*)(st+8);const unsigned short *idx=*(const unsigned short*const*)(st+16);
 for(unsigned id=0;id<LED_IDS;id++)for(unsigned d=0;d<2;d++){unsigned char *o=out+16+(id*2+d)*8;
  unsigned e=id<ids&&d<slots?idx[id*slots+d]:0;
  if(!e){for(int k=0;k<8;k++)o[k]=0;continue;}
  const unsigned char *L=leds+44*(e-1);unsigned per=*(const unsigned*)(L+28);
  o[0]=1;o[1]=L[16];o[2]=L[20];o[3]=L[40];o[4]=L[41];o[5]=L[42];o[6]=per&255;o[7]=(per>>8)&255;}
}
static void put_levels(unsigned char *o){
 long (*level)(void*,int)=(void*)0x50170;   /* DjEngineIF::getInputChLevelMono(EnMixerInput); uses the global engine */
 if(!*(void *volatile *)0x011493c0){for(int k=0;k<8;k++)o[k]=0;return;}
 for(int ch=0;ch<2;ch++){long v=level(0,ch);for(int k=0;k<4;k++)o[ch*4+k]=(unsigned long)v>>(8*k);}
}
/* The firmware's audio thread writes its outputs continuously. Once (2026-10-02) it was found spinning in a SIGSEGV
   loop in playengine::BeatSync::checkPrecision (null+0x24), its handler returning to the faulting instruction, and
   all audio stopped for good; the trigger is unknown. Say so in the player log when the writes stop. */
extern long long rx3_last_audio_write;extern long long rx3_now_us(void);
static void watch_audio(void){
 static int stalled;long long last=rx3_last_audio_write;if(!last)return;
 int now_stalled=rx3_now_us()-last>3000000;
 if(now_stalled&&!stalled){const char m[]="RX3 audio: the firmware's audio thread has stopped writing for 3 s (crash loop?); restart the player\n";write(2,m,sizeof(m)-1);}
 if(!now_stalled&&stalled){const char m[]="RX3 audio: the firmware's audio thread is writing again\n";write(2,m,sizeof(m)-1);}
 stalled=now_stalled;
}
static void *led_thread(void *unused){
 const unsigned char *mgr=0;
 for(int tries=0;!mgr;tries++){sleep(2);mgr=find_led_manager();if(tries==30&&!mgr){char m[96];int k=0;const char *t="RX3 LEDs: LedManager not found (maps ";while(*t)m[k++]=*t++;
   unsigned v[3]={nregions,scanned,hits};for(int j=0;j<3;j++){char d[12];int q=0;unsigned x=v[j];do{d[q++]='0'+x%10;x/=10;}while(x);while(q)m[k++]=d[--q];m[k++]=j<2?'/':')';}
   m[k++]='\n';write(2,m,k);return 0;}}
 const char m[]="RX3 LEDs: publishing the panel LEDs to /tmp/rx3-leds\n";write(2,m,sizeof(m)-1);
 int fd=open("/tmp/rx3-leds",O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd<0)return 0;
 static unsigned char a[16+LED_IDS*2*8+8],b[sizeof(a)],last[sizeof(a)];unsigned seq=0;
 a[0]='R';a[1]='X';a[2]='L';a[3]='1';a[8]=LED_IDS;
 for(;;){
  usleep(20000);watch_audio();
  /* The firmware clears and refills the table every 20 ms; publish only a state seen twice in a row. */
  snapshot_leds(mgr,a+0);usleep(1500);for(int k=0;k<16;k++)b[k]=a[k];snapshot_leds(mgr,b);
  if(memcmp(a+16,b+16,LED_IDS*2*8))continue;
  put_levels(a+16+LED_IDS*2*8);
  if(!memcmp(a+16,last+16,sizeof(a)-16))continue;
  seq++;a[4]=seq;a[5]=seq>>8;a[6]=seq>>16;a[7]=seq>>24;pwrite(fd,a,sizeof(a),0);for(unsigned k=0;k<sizeof(a);k++)last[k]=a[k];
 }
 return 0;
}
static void *setup_thread(void *unused){
 /* Crossfader assignment normally comes from the CROSS FADER CURVE panel switch (one position = THRU, which leaves the
    crossfader inert). Assign CH1=A, CH2=B the way the firmware's own "mixeron" debug command does, once the engine exists. */
 while(!*(void *volatile *)0x011493c0)sleep(1);
 void (*xf_assign)(void*,int,int)=(void*)0x4cc0c;  /* djengine::DjEngineIF::setCrossFaderAssign(EnMixerInput,EnCrossFaderAssign); uses the global engine */
 xf_assign(0,0,1);xf_assign(0,1,2);
 /* Player-to-mixer routing normally follows the INPUT SELECT panel switches; without them both mixer inputs end up fed by
    player 1. Route player 1 -> CH1 and player 2 -> CH2 (djengine::DjEngineIF::setRoute(EnPlayerChannel,EnMixerInput)). */
 sleep(5);
 void (*route)(void*,int,int)=(void*)0x50598;
 route(0,0,0);route(0,1,1);
 const char routed[]="RX3 mixer routing: player1->CH1, player2->CH2, crossfader A/B\n";write(2,routed,sizeof(routed)-1);
 const char *(*keyname)(void*)=(void*)0x37cde4;
 int names=open("/tmp/rx3-keycodes.txt",O_WRONLY|O_CREAT|O_TRUNC,0644);
 if(names>=0){
  uint32_t key[16]={0};const char *last=0;
  for(unsigned i=0;i<0x9000;i++){
   key[2]=i;const char *name=keyname(key);
   if(name&&name!=last){char hex[6];for(int j=0;j<4;j++)hex[j]="0123456789abcdef"[(i>>(12-j*4))&15];hex[4]=' ';hex[5]=0;write(names,hex,5);write(names,name,strlen(name));write(names,"\n",1);last=name;}
  }
  close(names);
 }
 return 0;
}
static void *control_thread(void *unused){
 sleep(3);
 int fd=open("/dev/rx3-control",O_RDWR);
 if(fd<0)return 0;
 void *manager=0;
 while(!manager){void *root=*(void *volatile *)0x026867c0;if(root)manager=*(void **)((char*)root+0x64);if(!manager)sleep(1);}
 /* The two physical panel CPUs normally release this startup input gate. */
 ((void (*)(void*,int))0x37c8d8)(manager,3);
 void (*sendkey)(void*,int,int,int,long,float,long)=(void*)0x37ad64;
 for(int ch=1;ch<=2;ch++){
  const int keys[]={0x5019,0x501a,0x501b,0x501c,0x509d,0x501e};
  for(int i=0;i<6;i++)sendkey(manager,keys[i],4,ch,0,i==5?1.f:.5f,0);
 }
 sendkey(manager,0x6017,4,0,0,.5f,0);
 sendkey(manager,0x4403,4,0,0,.6f,0);
 sendkey(manager,0x4406,4,0,0,.5f,0);
 sendkey(manager,0x4405,4,0,0,0.f,0);
 sendkey(manager,0x5020,0,1,0,0.f,0);
 sendkey(manager,0x5020,2,1,0,0.f,0);
 /* Mixer setup and the key-name dump wait for the engine and take several seconds; do them on their own thread so
    the screen accepts SOURCE/BROWSE/touch as soon as it is drawn. Presses made before then queue in the FIFO. */
 unsigned long setup;pthread_create(&setup,0,setup_thread,0);
 unsigned long leds;pthread_create(&leds,0,led_thread,0);
 const char ready[]="RX3 control adapter ready\n";write(2,ready,sizeof(ready)-1);
 struct command c;unsigned have=0;
 for(;;){int n=read(fd,(char*)&c+have,sizeof(c)-have);if(n<=0){sleep(1);continue;}have+=n;if(have<sizeof(c))continue;have=0;
  if(c.key==0xFFFF){query_state();continue;}
  if(c.key<0||c.key>65535||c.operation<0||c.operation>15||c.channel<0||c.channel>2)continue;
  sendkey(manager,c.key,c.operation,c.channel,c.value,c.analog,c.extra);
 }
 return 0;
}
__attribute__((constructor))static void start_control(void){
 if(!program_invocation_short_name||strcmp(program_invocation_short_name,"rbp-pi"))return;
 unsigned long thread;pthread_create(&thread,0,control_thread,0);
}
