/* rx3-fb-present: show the RX3 firmware's virtual 1280x800 screen on the real display, plus a sidebar of
   touch buttons. One bilinear pass from the firmware picture to the panel (no intermediate canvas, no
   nearest-neighbour steps), composed into a back buffer and copied to the framebuffer right after the
   panel's vertical blank, at the panel rate. usage: rx3-fb-present <chroot fb file> [framebuffer] */
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <signal.h>
#include "pi-controls.h"
static FT_Face face;static uint32_t *chrome;static struct layout L;
static unsigned char *fb_mem;static size_t fb_size;
/* Leaving the last frame on screen (systemctl stop, ESC, Ctrl+C, a crash) looks like a hang. Blank the real
   panel on the way out; async-signal-safe (no libc calls beyond the plain memory fill and _exit). */
static void blank_and_exit(int sig){(void)sig;if(fb_mem&&fb_size)for(size_t i=0;i<fb_size;i++)fb_mem[i]=0;_exit(0);}
static void box(int x,int y,int w,int h,uint32_t c){for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)if(xx>=0&&xx<L.LW&&yy>=0&&yy<L.LH)chrome[yy*L.LW+xx]=c;}
static void label(int cx,int cy,const char *s,int size,uint32_t c){
 FT_Set_Pixel_Sizes(face,0,size);int w=0;for(const char*p=s;*p;p++){FT_Load_Char(face,*p,FT_LOAD_RENDER);w+=face->glyph->advance.x>>6;}
 int x=cx-w/2;for(const char*p=s;*p;p++){if(FT_Load_Char(face,*p,FT_LOAD_RENDER))continue;FT_GlyphSlot g=face->glyph;
 for(unsigned y=0;y<g->bitmap.rows;y++)for(unsigned i=0;i<g->bitmap.width;i++){
 int px=x+g->bitmap_left+i,py=cy+size/3-g->bitmap_top+y;unsigned a=g->bitmap.buffer[y*g->bitmap.pitch+i];
 if(px<0||px>=L.LW||py<0||py>=L.LH||!a)continue;uint32_t old=chrome[py*L.LW+px],v=0;
 for(int k=0;k<3;k++){unsigned shift=k*8;v|=((((c>>shift)&255)*a+((old>>shift)&255)*(255-a))/255)<<shift;}chrome[py*L.LW+px]=v;
 }x+=g->advance.x>>6;}
}
static void drawbutton(int i,int down){const struct button*b=&buttons[i];int x=L.btn[i].x,y=L.btn[i].y,w=L.btn[i].w,h=L.btn[i].h;
 box(x,y,w,h,down?0x536f84:b->color);int fs=L.col/7;if(fs<12)fs=12;
 if(b->sub){label(x+w/2,y+h/2-fs/2,b->label,fs,0xffffff);label(x+w/2,y+h/2+fs,b->sub,fs*3/4,0xd1dae2);}
 else label(x+w/2,y+h/2,b->label,fs,0xffffff);}
static long now_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000000L+t.tv_nsec/1000;}
int main(int argc,char**argv){
 if(argc<2)return 2;
 const char*fbpath=argc>2?argv[2]:fb_device();
 int src=open(argv[1],O_RDONLY),dst=open(fbpath,O_RDWR);if(src<0||dst<0){perror(src<0?argv[1]:fbpath);return 1;}
 struct fb_fix_screeninfo f;struct fb_var_screeninfo v;ioctl(dst,FBIOGET_FSCREENINFO,&f);ioctl(dst,FBIOGET_VSCREENINFO,&v);
 if(v.bits_per_pixel!=32&&v.bits_per_pixel!=16){fprintf(stderr,"Need a 16- or 32-bit framebuffer (got %u bpp)\n",v.bits_per_pixel);return 1;}
 int bpp16=v.bits_per_pixel==16,W=v.xres,H=v.yres;
 L=make_layout(W,H,rotation_for(W,H));
 int fps=60;{const char*e=getenv("RX3_FPS");if(e&&atoi(e)>0)fps=atoi(e);}
 int nearest=0;{const char*e=getenv("RX3_FILTER");if(e&&!strcmp(e,"nearest"))nearest=1;}   /* cheaper for slow boards */
 fprintf(stderr,"presenter: %s %dx%d rotate %d -> logical %dx%d, firmware %dx%d at %d,%d (x%.3f, %s), sidebar %d px, %d fps\n",fbpath,W,H,L.rot,L.LW,L.LH,L.cw,L.ch,L.cx,L.cy,L.s,nearest?"nearest":"bilinear",L.col,fps);
 const uint32_t *s=mmap(0,FW_W*FW_H*4,PROT_READ,MAP_SHARED,src,0);unsigned char *d=mmap(0,f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,dst,0);if(s==MAP_FAILED||d==MAP_FAILED)return 1;
 fb_mem=d;fb_size=f.smem_len;signal(SIGTERM,blank_and_exit);signal(SIGINT,blank_and_exit);
 size_t fbsize=(size_t)f.line_length*H;unsigned char *back=malloc(fbsize);chrome=malloc(sizeof(uint32_t)*L.LW*L.LH);if(!back||!chrome)return 1;
 int sf=open(UI_STATE,O_RDWR|O_CREAT,0600);if(sf<0||ftruncate(sf,sizeof(struct ui_state)))return 1;
 struct ui_state *state=mmap(0,sizeof(*state),PROT_READ|PROT_WRITE,MAP_SHARED,sf,0);if(state==MAP_FAILED)return 1;
 if(state->magic!=0x52583332){*state=(struct ui_state){0x52583332,{1,.6,0,1,.5,.5},0,1};}
 static const char *fonts[]={"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
  "/usr/share/fonts/truetype/freefont/FreeSans.ttf","/usr/share/fonts/truetype/piboto/Piboto-Regular.ttf","/usr/share/fonts/truetype/crosextra/Carlito-Regular.ttf",0};
 FT_Library ft;if(FT_Init_FreeType(&ft)){fprintf(stderr,"rx3-fb-present: cannot initialise FreeType\n");return 1;}
 const char *chosen=getenv("RX3_FONT");
 if(!chosen||FT_New_Face(ft,chosen,0,&face)){chosen=0;for(int i=0;fonts[i];i++)if(!FT_New_Face(ft,fonts[i],0,&face)){chosen=fonts[i];break;}}
 if(!chosen){fprintf(stderr,"rx3-fb-present: no usable font found. Install one with:\n  sudo apt install fonts-dejavu-core\nOr point RX3_FONT at a .ttf. Looked for:\n");for(int i=0;fonts[i];i++)fprintf(stderr,"  %s\n",fonts[i]);return 1;}
 box(0,0,L.LW,L.LH,0x101820);for(int i=0;i<NBUTTONS;i++)drawbutton(i,0);unsigned drawn=0;
 /* Bilinear sampling tables: for each logical content column/row, the source coordinate and 8-bit fraction. */
 int *sx0=malloc(sizeof(int)*L.cw),*fx=malloc(sizeof(int)*L.cw),*sy0=malloc(sizeof(int)*L.ch),*fy=malloc(sizeof(int)*L.ch);
 for(int i=0;i<L.cw;i++){double u=(i+0.5)/L.s-0.5;if(u<0)u=0;int u0=(int)u;if(u0>FW_W-2)u0=FW_W-2;sx0[i]=u0;fx[i]=(int)((u-u0)*256);if(fx[i]>255)fx[i]=255;}
 for(int j=0;j<L.ch;j++){double u=(j+0.5)/L.s-0.5;if(u<0)u=0;int u0=(int)u;if(u0>FW_H-2)u0=FW_H-2;sy0[j]=u0;fy[j]=(int)((u-u0)*256);if(fy[j]>255)fy[j]=255;}
 /* Pointer arrow, 16x22, in logical space. */
 static unsigned char arrow[22][16];for(int y=0;y<22;y++)for(int x=0;x<16;x++)if(x<=y)arrow[y][x]=(x==0||x==y||y==21||x==15)?1:2;
 long period=1000000L/fps,next=now_us();int vsync_ok=1;
 /* Row tables: for every panel row, the constant logical coordinate (rotations 90/270 make each panel row one
    logical column; 0/180 make it one logical row) so the inner loop has no rotation arithmetic. */
 int side=L.rot==90||L.rot==270;
 long frames=0,rendered=0,t_report=now_us();uint32_t last_sig=0;int last_curx=-1,last_cury=-1;
 for(;;){
  /* Cheap signature of the firmware picture: one word in 61 (rows and columns both covered since 61 is odd), plus the
     button/cursor state. Unchanged means nothing to draw, so an idle firmware screen costs almost nothing. */
  uint32_t sig=2166136261u;for(int i=0;i<FW_W*FW_H;i+=61)sig=(sig^s[i])*16777619u;
  int curx=state->cursor_visible?state->cursor_x:-100,cury=state->cursor_y;
  int changed=sig!=last_sig||state->pressed!=drawn||curx!=last_curx||cury!=last_cury;
  if(changed){
   last_sig=sig;last_curx=curx;last_cury=cury;
   if(state->pressed!=drawn){for(int i=0;i<NBUTTONS;i++)if(((state->pressed^drawn)>>i)&1)drawbutton(i,(state->pressed>>i)&1);drawn=state->pressed;}
   for(int py=0;py<H;py++){unsigned char *line=back+(size_t)py*f.line_length;uint16_t*l16=(uint16_t*)line;uint32_t*l32=(uint32_t*)line;
    for(int px=0;px<W;px++){
     int lx,ly;
     if(side){if(L.rot==90){lx=py;ly=L.LH-1-px;}else{lx=L.LW-1-py;ly=px;}}
     else{if(L.rot==180){lx=L.LW-1-px;ly=L.LH-1-py;}else{lx=px;ly=py;}}
     int ix=lx-L.cx,iy=ly-L.cy;uint32_t c;
     if((unsigned)ix<(unsigned)L.cw&&(unsigned)iy<(unsigned)L.ch){
      const uint32_t *q=s+sy0[iy]*FW_W+sx0[ix];unsigned wx=fx[ix],wy=fy[iy],wx1=256-wx,wy1=256-wy;
      if(nearest){c=q[(wy>=128?FW_W:0)+(wx>=128?1:0)];goto have;}
      uint32_t a=q[0],b=q[1],cc=q[FW_W],dd=q[FW_W+1];
      uint32_t t_rb=(((a&0xff00ff)*wx1+(b&0xff00ff)*wx)>>8)&0xff00ff,t_g=(((a&0xff00)*wx1+(b&0xff00)*wx)>>8)&0xff00;
      uint32_t b_rb=(((cc&0xff00ff)*wx1+(dd&0xff00ff)*wx)>>8)&0xff00ff,b_g=(((cc&0xff00)*wx1+(dd&0xff00)*wx)>>8)&0xff00;
      c=(((t_rb*wy1+b_rb*wy)>>8)&0xff00ff)|(((t_g*wy1+b_g*wy)>>8)&0xff00);
     }else c=chrome[ly*L.LW+lx];
     have:;
     int axx=lx-curx,ayy=ly-cury;if((unsigned)axx<16u&&(unsigned)ayy<22u&&arrow[ayy][axx])c=arrow[ayy][axx]==1?0x000000:0xffffff;
     if(bpp16)l16[px]=(uint16_t)(((c>>8)&0xf800)|((c>>5)&0x07e0)|((c>>3)&0x001f));else l32[px]=c;
    }}
   rendered++;
  }
  /* Present right after vertical blank so the copy races ahead of the scanout; fall back to a timer. */
  int zero=0;if(vsync_ok&&ioctl(dst,FBIO_WAITFORVSYNC,&zero)<0){vsync_ok=0;fprintf(stderr,"presenter: no vsync ioctl, using a %d fps timer\n",fps);}
  long t=now_us();
  if(!vsync_ok){next+=period;if(next<t)next=t;if(next>t)usleep(next-t);}
  else if(t-next<period/4){/* a vsync that returned at once is no vsync: pace ourselves */usleep(period-(t-next));t=now_us();}
  next=t;
  if(changed)memcpy(d,back,fbsize);
  frames++;
  if(t-t_report>=60000000L){fprintf(stderr,"presenter: %.1f frames/s, %.1f rendered/s (vsync %s)\n",frames*1e6/(t-t_report),rendered*1e6/(t-t_report),vsync_ok?"yes":"no");frames=rendered=0;t_report=t;}
 }
}
