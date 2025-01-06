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

    song->loops = loops;

    MIDIEvent *ev = song->events;

    while (ev)
    {
        if (ev->status == 0xff)
        {
            int i;
            printf("%08x: %02x, %02x: ", ev->time, ev->status, ev->data[0]);
            for (i = 0; i < ev->extraLen; i++)
            {
                printf("%02x ", ev->extraData[i]);
            }
            printf("\n");

            if (ev->data[0] == 0x51)
            {
                assert(ev->extraLen == 3);
                song->microsecondsPerQuarterNote = (ev->extraData[0] << 16) + (ev->extraData[1] << 8) + ev->extraData[2];
                printf("microsecondsPerQuarterNote: %u, bpm: %u, tick time: %f\n",
                       song->microsecondsPerQuarterNote, 60000000U / song->microsecondsPerQuarterNote, (float)song->microsecondsPerQuarterNote / (float)song->ticksPerQuarterNote);
            }
            else if (ev->data[0] == 0x58)
            {
                assert(ev->extraLen == 2);
                song->timeSignature[0] = ev->extraData[0];
                song->timeSignature[1] = 1 << ev->extraData[1];
                printf("time signature: %d/%d\n", song->timeSignature[0], song->timeSignature[1]);
            }
        }
        else
        {
            printf("%08x: %02x, %02x, %02x\n", ev->time, ev->status, ev->data[0], ev->data[1]);
        }
        ev = ev->next;
    }

    song->active = 1;
}

void native_midi_stop()
{
    printf("%s\n", __FUNCTION__);

    s_nativeMidiSong.active = 0;
}

int native_midi_active()
{
    /*printf("%s\n", __FUNCTION__);*/

    return s_nativeMidiSong.active;
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
