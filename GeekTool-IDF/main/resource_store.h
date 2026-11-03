#pragma once
#include <stdbool.h>

// 共用已有storage只读FAT卷；首次挂载串行，失败后后续调用可重试，锁最多等2秒。
// Share the existing read-only storage FAT volume; serialize first mount, retry failures, wait at most2s for its lock.
bool resource_store_mount(void);
