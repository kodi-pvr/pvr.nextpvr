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

using namespace ADDON;
namespace timeshift {

  /**
   * Dummy buffer that just passes all calls through to the input file
   * handle without actually buffering anything
   */
  class RecordingBuffer : public Buffer
  {
  private:
    int m_Duration;
    bool m_buffering = false;
    std::string m_recordingURL;

  public:
    RecordingBuffer() : Buffer() { m_Duration = 0; XBMC->Log(LOG_NOTICE, "RecordingBuffer created!"); }
    virtual ~RecordingBuffer() {}

    virtual int Read(byte *buffer, size_t length) override;

    virtual int64_t Seek(int64_t position, int whence) override
    {
      int64_t retval =  XBMC->SeekFile(m_inputHandle, position, whence);
      XBMC->Log(LOG_DEBUG, "Seek: %s:%d  %lld  %lld %lld %lld", __FUNCTION__, __LINE__,position, XBMC->GetFilePosition(m_inputHandle), XBMC->GetFileLength(m_inputHandle), retval );
      return retval;
    }

    virtual bool CanPauseStream() const override
    {
      return true;
    }

    virtual bool CanSeekStream() const override
    {
      return true;
    }

    virtual bool IsRealTimeStream() const override
    {
      return false;
    }


    PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES *) override;

    virtual int64_t Length() const override
    {
      return XBMC->GetFileLength(m_inputHandle);
    }
    virtual int64_t Position() const override
    {
      return XBMC->GetFilePosition(m_inputHandle);
    }

    virtual int Duration(void);
    int GetDuration(void) { return m_Duration; XBMC->Log(LOG_ERROR, "XXXXX Duration set to %d XXXXX", m_Duration); }
    void SetDuration(int duration) { m_Duration = duration; XBMC->Log(LOG_ERROR, "XXXXX Duration set to %d XXXXX", m_Duration); }

    bool Open(const std::string inputUrl,const PVR_RECORDING &recording);

    std::atomic<bool> m_isLive;

    // recording start time
    time_t m_recordingTime;
  };
}
