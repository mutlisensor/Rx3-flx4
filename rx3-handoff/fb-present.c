#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <stdio.h>
#include <string.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <signal.h>
#include "pi-controls.h"
static uint32_t frame[1920*1200],chrome[1920*1200];
static unsigned char *fb_mem;static size_t fb_size;
/* Leaving the last frame on screen (systemctl stop, ESC, Ctrl+C, a crash) looks like a hang. Blank the real
   panel on the way out; async-signal-safe (no libc calls beyond the plain memory fill and _exit). */
static void blank_and_exit(int sig){(void)sig;if(fb_mem&&fb_size)for(size_t i=0;i<fb_size;i++)fb_mem[i]=0;_exit(0);}
static FT_Face face;
static void box(int x,int y,int w,int h,uint32_t c){for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)if(xx>=0&&xx<1920&&yy>=0&&yy<1200)frame[yy*1920+xx]=c;}
static void label(int cx,int cy,const char *s,int size,uint32_t c){
 FT_Set_Pixel_Sizes(face,0,size);int w=0;for(const char*p=s;*p;p++){FT_Load_Char(face,*p,FT_LOAD_RENDER);w+=face->glyph->advance.x>>6;}
 int x=cx-w/2;for(const char*p=s;*p;p++){if(FT_Load_Char(face,*p,FT_LOAD_RENDER))continue;FT_GlyphSlot g=face->glyph;
 for(unsigned y=0;y<g->bitmap.rows;y++)for(unsigned i=0;i<g->bitmap.width;i++){
 int px=x+g->bitmap_left+i,py=cy+size/3-g->bitmap_top+y;unsigned a=g->bitmap.buffer[y*g->bitmap.pitch+i];
 if(px<0||px>=1920||py<0||py>=1200||!a)continue;uint32_t old=frame[py*1920+px],v=0;
 for(int k=0;k<3;k++){unsigned shift=k*8;v|=((((c>>shift)&255)*a+((old>>shift)&255)*(255-a))/255)<<shift;}frame[py*1920+px]=v;
 }x+=g->advance.x>>6;}
}
static void drawbutton(int i,int down){int x=(i%6)*320,y=1000+(i/6)*100;box(x+4,y+4,312,92,down?0x536f84:buttons[i].color);label(x+160,y+50,buttons[i].label,26,0xffffff);}
int main(int argc,char**argv){
 if(argc<2)return 2;
 const char*fbpath=argc>2?argv[2]:fb_device();
 int src=open(argv[1],O_RDONLY),dst=open(fbpath,O_RDWR);if(src<0||dst<0){perror(src<0?argv[1]:fbpath);return 1;}
 struct fb_fix_screeninfo f;struct fb_var_screeninfo v;ioctl(dst,FBIOGET_FSCREENINFO,&f);ioctl(dst,FBIOGET_VSCREENINFO,&v);
 if(v.bits_per_pixel!=32&&v.bits_per_pixel!=16){fprintf(stderr,"Need a 16- or 32-bit framebuffer (got %u bpp)\n",v.bits_per_pixel);return 1;}
 int bpp16=v.bits_per_pixel==16;
 int W=v.xres,H=v.yres,rot=rotation_for(W,H);struct layout L=make_layout(W,H,rot);
 /* One canvas index per panel pixel (-1 in the letterbox), so the copy loop below is a plain lookup. */
 int *idx=malloc(sizeof(int)*W*H);if(!idx)return 1;
 for(int py=0;py<H;py++)for(int px=0;px<W;px++){int cx,cy;idx[py*W+px]=panel_to_canvas(&L,px,py,0,&cx,&cy)?cy*1920+cx:-1;}
 fprintf(stderr,"presenter: %s %dx%d, rotate %d, canvas %dx%d at %d,%d\n",fbpath,W,H,rot,L.dw,L.dh,L.ox,L.oy);
 uint32_t *s=mmap(0,1280*800*4,PROT_READ,MAP_SHARED,src,0);unsigned char *d=mmap(0,f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,dst,0);if(s==MAP_FAILED||d==MAP_FAILED)return 1;
 fb_mem=d;fb_size=f.smem_len;signal(SIGTERM,blank_and_exit);signal(SIGINT,blank_and_exit);
 int sf=open(UI_STATE,O_RDWR|O_CREAT,0600);if(sf<0||ftruncate(sf,sizeof(struct ui_state)))return 1;
 struct ui_state *state=mmap(0,sizeof(*state),PROT_READ|PROT_WRITE,MAP_SHARED,sf,0);if(state==MAP_FAILED)return 1;
 if(state->magic!=0x52583332){*state=(struct ui_state){0x52583332,{1,.6,0,1,.5,.5},0,1};}
 /* Any scalable sans will do. Try the usual Debian/Raspbian packages in turn rather than depending on
    one font package, and say which paths were tried instead of exiting silently. $RX3_FONT overrides. */
 static const char *fonts[]={
  "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
  "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
  "/usr/share/fonts/truetype/piboto/Piboto-Regular.ttf",
  "/usr/share/fonts/truetype/crosextra/Carlito-Regular.ttf",0};
 FT_Library ft;
 if(FT_Init_FreeType(&ft)){fprintf(stderr,"rx3-fb-present: cannot initialise FreeType\n");return 1;}
 const char *chosen=getenv("RX3_FONT");
 if(!chosen||FT_New_Face(ft,chosen,0,&face)){
  chosen=0;
  for(int i=0;fonts[i];i++) if(!FT_New_Face(ft,fonts[i],0,&face)){chosen=fonts[i];break;}
 }
 if(!chosen){
  fprintf(stderr,"rx3-fb-present: no usable font found. Install one with:\n"
                 "  sudo apt install fonts-dejavu-core\n"
                 "Or point RX3_FONT at a .ttf. Looked for:\n");
  for(int i=0;fonts[i];i++) fprintf(stderr,"  %s\n",fonts[i]);
  return 1;
 }
 box(0,0,1920,1200,0x101820);for(int i=0;i<12;i++)drawbutton(i,0);
 for(int i=0;i<6;i++){int x=i<3?0:1760,y=(i%3)*333;label(x+80,y+27,slider_names[i],23,0xffffff);}
 memcpy(chrome,frame,sizeof(frame));
 for(;;){
 memcpy(frame,chrome,sizeof(frame));
 for(int y=0;y<1000;y++){int sy=y*4/5;for(int x=0;x<1600;x++)frame[y*1920+x+160]=s[sy*1280+x*4/5];}
 for(int i=0;i<12;i++)if(state->pressed&(1u<<i))drawbutton(i,1);
 if(state->cursor_visible){int cx=state->cursor_x,cy=state->cursor_y;   /* arrow pointer: black outline, white fill */
  for(int y=0;y<22;y++)for(int x=0;x<=y&&x<16;x++){int px=cx+x,py=cy+y;if(px<0||px>=1920||py<0||py>=1200)continue;
   int edge=(x==0||x==y||y==21||x==15);frame[py*1920+px]=edge?0x000000:0xffffff;}}
 for(int i=0;i<6;i++){int x=i<3?0:1760,y=(i%3)*333;float n=state->level[i];if(n<0)n=0;if(n>1)n=1;
 box(x+70,y+85,20,180,0x35434e);int h=(int)(180*n);box(x+70,y+265-h,20,h,0x199feb);box(x+30,y+257-h,100,16,0xeaf3fa);
 if(i==0||i==3){unsigned bit=i==0?1:2;box(x+8,y+43,144,32,state->headphone_cue&bit?0x126db0:0x35434e);label(x+80,y+60,"HP CUE",19,0xffffff);}
 char val[24];snprintf(val,sizeof(val),"%d%%",(int)(n*100+.5));label(x+80,y+305,val,25,0xd1dae2);}
 for(int py=0;py<H;py++){const int *m=idx+py*W;unsigned char *line=d+py*f.line_length;
  if(bpp16){uint16_t*row=(uint16_t*)line;for(int px=0;px<W;px++){int i=m[px];if(i<0){row[px]=0;continue;}
   uint32_t c=frame[i];row[px]=(uint16_t)(((c>>8)&0xf800)|((c>>5)&0x07e0)|((c>>3)&0x001f));}}
  else{uint32_t*row=(uint32_t*)line;for(int px=0;px<W;px++){int i=m[px];row[px]=i<0?0:frame[i];}}}
 usleep(33333);
 }
}
