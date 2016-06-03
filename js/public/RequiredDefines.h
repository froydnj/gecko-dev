/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Various #defines required to build SpiderMonkey.  Embedders should add this
 * file to the start of the command line via -include or a similar mechanism,
 * or SpiderMonkey public headers may not work correctly.
 */

#ifndef js_RequiredDefines_h
#define js_RequiredDefines_h

/*
 * The c99 defining the limit macros (UINT32_MAX for example), says:
 *
 *   C++ implementations should define these macros only when
 *   __STDC_LIMIT_MACROS is defined before <stdint.h> is included.
 *
 * The same also occurs with __STDC_CONSTANT_MACROS for the constant macros
 * (INT8_C for example) used to specify a literal constant of the proper type,
 * and with __STDC_FORMAT_MACROS for the format macros (PRId32 for example) used
 * with the fprintf function family.
 */
#define __STDC_LIMIT_MACROS
#define __STDC_CONSTANT_MACROS
#define __STDC_FORMAT_MACROS

/*
 * Some standard library headers (notably bionic on Android) declare standard
 * functions (e.g. getchar()) and also #define macros for those standard
 * functions.  libc++ deals with this by doing something like the following
 * (explanatory comments added):
 *
 *   #ifdef FUNC
 *   // Capture the definition of FUNC.
 *   inline _LIBCPP_INLINE_VISIBILITY int __libcpp_FUNC(...) { return FUNC(...); }
 *   #undef FUNC
 *   // Use a real inline definition.
 *   inline _LIBCPP_INLINE_VISIBILITY int FUNC(...) { return _libcpp_FUNC(...); }
 *   #endif
 *
 * _LIBCPP_INLINE_VISIBILITY is typically defined as:
 *
 *   __attribute__((__visibility__("hidden"), __always_inline__))
 *
 * Unfortunately, this interacts badly with our system header wrappers, as the:
 *
 *   #pragma GCC visibility push(default)
 *
 * that they do prior to including the actual system header is treated by the
 * compiler as an explicit declaration of visibility on every function declared
 * in the header.  Therefore, when the libc++ code above is encountered, it is
 * as though the compiler has effectively seen:
 *
 *   int FUNC(...) __attribute__((__visibility__("default")));
 *   int FUNC(...) __attribute__((__visibility__("hidden")));
 *
 * and the compiler complains about the mismatched visibility declarations.
 *
 * However, libc++ will only define _LIBCPP_INLINE_VISIBILITY if there is no
 * existing definition.  We can therefore define it ourselves to the empty
 * string (since we are properly managing visibility ourselves) and avoid this
 * whole mess.
 */
#if defined(__clang__) && defined(__ANDROID__)
#define _LIBCPP_INLINE_VISIBILITY
#define _LIBCPP_INLINE_VISIBILITY_EXCEPT_GCC49
#endif

/* Also define a char16_t type if not provided by the compiler. */
#include "mozilla/Char16.h"

#endif /* js_RequiredDefines_h */
