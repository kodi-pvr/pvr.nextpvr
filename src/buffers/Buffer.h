/*
 *  Copyright (C) 2015-2020 Team Kodi
 *  Copyright (C) 2015 Sam Stenvall
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#if defined(TARGET_WINDOWS)
  #include <windows.h>
  #include <Synchapi.h>
#endif
#include <string>
#include <ctime>
#include <atomic>
#include "../client.h"
#include <mutex>
#include <thread>
#include  "../BackendRequest.h"


#if defined(TARGET_WINDOWS)
#define SLEEP(ms) Sleep(ms)
#else
#define SLEEP(ms) usleep(ms*1000)
#endif

namespace timeshift {

  /**
   * The basic type all buffers operate on
   */
#ifdef _WIN32
  #include <windows.h>
#else
  typedef unsigned char byte;
#endif

  /**
   * Base class for all timeshift buffers
   */
  class Buffer
  {
  public:
    Buffer() :
      m_active(false), m_inputHandle(nullptr), m_startTime(0),
      m_readTimeout(DEFAULT_READ_TIMEOUT) {XBMC->Log(LOG_NOTICE, "Buffer created!"); };
    virtual ~Buffer();

    /**
     * Opens the input handle
     * @return whether the input was successfully opened
     */
    virtual bool Open(const std::string inputUrl);

    /**
     * Opens the input handle with options  Kodi addons use 0
     * @return whether the input was successfully opened
     */
    virtual bool Open(const std::string inputUrl, int optFlag);

    /**
     * Closes the buffer
     */
    virtual void Close();

    /**
     * Reads "length" bytes into the specified buffer
     * @return the number of bytes read
     */
    virtual int Read(byte *buffer, size_t length) = 0;

    /**
     * Seeks to the specified position
     * @return the new position
     */
    virtual int64_t Seek(int64_t position, int whence) = 0;

    /**
     * Whether the buffer supports pausing
     */
    virtual bool CanPauseStream() const
    {
      return false;
    }

    virtual void PauseStream(bool bPause) {}

    /**
     * Whether the buffer supports seeking
     */
    virtual bool CanSeekStream() const
    {
      return false;
    }

    /**
     * @return the current read position
     */
    virtual int64_t Position() const = 0;

    /**
     * @return the current length of the buffer
     */
    virtual int64_t Length() const = 0;

    virtual bool IsTimeshifting() const
    {
      return false;
    }

    virtual bool IsRealTimeStream() const
    {
      if (m_active)
        return true;
      return false;
    }

    /**
     * @return stream times
     */
    virtual PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES *stimes)
    {
      stimes->startTime = m_startTime;
      stimes->ptsStart = 0;
      stimes->ptsBegin = 0;
      stimes->ptsEnd = 0;
      return PVR_ERROR_NO_ERROR;
    }

    /**
     * The time the buffer was created
     */
    int m_chunkSize = 16;

    virtual PVR_ERROR GetStreamReadChunkSize(int* chunksize)
    {
      // Return 16K for recordings, and non-timeshift
      if (g_NowPlaying == Radio)
        *chunksize = 4096;
      else
        *chunksize = m_chunkSize * 1024;
      return PVR_ERROR_NO_ERROR;
    }

    /**
     * @return basically the current time
     */
    virtual time_t GetEndTime()
    {
      return time(nullptr);
    }

    /**
     * Sets the read timeout (defaults to 10 seconds)
     * @param timeout the read timeout in seconds
     */
    void SetReadTimeout(int timeout)
    {
      m_readTimeout = timeout;
    }

    void Channel(int channel_id)
    {
      m_channel_id = channel_id;
    }

    virtual int Lease();

  protected:

    time_t m_nextRoll;
    time_t m_nextLease;
    time_t m_nextStreamInfo;
    bool m_isLeaseRunning;
    std::thread m_leaseThread;
    void LeaseWorker();
    virtual bool GetStreamInfo() {return true;}
    bool m_complete;
    mutable std::mutex m_mutex;


    const static int DEFAULT_READ_TIMEOUT;

    /**
     * Safely closes an open file handle.
     * @param the handle to close. The pointer will be nulled.
     */
    void CloseHandle(void *&handle);

    /**
     * The input handle (where data is read from)
     */
    void *m_inputHandle;

    /**
     * The time (in seconds) to wait when opening a read handle and when
     * waiting for the buffer to have enough data
     */
    int m_readTimeout;

    /**
     * Whether the buffer is active, i.e. m_inputHandle should be read from
     */
    volatile std::atomic<bool> m_active;

    /**
     * The time the buffer was created
     */
    time_t m_startTime;

    int m_channel_id;

  };
}
