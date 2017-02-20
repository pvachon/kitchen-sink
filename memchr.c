#include <string.h>
#include <c_types.h>

ICACHE_FLASH_ATTR
void *memchr(const void *s, int c, size_t n)
{
    if (n != 0) {
        const unsigned char *p = s;

        do {
            if (*p++ == c)
                return ((void *)(p - 1));
        } while (--n != 0);
    }

    return (NULL);
}
