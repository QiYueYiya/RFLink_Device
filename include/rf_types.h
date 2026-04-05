#pragma once

#include <stdint.h>
#include "config.h"

struct SignalHistory {
    unsigned long code;
    unsigned int bitLength;
    unsigned int pulseLength;
    uint8_t params[kProtocolParamLength];
    bool invertedSignal;
    unsigned int repeatCount;
    uint64_t timestamp;
};

struct PendingSendParams {
    unsigned long code;
    unsigned int bitLength;
    uint16_t pulseLength;
    uint8_t params[kProtocolParamLength];
    bool invertedSignal;
    unsigned int repeatCount;
};
