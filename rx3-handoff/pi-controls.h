#ifndef PI_CONTROLS_H
#define PI_CONTROLS_H
/* Shared geometry and controls for the presenter (fb-present.c) and the touch bridge (touch-bridge.c).
   7-inch layout: the firmware's 1280x800 picture is scaled (bilinear) to fill as much of the panel as a
   sidebar of four touch buttons allows. On a 16:9 panel that is exact: 1152x720 + 128 px sidebar on 1280x720.
   Everything else (deck faders, master/headphone levels, crossfader, cue buttons, browse/load/play keys)
   lives on the DDJ-FLX4 or on the firmware's own touch UI. */
/* Override at build time: gcc -DRX3_ROOT_PATH='"/home/you/rx3-rootfs"' ... (install.sh does this). */
#ifndef RX3_ROOT_PATH
#define RX3_ROOT_PATH "/home/rx3/rx3-rootfs"
#endif
#define UI_STATE RX3_ROOT_PATH "/dev/rx3-ui-state"
#define UI_CONTROL RX3_ROOT_PATH "/dev/rx3-control"
#define FW_W 1280
#define FW_H 800
struct command {int key,operation,channel,value;float analog;int extra;};
/* level[] and headphone_cue are unused since the on-screen mixer went; kept so the state file layout is unchanged. */
struct ui_state {unsigned magic;float level[6];unsigned pressed;unsigned headphone_cue;int cursor_x,cursor_y,cursor_visible;};
struct button {const char *label,*sub;int key,channel;unsigned color;};
#define NBUTTONS 4
static const struct button buttons[NBUTTONS]={
 {"SOURCE",0,0x201,0,0x08699c},{"BROWSE","hold: shortcuts",0x202,0,0x08699c},
 {"USB STOP 1","hold 2 s",0x8002,1,0x7a2f2f},{"USB STOP 2","hold 2 s",0x8002,2,0x7a2f2f}
};
/* Logical canvas = the panel seen upright (LW x LH): rotation 0/180 keep W x H, 90/270 swap them.
   Content rectangle (cx,cy,cw,ch) holds the firmware picture at scale s; the sidebar is the last `col` columns. */
struct layout {int W,H,rot,LW,LH,col,cx,cy,cw,ch;double s;struct {int x,y,w,h;} btn[NBUTTONS];};
static struct layout make_layout(int W,int H,int rot){
 struct layout L;L.W=W;L.H=H;L.rot=rot;int side=rot==90||rot==270;L.LW=side?H:W;L.LH=side?W:H;
 L.col=L.LW/10;double sx=(L.LW-L.col)*1.0/FW_W,sy=L.LH*1.0/FW_H;L.s=sx<sy?sx:sy;
 L.cw=(int)(FW_W*L.s);L.ch=(int)(FW_H*L.s);L.cx=(L.LW-L.col-L.cw)/2;L.cy=(L.LH-L.ch)/2;
 int pad=L.col/16,bw=L.col-2*pad,bh=(L.LH/2-3*pad)/2,x=L.LW-L.col+pad;
 int ys[NBUTTONS]={pad,2*pad+bh,L.LH/2+pad,L.LH/2+2*pad+bh};
 for(int i=0;i<NBUTTONS;i++){L.btn[i].x=x;L.btn[i].y=ys[i];L.btn[i].w=bw;L.btn[i].h=bh;}
 return L;}
/* Panel pixel -> logical pixel (rotation only; the canvas covers the whole panel). */
static inline void panel_to_logical(const struct layout*L,int px,int py,int*lx,int*ly){
 switch(L->rot){case 90:*lx=py;*ly=L->LH-1-px;break;case 180:*lx=L->LW-1-px;*ly=L->LH-1-py;break;case 270:*lx=L->LW-1-py;*ly=px;break;default:*lx=px;*ly=py;}}
static int button_at(const struct layout*L,int lx,int ly){
 for(int i=0;i<NBUTTONS;i++)if(lx>=L->btn[i].x&&lx<L->btn[i].x+L->btn[i].w&&ly>=L->btn[i].y&&ly<L->btn[i].y+L->btn[i].h)return i;return -1;}
/* RX3_ROTATE picks the rotation (0/90/180/270, clockwise); otherwise portrait panels get 90, landscape 0. */
static int rotation_for(int W,int H){const char*e=getenv("RX3_ROTATE");
 if(e&&*e){int r=atoi(e);if(r==0||r==90||r==180||r==270)return r;fprintf(stderr,"RX3_ROTATE=%s ignored: use 0, 90, 180 or 270\n",e);}
 return H>W?90:0;}
/* RX3_FB names the framebuffer to draw on (rx3-env.sh picks the DSI panel when one exists). */
static const char *fb_device(void){const char*e=getenv("RX3_FB");return e&&*e?e:"/dev/fb0";}
#endif
