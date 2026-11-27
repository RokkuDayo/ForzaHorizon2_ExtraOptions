#pragma once
#include <cstdint>
namespace fm6live {
void initialize(uint8_t *base,const wchar_t *ini);
void refresh();
bool enabled();
int intensity();
int fov_half(const char *key);
}
