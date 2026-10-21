#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum {UI_STORE_SETTINGS,UI_STORE_GARDEN,UI_STORE_FOCUS,UI_STORE_COUNT} ui_store_key_t;
typedef bool (*ui_store_writer_t)(const void *snapshot);

// 仅UI线程提交不可变副本；true只表示该精确副本已成功落盘，不表示仅入队。
// The UI submits immutable copies; true means this exact snapshot is durable, not merely queued.
bool ui_persistence_save(ui_store_key_t key,const void *snapshot,size_t size,ui_store_writer_t writer);
bool ui_persistence_saved(ui_store_key_t key,const void *snapshot,size_t size);
bool ui_persistence_pending(ui_store_key_t key,void *snapshot,size_t size);
// 退出屏障有界等待；失败保留副本供后续重试，绝不伪报保存完成。
// The bounded exit barrier retains failed snapshots for retry and never reports a failed write as saved.
bool ui_persistence_flush(unsigned timeout_ms);
