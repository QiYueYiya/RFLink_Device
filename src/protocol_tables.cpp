#include "protocol_tables.h"

#include <string.h>

namespace {
struct ProtocolPreset {
    uint8_t params[kProtocolParamLength];
    bool inverted;
};

constexpr ProtocolPreset kProtocolPresets[] = {
    {{1, 31, 1, 3, 3, 1}, false},
    {{1, 10, 1, 2, 2, 1}, false},
    {{30, 71, 4, 11, 9, 6}, false},
    {{1, 6, 1, 3, 3, 1}, false},
    {{6, 14, 1, 2, 2, 1}, false},
    {{23, 1, 1, 2, 2, 1}, true},
    {{2, 62, 1, 6, 6, 1}, false},
};
} // namespace

void getCustomParamsFromProtocol(unsigned int protocolId, uint8_t params[kProtocolParamLength], bool &invertedSignal) {
    unsigned int index = 0;
    if (protocolId >= 1 && protocolId <= 7) {
        index = protocolId - 1;
    }

    memcpy(params, kProtocolPresets[index].params, kProtocolParamLength);
    invertedSignal = kProtocolPresets[index].inverted;
}
