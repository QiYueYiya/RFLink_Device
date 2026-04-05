#pragma once

#include <Arduino.h>

#include "device_runtime.h"

void bleSendNotify(DeviceRuntime &runtime, const char *type, const char *data);
void bleSendNotify(DeviceRuntime &runtime, const String &type, const String &data);
void bleSetup(DeviceRuntime &runtime);
void bleProcessConnectionState(DeviceRuntime &runtime);
void bleProcessPendingTimeSync(DeviceRuntime &runtime);
void bleProcessHistoryQuery(DeviceRuntime &runtime);
