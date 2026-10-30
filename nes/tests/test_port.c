/* SPDX-License-Identifier: GPL-2.0-or-later */
#define main nes_program_main
#include "../nes.c"
#undef main
#include <assert.h>
#include <sys/stat.h>
static unsigned video_calls;
static size_t audio_frames;
static unsigned audio_nonzero;
static void test_video(const void *p,unsigned w,unsigned h,size_t stride)
{
    assert(p && w>=256 && h>=224 && stride>=w*2);
    video_calls++;
}
static size_t test_audio(const int16_t *p,size_t n)
{
    audio_frames+=n;
    for(size_t i=0;i<n*2;i++) audio_nonzero+=p[i]!=0;
    return n;
}
int main(void)
{
    stretch=0;
    /* Device discovery: absent, single, ambiguous, codec preference, removal. */
    char sounddir[]="/tmp/nes-pcm-XXXXXX",a[256],b[256],carddir[256],idpath[256],chosen[256];
    assert(mkdtemp(sounddir));
    assert(find_pcm(sounddir,sounddir,chosen,sizeof(chosen))<0 && errno==ENOENT);
    snprintf(a,sizeof(a),"%s/pcmC0D0p",sounddir);
    snprintf(b,sizeof(b),"%s/pcmC1D0p",sounddir);
    FILE *sf=fopen(a,"w"); assert(sf); assert(!fclose(sf));
    assert(!find_pcm(sounddir,sounddir,chosen,sizeof(chosen)) && !strcmp(chosen,a));
    sf=fopen(b,"w"); assert(sf); assert(!fclose(sf));
    assert(find_pcm(sounddir,sounddir,chosen,sizeof(chosen))<0 && errno==ENOTUNIQ);
    snprintf(carddir,sizeof(carddir),"%s/card1",sounddir); assert(!mkdir(carddir,0700));
    snprintf(idpath,sizeof(idpath),"%s/card1/id",sounddir);
    sf=fopen(idpath,"w"); assert(sf); fputs("alphawm8960\n",sf); assert(!fclose(sf));
    assert(!find_pcm(sounddir,sounddir,chosen,sizeof(chosen)) && !strcmp(chosen,b));
    unlink(b);
    assert(!find_pcm(sounddir,sounddir,chosen,sizeof(chosen)) && !strcmp(chosen,a));
    unlink(a); unlink(idpath); rmdir(carddir); rmdir(sounddir);
    /* Exercise actual scaler with padding, visible offsets and three formats. */
    unsigned char memory[8*7*4];
    uint32_t source[4]={0xff0000,0x00ff00,0x0000ff,0xffffff};
    uint16_t source16[4]={0xf800,0x07e0,0x001f,0xffff};
    vi.xres=4; vi.yres=4; vi.xoffset=1; vi.yoffset=1;
    vi.red=(struct fb_bitfield){16,8,0}; vi.green=(struct fb_bitfield){8,8,0}; vi.blue=(struct fb_bitfield){0,8,0};
    vi.bits_per_pixel=32; fi.line_length=8*4; fb=memory;
    memset(memory,0xaa,sizeof(memory)); pixel=RETRO_PIXEL_FORMAT_XRGB8888;
    video(source,2,2,8);
    uint32_t v; memcpy(&v,memory+fi.line_length+4,4); assert(v==0xff0000);
    memcpy(&v,memory+3*fi.line_length+12,4); assert(v==0xffffff);
    assert(memory[0]==0xaa && memory[fi.line_length]==0xaa);
    pixel=RETRO_PIXEL_FORMAT_RGB565; video(source16,2,2,4);
    memcpy(&v,memory+fi.line_length+4,4); assert(v==0xff0000);
    vi.bits_per_pixel=16; fi.line_length=16;
    vi.red=(struct fb_bitfield){11,5,0}; vi.green=(struct fb_bitfield){5,6,0}; vi.blue=(struct fb_bitfield){0,5,0};
    video(source16,2,2,4);
    uint16_t v16; memcpy(&v16,memory+18,2); assert(v16==0xf800);
    fb=NULL;
    nkeys=1; pressed[0][KEY_X]=1; pressed[0][KEY_W]=1;
    assert(input(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A)==1);
    assert(input(1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_UP)==1);
    assert(input(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B)==0);
    nkeys=0;
    /* Original mapper-0 fixture: configure pulse channel, then infinite loop. */
    unsigned char rom[16+16384+8192]={ 'N','E','S',0x1a,1,1,2 };
    unsigned char code[]={0x78,0xa9,1,0x8d,0x15,0x40,0xa9,0xbf,0x8d,0,0x40,
        0xa9,0xfd,0x8d,2,0x40,0xa9,8,0x8d,3,0x40,0x4c,0x15,0x80};
    memcpy(rom+16,code,sizeof(code));
    for(int i=0;i<3;i++) { rom[16+0x3ffa+i*2]=0; rom[16+0x3ffb+i*2]=0x80; }
    retro_set_environment(environment); retro_set_video_refresh(test_video);
    retro_set_audio_sample(audio_sample); retro_set_audio_sample_batch(test_audio);
    retro_set_input_poll(poll_input); retro_set_input_state(input); retro_init();
    char rompath[]="/tmp/nes-fixture-XXXXXX.nes";
    /* Core requests full paths; keep the original synthetic ROM on disk. */
    char dirrom[]="/tmp/nes-rom-XXXXXX"; assert(mkdtemp(dirrom));
    snprintf(rompath,sizeof(rompath),"%s/f.nes",dirrom);
    FILE *rf=fopen(rompath,"wb"); assert(rf);
    assert(fwrite(rom,1,sizeof(rom),rf)==sizeof(rom)); assert(!fclose(rf));
    struct retro_game_info game={.path=rompath,.data=rom,.size=sizeof(rom)};
    assert(retro_load_game(&game));
    for(int i=0;i<120;i++) retro_run();
    assert(video_calls==120 && audio_frames>90000 && audio_frames<100000 && audio_nonzero>100);
    char dir[]="/tmp/nes-test-XXXXXX",path[128]; assert(mkdtemp(dir));
    snprintf(path,sizeof(path),"%s/fixture.sav",dir);
    size_t n=retro_get_memory_size(RETRO_MEMORY_SAVE_RAM); assert(n>0);
    void *ram=retro_get_memory_data(RETRO_MEMORY_SAVE_RAM); assert(ram);
    memset(ram,0x5a,n); sram(path,1); assert(!failed);
    memset(ram,0,n); sram(path,0); assert(!failed);
    for(size_t i=0;i<n;i++) assert(((unsigned char *)ram)[i]==0x5a);
    unlink(path); rmdir(dir);
    retro_unload_game(); retro_deinit();
    char *args[]={"nes","--headless","--mute","--frames","120",rompath,NULL};
    assert(nes_program_main(6,args)==0);
    assert(frames==120);
    char mainsave[128]; snprintf(mainsave,sizeof(mainsave),"%s.sav",rompath);
    unlink(mainsave); unlink(rompath); rmdir(dirrom);
    printf("PASS: scaler, key mapping, 120 NES frames, %zu audio frames, SRAM round trip\n",audio_frames);
    return 0;
}
