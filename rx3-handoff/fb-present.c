/* rx3-fb-present: show the RX3 firmware's virtual 1280x800 screen on the real display, plus a sidebar of
   touch buttons. Frames come from the player shim (fbshim.c): each time the firmware finishes drawing one,
   the shim copies it into the snapshot half of the chroot's /dev/fb0 file (fb-frame.h) and wakes us. The
   picture is scaled in two separable bilinear passes (each walking memory in order, unlike a rotated
   per-pixel sample), rotated in cache-sized tiles into a back buffer, and copied to the panel right after
   its vertical blank. Without shim snapshots (an old shim) the live firmware picture is polled at the panel
   rate, as before. usage: rx3-fb-present <chroot fb file> [framebuffer] */
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <linux/fb.h>
#include <linux/futex.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <signal.h>
#include "pi-controls.h"
#include "fb-frame.h"
#if defined(__aarch64__) && defined(__ARM_NEON) && !defined(RX3_NO_NEON)
#include <arm_neon.h>
#define HAVE_NEON 1
#endif
static FT_Face face;static uint32_t *chrome,*canvas;static struct layout L;
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
/* Bilinear sampling tables: for each content column/row, the source coordinate and the 8-bit weight of the next one. */
static int *sx0,*fx,*sy0,*fy;
static inline uint32_t blend(uint32_t a,uint32_t b,unsigned w){unsigned w1=256-w;
 return ((((a&0xff00ff)*w1+(b&0xff00ff)*w)>>8)&0xff00ff)|((((a&0xff00)*w1+(b&0xff00)*w)>>8)&0xff00);}
/* Our own copy of the last frame taken, and which of its rows changed. Comparing row by row costs a fraction of a
   millisecond and lets the rest of the pipeline skip everything that stayed the same: on the deck screen only the
   waveforms and counters move, and an idle screen costs nothing at all. */
static uint32_t *prev;static unsigned char rowdirty[FW_H],*outdirty;static uint64_t rowh[FW_H];
/* Row hash (FNV-1a per 32-bit lane, lanes folded): reading the 4 MB frame once is what a frame costs when nothing
   moved; a byte-for-byte compare against our copy read twice as much. A one-word change always changes the hash. */
#ifdef HAVE_NEON
static inline uint64_t rowhash(const uint32_t *r){uint32x4_t h=vdupq_n_u32(2166136261u),p=vdupq_n_u32(16777619u);
 for(int x=0;x<FW_W;x+=4)h=vmulq_u32(veorq_u32(h,vld1q_u32(r+x)),p);
 uint32_t v[4];vst1q_u32(v,h);return ((uint64_t)(v[0]^v[2])<<32)|(v[1]^v[3]);}
#else
static inline uint64_t rowhash(const uint32_t *r){uint32_t h0=2166136261u,h1=h0;for(int x=0;x<FW_W;x+=2){h0=(h0^r[x])*16777619u;h1=(h1^r[x+1])*16777619u;}return ((uint64_t)h0<<32)|h1;}
#endif
static int take_frame(const uint32_t *src,int all){
 int n=0;
 for(int y=0;y<FW_H;y++){const uint32_t *r=src+(size_t)y*FW_W;uint64_t h=rowhash(r);
  if(all||h!=rowh[y]){rowh[y]=h;memcpy(prev+(size_t)y*FW_W,r,FW_W*4);rowdirty[y]=1;n++;}else rowdirty[y]=0;}
 return n;
}
#ifdef HAVE_NEON
/* Horizontal pass, four output pixels at a time: gather the two source pixels of each into lanes, widen to
   16 bits per channel, blend, narrow. Weights are precomputed per quad. */
static uint16x8_t *wlo,*whi,*w1lo,*w1hi;
static void hpass(const uint32_t *row,uint32_t *out){
 int i=0;
 for(;i+4<=L.cw;i+=4){const int *x=sx0+i;
  uint32x4_t a=vdupq_n_u32(0),b=vdupq_n_u32(0);
  a=vld1q_lane_u32(row+x[0],a,0);a=vld1q_lane_u32(row+x[1],a,1);a=vld1q_lane_u32(row+x[2],a,2);a=vld1q_lane_u32(row+x[3],a,3);
  b=vld1q_lane_u32(row+x[0]+1,b,0);b=vld1q_lane_u32(row+x[1]+1,b,1);b=vld1q_lane_u32(row+x[2]+1,b,2);b=vld1q_lane_u32(row+x[3]+1,b,3);
  uint8x16_t a8=vreinterpretq_u8_u32(a),b8=vreinterpretq_u8_u32(b);int k=i>>2;
  uint16x8_t lo=vmlaq_u16(vmulq_u16(vmovl_u8(vget_low_u8(a8)),w1lo[k]),vmovl_u8(vget_low_u8(b8)),wlo[k]);
  uint16x8_t hi=vmlaq_u16(vmulq_u16(vmovl_u8(vget_high_u8(a8)),w1hi[k]),vmovl_u8(vget_high_u8(b8)),whi[k]);
  vst1q_u32(out+i,vreinterpretq_u32_u8(vcombine_u8(vshrn_n_u16(lo,8),vshrn_n_u16(hi,8))));}
 for(;i<L.cw;i++){const uint32_t *q=row+sx0[i];out[i]=blend(q[0],q[1],fx[i]);}
}
#else
static void hpass(const uint32_t *row,uint32_t *out){for(int i=0;i<L.cw;i++){const uint32_t *q=row+sx0[i];out[i]=blend(q[0],q[1],fx[i]);}}
#endif
/* Scale the changed rows of the frame into the content rectangle of the canvas. Per output row: a vertical blend of
   two source rows across the full width (a plain elementwise loop the compiler vectorises), then the horizontal
   pass from that row. Both walk memory in order, so the source streams through the cache once. */
static void scale_into_canvas(void){
 static uint32_t vrow[FW_W];
 for(int j=0;j<L.ch;j++){
  if(!outdirty[L.cy+j])continue;
  const uint32_t *r0=prev+(size_t)sy0[j]*FW_W,*r1=r0+FW_W,*row=r0;unsigned wy=fy[j];
  if(wy==256)row=r1;else if(wy){for(int x=0;x<FW_W;x++)vrow[x]=blend(r0[x],r1[x],wy);row=vrow;}
  hpass(row,canvas+(size_t)(L.cy+j)*L.LW+L.cx);
 }
}
/* Everything outside the content rectangle comes from the pre-drawn chrome (sidebar, letterbox). */
static void restore_chrome(void){
 for(int y=0;y<L.LH;y++){if(!outdirty[y])continue;uint32_t *c=canvas+(size_t)y*L.LW;const uint32_t *k=chrome+(size_t)y*L.LW;
  if(y<L.cy||y>=L.cy+L.ch){memcpy(c,k,(size_t)L.LW*4);continue;}
  if(L.cx)memcpy(c,k,(size_t)L.cx*4);int e=L.cx+L.cw;if(e<L.LW)memcpy(c+e,k+e,(size_t)(L.LW-e)*4);}
}
/* Pointer arrow, 16x22, in logical space. */
static unsigned char arrow[22][16];
static void draw_cursor(int cx,int cy){
 for(int y=0;y<22;y++)for(int x=0;x<16;x++){if(!arrow[y][x])continue;int px=cx+x,py=cy+y;
  if(px<0||px>=L.LW||py<0||py>=L.LH)continue;canvas[(size_t)py*L.LW+px]=arrow[y][x]==1?0x000000:0xffffff;}
}
static inline void put(unsigned char *line,int px,uint32_t c,int bpp16){
 if(bpp16)((uint16_t*)line)[px]=(uint16_t)(((c>>8)&0xf800)|((c>>5)&0x07e0)|((c>>3)&0x001f));else((uint32_t*)line)[px]=c;}
/* Canvas (upright) -> panel rows, for the bands of rows that changed. Rotations 90/270 are a transpose, done
   in 32x32 tiles so every cache line fetched from the canvas is used up before it is evicted, and with NEON
   4x4 transposes inside each tile where the target is 32-bit. */
static void rotate_out(unsigned char *back,int W,int H,int pitch,int bpp16){
 if(L.rot==0||L.rot==180){
  for(int py=0;py<H;py++){int ly=L.rot?L.LH-1-py:py;if(!outdirty[ly])continue;unsigned char *line=back+(size_t)py*pitch;const uint32_t *row=canvas+(size_t)ly*L.LW;
   if(!bpp16&&L.rot==0)memcpy(line,row,(size_t)W*4);else for(int px=0;px<W;px++)put(line,px,row[L.rot?L.LW-1-px:px],bpp16);}
  return;}
 enum{T=32};
 for(int lyb=0;lyb<L.LH;lyb+=T){
  int ye=lyb+T<L.LH?lyb+T:L.LH,any=0;for(int y=lyb;y<ye;y++)any|=outdirty[y];if(!any)continue;
  for(int lxb=0;lxb<L.LW;lxb+=T){int xe=lxb+T<L.LW?lxb+T:L.LW;
#ifdef HAVE_NEON
   if(!bpp16&&xe-lxb==T&&ye-lyb==T){
    for(int ly=lyb;ly<ye;ly+=4)for(int lx=lxb;lx<xe;lx+=4){
     const uint32_t *c=canvas+(size_t)ly*L.LW+lx;
     uint32x4_t r0=vld1q_u32(c),r1=vld1q_u32(c+L.LW),r2=vld1q_u32(c+2*L.LW),r3=vld1q_u32(c+3*L.LW);
     uint32x4x2_t t0=vtrnq_u32(r0,r1),t1=vtrnq_u32(r2,r3);
     uint32x4_t col[4]={vcombine_u32(vget_low_u32(t0.val[0]),vget_low_u32(t1.val[0])),vcombine_u32(vget_low_u32(t0.val[1]),vget_low_u32(t1.val[1])),
      vcombine_u32(vget_high_u32(t0.val[0]),vget_high_u32(t1.val[0])),vcombine_u32(vget_high_u32(t0.val[1]),vget_high_u32(t1.val[1]))};
     for(int m=0;m<4;m++){
      if(L.rot==90){uint32_t *line=(uint32_t*)(back+(size_t)(lx+m)*pitch);uint32x4_t v=vrev64q_u32(col[m]);vst1q_u32(line+(L.LH-4-ly),vextq_u32(v,v,2));}
      else{uint32_t *line=(uint32_t*)(back+(size_t)(L.LW-1-lx-m)*pitch);vst1q_u32(line+ly,col[m]);}}}
    continue;}
#endif
   for(int lx=lxb;lx<xe;lx++){int py=L.rot==90?lx:L.LW-1-lx;unsigned char *line=back+(size_t)py*pitch;
    for(int ly=lyb;ly<ye;ly++){int px=L.rot==90?L.LH-1-ly:ly;put(line,px,canvas[(size_t)ly*L.LW+lx],bpp16);}}}
 }
}
/* Present right after the panel's vertical blank so the copy runs ahead of the scanout; boards whose driver has
   no vsync ioctl, or whose ioctl returns at once, get a timer at the configured rate instead. */
static int vsync_ok=1,fps=60;static long period,last_vb,next_tick;
static void wait_vblank(int dst){
 int zero=0;
 if(vsync_ok&&ioctl(dst,FBIO_WAITFORVSYNC,&zero)<0){vsync_ok=0;fprintf(stderr,"presenter: no vsync ioctl, using a %d fps timer\n",fps);}
 long t=now_us();
 if(!vsync_ok){next_tick+=period;if(next_tick<t)next_tick=t;if(next_tick>t)usleep(next_tick-t);}
 else if(t-last_vb<period/4){usleep(period-(t-last_vb));t=now_us();}   /* returned at once: not a real vsync */
 last_vb=t;
}
int main(int argc,char**argv){
 if(argc<2)return 2;
 const char*fbpath=argc>2?argv[2]:fb_device();
 int src=open(argv[1],O_RDWR);if(src<0)src=open(argv[1],O_RDONLY);int dst=open(fbpath,O_RDWR);if(src<0||dst<0){perror(src<0?argv[1]:fbpath);return 1;}
 struct fb_fix_screeninfo f;struct fb_var_screeninfo v;ioctl(dst,FBIOGET_FSCREENINFO,&f);ioctl(dst,FBIOGET_VSCREENINFO,&v);
 if(v.bits_per_pixel!=32&&v.bits_per_pixel!=16){fprintf(stderr,"Need a 16- or 32-bit framebuffer (got %u bpp)\n",v.bits_per_pixel);return 1;}
 int bpp16=v.bits_per_pixel==16,W=v.xres,H=v.yres;
 L=make_layout(W,H,rotation_for(W,H));
 {const char*e=getenv("RX3_FPS");if(e&&atoi(e)>0)fps=atoi(e);}period=1000000L/fps;
 int nearest=0;{const char*e=getenv("RX3_FILTER");if(e&&!strcmp(e,"nearest"))nearest=1;}   /* cheaper for slow boards */
 struct stat st;if(fstat(src,&st))return 1;int snapshots=st.st_size>=RX3_FB_FILE_BYTES;
 fprintf(stderr,"presenter: %s %dx%d rotate %d -> logical %dx%d, firmware %dx%d at %d,%d (x%.3f, %s), sidebar %d px, %s\n",fbpath,W,H,L.rot,L.LW,L.LH,L.cw,L.ch,L.cx,L.cy,L.s,nearest?"nearest":"bilinear",L.col,snapshots?"frames from the player shim":"polling the live picture (old shim)");
 const unsigned char *smap=mmap(0,snapshots?RX3_FB_FILE_BYTES:RX3_FB_BYTES,PROT_READ,MAP_SHARED,src,0);
 unsigned char *d=mmap(0,f.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,dst,0);if(smap==MAP_FAILED||d==MAP_FAILED)return 1;
 const uint32_t *live=(const uint32_t*)smap,*snap=(const uint32_t*)(smap+RX3_FB_SNAP);
 volatile const unsigned *seqp=snapshots?(volatile const unsigned*)(smap+RX3_FB_SEQ):0;
 fb_mem=d;fb_size=f.smem_len;signal(SIGTERM,blank_and_exit);signal(SIGINT,blank_and_exit);
 size_t fbsize=(size_t)f.line_length*H;unsigned char *back=malloc(fbsize);
 chrome=malloc(sizeof(uint32_t)*L.LW*L.LH);canvas=malloc(sizeof(uint32_t)*L.LW*L.LH);if(!back||!chrome||!canvas)return 1;
 int sf=open(UI_STATE,O_RDWR|O_CREAT,0600);if(sf<0||ftruncate(sf,sizeof(struct ui_state)))return 1;
 struct ui_state *state=mmap(0,sizeof(*state),PROT_READ|PROT_WRITE,MAP_SHARED,sf,0);if(state==MAP_FAILED)return 1;
 if(state->magic!=0x52583332){*state=(struct ui_state){0x52583332,{1,.6,0,1,.5,.5},0,1};}
 prev=malloc(RX3_FB_BYTES);outdirty=malloc(L.LH);if(!prev||!outdirty)return 1;
 static const char *fonts[]={"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
  "/usr/share/fonts/truetype/freefont/FreeSans.ttf","/usr/share/fonts/truetype/piboto/Piboto-Regular.ttf","/usr/share/fonts/truetype/crosextra/Carlito-Regular.ttf",0};
 FT_Library ft;if(FT_Init_FreeType(&ft)){fprintf(stderr,"rx3-fb-present: cannot initialise FreeType\n");return 1;}
 const char *chosen=getenv("RX3_FONT");
 if(!chosen||FT_New_Face(ft,chosen,0,&face)){chosen=0;for(int i=0;fonts[i];i++)if(!FT_New_Face(ft,fonts[i],0,&face)){chosen=fonts[i];break;}}
 if(!chosen){fprintf(stderr,"rx3-fb-present: no usable font found. Install one with:\n  sudo apt install fonts-dejavu-core\nOr point RX3_FONT at a .ttf. Looked for:\n");for(int i=0;fonts[i];i++)fprintf(stderr,"  %s\n",fonts[i]);return 1;}
 box(0,0,L.LW,L.LH,0x101820);for(int i=0;i<NBUTTONS;i++)drawbutton(i,0);unsigned drawn=0;
 sx0=malloc(sizeof(int)*L.cw);fx=malloc(sizeof(int)*L.cw);sy0=malloc(sizeof(int)*L.ch);fy=malloc(sizeof(int)*L.ch);
 for(int i=0;i<L.cw;i++){double u=(i+0.5)/L.s-0.5;if(u<0)u=0;int u0=(int)u;if(u0>FW_W-2)u0=FW_W-2;sx0[i]=u0;fx[i]=(int)((u-u0)*256);if(fx[i]>255)fx[i]=255;if(nearest)fx[i]=fx[i]>=128?256:0;}
 for(int j=0;j<L.ch;j++){double u=(j+0.5)/L.s-0.5;if(u<0)u=0;int u0=(int)u;if(u0>FW_H-2)u0=FW_H-2;sy0[j]=u0;fy[j]=(int)((u-u0)*256);if(fy[j]>255)fy[j]=255;if(nearest)fy[j]=fy[j]>=128?256:0;}
#ifdef HAVE_NEON
 {int nq=(L.cw+3)/4;wlo=aligned_alloc(16,nq*16);whi=aligned_alloc(16,nq*16);w1lo=aligned_alloc(16,nq*16);w1hi=aligned_alloc(16,nq*16);if(!wlo||!whi||!w1lo||!w1hi)return 1;
  for(int k=0;k<nq;k++){unsigned w[4];for(int m=0;m<4;m++)w[m]=k*4+m<L.cw?fx[k*4+m]:0;
   wlo[k]=vcombine_u16(vdup_n_u16(w[0]),vdup_n_u16(w[1]));whi[k]=vcombine_u16(vdup_n_u16(w[2]),vdup_n_u16(w[3]));
   w1lo[k]=vsubq_u16(vdupq_n_u16(256),wlo[k]);w1hi[k]=vsubq_u16(vdupq_n_u16(256),whi[k]);}}
#endif
 for(int y=0;y<22;y++)for(int x=0;x<16;x++)if(x<=y)arrow[y][x]=(x==0||x==y||y==21||x==15)?1:2;
 next_tick=now_us();
 long frames=0,renders=0,retries=0,r_sum=0,s_sum=0,r_max=0,c_sum=0,c_max=0,rows=0,t_report=now_us();
 uint32_t last_sig=0;unsigned last_seq=0;int have_snap=0,last_curx=-100,last_cury=0,first=1;
 for(;;){
  unsigned seq=seqp?*seqp:0;
  if(seq&&!have_snap){have_snap=1;fprintf(stderr,"presenter: the player shim is delivering completed frames\n");}
  int curx=state->cursor_visible?state->cursor_x:-100,cury=state->cursor_y,newframe;
  if(have_snap)newframe=seq!=last_seq&&!(seq&1);
  else{/* Cheap signature of the live picture: one word in 61 (61 is odd, so rows and columns are both covered). */
   uint32_t sig=2166136261u;for(int i=0;i<FW_W*FW_H;i+=61)sig=(sig^live[i])*16777619u;newframe=sig!=last_sig;last_sig=sig;}
  int buttons=state->pressed!=drawn,moved=curx!=last_curx||cury!=last_cury;
  if(!newframe&&!buttons&&!moved&&!first){
   if(have_snap){struct timespec ts={0,4000000};syscall(SYS_futex,seqp,FUTEX_WAIT,seq,&ts,0,0);}   /* a frame, or 4 ms for the buttons/cursor */
   else wait_vblank(dst);
   frames++;goto report;
  }
  long t0=now_us();
  memset(outdirty,first?1:0,L.LH);
  if(newframe||first){
   if(have_snap){
    /* Seqlock: take the frame between two equal, even readings; the shim's copy takes about a millisecond, so a
       retry is rare. Give up after a few and use what we have rather than stall. */
    for(int tries=0;;tries++){unsigned s0=*seqp;if(s0&1){usleep(200);continue;}
     __sync_synchronize();take_frame(snap,first);__sync_synchronize();
     if(*seqp==s0||tries>=2){last_seq=s0;if(*seqp!=s0)retries++;break;}retries++;}
   }else take_frame(live,first);
   for(int j=0;j<L.ch;j++)if(rowdirty[sy0[j]]||rowdirty[sy0[j]+1])outdirty[L.cy+j]=1;
  }
  if(buttons){for(int i=0;i<NBUTTONS;i++)if(((state->pressed^drawn)>>i)&1){drawbutton(i,(state->pressed>>i)&1);
    for(int y=L.btn[i].y;y<L.btn[i].y+L.btn[i].h&&y<L.LH;y++)if(y>=0)outdirty[y]=1;}drawn=state->pressed;}
  if(moved){for(int y=last_cury;y<last_cury+22;y++)if(y>=0&&y<L.LH)outdirty[y]=1;for(int y=cury;y<cury+22;y++)if(y>=0&&y<L.LH)outdirty[y]=1;}
  scale_into_canvas();
  long ts=now_us();s_sum+=ts-t0;
  restore_chrome();if(curx>-100)draw_cursor(curx,cury);last_curx=curx;last_cury=cury;
  rotate_out(back,W,H,f.line_length,bpp16);
  for(int y=0;y<L.LH;y++)rows+=outdirty[y];
  long t1=now_us();r_sum+=t1-t0;if(t1-t0>r_max)r_max=t1-t0;renders++;first=0;
  wait_vblank(dst);
  long t2=now_us();memcpy(d,back,fbsize);long t3=now_us();c_sum+=t3-t2;if(t3-t2>c_max)c_max=t3-t2;frames++;
 report:;
  long t=now_us();
  if(t-t_report>=60000000L){
   fprintf(stderr,"presenter: %.1f frames/s shown, %ld%% of rows redrawn, render %ld us avg (scale %ld) %ld max, panel copy %ld us avg %ld max, %ld torn reads, vsync %s, %s\n",
    renders*1e6/(t-t_report),renders?rows*100/(renders*L.LH):0,renders?r_sum/renders:0,renders?s_sum/renders:0,r_max,renders?c_sum/renders:0,c_max,retries,vsync_ok?"yes":"no",have_snap?"shim frames":"live polling");
   frames=renders=retries=r_sum=s_sum=r_max=c_sum=c_max=rows=0;t_report=t;}
 }
}
