// 麦克风实时频谱:worker 分析 18 段;UI 快照驱动固定 18×16 点阵和能量配色。
#include "app.h"
#include "tools_ui.h"
#include "ui_update.h"
#include "settings.h"
#include "audio_mic.h"
#include "audio_bus.h"
#include "board_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>
#include <string.h>

#define NB     18          // 频段数
#define WIN    480         // 每帧样本数(16kHz → ~33fps)
#define SR     16000.0f

#define ROWS 16
#define GRID_X 81
#define GRID_Y 147
#define GRID_W 304
#define GRID_H 163
typedef enum { CAPTURE_STARTING, CAPTURE_READY, CAPTURE_FAILED } capture_state_t;

static float              s_band[NB];          // 0..1 平滑后的频段能量
static volatile bool      s_run;
static TaskHandle_t       s_worker;
static portMUX_TYPE       s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool      s_visible = true;
static uint32_t           s_generation;
static capture_state_t    s_capture;
static lv_obj_t          *g_content, *g_grid, *g_caption, *g_input, *g_fault;
static uint32_t           s_colors[NB][ROWS]; // UI-owned, worker never accesses LVGL.

static void capture_publish(uint32_t generation, capture_state_t state, const float *bands) {
    portENTER_CRITICAL(&s_mux);
    // A read can finish after exit/re-entry; never publish an earlier activation's frame.
    if (s_run && s_generation == generation) {
        s_capture = state;
        if (bands) memcpy(s_band, bands, sizeof s_band);
    }
    portEXIT_CRITICAL(&s_mux);
}

static void audio_task(void *arg) {
    (void)arg;
    bool owns_bus = false;
    uint32_t failed_generation = UINT32_MAX;
    static int16_t buf[WIN];
    float coeff[NB];
    for (int b = 0; b < NB; b++) {                          // 频率 80Hz..6kHz 对数分布
        float f = 80.0f * powf(6000.0f / 80.0f, (float)b / (NB - 1));
        coeff[b] = 2.0f * cosf(2.0f * (float)M_PI * f / SR);
    }
    int logdiv = 0;
    unsigned missed = 0;
    for (;;) {
        portENTER_CRITICAL(&s_mux);
        bool run = s_run;
        uint32_t generation = s_generation;
        portEXIT_CRITICAL(&s_mux);
        if (!run) {
            if (owns_bus) { audio_mic_stop(); audio_bus_release(); owns_bus = false; }
            portENTER_CRITICAL(&s_mux);
            bool stop = !s_run;
            if (stop) s_worker = NULL;
            portEXIT_CRITICAL(&s_mux);
            if (stop) { vTaskDelete(NULL); return; }
            continue;
        }
        if (!owns_bus) {
            if (failed_generation == generation) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); continue; }
            if (!audio_bus_acquire(pdMS_TO_TICKS(50))) continue;
            if (!audio_mic_start(board_i2c_bus())) {
                ESP_LOGW("audio", "mic start failed");
                audio_mic_stop(); audio_bus_release(); failed_generation = generation;
                capture_publish(generation, CAPTURE_FAILED, NULL);
                continue;
            }
            owns_bus = true;
        }
        if (!s_visible) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(80)); continue; }
        int n = audio_mic_read(buf, WIN);
        if (n <= 0) {
            if (++missed >= 3) capture_publish(generation, CAPTURE_FAILED, NULL);
            vTaskDelay(pdMS_TO_TICKS(10)); continue;
        }
        missed = 0;

        float rms = 0;
        for (int i = 0; i < n; i++) rms += (float)buf[i] * buf[i];
        rms = sqrtf(rms / n);

        float bands[NB];
        portENTER_CRITICAL(&s_mux); memcpy(bands, s_band, sizeof bands); portEXIT_CRITICAL(&s_mux);
        for (int b = 0; b < NB; b++) {                      // Goertzel:每段一个频点的幅度
            float s0, s1 = 0, s2 = 0;
            for (int i = 0; i < n; i++) { s0 = buf[i] / 32768.0f + coeff[b] * s1 - s2; s2 = s1; s1 = s0; }
            float mag = sqrtf(s1 * s1 + s2 * s2 - coeff[b] * s1 * s2) / (n * 0.5f);
            float v = mag * 11.0f; if (v > 1.0f) v = 1.0f;     // 放大灵敏度,起伏更明显
            float prev = bands[b];
            bands[b] = v > prev ? v : prev * 0.80f + v * 0.20f;  // 快上慢下 → 起伏感
        }
        capture_publish(generation, CAPTURE_READY, bands);
        if (++logdiv >= 15) { logdiv = 0; ESP_LOGI("audio", "rms=%.0f", rms); }  // 拾音验证
    }
}

static void audio_notify(void) {
    portENTER_CRITICAL(&s_mux);
    if (s_worker) xTaskNotifyGive(s_worker);
    portEXIT_CRITICAL(&s_mux);
}

static uint32_t mix_color(uint32_t a, uint32_t b, float t) {
    uint32_t result = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        float channel = ((a >> shift) & 255) * (1-t) + ((b >> shift) & 255) * t;
        result |= (uint32_t)(channel + .5f) << shift;
    }
    return result;
}

static uint32_t spectrum_color(float energy, bool quiet) {
    if (quiet) return TOOLS_WHITE;
    static const float stops[] = {0,.30f,.67f,1};
    static const uint32_t colors[] = {TOOLS_WHITE,0xb5ddd9,0xe6c08b,0xdc7481};
    energy = fminf(1, fmaxf(0,energy));
    for (int i=1; i<4; ++i) if (energy <= stops[i])
        return mix_color(TOOLS_WHITE, mix_color(colors[i-1], colors[i],
                         (energy-stops[i-1])/(stops[i]-stops[i-1])), .85f);
    return colors[3];
}

static void spectrum_draw(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a; lv_obj_get_coords(g_grid, &a);
    for (int b=0; b<NB; ++b) for (int row=0; row<ROWS; ++row)
        tools_dot(layer, a.x1 + 8 + b*17, a.y1 + 156-row*10, 6, s_colors[b][row]);
}

static void scale_draw(lv_event_t *e) {
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a; lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    tools_line(layer, a.x1, a.y1, a.x1+290, a.y1, 1, TOOLS_LINE);
    const int ticks[] = {0,73,145,217,290};
    for (int i=0; i<5; ++i) tools_line(layer, a.x1+ticks[i],a.y1,a.x1+ticks[i],a.y1+5,1,TOOLS_GRAY);
}

static void audio_tick(void);

static void audio_enter(lv_obj_t *parent) {
    g_content = tools_surface(parent,0,0,466,466);
    lv_obj_t *indicator = tools_surface(g_content,161,107,8,8);
    lv_obj_set_style_radius(indicator,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(indicator,lv_color_hex(COL_RED),0);
    lv_obj_set_style_bg_opa(indicator,LV_OPA_COVER,0);
    g_input = tools_label(g_content,tools_text("STARTING","正在启动"),&font_tools_20,237,111,TOOLS_GRAY,settings_lang()?1:2);
    g_grid = tools_surface(g_content,GRID_X,GRID_Y,GRID_W,GRID_H);
    lv_obj_add_event_cb(g_grid,spectrum_draw,LV_EVENT_DRAW_MAIN,NULL);
    for (int b=0; b<NB; ++b) for (int row=0; row<ROWS; ++row) s_colors[b][row]=TOOLS_FAINT;
    lv_obj_t *scale = tools_surface(g_content,88,326,291,7);
    lv_obj_add_event_cb(scale,scale_draw,LV_EVENT_DRAW_MAIN,NULL);
    lv_obj_t *low = tools_label(g_content,"80 Hz",&font_tools_19,87,353,TOOLS_GRAY,0);
    lv_obj_set_x(low,87);
    lv_obj_t *high = tools_label(g_content,"6 kHz",&font_tools_19,379,353,TOOLS_GRAY,0);
    lv_obj_set_x(high,379-lv_obj_get_width(high));
    g_caption = tools_label(g_content,tools_text("LISTENING","聆听中"),&font_tools_21,233,395,TOOLS_WHITE,settings_lang()?1:2);
    tools_label(g_content,tools_text("18 BANDS","18 个频段"),&font_tools_20,233,423,TOOLS_GRAY,settings_lang()?0:1);
    g_fault = tools_fault(parent,true);
    // 采集任务拥有硬件,结束后释放任务栈;进退页面只发请求,不等待阻塞的 I2S read。
    portENTER_CRITICAL(&s_mux);
    memset(s_band, 0, sizeof s_band); s_capture=CAPTURE_STARTING; s_run = true; s_generation++;
    portEXIT_CRITICAL(&s_mux);
    s_visible = true;
    if (!s_worker && xTaskCreate(audio_task, "audio", 4096, NULL, 5, &s_worker) != pdPASS) {
        portENTER_CRITICAL(&s_mux); s_run = false; s_capture=CAPTURE_FAILED; portEXIT_CRITICAL(&s_mux);
        ESP_LOGE("audio", "capture task allocation failed");
    }
    audio_notify();
    audio_tick();
}

static void audio_tick(void) {
    if (!g_grid) return;
    float bands[NB];
    capture_state_t state;
    portENTER_CRITICAL(&s_mux); memcpy(bands, s_band, sizeof bands); state=s_capture; portEXIT_CRITICAL(&s_mux);
    if (state==CAPTURE_FAILED) {
        ui_obj_set_hidden(g_content,true); ui_obj_set_hidden(g_fault,false); return;
    }
    ui_obj_set_hidden(g_content,false); ui_obj_set_hidden(g_fault,true);
    const char *input=state==CAPTURE_READY ? tools_text("MIC INPUT","实时拾音") : tools_text("STARTING","正在启动");
    if (strcmp(lv_label_get_text(g_input),input)) {ui_text(g_input,input);tools_label_center(g_input,237,111);}
    float max=0; for (int b=0; b<NB; ++b) max=fmaxf(max,bands[b]);
    // All columns stay at one dot below the first two-dot step; use a calm white listening state.
    bool quiet=max<1.5f/ROWS;
    const char *caption=quiet ? tools_text("LISTENING","聆听中") : tools_text("LIVE SPECTRUM","实时频谱");
    if (strcmp(lv_label_get_text(g_caption),caption)) {
        ui_text(g_caption,caption); tools_label_center(g_caption,233,395);
    }
    lv_area_t coords; lv_obj_get_coords(g_grid,&coords);
    for (int b=0; b<NB; ++b) {
        float v=isfinite(bands[b]) ? fminf(1,fmaxf(0,bands[b])) : 0;
        int count=(int)roundf(v*ROWS); if (count<1) count=1;
        int first=ROWS, last=-1;
        for (int row=0; row<ROWS; ++row) {
            uint32_t color=row>=count ? TOOLS_FAINT : (!quiet && row==count-1 ? COL_RED :
                spectrum_color((row+1)/(float)ROWS*.72f+v*.28f,quiet));
            if (color!=s_colors[b][row]) {s_colors[b][row]=color; if (row<first) first=row; last=row;}
        }
        if (last>=first) {
            int x=coords.x1+8+b*17;
            lv_area_t dirty={x-3,coords.y1+156-last*10-3,x+2,coords.y1+156-first*10+2};
            lv_obj_invalidate_area(g_grid,&dirty);
        }
    }
}

static void audio_exit(void) {
    portENTER_CRITICAL(&s_mux); s_run = false; s_generation++; portEXIT_CRITICAL(&s_mux);
    audio_notify();
    g_content=g_grid=g_caption=g_input=g_fault=NULL;
}

static void audio_visibility(bool visible) {
    s_visible = visible;
    audio_notify();
}

const app_t app_audio = { "audio", COL_TXT, audio_enter, audio_tick, audio_exit, NULL, 20, audio_visibility };
