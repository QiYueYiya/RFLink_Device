#pragma once

#include <stdint.h>
#include "config.h"

// 依据 RCSwitch 协议号返回默认参数组。
void getCustomParamsFromProtocol(unsigned int protocolId, uint8_t params[kProtocolParamLength], bool &invertedSignal);
