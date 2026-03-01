#pragma once
#include <stdbool.h>
#include <assert.h>
#define ASSERT(arg)                               \
    if (!(arg)) {                                 \
        ASSError(__FILE__, __LINE__, #arg);       \
        assert(false);                            \
    }
#define ASSERT_MSG(arg, str, ...)                             \
    if (!(arg)) {                                             \
        ASSError(__FILE__, __LINE__, str, __VA_ARGS__);       \
        assert(false);                                        \
    }
#define VERIFY(arg)                                \
    if (!(arg)) {                                  \
        ASSError(__FILE__, __LINE__, #arg);        \
    }
void ASSError(const char* file, unsigned int line, const char* assertStr, ...);