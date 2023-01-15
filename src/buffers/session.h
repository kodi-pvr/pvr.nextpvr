/*
 *  Copyright (C) 2020-2023 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2017 Mike Burgett
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include <stdint.h>
#include <atomic>

namespace timeshift {

  typedef struct
  {
    /**
     * The offset into stream of the last block we successfully buffered.
     */
    volatile int64_t lastBlockBuffered;
    /**
     * Sliding window variable, should be in range 0..WINDOW_SIZE
     */
    int currentWindowSize;
    /**
     * Requests sent to back end this session.
     */
    int requestNumber;
    /**
     *
     */
    int inputBlockSize;

    volatile bool isPaused;
    volatile time_t pauseStart;
    volatile time_t lastPauseAdjust;
    /**
     * The next block to request.
     */
    int64_t requestBlock;
    /**
     * The last known length of the timeshift file on backend
     */
    volatile std::atomic<int64_t> lastKnownLength;
    volatile std::atomic<int64_t> ptsBegin;
    volatile std::atomic<int64_t> ptsEnd;

    volatile std::atomic<int64_t> tsbStart;

    volatile int iBytesPerSecond;
    volatile std::atomic<time_t> sessionStartTime;
    volatile std::atomic<time_t> tsbStartTime;
    volatile time_t tsbRollOff;
    volatile time_t lastBufferTime;
    /**
     * The next position a read will access. (in stream, not buffer)
     */
    std::atomic<int64_t> streamPosition;
  } session_data_t;
}
