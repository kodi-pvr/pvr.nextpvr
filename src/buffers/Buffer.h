#pragma once
/*
*      Copyright (C) 2015 Sam Stenvall
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

#include <string>
#include <ctime>
#include <atomic>
#include "../client.h"

using namespace ADDON; 

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
    virtual bool CanPauseStream() const = 0;

    /**
     * Whether the buffer supports seeking
     */
    virtual bool CanSeekStream() const = 0;

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
     * @return the time the buffering started
     */
    virtual time_t GetStartTime()
    {
      return m_startTime;
    }
    
    virtual time_t GetPlayingTime()
    {
      return time(nullptr);
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

  protected:
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

  private:
    /**
     * The time the buffer was created
     */
    time_t m_startTime;
  };
}
