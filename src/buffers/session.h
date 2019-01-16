#pragma once
/*
*      Copyright (C) 2017 Mike Burgett
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with XBMC; see the file COPYING.  If not, write to
*  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
*  MA 02110-1301  USA
*  http://www.gnu.org/copyleft/gpl.html
*
*/

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
