#ifndef BASE58_H
#define BASE58_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool base58_decode(void *bin, size_t *binszp, const char *b58, size_t b58sz);

#ifdef __cplusplus
}
#endif

#endif // LIBBASE58_H