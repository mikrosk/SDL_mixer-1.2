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
    /* delta times are stored as ticks */
    MIDIEvent *events;
    int loops;
    SDL_bool active;

    /*
     * Tempo ~ in beats per minute (BPM), 120 by default
     * MIDI quarter note = 1 beat long (i.e. two eighth notes per beat, four sixteenth notes per beat, ...)
     * MIDI half note = 2 beats long
     * MIDI whole note = 4 beats long
     * Time signature = number of beats in a bar / how many quarter notes there are in a beat:
     *      - 4/4 = four quarter-notes per bar (MIDI default)
     *      - 4/2 = four half-notes per bar (or 8 quarter notes)
     *      - 4/8 = four eighth-notes per bar (or 2 quarter notes)
     *      - 2/4 = two quarter-notes per bar
     */
    Uint16 ticksPerQuarterNote; /* 48 by default (independent of the tempo) */
    Uint32 microsecondsPerQuarterNote;  /* 500 000 by default, set by sysex; 24ths of a microsecond per MIDI clock */
    /* microsecondsPerTick (tick time) = microsecondsPerQuarterNote / ticksPerQuarterNote */
    /* elapsed ticks = (ticksPerQuarterNote / microsecondsPerQuarterNote) * 1000 * elapsed_time_in_ms */
    Uint8 timeSignature[2];
};
static NativeMidiSong s_nativeMidiSong;
static volatile MIDIEvent *s_events;
static Uint32 s_old_timer_b;
static Uint16 s_timer_b_ctrl = 0, s_timer_b_data = 1;
static volatile Uint32 s_timer_b_counter;

static void setup_timer_b();

static void __attribute__((interrupt)) timer_b(void)
{
    NativeMidiSong *song = &s_nativeMidiSong;
    volatile MIDIEvent *ev = s_events;

    if (!ev || ev->time > s_timer_b_counter)
        goto timer_b_done;

    while (ev && ev->time == s_timer_b_counter)
    {
        if (ev->status == 0xff)
        {
            printf("%08x: %02x, %02x: ", ev->time, ev->status, ev->data[0]);

            if (ev->data[0] == 0x51)
            {
                song->microsecondsPerQuarterNote = (ev->extraData[0] << 16) + (ev->extraData[1] << 8) + ev->extraData[2];
                setup_timer_b();
                Xbtimer(XB_TIMERB, s_timer_b_ctrl, s_timer_b_data, timer_b);
            }
            else if (ev->data[0] == 0x58)
            {
                song->timeSignature[0] = ev->extraData[0];
                song->timeSignature[1] = 1 << ev->extraData[1];
            }
        }
        else
        {
            static Uint8 buf[3];
            buf[0] = ev->status;
            buf[1] = ev->data[0];
            buf[2] = ev->data[1];
            Midiws(3-1, buf);
        }

        ev = ev->next;
    }
    s_events = ev;

timer_b_done:
    s_timer_b_counter++;
    *(volatile unsigned char *)0xFFFFFA0FL &= ~(1 << 0);    /* clear in service bit */
}

static void setup_timer_b()
{
    static const Uint32 clock = 2457600;
    static const Uint32 dividers[8] = { -1, 4, 10, 16, 50, 64, 100, 200 };
    const float desired_clock = (s_nativeMidiSong.ticksPerQuarterNote * 1000000.0f) / s_nativeMidiSong.microsecondsPerQuarterNote;
    int i, j;
    float diff = UINT_MAX;

    if (s_old_timer_b)
    {
        Jdisint(MFP_TIMERB);
        (void)Setexc(0x120>>2, s_old_timer_b);
        s_old_timer_b = 0;
    }

    printf("Requesting: %.2f Hz\n", desired_clock);

    for (i = 7; i > 0; --i)
    {
        const float prescaled = clock / dividers[i];

        for (j = 1; j < 256; ++j)
        {
            float val = prescaled / j;
            /* avoid math.h's abs() */
            if (val >= desired_clock && val - desired_clock < diff)
            {
                diff = val - desired_clock;
                s_timer_b_ctrl = i;
                s_timer_b_data = j;
            }
            else if (desired_clock > val && desired_clock - val < diff)
            {
                diff = desired_clock - val;
                s_timer_b_ctrl = i;
                s_timer_b_data = j;
            }
        }
    }

    printf("Got: %.2f Hz\n", (float)clock / dividers[s_timer_b_ctrl] / s_timer_b_data);

    s_old_timer_b = (Uint32)Setexc(0x120>>2, -1);
}

int native_midi_detect()
{
    return 1;  /* always available */
}

NativeMidiSong *native_midi_loadsong_RW(SDL_RWops *rw, int freerw)
{
    printf("%s\n", __FUNCTION__);

    s_nativeMidiSong.events = CreateMIDIEventList(rw, &s_nativeMidiSong.ticksPerQuarterNote);
    s_nativeMidiSong.microsecondsPerQuarterNote = 500000UL;
    s_nativeMidiSong.timeSignature[0] = 4;
    s_nativeMidiSong.timeSignature[1] = 4;

    s_events = s_nativeMidiSong.events;

    setup_timer_b();

    if (freerw)
        SDL_RWclose(rw);

    /*
     * fluidsynth: debug: tempo=500000, tick time=1.945525 msec, cur time=0 msec, cur tick=0
     * fluidsynth: debug: tempo=1804806, tick time=7.022591 msec, cur time=0 msec, cur tick=0
     *
     * microsecondsPerQuarterNote: 500000, bpm: 120, tick time: 1945.525269
     * microsecondsPerQuarterNote: 1804806, bpm: 33, tick time: 7022.591309
     */
    printf("ticksPerQuarterNote: %d\n", s_nativeMidiSong.ticksPerQuarterNote);
    printf("microsecondsPerQuarterNote: %u, bpm: %u, tick time: %f\n",
           s_nativeMidiSong.microsecondsPerQuarterNote,
           60000000U / s_nativeMidiSong.microsecondsPerQuarterNote,
           (float)s_nativeMidiSong.microsecondsPerQuarterNote / (float)s_nativeMidiSong.ticksPerQuarterNote);
    printf("time signature: %d/%d\n", s_nativeMidiSong.timeSignature[0], s_nativeMidiSong.timeSignature[1]);

    return s_nativeMidiSong.events ? &s_nativeMidiSong : NULL;
}

void native_midi_freesong(NativeMidiSong *song)
{
    printf("%s\n", __FUNCTION__);

    FreeMIDIEventList(song->events);
    song->events = NULL;
}

void native_midi_start(NativeMidiSong *song, int loops)
{
    printf("%s: %d\n", __FUNCTION__, loops);

    if (!song->events)
        return;

    /* TODO */
    song->loops = loops;

    assert(song == &s_nativeMidiSong);

    Xbtimer(XB_TIMERB, s_timer_b_ctrl, s_timer_b_data, timer_b);
    song->active = 1;
}

void native_midi_stop()
{
    printf("%s\n", __FUNCTION__);

    if (s_old_timer_b)
    {
        Jdisint(MFP_TIMERB);
        (void)Setexc(0x120>>2, s_old_timer_b);
        s_old_timer_b = 0;
    }

    s_timer_b_counter = 0;

    s_nativeMidiSong.active = 0;
}

int native_midi_active()
{
    /*printf("%s\n", __FUNCTION__);*/

    return s_nativeMidiSong.active && s_events;
}

void native_midi_setvolume(int volume)
{
    printf("%s: %d\n", __FUNCTION__, volume);
}

const char *native_midi_error(void)
{
    return "";
}

#endif  /* __MINT__ */
