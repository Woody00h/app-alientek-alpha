// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
int main(int argc,char **argv)
{
    if(argc!=2) {fprintf(stderr,"Usage: %s /dev/input/eventN (Ctrl+C to exit)\n",argv[0]);return 2;}
    int fd=open(argv[1],O_RDONLY);
    if(fd<0) {perror(argv[1]);return 1;}
    char name[128]={0};ioctl(fd,EVIOCGNAME(sizeof(name)),name);
    printf("Device: %s [%s]\n",argv[1],name);setvbuf(stdout,NULL,_IONBF,0);
    struct input_event e;ssize_t n;
    while((n=read(fd,&e,sizeof(e)))==(ssize_t)sizeof(e)) {
        if(e.type==EV_KEY) printf("KEY code=%u (0x%x) value=%d\n",e.code,e.code,e.value);
        if(e.type==EV_ABS) printf("ABS code=%u (0x%x) value=%d\n",e.code,e.code,e.value);
    }
    if(n<0) perror("read input");else fprintf(stderr,"Input disconnected or short read\n");
    close(fd);return 1;
}
