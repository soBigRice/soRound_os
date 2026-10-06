#pragma once
#include <stddef.h>
#include <stdint.h>
#define MERIT_SOUND_RATE 16000
#define MERIT_SOUND_SAMPLES (MERIT_SOUND_RATE * 180 / 1000)
// Deterministic struck-wood model, shared by device playback and the review WAV.
void merit_sound_fill(int16_t *out,size_t offset,size_t count);
