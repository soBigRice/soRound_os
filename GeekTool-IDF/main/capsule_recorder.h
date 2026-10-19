#pragma once
#include "capsule_store.h"
typedef enum { CAPSULE_LOADING, CAPSULE_READY, CAPSULE_WAITING, CAPSULE_RECORDING,
               CAPSULE_SAVING, CAPSULE_PLAYING, CAPSULE_ERROR } capsule_state_t;
typedef struct {
    capsule_state_t state;capsule_result_t result;capsule_catalog_t catalog;
    uint32_t samples;uint8_t waveform[32];bool mic_failed,cancelled,saved,muted;
} capsule_snapshot_t;
void capsule_recorder_enter(void);
void capsule_recorder_exit(void);
bool capsule_recorder_record(void);
bool capsule_recorder_stop(void);
bool capsule_recorder_play(const char *name);
bool capsule_recorder_delete(const char *name);
void capsule_recorder_snapshot(capsule_snapshot_t *snapshot);
