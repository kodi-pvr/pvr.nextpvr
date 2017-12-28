#pragma once
/*
*      Copyright (C) 2015 Sam Stenvall
*
*      Block request and processing logic copied from liveshift.cpp and
*      RingBuffer.cpp which are Copyright (C) Team XBMC and distributed
*      under the same license.
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
    virtual int Read(byte *buffer, size_t length) override;
    virtual int64_t Seek(int64_t position, int whence) override;

    virtual bool CanPauseStream() const override
    {
      return true;
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

    virtual time_t GetPlayingTime() override;
    virtual time_t GetStartTime() override;
    virtual time_t GetEndTime() override;

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
     * Protects m_output*Handle
     */
    mutable std::mutex m_mutex;

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
    time_t m_tsbStartTime;
  };
}
