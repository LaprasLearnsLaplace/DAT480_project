#ifndef SCANNER_H
#define SCANNER_H

#include "krnl_proj.h"
#include "patterns.h"
#include <ap_int.h>

void dcam_step(
    unsigned char in_byte,
    bool reset,
    ap_uint<TDWIDTH> &dest_signal
);

#endif // SCANNER_H
