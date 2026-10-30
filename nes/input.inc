/* SPDX-License-Identifier: GPL-2.0-or-later */
/* evdev keyboard and gamepad input, including hotplug and SYN_DROPPED. */
static unsigned char is_pad[16], dropped[16];
static unsigned pad_port[16];
static struct input_absinfo axes[16][4];
static unsigned char have_axis[16][4];
static const unsigned axis_codes[4]={ABS_X,ABS_Y,ABS_HAT0X,ABS_HAT0Y};
static dev_t input_rdev[16];
static int auto_scan;
static struct timespec last_scan;
static int bit(const unsigned char *bits,unsigned code) { return (bits[code/8]>>(code%8))&1; }
static void sync_input(int i)
{
    unsigned char bits[(KEY_MAX+8)/8]={0};
    if(ioctl(keys[i],EVIOCGKEY(sizeof(bits)),bits)>=0)
        for(unsigned k=0;k<=KEY_MAX;k++) pressed[i][k]=(unsigned char)bit(bits,k);
    for(unsigned a=0;a<4;a++)
        have_axis[i][a]=ioctl(keys[i],EVIOCGABS(axis_codes[a]),&axes[i][a])==0;
}
static void remove_input(int i)
{
    close(keys[i]); keys[i]=-1;
    memset(pressed[i],0,sizeof(pressed[i]));
    memset(have_axis[i],0,sizeof(have_axis[i]));
    is_pad[i]=dropped[i]=0;
}
static int add_keyboard(const char *path, int automatic)
{
    struct stat st;
    if(stat(path,&st)<0) return -1;
    for(int i=0;i<nkeys;i++) if(keys[i]>=0 && input_rdev[i]==st.st_rdev) return 0;
    int fd=open(path,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    unsigned char caps[(KEY_MAX+8)/8]={0};
    if(fd<0) return -1;
    if(ioctl(fd,EVIOCGBIT(EV_KEY,sizeof(caps)),caps)<0) {close(fd);return -1;}
    int pad=bit(caps,BTN_SOUTH)||bit(caps,BTN_TRIGGER)||bit(caps,BTN_DPAD_UP);
    int keyboard=bit(caps,KEY_ENTER)&&bit(caps,KEY_UP);
    if(automatic && !pad && !keyboard) {close(fd);return -1;}
    int slot=0;
    while(slot<nkeys && keys[slot]>=0) slot++;
    if(slot==16) {close(fd);errno=ENOSPC;return -1;}
    if(slot==nkeys) nkeys++;
    keys[slot]=fd; input_rdev[slot]=st.st_rdev;
    memset(pressed[slot],0,sizeof(pressed[slot]));
    is_pad[slot]=(unsigned char)pad; dropped[slot]=0;
    if(pad) {
        unsigned port=0;
        for(;port<2;port++) {
            int taken=0;
            for(int i=0;i<nkeys;i++) if(i!=slot && keys[i]>=0 && is_pad[i] && pad_port[i]==port) taken=1;
            if(!taken) break;
        }
        if(port==2) {remove_input(slot);errno=ENOSPC;return -1;}
        pad_port[slot]=port;
    }
    sync_input(slot);
    if(ioctl(fd,EVIOCGRAB,1)<0) fprintf(stderr,"Warning: cannot grab %s: %s\n",path,strerror(errno));
    char name[128]={0};ioctl(fd,EVIOCGNAME(sizeof(name)),name);
    if(pad) fprintf(stderr,"Input: %s [%s] gamepad -> player %u\n",path,name,pad_port[slot]+1);
    else fprintf(stderr,"Input: %s [%s] keyboard\n",path,name);
    return 0;
}
static void scan_inputs(void)
{
    for(int i=0;i<32;i++) {
        char path[64];snprintf(path,sizeof(path),"/dev/input/event%d",i);
        add_keyboard(path,1);
    }
    clock_gettime(CLOCK_MONOTONIC,&last_scan);
}
static void handle_event(int i,const struct input_event *e)
{
    if(e->type==EV_SYN && e->code==SYN_DROPPED) {dropped[i]=1;return;}
    if(dropped[i]) {
        if(e->type==EV_SYN && e->code==SYN_REPORT) {sync_input(i);dropped[i]=0;}
        return;
    }
    if(e->type==EV_KEY && e->code<=KEY_MAX) {
        pressed[i][e->code]=e->value!=0;
        if(e->code==KEY_ESC && e->value) stopped=1;
    }
    if(e->type==EV_ABS)
        for(unsigned a=0;a<4;a++) if(e->code==axis_codes[a]) axes[i][a].value=e->value;
}
static void poll_input(void)
{
    for(int i=0;i<nkeys;i++) {
        if(keys[i]<0) continue;
        struct input_event e;ssize_t n;
        while((n=read(keys[i],&e,sizeof(e)))==(ssize_t)sizeof(e)) handle_event(i,&e);
        if(n==0 || (n<0 && errno!=EAGAIN && errno!=EINTR)) remove_input(i);
    }
    if(auto_scan) {
        struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
        if(now.tv_sec>last_scan.tv_sec) scan_inputs();
    }
}
static int axis_direction(int i,unsigned a,int direction)
{
    if(!have_axis[i][a]) return 0;
    const struct input_absinfo *v=&axes[i][a];
    int64_t range=(int64_t)v->maximum-v->minimum;
    if(range<=0) return 0;
    int64_t center=(int64_t)v->minimum+range/2,dead=range/4;
    if(dead<v->flat) dead=v->flat;
    return direction<0 ? v->value<center-dead : v->value>center+dead;
}
static int pad_button(int i,unsigned id)
{
    const unsigned char *p=pressed[i];
    switch(id) {
    case RETRO_DEVICE_ID_JOYPAD_A:return p[BTN_SOUTH]||p[BTN_TRIGGER];
    case RETRO_DEVICE_ID_JOYPAD_B:return p[BTN_EAST]||p[BTN_THUMB];
    case RETRO_DEVICE_ID_JOYPAD_X:return p[BTN_NORTH]||p[BTN_THUMB2];
    case RETRO_DEVICE_ID_JOYPAD_Y:return p[BTN_WEST]||p[BTN_TOP];
    case RETRO_DEVICE_ID_JOYPAD_START:return p[BTN_START]||p[BTN_BASE2];
    case RETRO_DEVICE_ID_JOYPAD_SELECT:return p[BTN_SELECT]||p[BTN_BASE];
    case RETRO_DEVICE_ID_JOYPAD_LEFT:return p[BTN_DPAD_LEFT]||axis_direction(i,0,-1)||axis_direction(i,2,-1);
    case RETRO_DEVICE_ID_JOYPAD_RIGHT:return p[BTN_DPAD_RIGHT]||axis_direction(i,0,1)||axis_direction(i,2,1);
    case RETRO_DEVICE_ID_JOYPAD_UP:return p[BTN_DPAD_UP]||axis_direction(i,1,-1)||axis_direction(i,3,-1);
    case RETRO_DEVICE_ID_JOYPAD_DOWN:return p[BTN_DPAD_DOWN]||axis_direction(i,1,1)||axis_direction(i,3,1);
    default:return 0;
    }
}
static int16_t input(unsigned port,unsigned device,unsigned index,unsigned id)
{
    (void)index;
    static const unsigned map[2][16]={
        {KEY_Z,0,KEY_RIGHTSHIFT,KEY_ENTER,KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,KEY_X},
        {KEY_G,0,KEY_R,KEY_T,KEY_W,KEY_S,KEY_A,KEY_D,KEY_H}
    };
    if(port>1 || device!=RETRO_DEVICE_JOYPAD || id>=16) return 0;
    for(int i=0;i<nkeys;i++) {
        if(is_pad[i]) {if(pad_port[i]==port && pad_button(i,id)) return 1;}
        else if(map[port][id] && pressed[i][map[port][id]]) return 1;
    }
    return 0;
}
