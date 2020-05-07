/*
 *  Copyright (C) 2015-2020 Team Kodi
 *  Copyright (C) 2015 Sam Stenvall
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

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
    bool m_isRadio = false;

  protected:
    void *m_slipHandle = nullptr;
    time_t m_streamStart;

    std::atomic<time_t> m_rollingStartSeconds;

    std::atomic<int64_t> m_stream_length;
    std::atomic<int64_t> m_stream_duration;
    std::atomic<int> m_bytesPerSecond;

    bool m_isEpgBased;
    int m_prebuffer;
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
      m_lastClose = 0;
      XBMC->Log(LOG_INFO, "EPG Based Buffer created!");
    }

    virtual ~RollingFile() {}

    virtual bool Open(const std::string inputUrl, bool isRadio = false) override;
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

    virtual PVR_ERROR GetStreamReadChunkSize(int *chunksize) override
    {
      *chunksize = m_settings.m_liveChunkSize;
      return PVR_ERROR_NO_ERROR;
    }

    bool RollingFileOpen();

    virtual bool GetStreamInfo();

    virtual PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES *) override;
  };
}
