// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sound/asound.h>
#include "libretro.h"
#define RATE 48000U
#define PERIOD 1024U
#define BUFFER (PERIOD * 4U)
static volatile sig_atomic_t stopped;
static int failed, audio_fd = -1, fb_fd = -1, keys[16], nkeys;
static unsigned char pressed[16][KEY_MAX+1];
static struct fb_var_screeninfo vi;
static struct fb_fix_screeninfo fi;
static unsigned char *fb;
static enum retro_pixel_format pixel = RETRO_PIXEL_FORMAT_0RGB1555;
static unsigned long frames, limit;
static int headless;
static unsigned requested_scale;
static int stretch=1;
static unsigned *xends;
static size_t xends_capacity;
static int frameskip=-1;
static unsigned consecutive_skips;
static unsigned long rendered_frames, skipped_frames;
static double game_fps=60.0;
static unsigned char *scaled_row;
static size_t scaled_row_capacity;
static unsigned long audio_underruns;
static uint64_t audio_frames_written;
static unsigned audio_peak;
static uint64_t audio_clipped;
static struct timespec run_start;
static int run_started;
static void stop_handler(int sig) { (void)sig; stopped = 1; }
#include "pcm.inc"
/* Prefer the board codec; only use a generic device when unambiguous. */
static int find_pcm(const char *devdir, const char *procdir, char *out, size_t size)
{
    char pattern[PATH_MAX];
    glob_t paths={0};
    snprintf(pattern,sizeof(pattern),"%s/pcmC*D*p",devdir);
    int rc=glob(pattern,0,NULL,&paths);
    if (rc) { globfree(&paths); errno=rc==GLOB_NOMATCH?ENOENT:EIO; return -1; }
    size_t preferred=0,chosen=0;
    for (size_t i=0;i<paths.gl_pathc;i++) {
        const char *base=strrchr(paths.gl_pathv[i],'/');
        unsigned card,device; char extra;
        if (!base || sscanf(base+1,"pcmC%uD%up%c",&card,&device,&extra)!=2) continue;
        char idpath[PATH_MAX],id[128]={0};
        snprintf(idpath,sizeof(idpath),"%s/card%u/id",procdir,card);
        FILE *f=fopen(idpath,"r");
        if (f) { if (!fgets(id,sizeof(id),f)) id[0]=0; fclose(f); }
        if (strstr(id,"wm8960") || strstr(id,"WM8960")) { preferred++; chosen=i; }
    }
    if (preferred!=1 && !(preferred==0 && paths.gl_pathc==1)) {
        fprintf(stderr,"Multiple playback devices; select one with --pcm:\n");
        for(size_t i=0;i<paths.gl_pathc;i++) fprintf(stderr,"  %s\n",paths.gl_pathv[i]);
        globfree(&paths); errno=ENOTUNIQ; return -1;
    }
    if (!preferred) chosen=0;
    int n=snprintf(out,size,"%s",paths.gl_pathv[chosen]);
    globfree(&paths);
    if(n<0 || (size_t)n>=size) { errno=ENAMETOOLONG; return -1; }
    return 0;
}
static bool environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixel = *(enum retro_pixel_format *)data;
        return pixel == RETRO_PIXEL_FORMAT_RGB565 || pixel == RETRO_PIXEL_FORMAT_XRGB8888 || pixel == RETRO_PIXEL_FORMAT_0RGB1555;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        if (!strcmp(v->key, "fceumm_sndrate_hint")) { v->value = "48KHz"; return true; }
        if (!strcmp(v->key, "fceumm_hdpacks")) { v->value = "disabled"; return true; }
        v->value = NULL; return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = "."; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: return true;
    case RETRO_ENVIRONMENT_SHUTDOWN: stopped = 1; return true;
    default: return false;
    }
}
static uint32_t component(unsigned c, struct fb_bitfield b)
{ return b.length ? ((c >> (8-b.length)) << b.offset) : 0; }
static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data || !fb || !w || !h) return;
    int skip=0;
    if (frameskip>=0) skip=frames%(unsigned)(frameskip+1)!=0;
    else if (audio_fd>=0 && run_started && consecutive_skips<9) {
        struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
        double elapsed=(now.tv_sec-run_start.tv_sec)+(now.tv_nsec-run_start.tv_nsec)/1e9;
        skip=elapsed>(frames+0.5)/game_fps;
    }
    if (skip) { skipped_frames++; consecutive_skips++; return; }
    consecutive_skips=0;
    unsigned dw=vi.xres,dh=vi.yres,x0=0,y0=0;
    if (!stretch) {
        unsigned scale=vi.xres/w;
        if (scale>vi.yres/h) scale=vi.yres/h;
        if (requested_scale && scale>requested_scale) scale=requested_scale;
        if (!scale) { fprintf(stderr,"LCD smaller than NES image\n"); failed=stopped=1; return; }
        dw=w*scale; dh=h*scale;
        x0=(vi.xres-dw)/2; y0=(vi.yres-dh)/2;
    }
    unsigned bytes=vi.bits_per_pixel/8;
    size_t row_bytes=(size_t)dw*bytes;
    if (xends_capacity<w) {
        unsigned *new_ends=realloc(xends,(size_t)w*sizeof(*xends));
        if (!new_ends) { perror("scale map"); failed=stopped=1; return; }
        xends=new_ends; xends_capacity=w;
    }
    for(unsigned x=0;x<w;x++) xends[x]=(unsigned)(((uint64_t)(x+1)*dw+w-1)/w);
    if (scaled_row_capacity<row_bytes) {
        unsigned char *new_row=realloc(scaled_row,row_bytes);
        if (!new_row) { perror("scale buffer"); failed=stopped=1; return; }
        scaled_row=new_row; scaled_row_capacity=row_bytes;
    }
    int direct32=pixel==RETRO_PIXEL_FORMAT_XRGB8888 && bytes==4 &&
        vi.red.offset==16 && vi.red.length==8 && vi.green.offset==8 &&
        vi.green.length==8 && vi.blue.offset==0 && vi.blue.length==8;
    unsigned dest_y=0;
    for (unsigned y=0; y<h; y++) {
        unsigned end_y=(unsigned)(((uint64_t)(y+1)*dh+h-1)/h);
        if (end_y==dest_y) continue;
        const unsigned char *src=(const unsigned char *)data+y*pitch;
        /* Build the scaled row in cached RAM. Never read framebuffer memory:
         * mmap fbdev may be uncached/write-combined on the i.MX6ULL. */
        if (direct32 && dw==w) memcpy(scaled_row,src,row_bytes);
        else if (direct32) {
            uint32_t *dst=(uint32_t *)scaled_row;
            unsigned dx=0;
            for (unsigned x=0; x<w; x++) {
                uint32_t value; memcpy(&value,src+x*4,4);
                while (dx<xends[x]) dst[dx++]=value;
            }
        } else {
          unsigned dx=0;
          for (unsigned x=0; x<w; x++) {
            uint32_t r,g,b,v;
            if (pixel == RETRO_PIXEL_FORMAT_XRGB8888) {
                memcpy(&v,src+x*4,4); r=(v>>16)&255; g=(v>>8)&255; b=v&255;
            } else {
                uint16_t q; memcpy(&q,src+x*2,2);
                b=(q&31)*255/31;
                if (pixel == RETRO_PIXEL_FORMAT_RGB565) { r=(q>>11)*255/31; g=((q>>5)&63)*255/63; }
                else { r=((q>>10)&31)*255/31; g=((q>>5)&31)*255/31; }
            }
            v=component(r,vi.red)|component(g,vi.green)|component(b,vi.blue);
            while (dx<xends[x]) { memcpy(scaled_row+(size_t)dx*bytes,&v,bytes); dx++; }
          }
        }
        while (dest_y<end_y) {
            unsigned char *row=fb+(y0+dest_y+vi.yoffset)*fi.line_length+(x0+vi.xoffset)*bytes;
            memcpy(row,scaled_row,row_bytes); dest_y++;
        }
    }
    rendered_frames++;
}
static size_t audio_batch(const int16_t *data, size_t count)
{
    for (size_t i=0;i<count*2;i++) {
        int sample=data[i];
        unsigned magnitude=(unsigned)(sample<0?-sample:sample);
        if (magnitude>audio_peak) audio_peak=magnitude;
        if (sample==INT16_MIN || sample==INT16_MAX) audio_clipped++;
    }
    if (audio_fd < 0) return count;
    size_t done=0;
    while (done<count && !stopped) {
        struct snd_xferi x = { .buf=(void *)(data+done*2), .frames=count-done };
        int rc=ioctl(audio_fd,SNDRV_PCM_IOCTL_WRITEI_FRAMES,&x);
        int err=rc<0 ? errno : x.result<0 ? (int)-x.result : 0;
        if (err==EINTR) continue;
        if (err==EPIPE) {
            audio_underruns++;
            if (audio_underruns==1 || audio_underruns%60==0)
                fprintf(stderr,"Audio underruns: %lu (CPU load or playback clock mismatch)\n",audio_underruns);
            if (!ioctl(audio_fd,SNDRV_PCM_IOCTL_PREPARE)) continue;
            err=errno;
        }
        if (err || x.result<=0 || (size_t)x.result>count-done) {
            errno=err?err:EIO; perror("PCM write"); failed=stopped=1; break;
        }
        done+=(size_t)x.result;
        audio_frames_written+=(size_t)x.result;
    }
    return done;
}
static void audio_sample(int16_t l,int16_t r) { int16_t a[2]={l,r}; audio_batch(a,1); }
#include "input.inc"
static int open_fb(const char *path)
{
    fb_fd=open(path,O_RDWR|O_CLOEXEC);
    if (fb_fd<0 || ioctl(fb_fd,FBIOGET_VSCREENINFO,&vi)<0 || ioctl(fb_fd,FBIOGET_FSCREENINFO,&fi)<0) return -1;
    unsigned bytes=vi.bits_per_pixel/8;
    if ((bytes!=2 && bytes!=3 && bytes!=4) || vi.red.length>8 || vi.green.length>8 || vi.blue.length>8 ||
        vi.red.msb_right || vi.green.msb_right || vi.blue.msb_right || fi.type!=FB_TYPE_PACKED_PIXELS || fi.visual!=FB_VISUAL_TRUECOLOR ||
        vi.red.offset+vi.red.length>vi.bits_per_pixel || vi.green.offset+vi.green.length>vi.bits_per_pixel || vi.blue.offset+vi.blue.length>vi.bits_per_pixel ||
        !vi.xres || !vi.yres || ((uint64_t)vi.xoffset+vi.xres)*bytes>fi.line_length ||
        ((uint64_t)vi.yoffset+vi.yres)*fi.line_length>fi.smem_len) { errno=EINVAL; return -1; }
    fb=mmap(NULL,fi.smem_len,PROT_READ|PROT_WRITE,MAP_SHARED,fb_fd,0);
    if (fb==MAP_FAILED) { fb=NULL; return -1; }
    for (unsigned y=0;y<vi.yres;y++) memset(fb+(y+vi.yoffset)*fi.line_length+vi.xoffset*bytes,0,vi.xres*bytes);
    return 0;
}
static void sram(const char *path,int writeback)
{
    size_t n=retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
    void *data=retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!n || !data) return;
    char *temp=NULL;
    if (writeback) {
        temp=malloc(strlen(path)+12);
        if (!temp) { failed=1; return; }
        sprintf(temp,"%s.tmpXXXXXX",path);
    }
    int fd=writeback ? mkstemp(temp) : open(path,O_RDONLY|O_CLOEXEC);
    if (fd<0) {
        if (writeback || errno!=ENOENT) { perror(path); failed=1; }
        free(temp); return;
    }
    FILE *f=fdopen(fd,writeback?"wb":"rb");
    int bad=0;
    if (!f) { close(fd); bad=1; }
    else {
        if (writeback ? fwrite(data,1,n,f)!=n : fread(data,1,n,f)!=n) bad=1;
        if (writeback && (fflush(f) || fsync(fd))) bad=1;
        if (fclose(f)) bad=1;
    }
    if (writeback && !bad && rename(temp,path)) bad=1;
    if (bad) { fprintf(stderr,"SRAM I/O failed: %s\n",path); failed=1; }
    if (temp) { unlink(temp); free(temp); }
}
int main(int argc,char **argv)
{
    const char *rom=NULL,*fbpath="/dev/fb0",*pcm="auto",*keypath=NULL;
    void *romdata=NULL; char *save=NULL; FILE *f=NULL; int loaded=0,initialized=0;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--mute")) pcm=NULL;
        else if (!strcmp(argv[i],"--headless")) headless=1;
        else if (!strcmp(argv[i],"--stretch")) stretch=1;
        else if (!strcmp(argv[i],"--integer-scale")) stretch=0;
        else if ((!strcmp(argv[i],"--frameskip") || !strcmp(argv[i],"--scale")) && i+1<argc) {
            const char *opt=argv[i++];
            if (!strcmp(opt,"--frameskip") && !strcmp(argv[i],"auto")) frameskip=-1;
            else {
                char *end; errno=0; unsigned long n=strtoul(argv[i],&end,10);
                if(errno || !*argv[i] || *end) goto usage;
                if (!strcmp(opt,"--scale")) { if(n<1 || n>4) goto usage; requested_scale=(unsigned)n; stretch=0; }
                else { if(n>9) goto usage; frameskip=(int)n; }
            }
        }
        else if ((!strcmp(argv[i],"--fb") || !strcmp(argv[i],"--pcm") || !strcmp(argv[i],"--input") || !strcmp(argv[i],"--frames")) && i+1<argc) {
            const char *opt=argv[i++];
            if (!strcmp(opt,"--fb")) fbpath=argv[i];
            else if (!strcmp(opt,"--pcm")) pcm=argv[i];
            else if (!strcmp(opt,"--input")) keypath=argv[i];
            else { char *end; errno=0; limit=strtoul(argv[i],&end,10); if(errno || *end || !limit) goto usage; }
        } else if (argv[i][0]=='-' || rom) goto usage;
        else rom=argv[i];
    }
    if (!rom) goto usage;
    signal(SIGINT,stop_handler); signal(SIGTERM,stop_handler);
    f=fopen(rom,"rb"); if (!f) { perror(rom); goto error; }
    if (fseek(f,0,SEEK_END)) goto error;
    long length=ftell(f);
    if (length<16 || length>64*1024*1024 || fseek(f,0,SEEK_SET)) { fprintf(stderr,"Invalid ROM size\n"); goto error; }
    romdata=malloc((size_t)length); if (!romdata) goto error;
    if (fread(romdata,1,(size_t)length,f)!=(size_t)length) goto error;
    fclose(f); f=NULL;
    if (memcmp(romdata,"NES\032",4)) { fprintf(stderr,"Expected uncompressed iNES .nes ROM\n"); goto error; }
    save=malloc(strlen(rom)+5); if (!save) goto error; sprintf(save,"%s.sav",rom);
    retro_set_environment(environment); retro_set_video_refresh(video);
    retro_set_audio_sample(audio_sample); retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(poll_input); retro_set_input_state(input);
    retro_init(); initialized=1;
    struct retro_game_info game={.path=rom,.data=romdata,.size=(size_t)length};
    if (!retro_load_game(&game)) { fprintf(stderr,"Core rejected ROM\n"); goto error; }
    loaded=1;
    retro_set_controller_port_device(0,RETRO_DEVICE_JOYPAD); retro_set_controller_port_device(1,RETRO_DEVICE_JOYPAD);
    struct retro_system_av_info av; retro_get_system_av_info(&av);
    fprintf(stderr,"NES: %.6f fps, %.0f Hz\n",av.timing.fps,av.timing.sample_rate);
    game_fps=av.timing.fps;
    if (av.timing.sample_rate!=RATE || av.timing.fps<40 || av.timing.fps>70) goto error;
    if (!headless) {
        if (open_fb(fbpath)) { perror("framebuffer"); goto error; }
        if (keypath) { if (add_keyboard(keypath,0)) { perror(keypath); goto error; } }
        else { auto_scan=1; scan_inputs(); }
        if (!nkeys) fprintf(stderr,"No input device yet; waiting for keyboard/gamepad hotplug\n");
    }
    char detected_pcm[PATH_MAX];
    if (pcm) {
        if (!strcmp(pcm,"auto")) {
            if (find_pcm("/dev/snd","/proc/asound",detected_pcm,sizeof(detected_pcm))) {
                perror("Find PCM");
                fprintf(stderr,"Check: cat /proc/asound/cards; cat /proc/asound/pcm; ls /dev/snd\n");
                goto error;
            }
            pcm=detected_pcm;
        }
        fprintf(stderr,"Audio: %s (48000 Hz, S16_LE, stereo)\n",pcm);
        audio_fd=pcm_open(pcm,0);
        if (audio_fd<0) {
            fprintf(stderr,"PCM %s: %s\n",pcm,strerror(errno));
            fprintf(stderr,"Check /proc/asound/cards and /dev/snd; use --pcm auto to detect or --mute to test video/input.\n");
            goto error;
        }
    }
    sram(save,0);
    if (failed) goto out;
    struct timespec deadline; clock_gettime(CLOCK_MONOTONIC,&deadline);
    run_start=deadline; run_started=1;
    long step=(long)(1e9/av.timing.fps);
    while (!stopped && (!limit || frames<limit)) {
        retro_run(); frames++;
        deadline.tv_nsec+=step;
        if(deadline.tv_nsec>=1000000000L) { deadline.tv_sec++; deadline.tv_nsec-=1000000000L; }
        /* PCM blocking writes pace audio playback; avoid a second clock slowly
         * draining the audio queue. Wall-clock pacing is only needed muted. */
        if (!headless && audio_fd<0) {
            struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
            if ((now.tv_sec-deadline.tv_sec)*1000000000LL+now.tv_nsec-deadline.tv_nsec>100000000LL) deadline=now;
            while (!stopped && clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&deadline,NULL)==EINTR) {}
        }
    }
    sram(save,1);
    fprintf(stderr,"Completed %lu frames\n",frames);
    goto out;
error: failed=1;
out:
    if (run_started) {
        struct timespec now; clock_gettime(CLOCK_MONOTONIC,&now);
        double elapsed=(now.tv_sec-run_start.tv_sec)+(now.tv_nsec-run_start.tv_nsec)/1e9;
        fprintf(stderr,"Runtime: %.3f s, %.2f fps; PCM frames: %llu, underruns: %lu, peak: %u/32768, clipped samples: %llu\n",
                elapsed,elapsed>0?frames/elapsed:0,(unsigned long long)audio_frames_written,
                audio_underruns,audio_peak,(unsigned long long)audio_clipped);
    }
    if (audio_fd>=0) { ioctl(audio_fd,SNDRV_PCM_IOCTL_DROP); close(audio_fd); }
    for(int i=0;i<nkeys;i++) if(keys[i]>=0) close(keys[i]);
    if(fb) munmap(fb,fi.smem_len);
    if(fb_fd>=0) close(fb_fd);
    if(loaded) retro_unload_game();
    if(initialized) retro_deinit();
    if(f) fclose(f);
    fprintf(stderr,"Display: %lu drawn, %lu skipped (frameskip=%s)\n",rendered_frames,skipped_frames,frameskip<0?"auto":"fixed");
    free(xends); xends=NULL; xends_capacity=0;
    free(scaled_row); scaled_row=NULL; scaled_row_capacity=0;
    free(romdata); free(save);
    return failed?1:0;
usage:
    fprintf(stderr,"Usage: %s [--fb PATH] [--pcm PATH|auto | --mute] [--input PATH] [--frames N] [--frameskip auto|0..9] [--stretch | --integer-scale | --scale 1..4] [--headless] game.nes\n",argv[0]);
    return 2;
}
