#include "mimi_motion.h"
#include <stddef.h>

typedef struct {
    int16_t body[64][2], eyes[2][48][2], dots[5][3], notch[4];
    uint8_t dot_opa[5], eye_opa[2], arc_opa[6], body_opa, arc_kind, dots_behind;
} mimi_keyframe_t;
typedef struct { uint16_t first, count, duration; } mimi_clip_t;
#include "mimi_motions.inc"
_Static_assert(sizeof motion_clips / sizeof motion_clips[0] == MIMI_MOTION_COUNT, "Every motion must be baked");

static float mix(float a, float b, float t) { return a + (b-a)*t; }
uint32_t mimi_motion_duration(unsigned id) { return id < MIMI_MOTION_COUNT ? motion_clips[id].duration : 0; }
const char *mimi_motion_name(unsigned id) {
    static const char *const names[]={"idle","thinking","wink","wide","alert","notify","exclaim","sleep","egg","hexagon","play","orbit","burst","comet"};
    return id < MIMI_MOTION_COUNT ? names[id] : "invalid";
}
const mimi_arc_seed_t *mimi_motion_arcs(unsigned kind, unsigned *count) {
    *count = kind == 1 ? 6 : kind == 2 || kind == 3 ? 4 : 0;
    return *count ? motion_arcs[kind-1] : NULL;
}
void mimi_motion_sample(unsigned id, uint32_t elapsed_ms, mimi_motion_pose_t *pose) {
    if (id >= MIMI_MOTION_COUNT) id = 0;
    const mimi_clip_t *clip = &motion_clips[id];
    if (elapsed_ms > clip->duration) elapsed_ms = clip->duration;
    unsigned frame = elapsed_ms / 50;
    if (frame >= clip->count-1u) frame = clip->count-1u;
    const mimi_keyframe_t *a = &motion_frames[clip->first+frame];
    const mimi_keyframe_t *b = frame+1u < clip->count ? a+1 : a;
    float t=(elapsed_ms % 50)/50.f;
    for (unsigned i=0;i<64;++i) pose->body[i]=(mimi_point_t){mix(a->body[i][0],b->body[i][0],t)*.001f,mix(a->body[i][1],b->body[i][1],t)*.001f};
    for (unsigned eye=0;eye<2;++eye) {
        pose->eye_opa[eye]=mix(a->eye_opa[eye],b->eye_opa[eye],t)/255;
        for (unsigned i=0;i<48;++i) pose->eyes[eye][i]=(mimi_point_t){mix(a->eyes[eye][i][0],b->eyes[eye][i][0],t)*.001f,mix(a->eyes[eye][i][1],b->eyes[eye][i][1],t)*.001f};
    }
    for (unsigned i=0;i<5;++i) {
        for (unsigned j=0;j<3;++j) pose->dots[i][j]=mix(a->dots[i][j],b->dots[i][j],t)*.001f;
        pose->dot_opa[i]=mix(a->dot_opa[i],b->dot_opa[i],t)/255;
    }
    for (unsigned i=0;i<4;++i) pose->notch[i]=mix(a->notch[i],b->notch[i],t)*.001f;
    for (unsigned i=0;i<6;++i) pose->arc_opa[i]=mix(a->arc_opa[i],b->arc_opa[i],t)/255;
    pose->body_opa=mix(a->body_opa,b->body_opa,t)/255;
    pose->arc_kind=a->arc_kind;
    pose->dots_behind=a->dots_behind;
}
