/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#ifndef __CNES_UTILS_LOG_
#define __CNES_UTILS_LOG_

#define LOG_LVL_NONE  0
#define LOG_LVL_ERROR 1
#define LOG_LVL_WARN  2
#define LOG_LVL_DEBUG 3

#if LOG_LEVEL >= LOG_LVL_ERROR
    #define LogPrintErr(_fmt, ...) \
        printf("%s:%d %s:Error! "_fmt, \
                   __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
    #define LogPrintErr EMPTY_FUNC
#endif

#if LOG_LEVEL >= LOG_LVL_WARN
    #define LogPrintWrn(_fmt, ...) \
        printf("%s:%d %s:Warning! "_fmt, \
                   __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
    #define LogPrintWrn EMPTY_FUNC
#endif

#if LOG_LEVEL >= LOG_LVL_DEBUG
    #define LogPrintDbg(_fmt, ...) \
        printf("%s:%d %s: "_fmt, \
                   __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
    #define LogPrintDbg EMPTY_FUNC
#endif

#ifdef NDEBUG
    #define LogPrintAssert EMPTY_FUNC
#else
    #define LogPrintAssert(_assert, _fmt, ...) \
        do {                     \
            if (!(_assert)) {    \
                printf(_fmt, ##__VA_ARGS__); \
                fflush(stdout);  \
                assert(_assert); \
            }                    \
        } while (0)
#endif

#if LOG_LEVEL >= LOG_LVL_DEBUG
    #define PRINT_FIELD_STRING(_arg) \
        printf(#_arg" = %s\n", _arg);

    /* XXX: */
    #define PRINT_FIELD_SIZE(_arg) \
        printf(#_arg" = %umb %ukb %ub \n",\
               (_arg >> MB_BIT) % GB(1), (_arg >> KB_BIT) % MB(1), _arg % KB(1))

    #define PRINT_FIELD_INT32(_arg) \
        printf(#_arg" = %d\n", _arg)

    #define PRINT_FIELD_UINT32(_arg) \
        printf(#_arg" = %u\n", _arg)

    #define PRINT_FIELD_HEX32(_arg) \
        printf(#_arg" = 0x%x\n", _arg)

    #define PRINT_FIELD_HEX16(_arg) \
        printf(#_arg" = 0x%04x\n", _arg)

    #define PRINT_FIELD_HEX8(_arg) \
        printf(#_arg" = 0x%02x\n", _arg)

    #define PRINT_FIELD_POINTER(_arg) \
        printf(#_arg" = %p\n", _arg)

    #define PRINT_FIELD_BOOL(_arg) \
        printf(#_arg" = %s\n", _arg ? "True" : "False")

    #define PRINT_FIELD_DATA_4(_arg) \
        printf(#_arg" = \\x%02x\\x%02x\\x%02x\\x%02x\n", \
            _arg[0], _arg[1], _arg[2], _arg[3])

#else /* LOG_LVL_DEBUG */

    #define PRINT_FIELD_STRING  EMPTY_FUNC
    #define PRINT_FIELD_SIZE    EMPTY_FUNC
    #define PRINT_FIELD_INT32   EMPTY_FUNC
    #define PRINT_FIELD_UINT32  EMPTY_FUNC
    #define PRINT_FIELD_HEX32   EMPTY_FUNC
    #define PRINT_FIELD_HEX16   EMPTY_FUNC
    #define PRINT_FIELD_HEX8    EMPTY_FUNC
    #define PRINT_FIELD_POINTER EMPTY_FUNC
    #define PRINT_FIELD_BOOL    EMPTY_FUNC
    #define PRINT_FIELD_DATA_4  EMPTY_FUNC
#endif /* LOG_LVL_DEBUG */

#endif /* __CNES_UTILS_LOG_ */
