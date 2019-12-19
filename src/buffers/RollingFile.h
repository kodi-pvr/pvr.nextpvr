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
    std::string m_activeFilename;
    int64_t m_activeLength;

  protected:
    void *m_slipHandle = nullptr;
    time_t m_streamStart;

    std::atomic<time_t> m_rollingStartSeconds;

    std::atomic<int64_t> m_stream_length;
    std::atomic<int64_t> m_stream_duration;
    std::atomic<int> m_bytesPerSecond;

    bool m_isEpgBased;
    int m_prebuffer;
    int m_liveChunkSize;
    time_t m_lastClose;

    bool m_isPaused;

    struct slipFile{
      std::string filename;
      int64_t offset;
      int64_t length;
      int seconds;
    };

    std::list <slipFile> slipFiles;

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
      m_lastClose = 0;
      XBMC->Log(LOG_NOTICE, "EPG Based Buffer created!");
    }

    virtual ~RollingFile() {}

    virtual bool Open(const std::string inputUrl) override;
    virtual void Close() override;

    virtual void PauseStream(bool bPause) override
    {
      if ((m_isPaused = bPause))
        m_nextLease = 20;
    }

    virtual int64_t Length() const override
    {
      return m_stream_length;
    }

    virtual int64_t Position() const override
    {
      return m_activeLength + XBMC->GetFilePosition(m_inputHandle);
    }

    virtual int Read(byte *buffer, size_t length) override;

    int64_t Seek(int64_t position, int whence) override;


    bool RollingFileOpen();

    virtual bool GetStreamInfo();

    virtual PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES *) override;
  };
}
