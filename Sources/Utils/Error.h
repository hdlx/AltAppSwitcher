
#pragma once
#define ASSERT(arg) if (!(arg)) { ASSError(__FILE__, __LINE__, #arg); }
void ASSError(const char* file, unsigned int line, const char* assertStr);