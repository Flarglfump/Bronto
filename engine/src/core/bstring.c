#include "core/bstring.h"

#include <ctype.h>  // isspace
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "containers/darray.h"
#include "core/bmemory.h"
#include "core/logger.h"
// #include "math/kmath.h"
// #include "math/transform.h"

#ifndef _MSC_VER
#include <strings.h>
#endif

u64 string_length(const char * str) {
  return strlen(str);
}

char* string_duplicate(const char * str) {
  u64 length = string_length(str);
  char * copy = b_alloc(length + 1, MEMORY_TAG_STRING);
  b_copy_mem(copy, str, length);
  copy[length] = 0;
  return copy;
}
