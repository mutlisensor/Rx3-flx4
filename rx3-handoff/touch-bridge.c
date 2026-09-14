/* rx3-touch-bridge: turn touchscreen (or mouse, or replayed) input into RX3 firmware touch reports and sidebar
   button presses, using the same layout as the presenter. usage: rx3-touch-bridge [--mouse] <event device> <touch fifo>
   or rx3-touch-bridge --replay <touch fifo> with evdev events on stdin in firmware coordinates (1280x800). */
#include <linux/input.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include "pi-controls.h"
struct __attribute__((packed)) report {uint8_t down,pad;uint16_t x,y;};
struct finger {int x,y,down,active,region;};
static int control;
static struct ui_state *state;
static void command(int key,int op,int ch,int value,float a){struct command c={key,op,ch,value,a,0};if(write(control,&c,sizeof(c))!=sizeof(c))perror("control");}
/* USB STOP (0x8002) is a plain press/release: the firmware times the hold itself (ejects ~1.9 s after press, an earlier
   release cancels). Never send its code 1 "long-pressed" event without a press: that poisons the slot until tapped. */
static void button(int i,int down){const struct button*b=&buttons[i];if(down)state->pressed|=1u<<i;else state->pressed&=~(1u<<i);command(b->key,down?0:2,b->channel,0,0);}
int main(int argc,char**argv){
 if(argc<3)return 2;
 int replay=!strcmp(argv[1],"--replay"),mouse=!strcmp(argv[1],"--mouse");if(mouse){if(argc<4)return 2;argv++;}
 int in=replay?0:open(argv[1],O_RDONLY),out=open(argv[2],O_RDWR|O_NONBLOCK);control=open(UI_CONTROL,O_RDWR|O_NONBLOCK);
 if(in<0||out<0||control<0){perror("open");return 1;}
 int sf=open(UI_STATE,O_RDWR|O_CREAT,0600);if(sf<0||ftruncate(sf,sizeof(struct ui_state)))return 1;
 state=mmap(0,sizeof(*state),PROT_READ|PROT_WRITE,MAP_SHARED,sf,0);if(state==MAP_FAILED)return 1;
 if(state->magic!=0x52583332)*state=(struct ui_state){0x52583332,{1,.6,0,1,.5,.5},0,1};
 int W=1280,H=720;{int fb=open(fb_device(),O_RDONLY);if(fb>=0){struct fb_var_screeninfo v;if(!ioctl(fb,FBIOGET_VSCREENINFO,&v)){W=v.xres;H=v.yres;}close(fb);}}
 struct layout L=make_layout(W,H,rotation_for(W,H));
 struct input_absinfo ax={.maximum=W-1},ay={.maximum=H-1};struct finger fingers[10]={0};
 if(!replay&&!mouse&&(ioctl(in,EVIOCGABS(ABS_MT_POSITION_X),&ax)||ioctl(in,EVIOCGABS(ABS_MT_POSITION_Y),&ay))){perror("touch ranges");return 1;}
 fprintf(stderr,"touch bridge: %s, panel %dx%d rotate %d, logical %dx%d, firmware %dx%d at %d,%d, touch %d..%d x %d..%d\n",
  mouse?"mouse":replay?"replay (firmware coordinates)":"touchscreen",W,H,L.rot,L.LW,L.LH,L.cw,L.ch,L.cx,L.cy,ax.minimum,ax.maximum,ay.minimum,ay.maximum);
 if(mouse){fingers[0].x=L.LW/2;fingers[0].y=L.LH/2;state->cursor_x=fingers[0].x;state->cursor_y=fingers[0].y;state->cursor_visible=1;fprintf(stderr,"mouse mode: left=touch, wheel=browse, right=back, middle=enter\n");}
 int slot=0,source=-1,ux=0,uy=0,release=0;struct input_event e;struct pollfd p={in,POLLIN,0};
 for(;;){
  int ready=poll(&p,1,10);if(ready<0)return 1;
  if(ready){if(read(in,&e,sizeof(e))!=sizeof(e))break;
   if(mouse){struct finger*f=&fingers[0];
    if(e.type==EV_REL){if(e.code==REL_X)f->x+=e.value;if(e.code==REL_Y)f->y+=e.value;if(f->x<0)f->x=0;if(f->x>L.LW-1)f->x=L.LW-1;if(f->y<0)f->y=0;if(f->y>L.LH-1)f->y=L.LH-1;
     if(e.code==REL_WHEEL&&e.value)command(0x420c,4,0,e.value>0?-1:1,0);}
    if(e.type==EV_KEY){if(e.code==BTN_LEFT)f->down=e.value;if(e.code==BTN_RIGHT&&e.value<2)command(0x420d,e.value?0:2,0,0,0);if(e.code==BTN_MIDDLE&&e.value<2)command(0x420c,e.value?0:2,0,0,0);}
    state->cursor_x=f->x;state->cursor_y=f->y;}
   else if(e.type==EV_ABS){if(e.code==ABS_MT_SLOT)slot=e.value;if(slot>=0&&slot<10){struct finger*f=&fingers[slot];if(e.code==ABS_MT_POSITION_X)f->x=e.value;if(e.code==ABS_MT_POSITION_Y)f->y=e.value;if(e.code==ABS_MT_TRACKING_ID)f->down=e.value>=0;}}
   if(e.type!=EV_SYN||e.code!=SYN_REPORT)continue;
  }
  for(int i=0;i<10;i++){
   struct finger*f=&fingers[i];int lx,ly,fwx,fwy;
   if(replay){fwx=f->x;fwy=f->y;lx=L.cx+(int)(fwx*L.s);ly=L.cy+(int)(fwy*L.s);}
   else{if(mouse){lx=f->x;ly=f->y;}else{int px=(int)((long)(f->x-ax.minimum)*W/(ax.maximum-ax.minimum+1)),py=(int)((long)(f->y-ay.minimum)*H/(ay.maximum-ay.minimum+1));panel_to_logical(&L,px,py,&lx,&ly);}
    fwx=(int)((lx-L.cx)/L.s);fwy=(int)((ly-L.cy)/L.s);}
   if(fwx<0)fwx=0;if(fwx>FW_W-1)fwx=FW_W-1;if(fwy<0)fwy=0;if(fwy>FW_H-1)fwy=FW_H-1;
   if(f->down&&!f->active){f->active=1;int b=button_at(&L,lx,ly);
    if(b>=0){f->region=1+b;button(b,1);}else if(lx<L.LW-L.col&&source<0){source=i;f->region=0;}else f->region=-1;
    fprintf(stderr,"touch begin slot=%d logical=%d,%d firmware=%d,%d region=%d\n",i,lx,ly,fwx,fwy,f->region);
   }
   if(f->active&&f->down&&f->region==0){ux=fwx;uy=fwy;}
   if(f->active&&!f->down){if(f->region>0)button(f->region-1,0);if(source==i){source=-1;release=10;}f->active=0;}
  }
  if(source>=0||release){struct report r={source>=0,0,37+(FW_W-ux)*3976/FW_W,72+uy*3856/FW_H};if(write(out,&r,sizeof(r))!=sizeof(r))perror("touch report");if(source<0)release--;}
 }
 for(int i=0;i<10;i++)if(fingers[i].active&&fingers[i].region>0)button(fingers[i].region-1,0);
 return 0;
}
