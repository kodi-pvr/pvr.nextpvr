/*
 *  Copyright (C) 2015-2020 Team Kodi
 *  Copyright (C) 2015 Sam Stenvall
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#include "RollingFile.h"
#include <thread>
#include <list>

using namespace NextPVR;

namespace timeshift {

  class ATTRIBUTE_HIDDEN ClientTimeShift : public RollingFile
  {
  private:
    bool m_isPaused = false;
    int64_t m_streamPosition;

	/**
	 * The current live stream url with &seek=
	 */
	std::string m_sourceURL;

  public:
    ClientTimeShift() : RollingFile()
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

  };
}
