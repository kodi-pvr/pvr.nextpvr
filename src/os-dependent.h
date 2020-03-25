/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#if defined(TARGET_WINDOWS)
#define WIN32_LEAN_AND_MEAN           // Enable LEAN_AND_MEAN support
#define NOMINMAX                      // don't define min() and max() to prevent a clash with std::min() and std::max
#endif

#include "p8-platform/os.h"

#if defined(TARGET_WINDOWS)
#  include "windows/os_windows.h"
#else
#  include "posix/os_posix.h"
#endif

#if defined(TARGET_DARWIN)
#  ifndef PTHREAD_MUTEX_RECURSIVE_NP
#    define PTHREAD_MUTEX_RECURSIVE_NP PTHREAD_MUTEX_RECURSIVE
#  endif
#endif
