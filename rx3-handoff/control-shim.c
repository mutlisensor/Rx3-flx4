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
