#include <stdint.h>
#include "pal.h"


int pal_lsbit32_index(long l)  __attribute__((weak))
{
        int i;
        if (l)
        {
                for (i=0; i<31; i++)
                {
                        if (l & (1 << i)) { break; }
                }
                return i;
        }
        return -1;
}


int pal_lsbit64_index(long long ll)  __attribute__((weak))
{
        int high_part = (int)((ll >> 32) & 0xffffffffU);
        int low_part = (int)(ll & 0xffffffffU);
        if (low_part)
        {
                return pal_lsbit32_index(low_part);
        }
        else
        {
                return 32 + pal_lsbit32_index((int)((ll >> 32) & 0xffffffffU));
        }
}


int pal_msbit32_index(long l)   __attribute__((weak))
{
        int i;
        if (l)
        {
                for (i=31; i>=0; i--)
                {
                        if (l & (1 << i)) { break; }
                }
                return i;
        }
        return -1;
}


int pal_msbit64_index(long long ll)  __attribute__((weak))
{
        int high_part = (int)((ll >> 32) & 0xffffffffU);
        int low_part = (int)(ll & 0xffffffffU);

        if (high_part)
        {
                return 32 + pal_msbit32_index(high_part);
        }
        else
        {
                return pal_msbit32_index(low_part);
        }
}





