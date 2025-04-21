#pragma once
#include <stdbool.h>
#define ASSERT(arg) if (!(arg)) { ASSError(__FILE__, __LINE__, #arg, true); }
#define VERIFY(arg) if (!(arg)) { ASSError(__FILE__, __LINE__, #arg, false); }
void ASSError(const char* file, unsigned int line, const char* assertStr, bool crash);