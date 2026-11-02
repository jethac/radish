#ifndef RADISH_ETC_ORACLE_H
#define RADISH_ETC_ORACLE_H
#include <stdbool.h>
#include <stdint.h>
bool etc_oracle_decode(unsigned format, const uint8_t block[16], float rgba[16][4]);
#endif
