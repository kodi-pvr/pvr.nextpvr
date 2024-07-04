/*
 *  Copyright (C) 2015-2023 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2015 Sam Stenvall
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "RecordingBuffer.h"
#include <thread>
#include <list>

using namespace NextPVR;

namespace timeshift {

  class ATTR_DLL_LOCAL ClientTimeShift : public RecordingBuffer
  {
  private:
    bool m_isPaused = false;
    int64_t m_streamPosition;

    /**
     * The current live stream url with &seek=
     */
    std::string m_sourceURL;

    std::atomic<int64_t> m_stream_length;
    std::atomic<int64_t> m_stream_duration;
    std::atomic<int> m_bytesPerSecond;
    time_t m_lastClose;
    int m_prebuffer;
    std::atomic<time_t> m_rollingStartSeconds;
    time_t m_streamStart;

  public:
    ClientTimeShift(const std::shared_ptr<InstanceSettings>& settings, Request& request) : RecordingBuffer(settings, request)
    {
      m_lastClose = 0;
      m_channel_id = 0;
      kodi::Log(ADDON_LOG_INFO, "ClientTimeShift Buffer created!");
    }

    virtual void PauseStream(bool bPause) override
    {
      if ((m_isPaused = bPause))
      {
        // pause save restart position
        m_streamPosition = m_inputHandle.GetPosition();
      }
      else
      {
        Resume();
      }
    }

    virtual ~ClientTimeShift() {}

    virtual bool Open(const std::string inputUrl) override;
    virtual void Close() override;

    virtual bool GetStreamInfo() override;

    virtual int64_t Length() const override
    {
      return m_stream_length;
    }

    virtual int64_t Position() const override
    {
      return m_inputHandle.GetPosition();
    }
    virtual ssize_t Read(byte *buffer, size_t length) override
    {
      ssize_t dataLen = m_inputHandle.Read(buffer, length);
      if (m_complete && dataLen == 0)
      {
        kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %u %lld %lld", __FUNCTION__, __LINE__, length, m_inputHandle.GetLength() , m_inputHandle.GetPosition());
      }
      return dataLen;
    }

    void Resume();
    int64_t Seek(int64_t position, int whence) override;
    void StreamStop();

    virtual bool CanSeekStream() const override
    {
      return true;
    }
    virtual PVR_ERROR GetStreamTimes(kodi::addon::PVRStreamTimes& times) override;

    virtual bool IsRealTimeStream() const override
    {
      return time(nullptr) - m_streamStart < 10 + m_prebuffer;
    }

  };
}
