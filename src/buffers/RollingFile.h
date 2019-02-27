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
#include "RecordingBuffer.h"
#include <thread>
#include <mutex>
#include <list>
#include "session.h"

std::string UriEncode(const std::string sSrc);

using namespace ADDON;
namespace timeshift {

  /**
   * Dummy buffer that just passes all calls through to the input file
   * handle without actually buffering anything
   */
  class RollingFile : public RecordingBuffer
  {
  private:
    mutable std::mutex m_mutex;
    session_data_t m_sd;
    std::string m_activeFilename;
    int64_t m_activeLength;
    void *m_slipHandle = nullptr;
    time_t m_slipStart;
    time_t m_rollingBegin;
    time_t m_nextRoll;
    bool m_isEpgBased;
    int m_prebuffer;
    int m_liveChunkSize;

    struct slipFile{
      std::string filename;
      int64_t offset;
      int64_t length;
    };

    std::list <slipFile> slipFiles;

    /**
     * The thread that keeps track of the size of the current tsb, and
     * drags the starting time forward when slip seconds is exceeded
     */
    std::thread m_tsbThread;

  public:
    RollingFile() : RecordingBuffer()
    {
      if (!XBMC->GetSetting("prebuffer", &m_prebuffer))
      {
        m_prebuffer = 8;
      }
      if (!XBMC->GetSetting("chunklivetv", &m_liveChunkSize))
      {
        m_liveChunkSize = 64;
      }
      XBMC->Log(LOG_NOTICE, "EPG Based Buffer created!");
    }

    virtual ~RollingFile() {}

    virtual bool Open(const std::string inputUrl) override;
    virtual void Close() override;

    virtual void PauseStream(bool bPause) override
    {
      if ((m_sd.isPaused = bPause))
        m_sd.lastBufferTime = 0;
    }

    virtual bool IsTimeshifting() const override
    {
      XBMC->Log(LOG_DEBUG, "%s:%d: %lld %lld", __FUNCTION__, __LINE__, XBMC->GetFileLength(m_inputHandle) ,XBMC->GetFilePosition(m_inputHandle));
      return true;
    }

    virtual int64_t Length() const override
    {
      return m_sd.lastKnownLength.load();
    }

    virtual int64_t Position() const override
    {
      return m_activeLength + XBMC->GetFilePosition(m_inputHandle);
    }

    virtual int Read(byte *buffer, size_t length) override;

    int64_t Seek(int64_t position, int whence) override;

    void TSBTimerProc();
    bool RollingFileOpen();

    bool GetStreamInfo();
    virtual PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES *) override;
  };
}
