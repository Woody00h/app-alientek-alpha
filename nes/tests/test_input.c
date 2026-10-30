/* SPDX-License-Identifier: GPL-2.0-or-later */
#define main nes_program_main
#include "../nes.c"
#undef main
#include <assert.h>
static void event(unsigned type,unsigned code,int value)
{ struct input_event e={.type=type,.code=code,.value=value};handle_event(0,&e); }
int main(void)
{
    nkeys=2;is_pad[0]=is_pad[1]=1;pad_port[0]=0;pad_port[1]=1;
    event(EV_KEY,BTN_SOUTH,1);
    assert(input(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A));
    assert(!input(1,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A));
    event(EV_KEY,BTN_SOUTH,0);event(EV_KEY,BTN_THUMB,1);
    assert(input(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_B));
    event(EV_KEY,BTN_BASE2,1);assert(pad_button(0,RETRO_DEVICE_ID_JOYPAD_START));
    event(EV_KEY,BTN_BASE,1);assert(pad_button(0,RETRO_DEVICE_ID_JOYPAD_SELECT));
    event(EV_KEY,BTN_DPAD_UP,1);assert(pad_button(0,RETRO_DEVICE_ID_JOYPAD_UP));
    event(EV_KEY,BTN_DPAD_UP,0);
    have_axis[0][0]=1;axes[0][0]=(struct input_absinfo){.minimum=0,.maximum=255,.value=127};
    assert(!pad_button(0,RETRO_DEVICE_ID_JOYPAD_LEFT));
    assert(!pad_button(0,RETRO_DEVICE_ID_JOYPAD_RIGHT));
    event(EV_ABS,ABS_X,0);assert(pad_button(0,RETRO_DEVICE_ID_JOYPAD_LEFT));
    event(EV_ABS,ABS_X,255);assert(pad_button(0,RETRO_DEVICE_ID_JOYPAD_RIGHT));
    have_axis[0][3]=1;axes[0][3]=(struct input_absinfo){.minimum=-1,.maximum=1};
    event(EV_ABS,ABS_HAT0Y,-1);assert(pad_button(0,RETRO_DEVICE_ID_JOYPAD_UP));
    event(EV_ABS,ABS_HAT0Y,1);assert(pad_button(0,RETRO_DEVICE_ID_JOYPAD_DOWN));
    event(EV_SYN,SYN_DROPPED,0);event(EV_KEY,BTN_SOUTH,1);
    assert(!pressed[0][BTN_SOUTH]);assert(dropped[0]);
    keys[0]=open("/dev/null",O_RDONLY);assert(keys[0]>=0);
    event(EV_SYN,SYN_REPORT,0);assert(!dropped[0]);
    remove_input(0);assert(keys[0]==-1 && !is_pad[0] && !have_axis[0][0]);
    for(unsigned k=0;k<=KEY_MAX;k++) assert(!pressed[0][k]);
    is_pad[1]=0;pressed[1][KEY_X]=1;
    assert(input(0,RETRO_DEVICE_JOYPAD,0,RETRO_DEVICE_ID_JOYPAD_A));
    puts("PASS: gamepad buttons, player assignment, analog deadzone, hat, SYN_DROPPED, disconnect and keyboard fallback");
    return 0;
}
