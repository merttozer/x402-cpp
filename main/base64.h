#ifndef BASE64_H
#define BASE64_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

char *base64_encode(const unsigned char *data, size_t input_length);

#ifdef __cplusplus
}
#endif

#endif // BASE64_H