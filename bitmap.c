#include "bitmap.h"
#include <stdint.h>


int
bitmap_get(void* bm, int ii)
{
    return (int) *((char*) bm + ii); 
}

void
bitmap_put(void* bm, int ii, int vv)
{
    *((uint8_t*) bm + ii) = (uint8_t) vv;    
}
