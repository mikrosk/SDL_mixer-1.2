/*
  native_midi:  Hardware Midi support on Atari for the SDL_mixer library
  Copyright (C) 2025  Miro Kropacek <miro.kropacek@gmail.com>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL_config.h"

#ifdef __MINT__

#include <assert.h>
#include <limits.h>

#include <mint/osbind.h>

#include "native_midi.h"
#include "native_midi_common.h"

struct _NativeMidiSong
{
    MIDIEvent *events;
    MIDIEvent *firstEvent;
    int loops;
    SDL_bool active;

    Uint16 ticksPerQuarterNote; /* 48 by default (independent of the tempo) */

    Uint32 old_timer_b;
    volatile Uint32 timer_b_counter;
    struct
    {
        Uint8 ctrl;
        Uint8 data;
    } timer_b_values[256];
    volatile Uint8 timer_b_current_value;
};
static NativeMidiSong s_nativeMidiSong;

static void __attribute__((interrupt)) timer_b(void)
{
    NativeMidiSong *song = &s_nativeMidiSong;
    MIDIEvent *ev = song->events;

    while (ev && ev->time == song->timer_b_counter)
    {
        if (ev->status == 0xff)
        {
            /* Meta messages (not to be sent over MIDI ports) */
            //printf("%08x: %02x, %02x\n", ev->time, ev->status, ev->data[0]);

            if (ev->data[0] == 0x51)
            {
                song->timer_b_current_value++;
                *(volatile Uint8 *)0xFFFFFA1BL = song->timer_b_values[song->timer_b_current_value].ctrl;
                *(volatile Uint8 *)0xFFFFFA21L = song->timer_b_values[song->timer_b_current_value].data;
            }
        }
        else
        {
#if 0
            static Uint8 buf[3];
            buf[0] = ev->status;
#else
            volatile Uint8 *midi_acia_ctrl = (volatile Uint8 *)0xFFFFFC04L;
            volatile Uint8 *midi_acia_data = (volatile Uint8 *)0xFFFFFC06L;
#endif
            switch(ev->status >> 4)
            {
            case MIDI_STATUS_NOTE_OFF:
            case MIDI_STATUS_NOTE_ON:
            case MIDI_STATUS_AFTERTOUCH:
            case MIDI_STATUS_CONTROLLER:
            case MIDI_STATUS_PITCH_WHEEL:
#if 0
                    buf[1] = ev->data[0];
                    buf[2] = ev->data[1];
                    Midiws(3-1, buf);
#else
                while ((*midi_acia_ctrl & (1 << 1)) == 0);
                *midi_acia_data = ev->status;
                while ((*midi_acia_ctrl & (1 << 1)) == 0);
                *midi_acia_data = ev->data[0];
                while ((*midi_acia_ctrl & (1 << 1)) == 0);
                *midi_acia_data = ev->data[1];
#endif
                break;

            case MIDI_STATUS_PROG_CHANGE:
            case MIDI_STATUS_PRESSURE:
#if 0
                    buf[1] = ev->data[0];
                    Midiws(2-1, buf);
#else
                while ((*midi_acia_ctrl & (1 << 1)) == 0);
                *midi_acia_data = ev->status;
                while ((*midi_acia_ctrl & (1 << 1)) == 0);
                *midi_acia_data = ev->data[0];
#endif
                break;

            default:
                printf("Unknown status: %02x\n", ev->status);
            }
        }

        ev = ev->next;
    }
    song->events = ev;
    song->timer_b_counter++;

    *(volatile Uint8 *)0xFFFFFA0FL = ~(1 << 0);    /* clear in service bit */
}

static void setup_timer(float desired_clock, Uint8 *ctrl, Uint8 *data)
{
    static const Uint32 clock = 2457600;
    static const Uint32 dividers[8] = { -1, 4, 10, 16, 50, 64, 100, 200 };
    int i, j;
    float diff = UINT_MAX;

    printf("Requesting: %.2f Hz\n", desired_clock);

    for (i = 7; i > 0; --i)
    {
        const float prescaled = clock / dividers[i];

        for (j = 1; j < 256; ++j)
        {
            float diff_current = (prescaled / j) - desired_clock;
            /* avoid math.h's abs() */
            diff_current = diff_current >= 0 ? diff_current : -diff_current;
            if (diff_current < diff)
            {
                diff = diff_current;
                *ctrl = i;
                *data = j;
            }
        }
    }

    printf("Got: %.2f Hz\n", (float)clock / dividers[*ctrl] / *data);
}

int native_midi_detect()
{
    return 1;  /* always available */
}

NativeMidiSong *native_midi_loadsong_RW(SDL_RWops *rw, int freerw)
{
    int timer_b_index = 0;

    printf("%s\n", __FUNCTION__);

    s_nativeMidiSong.events = s_nativeMidiSong.firstEvent = CreateMIDIEventList(rw, &s_nativeMidiSong.ticksPerQuarterNote);
    s_nativeMidiSong.timer_b_counter = 0;
    s_nativeMidiSong.timer_b_current_value = 0;

    setup_timer(
        (s_nativeMidiSong.ticksPerQuarterNote * 1000000.0f) / 500000,
        &s_nativeMidiSong.timer_b_values[timer_b_index].ctrl,
        &s_nativeMidiSong.timer_b_values[timer_b_index].data);
    timer_b_index++;

    for (const MIDIEvent *ev = s_nativeMidiSong.firstEvent; ev; ev = ev->next)
    {
        if (ev->status == 0xff && ev->data[0] == 0x51)
        {
            if (timer_b_index == 255)
            {
                printf("Too many tempo changes\n");
                return NULL;
            }

            setup_timer(
                (s_nativeMidiSong.ticksPerQuarterNote * 1000000.0f) / ((ev->extraData[0] << 16) + (ev->extraData[1] << 8) + ev->extraData[2]),
                &s_nativeMidiSong.timer_b_values[timer_b_index].ctrl,
                &s_nativeMidiSong.timer_b_values[timer_b_index].data);
            timer_b_index++;
        }
    }

    if (freerw)
        SDL_RWclose(rw);

    return s_nativeMidiSong.firstEvent ? &s_nativeMidiSong : NULL;
}

void native_midi_freesong(NativeMidiSong *song)
{
    printf("%s\n", __FUNCTION__);

    FreeMIDIEventList(song->firstEvent);
    song->firstEvent = song->events = NULL;
}

void native_midi_start(NativeMidiSong *song, int loops)
{
    printf("%s: %d\n", __FUNCTION__, loops);

    assert(song == &s_nativeMidiSong);

    if (song->active)
        return;

    song->events = s_nativeMidiSong.firstEvent;
    song->timer_b_counter = 0;
    song->timer_b_current_value = 0;

    /* TODO */
    song->loops = loops;

    Jdisint(MFP_TIMERB);
    song->old_timer_b = (Uint32)Setexc(0x120>>2, -1);
    Xbtimer(XB_TIMERB, song->timer_b_values[song->timer_b_current_value].ctrl, song->timer_b_values[song->timer_b_current_value].data, timer_b);

    song->active = 1;
}

void native_midi_stop()
{
    printf("%s\n", __FUNCTION__);

    Jdisint(MFP_TIMERB);
    if (s_nativeMidiSong.old_timer_b)
    {
        (void)Setexc(0x120>>2, s_nativeMidiSong.old_timer_b);
        s_nativeMidiSong.old_timer_b = 0;
    }

    s_nativeMidiSong.active = 0;
}

int native_midi_active()
{
    /* printf("%s (%d/%p)\n", __FUNCTION__, s_nativeMidiSong.active, s_nativeMidiSong.events); */

    return s_nativeMidiSong.active && s_nativeMidiSong.events;
}

void native_midi_setvolume(int volume)
{
#if 0
    int i;
    Uint32 counter;
    /* https://www.recordingblogs.com/wiki/midi-controller-message (channel 0 out of 15) */
    Uint8 controller_message[3] = { 0xB0, 0x07, volume };

    printf("%s: %d\n", __FUNCTION__, volume);

    if (native_midi_active())
    {
        counter = s_nativeMidiSong.timer_b_counter;
        while (counter == s_nativeMidiSong.timer_b_counter);
    }

    for (i = 0; i < 16; ++i)
    {
        Midiws(3-1, controller_message);
        controller_message[0]++;
    }
#endif
}

const char *native_midi_error(void)
{
    return "";
}

#endif  /* __MINT__ */
