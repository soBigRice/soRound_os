#pragma once
#include <stdint.h>
enum { MAZE_LEVEL_COUNT=12, MAZE_MAX_CELLS=36 };
typedef struct {uint8_t size,steps;const char *name_zh,*name_en;uint8_t walls[MAZE_MAX_CELLS];} maze_level_t;
extern const maze_level_t maze_levels[MAZE_LEVEL_COUNT];
