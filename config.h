/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2015 Pavel Butsykin
 */
#ifndef __CNES_CONFIG_
#define __CNES_CONFIG_

/* Local section */
#ifdef CNES_MAIN
    #ifdef __SANITIZE_ADDRESS__
    const char *__lsan_default_suppressions(void)
    {
        return "leak:libfontconfig\nleak:libSDL2\n";
    }
    #endif
#endif /* CNES_MAIN */

#ifdef UTILS
    #ifdef UTILS_MEMORY   // On power-on, the memory state of devices is non-deterministic,
        #ifdef CNES_TEST  // but for accurate tracing tests, it must be initialized to
        #define ZMEMORY   // constant state.
        #endif
    #endif
#endif /* UTILS */

#ifdef CNES_APU
    #ifdef APU
    #define APU_REG_TRACE false
    #endif
#endif /* CNES_APU */

#ifndef HAVE_MMAP
#define HAVE_MMAP 1
#endif

/* Global section */
#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

#endif /* __CNES_CONFIG_ */
