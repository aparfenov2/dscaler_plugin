#ifndef _dscaler_h
#define _dscaler_h

#include <Windows.h>
#include "DS_Filter.h"

typedef struct {
	int magic;
	TDeinterlaceInfo info;
    //int rate_numerator;
    //int rate_denominator;
	int magic2;
} frame_header_t;

typedef struct {
	frame_header_t *hdr;
	void * data;
} frame_t;

#endif