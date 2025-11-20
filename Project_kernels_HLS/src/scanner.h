#ifndef SCANNER_H
#define SCANNER_H

#include "krnl_proj.h"  
#include "patterns.h"
#include <ap_int.h>

void perform_scan(
    unsigned char window[WINDOW_SIZE],
    ap_uint<TDWIDTH> &dest_signal
);

#endif // SCANNER_H
