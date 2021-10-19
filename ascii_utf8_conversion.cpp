#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <intrin.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#define null nullptr

#define ArrayLength(array) (sizeof(array) / sizeof(*(array)))

#define CSTR_LEN(cstr)  (ArrayLength(cstr) - 1)
#define CSTR_ARGS(cstr) cstr, CSTR_LEN(cstr)

#define KB(x) ((x) << 10)
#define MB(x) ((x) << 20)
#define GB(x) ((x) << 30)
#define TB(x) ((x) << 40)

#define ALIGN_UP(x, a) (((x) + (a - 1)) & ~((a) - 1))

static_assert(sizeof(int) == sizeof(long));

using s8  = signed char;
using s16 = signed short;
using s32 = signed long;
using s64 = signed long long;

using u8  = unsigned char;
using u16 = unsigned short;
using u32 = unsigned long;
using u64 = unsigned long long;

using f32 = float;
using f64 = double;

using byte = unsigned char;

#define U16_MAX 0xFFFF
#define U32_MAX 0xFFFF'FFFF

#pragma warning(push)
#pragma warning(disable: 4200)
struct ASCIIString
{
    u64  length;
    byte data[0];
};

struct UTF8String
{
    u64  bytes;
    byte data[0];
};
#pragma warning(pop)

struct Arena
{
    void *base;
    u64   occupied;
    u64   capacity;
} g_Arena;

void Arena_Init(u64 bytes)
{
    SYSTEM_INFO info{};
    GetSystemInfo(&info);

    g_Arena.occupied = 0;
    g_Arena.capacity = ALIGN_UP(bytes, (u64)info.dwPageSize);
    g_Arena.base     = VirtualAlloc(null, g_Arena.capacity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    assert(g_Arena.base);
}

#define Arena_Destroy() VirtualFree(g_Arena.base, 0, MEM_RELEASE)

void *Arena_PushBytes(u64 bytes)
{
    assert((bytes <= g_Arena.capacity - g_Arena.occupied) && "Arena overflow");

    byte *retptr = (byte *)g_Arena.base + g_Arena.occupied;
    g_Arena.occupied += bytes;
    return (void *)retptr;
}

#define Arena_Reset() g_Arena.occupied = 0

u64 ASCIIToUTF8StringBytes(const char *ascii_string, u64 ascii_string_length)
{
    u64 utf8_bytes  = 0;
    
    const byte *it_end = (const byte *)ascii_string + ascii_string_length;
    for (const byte *it = (const byte *)ascii_string; it < it_end; ++it)
    {
        if (*it < 0x80) utf8_bytes += 1;
        else            utf8_bytes += 2;
    }

    return utf8_bytes;
}

u64 UTF8ToASCIIStringBytes(const UTF8String *utf8_string)
{
    u64 ascii_length = 0;

    const byte *it     = utf8_string->data;
    const byte *it_end = utf8_string->data + utf8_string->bytes;

    while (it < it_end)
    {
        if (*it >> 4 == 0xE) // >= 3 bytes
        {
            perror("ASCII overflow");
            break;
        }
        else if (*it >> 5 == 6) // 2 bytes
        {
            it += 2;
        }
        else if (*it >> 7 == 0) // 1 byte
        {
            it++;
        }

        ascii_length++;
    }

    return ascii_length;
}

UTF8String *ASCIIToUTF8String(const char *ascii_string, u64 ascii_string_length)
{
    u64         utf8_string_bytes = ASCIIToUTF8StringBytes(ascii_string, ascii_string_length);
    UTF8String *utf8_string       = (UTF8String *)Arena_PushBytes(offsetof(UTF8String, data) + utf8_string_bytes);

    if (utf8_string)
    {
        utf8_string->bytes = utf8_string_bytes;

        const byte *ascii_it     = (const byte *)ascii_string;
        const byte *ascii_it_end = (const byte *)ascii_string + ascii_string_length;

        byte *utf8_it     = utf8_string->data;
        byte *utf8_it_end = utf8_string->data + utf8_string->bytes;

        while (ascii_it < ascii_it_end)
        {
            if (*ascii_it < 0x80)
            {
                *utf8_it++ = *ascii_it++;
            }
            else // 2 byte
            {
                *utf8_it++ = 0xC0 | (((*ascii_it) >> 6) & 0x1F);
                *utf8_it++ = 0x80 | (((*ascii_it) >> 0) & 0x3F);
                ascii_it++;
            }
        }

        assert(utf8_it  == utf8_it_end);
        assert(ascii_it == ascii_it_end);
    }

    return utf8_string;
}

ASCIIString *UTF8ToASCIIString(const UTF8String *utf8_string)
{
    u64          ascii_string_length = UTF8ToASCIIStringBytes(utf8_string);
    ASCIIString *ascii_string        = (ASCIIString *)Arena_PushBytes(offsetof(ASCIIString, data) + ascii_string_length);

    if (ascii_string)
    {
        ascii_string->length = ascii_string_length;

        const byte *utf8_it     = utf8_string->data;
        const byte *utf8_it_end = utf8_string->data + utf8_string->bytes;

        byte *ascii_it     = ascii_string->data;
        byte *ascii_it_end = ascii_string->data + ascii_string->length;

        while (utf8_it < utf8_it_end)
        {
            if (*utf8_it >> 4 == 0xE) // >= 3 bytes
            {
                perror("ASCII overflow");
                break;
            }
            else if (*utf8_it >> 5 == 6) // 2 byte
            {
                byte byte_high = utf8_it[0] & 0x1F;
                byte byte_low  = utf8_it[1] & 0x3F;

                u32 bits_in_byte_high = 0;
                _BitScanReverse(&bits_in_byte_high, byte_high);

                if (bits_in_byte_high > 2)
                {
                    perror("ASCII literal overflow");
                    break;
                }

                *ascii_it++ = (byte_high << 6) | byte_low;

                utf8_it += 2;
            }
            else if (*utf8_it >> 7 == 0) // 1 byte
            {
                *ascii_it++ = *utf8_it++;
            }
            else
            {
                perror("Unknown UTF8 encoding");
                break;
            }
        }

        assert(ascii_it == ascii_it_end);
        assert(utf8_it  == utf8_it_end);
    }

    return ascii_string;
}

UTF8String *Win32ASCIIToUTF8String(const char *ascii_string, u64 ascii_string_length)
{
    int      unicode_string_length = MultiByteToWideChar(CP_ACP, 0, (LPCCH)ascii_string, (int)ascii_string_length, null, 0);
    wchar_t *unicode_string        = (wchar_t *)Arena_PushBytes(sizeof(wchar_t) * unicode_string_length);

    if (unicode_string)
    {
        int wchars_converted = MultiByteToWideChar(CP_ACP, 0, (LPCCH)ascii_string, (int)ascii_string_length, unicode_string, unicode_string_length);
        assert(wchars_converted == unicode_string_length);

        int         utf8_string_bytes = WideCharToMultiByte(CP_UTF8, 0, unicode_string, unicode_string_length, null, 0, null, null);
        UTF8String *utf8_string       = (UTF8String *)Arena_PushBytes(offsetof(UTF8String, data) + utf8_string_bytes);

        if (utf8_string)
        {
            utf8_string->bytes  = utf8_string_bytes;
            int bytes_converted = WideCharToMultiByte(CP_UTF8, 0, unicode_string, unicode_string_length, (LPSTR)utf8_string->data, utf8_string_bytes, null, null);
            assert(bytes_converted == utf8_string_bytes);
        }

        return utf8_string;
    }

    return null;
}

ASCIIString *Win32UTF8ToASCIIString(const UTF8String *utf8_string)
{
    int      unicode_string_length = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)utf8_string->data, (int)utf8_string->bytes, null, 0);
    wchar_t *unicode_string        = (wchar_t *)Arena_PushBytes(sizeof(wchar_t) * unicode_string_length);

    if (unicode_string)
    {
        int wchars_converted = MultiByteToWideChar(CP_UTF8, 0, (LPCCH)utf8_string->data, (int)utf8_string->bytes, unicode_string, unicode_string_length);
        assert(wchars_converted == unicode_string_length);

        int          ascii_string_bytes = WideCharToMultiByte(CP_ACP, 0, unicode_string, unicode_string_length, null, 0, null, null);
        ASCIIString *ascii_string       = (ASCIIString *)Arena_PushBytes(offsetof(ASCIIString, data) + ascii_string_bytes);

        if (ascii_string)
        {
            ascii_string->length = ascii_string_bytes;
            int bytes_converted  = WideCharToMultiByte(CP_ACP, 0, unicode_string, unicode_string_length, (LPSTR)ascii_string->data, ascii_string_bytes, null, null);
            assert(bytes_converted == ascii_string_bytes);
        }

        return ascii_string;
    }

    return null;
}

int main(int args_count, const char **args)
{
    if (args_count < 2)
    {
        fprintf(stderr, "Use: %s <ascii_string>\n", args[0]);
        return 1;
    }

    const char *source_ascii_string        = args[1];
    u64         source_ascii_string_length = strlen(source_ascii_string);

    Arena_Init(MB(1));

    printf("\nSource ASCII string (HEX): ");
    {
        const byte *it_end = (const byte *)source_ascii_string + source_ascii_string_length;
        for (const byte *it = (const byte *)source_ascii_string; it < it_end; ++it)
        {
            printf("%hhX ", *it);
        }
    }
    puts("\n");

    #pragma region conversion test
    {
        UTF8String  *my_utf8_string     = ASCIIToUTF8String(source_ascii_string, source_ascii_string_length);
        UTF8String  *win32_utf8_string  = Win32ASCIIToUTF8String(source_ascii_string, source_ascii_string_length);
        ASCIIString *my_ascii_string    = UTF8ToASCIIString(my_utf8_string);
        ASCIIString *win32_ascii_string = Win32UTF8ToASCIIString(win32_utf8_string);

        printf("My    converted UTF8  string (HEX): ");
        {
            const byte *it_end = my_utf8_string->data + my_utf8_string->bytes;
            for (const byte *it = my_utf8_string->data; it < it_end; ++it)
            {
                printf("%hhX ", *it);
            }
        }
        puts("");

        printf("Win32 converted UTF8  string (HEX): ");
        {
            const byte *it_end = win32_utf8_string->data + win32_utf8_string->bytes;
            for (const byte *it = win32_utf8_string->data; it < it_end; ++it)
            {
                printf("%hhX ", *it);
            }
        }
        puts("");

        printf("My    converted ASCII string (HEX): ");
        {
            const byte *it_end = my_ascii_string->data + my_ascii_string->length;
            for (const byte *it = my_ascii_string->data; it < it_end; ++it)
            {
                printf("%hhX ", *it);
            }
        }
        puts("");

        printf("Win32 converted ASCII string (HEX): ");
        {
            const byte *it_end = win32_ascii_string->data + win32_ascii_string->length;
            for (const byte *it = win32_ascii_string->data; it < it_end; ++it)
            {
                printf("%hhX ", *it);
            }
        }
        puts("\n");
    }
    Arena_Reset();
    #pragma endregion

    #pragma region performance test
    u64 my_ascii_to_utf8_start     = 0;
    u64 my_ascii_to_utf8_end       = 0;
    u64 my_ascii_to_utf8           = 0;
    u64 my_ascii_to_utf8_entire    = 0;
    u64 win32_ascii_to_utf8_start  = 0;
    u64 win32_ascii_to_utf8_end    = 0;
    u64 win32_ascii_to_utf8        = 0;
    u64 win32_ascii_to_utf8_entire = 0;
    u64 my_utf8_to_ascii_start     = 0;
    u64 my_utf8_to_ascii_end       = 0;
    u64 my_utf8_to_ascii           = 0;
    u64 my_utf8_to_ascii_entire    = 0;
    u64 win32_utf8_to_ascii_start  = 0;
    u64 win32_utf8_to_ascii_end    = 0;
    u64 win32_utf8_to_ascii        = 0;
    u64 win32_utf8_to_ascii_entire = 0;

    for (u64 i = 0; i < 1000000; ++i)
    {
        QueryPerformanceCounter((LARGE_INTEGER *)&my_ascii_to_utf8_start);
        UTF8String *my_utf8_string = ASCIIToUTF8String(source_ascii_string, source_ascii_string_length);
        QueryPerformanceCounter((LARGE_INTEGER *)&my_ascii_to_utf8_end);

        QueryPerformanceCounter((LARGE_INTEGER *)&win32_ascii_to_utf8_start);
        UTF8String *win32_utf8_string = Win32ASCIIToUTF8String(source_ascii_string, source_ascii_string_length);
        QueryPerformanceCounter((LARGE_INTEGER *)&win32_ascii_to_utf8_end);

        QueryPerformanceCounter((LARGE_INTEGER *)&my_utf8_to_ascii_start);
        ASCIIString *my_ascii_string = UTF8ToASCIIString(my_utf8_string);
        QueryPerformanceCounter((LARGE_INTEGER *)&my_utf8_to_ascii_end);

        QueryPerformanceCounter((LARGE_INTEGER *)&win32_utf8_to_ascii_start);
        ASCIIString *win32_ascii_string = Win32UTF8ToASCIIString(win32_utf8_string);
        QueryPerformanceCounter((LARGE_INTEGER *)&win32_utf8_to_ascii_end);

           my_ascii_to_utf8 =    my_ascii_to_utf8_end -    my_ascii_to_utf8_start;
        win32_ascii_to_utf8 = win32_ascii_to_utf8_end - win32_ascii_to_utf8_start;
           my_utf8_to_ascii =    my_utf8_to_ascii_end -    my_utf8_to_ascii_start;
        win32_utf8_to_ascii = win32_utf8_to_ascii_end - win32_utf8_to_ascii_start;

           my_ascii_to_utf8_entire +=    my_ascii_to_utf8;
        win32_ascii_to_utf8_entire += win32_ascii_to_utf8;
           my_utf8_to_ascii_entire +=    my_utf8_to_ascii;
        win32_utf8_to_ascii_entire += win32_utf8_to_ascii;

    #if 0
        printf("My    ASCII to UTF8  string (ticks): %I64u\n",    my_ascii_to_utf8);
        printf("Win32 ASCII to UTF8  string (ticks): %I64u\n", win32_ascii_to_utf8);
        printf("My    UTF8  to ASCII string (ticks): %I64u\n",    my_utf8_to_ascii);
        printf("Win32 UTF8  to ASCII string (ticks): %I64u\n", win32_utf8_to_ascii);
    #endif

        Arena_Reset();
    }

    f64 my_ascii_to_utf8_average    =    my_ascii_to_utf8_entire / 1000000.0;
    f64 win32_ascii_to_utf8_average = win32_ascii_to_utf8_entire / 1000000.0;
    f64 my_utf8_to_ascii_average    =    my_utf8_to_ascii_entire / 1000000.0;
    f64 win32_utf8_to_ascii_average = win32_utf8_to_ascii_entire / 1000000.0;

    printf("My    ASCII to UTF8  string conversion average ticks (1`000`000 tests): %lf\n",    my_ascii_to_utf8_average);
    printf("Win32 ASCII to UTF8  string conversion average ticks (1`000`000 tests): %lf\n", win32_ascii_to_utf8_average);
    printf("My    UTF8  to ASCII string conversion average ticks (1`000`000 tests): %lf\n",    my_utf8_to_ascii_average);
    printf("Win32 UTF8  to ASCII string conversion average ticks (1`000`000 tests): %lf\n", win32_utf8_to_ascii_average);
    puts("");
    printf("My    ASCII to UTF8  string conversion entire ticks (1`000`000 tests): %I64u\n",    my_ascii_to_utf8_entire);
    printf("Win32 ASCII to UTF8  string conversion entire ticks (1`000`000 tests): %I64u\n", win32_ascii_to_utf8_entire);
    printf("My    UTF8  to ASCII string conversion entire ticks (1`000`000 tests): %I64u\n",    my_utf8_to_ascii_entire);
    printf("Win32 UTF8  to ASCII string conversion entire ticks (1`000`000 tests): %I64u\n", win32_utf8_to_ascii_entire);
    puts("");

    if (my_ascii_to_utf8_average < win32_ascii_to_utf8_average)
    {
        printf("My over Win32 ASCII to UTF8  string conversion average speed is increased by %lf%%\n", my_ascii_to_utf8_average / win32_ascii_to_utf8_average * 100.0);
    }
    else
    {
        printf("My over Win32 ASCII to UTF8  string conversion average speed is decreased by %lf%%\n", win32_ascii_to_utf8_average / my_ascii_to_utf8_average * 100.0);
    }

    if (my_utf8_to_ascii_average < win32_utf8_to_ascii_average)
    {
        printf("My over Win32 UTF8  to ASCII string conversion average speed is increased by %lf%%\n", my_utf8_to_ascii_average / win32_utf8_to_ascii_average * 100.0);
    }
    else
    {
        printf("My over Win32 UTF8  to ASCII string conversion average speed is decreased by %lf%%\n", win32_utf8_to_ascii_average / my_utf8_to_ascii_average * 100.0);
    }

    #pragma endregion

    Arena_Destroy();
    return 0;
}
