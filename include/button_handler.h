#pragma once

#include <OneButton.h>

// 统一封装按键逻辑，主循环只需要调用 tick。
class ButtonHandler {
public:
    explicit ButtonHandler(uint8_t pin);

    void setup();
    void tick();

private:
    static void onLongPressStopStatic();
    void onLongPressStop();

    OneButton button_;
    static ButtonHandler *instance_;
};
