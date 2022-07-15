#ifndef _DITHERPATTERNS_H_INCLUDE
#define _DITHERPATTERNS_H_INCLUDE

#define NUM_ONOFF_SETS 2
#define NUM_LIGHT_SETS 2
#define NUM_CONTRAST_SETS 16
#define NUM_CONTRAST_SIZE 48

extern const uint8_t ditherPatterns[NUM_ONOFF_SETS][NUM_LIGHT_SETS][NUM_CONTRAST_SIZE];

#endif