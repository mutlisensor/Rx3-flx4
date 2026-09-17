#include <asm/ioctl.h>
#include <linux/fb.h>
#include <stdarg.h>
#include <errno.h>
static unsigned yoffset;
static void clear(void *p,unsigned n){unsigned char *q=p;while(n--)*q++=0;}
int ioctl(int fd,unsigned long request,...){
 va_list ap;va_start(ap,request);void *arg=va_arg(ap,void*);va_end(ap);
 if(((request>>8)&255)==0x70){if((request&0x80000000)&&arg){unsigned n=(request>>16)&0x3fff;if(n<=64)clear(arg,n);}return 0;}
 if(request==0x80046b00){*(unsigned*)arg=3;return 0;}
 if(request==0x80026b01){*(unsigned*)arg=3900;return 0;}
 if(request==0x40046b00||request==0x40026b01)return 0;
 if(request==FBIOGET_FSCREENINFO){struct fb_fix_screeninfo *f=arg;clear(f,sizeof(*f));f->id[0]='R';f->id[1]='X';f->id[2]='3';f->smem_len=1280*800*4;f->type=FB_TYPE_PACKED_PIXELS;f->visual=FB_VISUAL_TRUECOLOR;f->line_length=1280*4;return 0;}
 if(request==FBIOGET_VSCREENINFO){struct fb_var_screeninfo *v=arg;clear(v,sizeof(*v));v->xres=v->xres_virtual=1280;v->yres=v->yres_virtual=800;v->bits_per_pixel=32;v->red.offset=16;v->red.length=8;v->green.offset=8;v->green.length=8;v->blue.length=8;v->height=135;v->width=216;v->pixclock=20000;v->left_margin=40;v->right_margin=40;v->upper_margin=10;v->lower_margin=10;v->hsync_len=20;v->vsync_len=3;return 0;}
 if(request==FBIOPUT_VSCREENINFO){struct fb_var_screeninfo *v=arg;if(v->bits_per_pixel!=32){errno=EINVAL;return -1;}return 0;}
 if(request==FBIOPAN_DISPLAY||request==FBIOPUTCMAP||request==FBIOGETCMAP||request==FBIOBLANK||request==FBIO_WAITFORVSYNC)return 0;
 register long r0 asm("r0")=fd;register long r1 asm("r1")=request;register void *r2 asm("r2")=arg;register long r7 asm("r7")=54;
 asm volatile("svc 0":"+r"(r0):"r"(r1),"r"(r2),"r"(r7):"memory");
 if(r0<0 && r0>=-4095){errno=-r0;return -1;}return r0;
}
extern void *dlsym(void*,const char*);
extern void *dlvsym(void*,const char*,const char*);
extern int write(int,const void*,unsigned);
static void logtext(const char *s){unsigned n=0;while(s[n])n++;write(2,s,n);}
static int logresult(const char *s,int v){char h[12]=" 00000000\n";unsigned u=v;for(int i=8;i>0;i--){h[i]="0123456789abcdef"[u&15];u>>=4;}logtext(s);write(2,h,10);return v;}
void *dlopen(const char *name,int flags){static void *(*real)(const char*,int);if(!real)real=dlsym((void*)-1,"dlopen");return real(name,flags&~8);}
/* USB STOP: the firmware unmounts /media/usbN/<part> itself (do_umount0, then do_umount1 every 10 s, then E-8307)
   but runs unprivileged in the chroot, so hand the unmount to the root helper rx3-priv.sh over /dev/rx3-priv and
   wait until the mountpoint is gone. */
extern int open(const char*,int,...);extern int close(int);extern int usleep(unsigned);
/* glibc 2.13 exports no stat64 symbol (only __xstat64), so use the raw ARM EABI stat64 syscall (195); st_dev is the first u64. */
static long sys_stat64(const char *p,void *b){register long r0 asm("r0")=(long)p;register void *r1 asm("r1")=b;register long r7 asm("r7")=195;asm volatile("svc 0":"+r"(r0):"r"(r1),"r"(r7):"memory");return r0;}
static int is_media(const char *t){const char *m="/media/usb";for(int i=0;m[i];i++)if(t[i]!=m[i])return 0;return 1;}
static int mounted(const char *t){unsigned long long a[32],b[32];char parent[160];unsigned n=0,last=0;while(t[n]&&n<158){parent[n]=t[n];if(t[n]=='/')last=n;n++;}parent[last?last:1]=0;
 if(sys_stat64(t,a)<0)return 0;if(sys_stat64(parent,b)<0)return 1;return a[0]!=b[0];}
static int priv_umount(const char *t){char req[176];unsigned n=0;const char *p="umount ";while(*p)req[n++]=*p++;for(unsigned i=0;t[i]&&n<170;i++)req[n++]=t[i];req[n++]='\n';
 if(!mounted(t))return 0;int fd=open("/dev/rx3-priv",1|04000);if(fd<0)return -1;write(fd,req,n);close(fd);
 for(int i=0;i<60;i++){if(!mounted(t)){logtext("RX3 priv umount done\n");return 0;}usleep(100000);}return -1;}
int umount2(const char *target,int flags){static int(*real)(const char*,int);if(!real)real=dlsym((void*)-1,"umount2");
 if(target&&is_media(target)&&priv_umount(target)==0)return 0;return real(target,flags);}
int umount(const char *target){return umount2(target,0);}
/* ---- Output PCMs, and surviving the controller being unplugged ----
   When the DJ controller (which is also the sound card) is unplugged, the firmware's real-time audio thread
   (JuceALSA) retries the vanished device in a tight loop, ~25 000 failing ioctls a second: that starves the rest of
   the player and the decks run erratically. So once an output fails with "no such device" the shim takes it over:
   writes report success at real-time pace (the decks keep playing, silently, like a unit whose outputs were pulled)
   and every half second it tries to reopen the outputs with the settings the firmware chose. The firmware keeps
   never sees the real handle: it gets a stable token (the address of its slot below), and every call on the token
   goes to the current real handle. A freed real handle can then never be confused with one alsa-lib reuses. Both
   outputs share one dmix slave, so they are closed and reopened together. The lock is re-entrant because alsa-lib
   calls some public functions (prepare, close) on its own internal objects while we hold it. */
typedef struct {long s,ns;} rx3_ts;extern int clock_gettime(int,rx3_ts*);
static long long now_us(void){rx3_ts t;clock_gettime(1,&t);return (long long)t.s*1000000+t.ns/1000;}
extern void *malloc(unsigned);extern void free(void*);
extern int pthread_mutex_lock(void*);extern int pthread_mutex_unlock(void*);extern unsigned long pthread_self(void);
static char pcm_mutex[64];      /* pthread_mutex_t; all zero bytes is PTHREAD_MUTEX_INITIALIZER in glibc */
static unsigned long lock_owner;static int lock_depth;
static void lk(void){unsigned long me=pthread_self();if(lock_owner==me){lock_depth++;return;}pthread_mutex_lock(pcm_mutex);lock_owner=me;lock_depth=1;}
static void ulk(void){if(--lock_depth==0){lock_owner=0;pthread_mutex_unlock(pcm_mutex);}}
struct outpcm {void *fw,*real;const char *target;int mode,format,channels;unsigned rate,periods;unsigned long period;
 void *swp;int lost,peak_val,peak_n;long long last_write,next_due;};
static struct outpcm outs[4];
static long long last_reopen;
static struct outpcm *out_of(void *pcm){for(int i=0;i<4;i++)if(pcm==(void*)&outs[i]&&outs[i].fw)return &outs[i];return 0;}
static void *real_of(void *pcm){struct outpcm *o=out_of(pcm);return o?o->real:pcm;}
/* Setup calls on an output whose device is gone (no real handle yet) just succeed; the recorded settings are
   applied when the device is reopened. */
#define MAPPED(a,r) void *r=real_of(a);if(!r)return 0
#define REAL(name,ret,args) static ret(*real)args;if(!real)real=dlsym((void*)-1,#name)
#define REALV(name,ret,args) static ret(*real)args;if(!real)real=dlvsym((void*)-1,#name,"ALSA_0.9.0rc4")
int snd_pcm_open(void **pcm,const char *name,int stream,int mode){
 REAL(snd_pcm_open,int,(void**,const char*,int,int));
 unsigned n=0;while(name[n])n++;const char *target=stream?"null":(n&&name[n-1]=='0'?"rx3out":(n&&name[n-1]=='1'?"rx3cue":"null"));
 logtext("PCM open ");logtext(name);int r=logresult(stream?" capture":" playback",real(pcm,target,stream,mode));
 if(r>=0&&!stream&&target[0]=='r'){lk();
  for(int i=0;i<4;i++)if(!outs[i].fw){struct outpcm o={0};o.fw=&outs[i];o.real=*pcm;o.target=target;o.mode=mode;outs[i]=o;*pcm=&outs[i];break;}
  ulk();}
 return r;
}
int snd_pcm_close(void *pcm){
 REAL(snd_pcm_close,int,(void*));
 lk();struct outpcm *o=out_of(pcm);void *r=o?o->real:pcm;
 if(o){if(o->swp)free(o->swp);struct outpcm z={0};*o=z;}
 ulk();
 return r?real(r):0;
}
int snd_pcm_hw_params(void*a,void*b){REAL(snd_pcm_hw_params,int,(void*,void*));MAPPED(a,r);return logresult("snd_pcm_hw_params",real(r,b));}
int snd_pcm_hw_params_any(void*a,void*b){REAL(snd_pcm_hw_params_any,int,(void*,void*));MAPPED(a,r);return logresult("snd_pcm_hw_params_any",real(r,b));}
int snd_pcm_hw_params_set_access(void*a,void*b,int c){REAL(snd_pcm_hw_params_set_access,int,(void*,void*,int));MAPPED(a,r);return logresult("interleaved access",real(r,b,c==4?3:c));}
int snd_pcm_hw_params_set_format(void*a,void*b,int c){REAL(snd_pcm_hw_params_set_format,int,(void*,void*,int));
 MAPPED(a,r);int v=real(r,b,c);struct outpcm *o=out_of(a);if(v>=0&&o)o->format=c;
 logresult("snd_pcm_hw_params_set_format value",c);return logresult("snd_pcm_hw_params_set_format",v);}
int snd_pcm_hw_params_set_channels(void*a,void*b,unsigned c){REAL(snd_pcm_hw_params_set_channels,int,(void*,void*,unsigned));
 MAPPED(a,r);int v=real(r,b,c);struct outpcm *o=out_of(a);if(v>=0&&o)o->channels=c;
 logresult("snd_pcm_hw_params_set_channels value",c);return logresult("snd_pcm_hw_params_set_channels",v);}
int snd_pcm_hw_params_set_rate_near(void*a,void*b,unsigned*c,int*d){REALV(snd_pcm_hw_params_set_rate_near,int,(void*,void*,unsigned*,int*));
 MAPPED(a,r);int v=real(r,b,c,d);struct outpcm *o=out_of(a);if(v>=0&&o&&c)o->rate=*c;return v;}
int snd_pcm_hw_params_set_period_size_near(void*a,void*b,unsigned long*c,int*d){REALV(snd_pcm_hw_params_set_period_size_near,int,(void*,void*,unsigned long*,int*));
 MAPPED(a,r);int v=real(r,b,c,d);struct outpcm *o=out_of(a);if(v>=0&&o&&c)o->period=*c;return v;}
int snd_pcm_hw_params_set_periods_near(void*a,void*b,unsigned*c,int*d){REALV(snd_pcm_hw_params_set_periods_near,int,(void*,void*,unsigned*,int*));
 MAPPED(a,r);int v=real(r,b,c,d);struct outpcm *o=out_of(a);if(v>=0&&o&&c)o->periods=*c;return v;}
int snd_pcm_hw_params_test_rate(void*a,void*b,unsigned c,int d){REAL(snd_pcm_hw_params_test_rate,int,(void*,void*,unsigned,int));MAPPED(a,r);return real(r,b,c,d);}
extern unsigned snd_pcm_sw_params_sizeof(void);extern void snd_pcm_sw_params_copy(void*,const void*);
int snd_pcm_sw_params(void*a,void*b){REAL(snd_pcm_sw_params,int,(void*,void*));MAPPED(a,r);int v=real(r,b);struct outpcm *o=out_of(a);
 if(v>=0&&o){if(!o->swp)o->swp=malloc(snd_pcm_sw_params_sizeof());if(o->swp)snd_pcm_sw_params_copy(o->swp,b);}return v;}
int snd_pcm_sw_params_current(void*a,void*b){REAL(snd_pcm_sw_params_current,int,(void*,void*));MAPPED(a,r);return real(r,b);}
int snd_pcm_sw_params_set_silence_size(void*a,void*b,unsigned long c){REAL(snd_pcm_sw_params_set_silence_size,int,(void*,void*,unsigned long));MAPPED(a,r);return real(r,b,c);}
int snd_pcm_sw_params_set_silence_threshold(void*a,void*b,unsigned long c){REAL(snd_pcm_sw_params_set_silence_threshold,int,(void*,void*,unsigned long));MAPPED(a,r);return real(r,b,c);}
int snd_pcm_sw_params_set_start_threshold(void*a,void*b,unsigned long c){REAL(snd_pcm_sw_params_set_start_threshold,int,(void*,void*,unsigned long));MAPPED(a,r);return real(r,b,c);}
int snd_pcm_sw_params_set_stop_threshold(void*a,void*b,unsigned long c){REAL(snd_pcm_sw_params_set_stop_threshold,int,(void*,void*,unsigned long));MAPPED(a,r);return real(r,b,c);}
int snd_pcm_link(void*a,void*b){REAL(snd_pcm_link,int,(void*,void*));void *ra=real_of(a),*rb=real_of(b);if(!ra||!rb)return 0;return real(ra,rb);}
long snd_pcm_readi(void*a,void*b,unsigned long c){REAL(snd_pcm_readi,long,(void*,void*,unsigned long));return real(real_of(a),b,c);}
int snd_pcm_prepare(void*a){REAL(snd_pcm_prepare,int,(void*));
 lk();struct outpcm *o=out_of(a);int lost=o&&(o->lost||!o->real);void *r=real_of(a);ulk();
 return lost?0:real(r);}
extern int open(const char*,int,...);extern int read(int,void*,unsigned);extern int close(int);
/* ALSA control device for the RX3's card queries: read once from /etc/rx3-ctl (e.g. "hw:CARD=DDJFLX4"), default hw:2. */
static const char *ctl_name(void){static char buf[64];if(!buf[0]){int fd=open("/etc/rx3-ctl",0);int n=fd<0?0:read(fd,buf,sizeof(buf)-1);if(fd>=0)close(fd);if(n<0)n=0;buf[n]=0;while(n>0&&(buf[n-1]=='\n'||buf[n-1]==' '))buf[--n]=0;if(!buf[0]){buf[0]='h';buf[1]='w';buf[2]=':';buf[3]='2';buf[4]=0;}}return buf;}
int snd_ctl_open(void **ctl,const char *name,int mode){static int(*real)(void**,const char*,int);if(!real)real=dlsym((void*)-1,"snd_ctl_open");logtext("CTL open ");logtext(ctl_name());return logresult("",real(ctl,ctl_name(),mode));}
int snd_pcm_hw_params_get_channels_max(const void *p,unsigned *v){static int(*real)(const void*,unsigned*);if(!real)real=dlvsym((void*)-1,"snd_pcm_hw_params_get_channels_max","ALSA_0.9.0rc4");int r=real(p,v);if(r>=0&&*v>2)*v=2;logresult("channels max",*v);return logresult("channels max result",r);}
int snd_ctl_pcm_info(void *ctl,void *info){
 static int(*real)(void*,void*);static int(*stream)(const void*);
 if(!real)real=dlsym((void*)-1,"snd_ctl_pcm_info");
 if(!stream)stream=dlsym((void*)-1,"snd_pcm_info_get_stream");
 if(stream(info)==1)return 0; /* Virtual silent capture, exposed by the null PCM. */
 return real(ctl,info);
}

/* Reopen every lost output with the recorded settings. All or nothing: the outputs share one dmix slave. */
static int reopen_outputs(void){
 REAL(snd_pcm_open,int,(void**,const char*,int,int));
 static int(*rclose)(void*),(*rany)(void*,void*),(*racc)(void*,void*,int),(*rfmt)(void*,void*,int),(*rch)(void*,void*,unsigned),
  (*rrate)(void*,void*,unsigned*,int*),(*rper)(void*,void*,unsigned long*,int*),(*rpers)(void*,void*,unsigned*,int*),
  (*rhw)(void*,void*),(*rsw)(void*,void*),(*rprep)(void*);
 static unsigned(*hwsize)(void);
 if(!rclose){rclose=dlsym((void*)-1,"snd_pcm_close");rany=dlsym((void*)-1,"snd_pcm_hw_params_any");racc=dlsym((void*)-1,"snd_pcm_hw_params_set_access");
  rfmt=dlsym((void*)-1,"snd_pcm_hw_params_set_format");rch=dlsym((void*)-1,"snd_pcm_hw_params_set_channels");
  rrate=dlvsym((void*)-1,"snd_pcm_hw_params_set_rate_near","ALSA_0.9.0rc4");rper=dlvsym((void*)-1,"snd_pcm_hw_params_set_period_size_near","ALSA_0.9.0rc4");
  rpers=dlvsym((void*)-1,"snd_pcm_hw_params_set_periods_near","ALSA_0.9.0rc4");rhw=dlsym((void*)-1,"snd_pcm_hw_params");
  rsw=dlsym((void*)-1,"snd_pcm_sw_params");rprep=dlsym((void*)-1,"snd_pcm_prepare");hwsize=dlsym((void*)-1,"snd_pcm_hw_params_sizeof");}
 for(int i=0;i<4;i++)if(outs[i].fw&&outs[i].lost&&outs[i].real){rclose(outs[i].real);outs[i].real=0;}
 void *hp=malloc(hwsize());if(!hp)return -1;
 int ok=1;
 for(int i=0;i<4&&ok;i++){struct outpcm *o=&outs[i];if(!o->fw||!o->lost)continue;void *p;
  if(real(&p,o->target,0,o->mode)<0){ok=0;break;}
  int e=rany(p,hp);if(e>=0)e=racc(p,hp,3);if(e>=0&&o->format)e=rfmt(p,hp,o->format);if(e>=0&&o->channels)e=rch(p,hp,o->channels);
  if(e>=0&&o->rate){unsigned v=o->rate;e=rrate(p,hp,&v,0);}
  if(e>=0&&o->period){unsigned long v=o->period;e=rper(p,hp,&v,0);}
  if(e>=0&&o->periods){unsigned v=o->periods;e=rpers(p,hp,&v,0);}
  if(e>=0)e=rhw(p,hp);
  if(e>=0&&o->swp)rsw(p,o->swp);
  if(e>=0)e=rprep(p);
  if(e<0){rclose(p);ok=0;break;}
  o->real=p;
 }
 free(hp);
 if(!ok){for(int i=0;i<4;i++)if(outs[i].fw&&outs[i].lost&&outs[i].real){rclose(outs[i].real);outs[i].real=0;}return -1;}
 for(int i=0;i<4;i++)if(outs[i].fw&&outs[i].lost){outs[i].lost=0;outs[i].next_due=0;}
 logtext("RX3 audio: outputs reopened after the sound card came back\n");
 return 0;
}
/* While the outputs are lost, pace the audio thread as the real device would. It writes every output once per
   cycle, so only the first output written recently sets the pace. */
static void pace(struct outpcm *o,unsigned long frames){
 long long t=now_us();
 for(struct outpcm *q=outs;q<o;q++)if(q->fw&&q->lost&&t-q->last_write<100000){o->last_write=t;return;}
 o->last_write=t;
 unsigned rate=o->rate>=8000?o->rate:44100;
 if(o->next_due<t-100000||o->next_due>t+1000000)o->next_due=t;
 o->next_due+=(unsigned)(frames*10000u/(rate/100u));   /* 32-bit maths: no libgcc division helper in this -nostdlib shim */
 if(o->next_due>t)usleep((unsigned)(o->next_due-t));
}
/* Audio peak meter: loudest sample written to each output, reported every 256 writes to /tmp/rx3-audio-peaks. */
long snd_pcm_writei(void *pcm,const void *buf,unsigned long frames){
 REAL(snd_pcm_writei,long,(void*,const void*,unsigned long));
 lk();
 struct outpcm *o=out_of(pcm);
 if(o){unsigned long n=frames*2;int m=o->peak_val;int f=o->format;
  if(f==6||f==10){const int *s=buf;for(unsigned long i=0;i<n;i++){int v=s[i];if(f==6)v=(int)((unsigned)v<<8)>>16;else v>>=16;if(v<0)v=-v;if(v>m)m=v;}}   /* S24_LE in 32-bit words, S32_LE */
  else{const short *s=buf;for(unsigned long i=0;i<n;i++){int v=s[i];if(v<0)v=-v;if(v>m)m=v;}}
  o->peak_val=m;
  if(++o->peak_n>=256){o->peak_n=0;char line[64];int k=0;const char *t=o->target;while(*t&&k<20)line[k++]=*t++;line[k++]=' ';line[k++]='f';line[k++]='0'+f%10;line[k++]=' ';
   char num[8];int d=0;int x=m;do{num[d++]='0'+x%10;x/=10;}while(x);while(d)line[k++]=num[--d];if(o->lost){const char *l=" lost";while(*l)line[k++]=*l++;}line[k++]='\n';
   int fd=open("/tmp/rx3-audio-peaks",01|0100|02000,0644);if(fd>=0){write(fd,line,k);close(fd);}o->peak_val=0;}
  if(o->lost){
   long long t=now_us();
   if(t-last_reopen>=500000){last_reopen=t;reopen_outputs();}
   if(o->lost){ulk();pace(o,frames);return (long)frames;}
  }
 }
 void *r=o?o->real:pcm;
 ulk();
 long v=real(r,buf,frames);
 if(o&&(v==-19||v==-77||v==-108||v==-5)){   /* ENODEV, EBADFD, ESHUTDOWN, EIO: the card is gone */
  lk();
  int was=0;for(int i=0;i<4;i++){if(outs[i].fw){was|=outs[i].lost;outs[i].lost=1;}}
  last_reopen=now_us();
  ulk();
  if(!was)logtext("RX3 audio: sound card gone, keeping the decks running silently until it returns\n");
  pace(o,frames);return (long)frames;
 }
 return v;
}
