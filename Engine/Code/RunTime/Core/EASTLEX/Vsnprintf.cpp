#include <stdio.h>

int Vsnprintf8(char* p, size_t n, const char* pFormat, va_list arguments)
{
    if (p == nullptr && n == 0)
    {
#ifdef _MSC_VER
        return _vscprintf(pFormat, arguments);
#else
        return vsnprintf(nullptr, 0, pFormat, arguments);
#endif
    }

    #ifdef _MSC_VER
        return vsnprintf_s(p, n, _TRUNCATE, pFormat, arguments);
    #else
        return vsnprintf(p, n, pFormat, arguments);
    #endif
}