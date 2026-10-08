#include "display_frame_gate.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    display_frame_gate_t gate={0};
    assert(!display_frame_gate_done(&gate,0));
    display_frame_gate_submit(&gate,true); // Cover DMA is already in flight.
    uint32_t home=display_frame_gate_request(&gate);
    display_frame_gate_complete(&gate);
    assert(!display_frame_gate_done(&gate,home));
    display_frame_gate_submit(&gate,false);display_frame_gate_complete(&gate);
    assert(!display_frame_gate_done(&gate,home)); // An intermediate tile is insufficient.
    display_frame_gate_submit(&gate,true);
    assert(!display_frame_gate_done(&gate,home)); // Submitted is not completed.
    display_frame_gate_complete(&gate);assert(display_frame_gate_done(&gate,home));
    display_frame_gate_submit(&gate,true);
    uint32_t next=display_frame_gate_request(&gate);
    display_frame_gate_complete(&gate);assert(!display_frame_gate_done(&gate,next));
    display_frame_gate_submit(&gate,true);display_frame_gate_complete(&gate);
    assert(display_frame_gate_done(&gate,next));
    puts("Old cover DMA, partial tiles and queued final tile cannot confirm home; only its final DMA can");
}
