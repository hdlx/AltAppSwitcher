#pragma once
#include <stdbool.h>
#include <assert.h>
#define ASSERT(arg)                         \
    if (!(arg)) {                           \
        ASSError(__FILE__, __LINE__, #arg); \
        assert(false);                      \
    }
#define ASSERT_MSG(arg, str, ...)                       \
    if (!(arg)) {                                       \
        ASSError(__FILE__, __LINE__, str, __VA_ARGS__); \
        assert(false);                                  \
    }
#define VERIFY(arg)                         \
    if (!(arg)) {                           \
        ASSError(__FILE__, __LINE__, #arg); \
    }
#define VERIFY_MSG(arg, str, ...)                       \
    if (!(arg)) {                                       \
        ASSError(__FILE__, __LINE__, str, __VA_ARGS__); \
    }

#if 0 
#define AAS_MSG(str, ...) AASMsg(__FILE__, __LINE__, str, ##__VA_ARGS__)
#else
#define AAS_MSG(str, ...)
#endif
void ASSError(const char* file, unsigned int line, const char* assertStr, ...);
void AASMsg(const char* file, unsigned int line, const char* msg, ...);