#include <stddef.h>

/* MinGW (windows-gnu) clang emits a call to __main at the top of main() to run
   C++ global constructors. A freestanding C test has none, so a no-op stub
   satisfies the linker. MSVC-target clang never references it. */
void __main(void)
{
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *bytes = (unsigned char *)destination;

    for (size_t index = 0U; index < count; ++index) {
        bytes[index] = (unsigned char)value;
    }
    return destination;
}

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *destination_bytes = (unsigned char *)destination;
    const unsigned char *source_bytes = (const unsigned char *)source;

    for (size_t index = 0U; index < count; ++index) {
        destination_bytes[index] = source_bytes[index];
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *left_bytes = (const unsigned char *)left;
    const unsigned char *right_bytes = (const unsigned char *)right;
    for (size_t index = 0U; index < count; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return left_bytes[index] < right_bytes[index] ? -1 : 1;
        }
    }
    return 0;
}
