#ifndef FONT8X16_H
#define FONT8X16_H

#include <stdint.h>

// Standard 8x16 bitmap font for ASCII characters 32 to 127
// Each character is 16 rows of 8 bits. MSB is the leftmost pixel.
extern const uint8_t font8x16_data[96][16];

#endif // FONT8X16_H
