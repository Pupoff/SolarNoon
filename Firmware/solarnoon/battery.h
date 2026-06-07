#pragma once

void  initBattery();
void  updateBatteryLeds();      // call every loop() — reads slowly, blinks fast
void  checkBatteryShutdown();   // call every loop() — deep-sleeps if < 3.1 V for 5 s
float getBatteryVoltage();      // cached, V
int   getBatteryPercent();   // cached, 0-100
