#include "compat.h"
#include <stdarg.h>
int eo_snprintf(char *out,size_t size,const char *format,...){
    va_list args;va_start(args,format);int result=_vsnprintf(out,size,format,args);va_end(args);
    if(size)out[size-1]=0;return result;
}
int eo_swprintf(wchar_t *out,size_t size,const wchar_t *format,...){
    va_list args;va_start(args,format);int result=_vsnwprintf(out,size,format,args);va_end(args);
    if(size)out[size-1]=0;return result;
}
