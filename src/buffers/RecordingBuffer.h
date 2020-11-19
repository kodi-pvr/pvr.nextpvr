/*
 *  Copyright (C) 2015-2020 Team Kodi
 *  Copyright (C) 2015 Sam Stenvall
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "Buffer.h"


namespace timeshift {

  /**
   * Dummy buffer that just passes all calls through to the input file
   * handle without actually buffering anything
   */
  class ATTRIBUTE_HIDDEN RecordingBuffer : public Buffer
  {
  private:
    int m_Duration;
    bool m_buffering = false;
    std::string m_recordingURL;
    std::string m_recordingID;

  public:
    RecordingBuffer() : Buffer() { m_Duration = 0; kodi::Log(ADDON_LOG_INFO, "RecordingBuffer created!"); }
    virtual ~RecordingBuffer() {}

    virtual ssize_t Read(byte *buffer, size_t length) override;

    virtual int64_t Seek(int64_t position, int whence) override
    {
      int64_t retval = m_inputHandle.Seek(position, whence);
      kodi::Log(ADDON_LOG_DEBUG, "Seek: %s:%d  %lld  %lld %lld %lld", __FUNCTION__, __LINE__, position, m_inputHandle.GetPosition(), m_inputHandle.GetLength(), retval );
      return retval;
    }

    virtual bool CanPauseStream() const override
    {
      return true;
    }

    virtual bool CanSeekStream() const override
    {
      return m_inputHandle.GetLength() != 0;
    }

    virtual bool IsRealTimeStream() const override
    {
      return false;
    }

    PVR_ERROR GetStreamTimes(kodi::addon::PVRStreamTimes& stimes) override;

    virtual int64_t Length() const override
    {
      return m_inputHandle.GetLength();
    }
    virtual int64_t Position() const override
    {
      return m_inputHandle.GetPosition();
    }

    virtual int Duration(void);
    int GetDuration(void) { return m_Duration; kodi::Log(ADDON_LOG_ERROR, "Duration get %d", m_Duration); }
    void SetDuration(int duration) { m_Duration = duration; kodi::Log(ADDON_LOG_ERROR, "Duration set to %d", m_Duration); }

   PVR_ERROR GetStreamReadChunkSize(int* chunksize)
    {
      *chunksize = m_settings.m_chunkRecording * 1024;
      return PVR_ERROR_NO_ERROR;
    }

    bool Open(const std::string inputUrl, const kodi::addon::PVRRecording& recording);

    std::atomic<bool> m_isLive;

    // recording start time
    time_t m_recordingTime;
  };
}
