/*
 *  Copyright (C) 2015-2020 Team Kodi
 *  Copyright (C) 2015 Sam Stenvall
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

/*
 * Block request and processing logic copied from liveshift.cpp and
 * RingBuffer.cpp which are Copyright (C) Team XBMC and distributed
 * under the same license.
 */

#pragma once

#include "Buffer.h"
#include <thread>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include "../Socket.h"
#include "CircularBuffer.h"
#include "Seeker.h"
#include "session.h"


namespace timeshift {

  /**
   * Timeshift buffer which buffers into a file
   */
  class TimeshiftBuffer : public Buffer
  {
  public:

    TimeshiftBuffer();
    virtual ~TimeshiftBuffer();

    virtual bool Open(const std::string inputUrl) override;
    virtual void Close() override;
    virtual ssize_t Read(byte *buffer, size_t length) override;
    virtual int64_t Seek(int64_t position, int whence) override;

    virtual bool CanPauseStream() const override
    {
      return m_CanPause;
    }

    virtual void PauseStream(bool bPause) override
    {
      if ((m_sd.isPaused = bPause))
        m_sd.lastPauseAdjust = m_sd.pauseStart = time(nullptr);
      else
        m_sd.lastPauseAdjust = m_sd.pauseStart = 0;
    }

    virtual bool CanSeekStream() const override
    {
      return true;
    }

    virtual int64_t Position() const override
    {
      return m_sd.streamPosition.load();  // very approximate
    }

    virtual int64_t Length() const override
    {
      return m_sd.lastKnownLength.load();
    }

    virtual bool IsTimeshifting() const override
    {
      if (m_active)
        return true;
      return false;
    }

    virtual PVR_ERROR GetStreamTimes(kodi::addon::PVRStreamTimes& stimes) override;
    virtual PVR_ERROR GetStreamReadChunkSize(int& chunksize) override;

  private:

    const static int INPUT_READ_LENGTH;
    const static int WINDOW_SIZE;
    const static int BUFFER_BLOCKS;

    NextPVR::Socket           *m_streamingclient;

    /**
     * The method that runs on m_inputThread. It reads data from the input
     * handle and writes it to the output handle
     */
    void ConsumeInput();

    void TSBTimerProc();


    bool WriteData(const byte *, unsigned int, uint64_t);

    /**
     * Closes any open file handles and resets all file positions
     */
    void Reset();

    /**
     * Sends requests for blocks to backend.
     */
    void RequestBlocks(void);          // Acquires lock, calls internalRequestBlocks();
    void internalRequestBlocks(void);  // Call when already holding lock.

    /**
     * Pull in incoming blocks.
     */
    uint32_t WatchForBlock(byte *, uint64_t *);

    /**
     * The thread that reads from m_inputHandle and writes to the output
     * handles
     */
    std::thread m_inputThread;

    /**
     * The thread that keeps track of the size of the current tsb, and
     * drags the starting time forward when slip seconds is exceeded
     */
    std::thread m_tsbThread;

    /**
     * Protects m_output*Handle
     */
    // mutable std::mutex m_mutex moved to base class

    /**
     * Protects seek completion
     */
    mutable std::mutex m_sLock;

    /**
     * Signaled whenever new packets have been added to the buffer
     */
    mutable std::condition_variable m_reader;

    /**
     * Signaled whenever data has read from the buffer
     */
    mutable std::condition_variable m_writer;

    /**
     * Signaled whenever seek processing is complete.
     */
    mutable std::condition_variable m_seeker;

    /**
     * The current write position in the buffer file
     */
    Seeker m_seek;
    CircularBuffer m_circularBuffer;
    session_data_t m_sd;
    bool m_CanPause;
  };
}
