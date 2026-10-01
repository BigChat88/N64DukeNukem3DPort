// converted from asm to c by Jonof

#include <stdio.h>
#include "platform.h"
#include "fixedPoint_math.h"

void clearbuf(void *d, int32_t c, int32_t a)
{
	int32_t *p = (int32_t*)d;
	while ((c--) > 0) *(p++) = a;
}

void clearbufbyte(void *D, int32_t c, int32_t a)
{
	// Fills c bytes with the 32-bit pattern a, repeated as it lies in memory
	// (like the original "rep stosb" of the int). Extracting the bytes with
	// shifts instead assumed a little endian CPU: on the N64 the short arrays
	// filled this way (e.g. mostbuf = ydimen+(ydimen<<16)) got byte-swapped
	// values, and wall segments were clipped against garbage.
	uint8_t  *p = (uint8_t *)D;
	uint8_t pattern[4];
	int32_t z=0;
	memcpy(pattern, &a, sizeof(pattern));
	while ((c--) > 0) {
		*(p++) = pattern[z];
		z=(z+1)&3;
	}
}

void copybuf(void *s, void *d, int32_t c)
{
	int32_t *p = (int32_t*)s, *q = (int32_t*)d;
	while ((c--) > 0) *(q++) = *(p++);
}

void copybufbyte(void *S, void *D, int32_t c)
{
	uint8_t  *p = (uint8_t *)S, *q = (uint8_t *)D;
	while((c--) > 0) *(q++) = *(p++);
}

void copybufreverse(void *S, void *D, int32_t c)
{
	uint8_t  *p = (uint8_t *)S, *q = (uint8_t *)D;
	while((c--) > 0) *(q++) = *(p--);
}

void qinterpolatedown16(int32_t* bufptr, int32_t num, int32_t val, int32_t add)
{ // gee, I wonder who could have provided this...
    int32_t i, *lptr = bufptr;
    for(i=0;i<num;i++) { lptr[i] = (val>>16); val += add; }
}

void qinterpolatedown16short(int32_t* bufptr, int32_t num, int32_t val, int32_t add)
{ // ...maybe the same person who provided this too?
    int32_t i; short *sptr = (short *)bufptr;
    for(i=0;i<num;i++) { sptr[i] = (short)(val>>16); val += add; }
}

