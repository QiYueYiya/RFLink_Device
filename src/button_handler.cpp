#include "button_handler.h"

#include <Arduino.h>

ButtonHandler *ButtonHandler::instance_ = nullptr;

ButtonHandler::ButtonHandler(uint8_t pin)
    : button_(pin, true, true) {
    instance_ = this;
}

void ButtonHandler::setup() {
    button_.setPressMs(5000);
    button_.attachLongPressStop(onLongPressStopStatic);
}

void ButtonHandler::tick() {
    button_.tick();
}

void ButtonHandler::onLongPressStopStatic() {
    if (instance_ != nullptr) {
        instance_->onLongPressStop();
    }
}

void ButtonHandler::onLongPressStop() {
    Serial.println("按键长按5秒，重启设备");
    delay(50);
    ESP.restart();
}
