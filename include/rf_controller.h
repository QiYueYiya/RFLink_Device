#pragma once

#include <RCSwitch.h>

#include "device_runtime.h"

void rfSetTransmitEnable(RCSwitch &rfSwitch, bool enable);
void rfSetReceiveEnable(RCSwitch &rfSwitch, bool enable);
void rfSyncTransmitStateWithBle(DeviceRuntime &runtime, RCSwitch &rfSwitch);
void rfProcessReceivedData(DeviceRuntime &runtime, RCSwitch &rfSwitch);
void rfProcessSendData(DeviceRuntime &runtime, RCSwitch &rfSwitch);
