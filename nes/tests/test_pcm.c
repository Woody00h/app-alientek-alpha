/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdarg.h>
#include <sys/ioctl.h>
static int mock_ioctl(int fd, unsigned long cmd, ...);
#define ioctl mock_ioctl
#define main nes_program_main
#include "../nes.c"
#undef main
#undef ioctl
static int calls, prepares;
static const int16_t *original;
static int mock_ioctl(int fd, unsigned long cmd, ...)
{
    (void)fd;
    if(cmd==SNDRV_PCM_IOCTL_PREPARE) { prepares++; return 0; }
    assert(cmd==SNDRV_PCM_IOCTL_WRITEI_FRAMES);
    va_list ap; va_start(ap,cmd); struct snd_xferi *x=va_arg(ap,struct snd_xferi *); va_end(ap);
    calls++;
    if(calls==1) { assert(x->buf==original && x->frames==4); errno=EINTR; return -1; }
    if(calls==2) { x->result=2; return 0; }
    assert(x->buf==original+4 && x->frames==2);
    if(calls==3) { x->result=-EPIPE; return 0; }
    x->result=2; return 0;
}
int main(void)
{
    int16_t data[]={-32768,32767,100,-200,0,0,-300,400};
    original=data; audio_fd=123;
    assert(audio_batch(data,4)==4);
    assert(calls==4 && prepares==1 && audio_underruns==1);
    assert(audio_frames_written==4 && audio_peak==32768 && audio_clipped==2);
    assert(!failed && !stopped);
    puts("PASS: PCM partial writes, EINTR, underrun recovery and sample statistics");
    return 0;
}
