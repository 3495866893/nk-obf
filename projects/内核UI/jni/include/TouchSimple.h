#pragma once
#include <cstdint>

bool Touch_Init(int w, int h, uint32_t orientation, bool readOnly);
void Touch_PumpAll();
void Touch_Close();
void UpdateScreenData(int w, int h, uint32_t orientation);
