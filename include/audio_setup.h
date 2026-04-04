#ifndef AUDIO_SETUP_H
#define AUDIO_SETUP_H

/*
 * audio_setup.h — SDL3 audio device enumeration / diagnostic helpers.
 *
 * Moved out of synth.c so the synth logic stays clean.
 * Call audio_setup_print() once at startup for diagnostics.
 */

void audio_setup_print(void);

#endif /* AUDIO_SETUP_H */
