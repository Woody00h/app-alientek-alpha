/* SPDX-License-Identifier: GPL-2.0-or-later */
#define main nes_program_main
#include "../nes.c"
#undef main
#include <assert.h>
static void reference(unsigned char *out, const void *src, unsigned w,unsigned h,size_t pitch,unsigned scale)
{
    unsigned bytes=vi.bits_per_pixel/8,x0=(vi.xres-w*scale)/2,y0=(vi.yres-h*scale)/2;
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<w;x++) {
        const unsigned char *p=(const unsigned char *)src+y*pitch;
        uint32_t r,g,b,v;
        if(pixel==RETRO_PIXEL_FORMAT_XRGB8888) {
            memcpy(&v,p+x*4,4); r=(v>>16)&255; g=(v>>8)&255; b=v&255;
        } else {
            uint16_t q; memcpy(&q,p+x*2,2); b=(q&31)*255/31;
            if(pixel==RETRO_PIXEL_FORMAT_RGB565) {r=(q>>11)*255/31;g=((q>>5)&63)*255/63;}
            else {r=((q>>10)&31)*255/31;g=((q>>5)&31)*255/31;}
        }
        v=component(r,vi.red)|component(g,vi.green)|component(b,vi.blue);
        for(unsigned dy=0;dy<scale;dy++) for(unsigned dx=0;dx<scale;dx++)
            memcpy(out+(y0+y*scale+dy+vi.yoffset)*fi.line_length+(x0+x*scale+dx+vi.xoffset)*bytes,&v,bytes);
    }
}
int main(void)
{
    stretch=0;
    for(unsigned fmt=0;fmt<3;fmt++) for(unsigned bytes=2;bytes<=4;bytes++) for(unsigned scale=1;scale<=3;scale++) {
        pixel=(enum retro_pixel_format)fmt;
        unsigned srcbytes=pixel==RETRO_PIXEL_FORMAT_XRGB8888?4:2;
        size_t pitch=256*srcbytes+16;
        unsigned char *src=calloc(240,pitch); assert(src);
        for(unsigned y=0;y<240;y++) for(unsigned x=0;x<256;x++) {
            uint32_t val=(x*713+y*23117)&(srcbytes==4?0xffffff:0xffff);
            memcpy(src+y*pitch+x*srcbytes,&val,srcbytes);
        }
        vi.xres=256*scale+10;vi.yres=240*scale+10;vi.xoffset=2;vi.yoffset=3;
        vi.bits_per_pixel=bytes*8;
        vi.red=(struct fb_bitfield){bytes==2?11:16,bytes==2?5:8,0};
        vi.green=(struct fb_bitfield){bytes==2?5:8,bytes==2?6:8,0};
        vi.blue=(struct fb_bitfield){0,bytes==2?5:8,0};
        fi.line_length=(vi.xres+8)*bytes+16;
        size_t size=(vi.yres+8)*fi.line_length;
        unsigned char *expected=malloc(size);fb=malloc(size);assert(fb && expected);
        memset(fb,0xa5,size);memset(expected,0xa5,size);
        reference(expected,src,256,240,pitch,scale);
        video(src,256,240,pitch);assert(!memcmp(fb,expected,size));
        unsigned long drawn=rendered_frames,skipped=skipped_frames;
        frameskip=1;frames=1;video(src,256,240,pitch);
        assert(rendered_frames==drawn && skipped_frames==skipped+1);
        frameskip=-1;frames=0;
        free(src);free(fb);free(expected);
    }
    /* Full LCD coverage including fractional vertical stretch and downscaling. */
    stretch=1;
    for(unsigned bytes=2;bytes<=4;bytes++) for(unsigned small=0;small<2;small++) {
        unsigned sw=256,sh=240;
        vi.xres=small?127:1024;vi.yres=small?119:600;
        vi.xoffset=3;vi.yoffset=2;vi.bits_per_pixel=bytes*8;
        vi.red=(struct fb_bitfield){bytes==2?11:16,bytes==2?5:8,0};
        vi.green=(struct fb_bitfield){bytes==2?5:8,bytes==2?6:8,0};
        vi.blue=(struct fb_bitfield){0,bytes==2?5:8,0};
        pixel=RETRO_PIXEL_FORMAT_XRGB8888;
        uint32_t *source=malloc(sw*sh*4);assert(source);
        for(unsigned y=0;y<sh;y++) for(unsigned x=0;x<sw;x++) source[y*sw+x]=(x<<16)|(y<<8)|(x^y);
        fi.line_length=(vi.xres+8)*bytes+12;
        size_t size=(vi.yres+5)*fi.line_length;
        fb=malloc(size);unsigned char *expected=malloc(size);assert(fb && expected);
        memset(fb,0xa5,size);memset(expected,0xa5,size);
        for(unsigned dy=0;dy<vi.yres;dy++) for(unsigned dx=0;dx<vi.xres;dx++) {
            unsigned sx=(unsigned)((uint64_t)dx*sw/vi.xres),sy=(unsigned)((uint64_t)dy*sh/vi.yres);
            uint32_t q=source[sy*sw+sx];
            uint32_t v=component((q>>16)&255,vi.red)|component((q>>8)&255,vi.green)|component(q&255,vi.blue);
            memcpy(expected+(dy+vi.yoffset)*fi.line_length+(dx+vi.xoffset)*bytes,&v,bytes);
        }
        video(source,sw,sh,sw*4);assert(!memcmp(fb,expected,size));
        free(source);free(fb);free(expected);fb=NULL;
    }
    /* Auto mode skips late video work while still periodically showing a frame. */
    unsigned char src[4]={0};fb=src;audio_fd=123;run_started=1;
    clock_gettime(CLOCK_MONOTONIC,&run_start);run_start.tv_sec--;
    consecutive_skips=0;
    unsigned long skipped=skipped_frames;
    video(src,1,1,4);assert(skipped_frames==skipped+1);
    audio_fd=-1;run_started=0;fb=NULL;
    free(scaled_row);free(xends);
    puts("PASS: 27 integer scale combinations, 6 stretch/downscale cases, padding/offset guards and skipping");
    return 0;
}
