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

std::string UriEncode(const std::string sSrc);

using namespace ADDON;
namespace timeshift {

  class ClientTimeShift : public RollingFile
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
      if (!XBMC->GetSetting("prebuffer", &m_prebuffer))
      {
        m_prebuffer = 0;
      }
      if (!XBMC->GetSetting("chunklivetv", &m_liveChunkSize))
      {
        m_liveChunkSize = 64;
      }
      m_lastClose = 0;
      m_channel_id = 0;
      XBMC->Log(LOG_NOTICE, "ClientTimeShift Buffer created!");
    }

    virtual void PauseStream(bool bPause) override
    {
      if ((m_isPaused = bPause))
      {
        // pause save restart position
        m_streamPosition = XBMC->GetFilePosition(m_inputHandle);
      }
      else
      {
        Seek(m_streamPosition,0);
      }
    }

    virtual ~ClientTimeShift() {}

    virtual bool Open(const std::string inputUrl) override;
    virtual void Close() override;

    virtual bool GetStreamInfo();

    virtual int64_t Position() const override
    {
      return XBMC->GetFilePosition(m_inputHandle);
    }
    virtual int Read(byte *buffer, size_t length) override
    {
      int64_t dataLen = XBMC->ReadFile(m_inputHandle, buffer, length);
      if (m_complete && dataLen == 0)
      {
        XBMC->Log(LOG_DEBUG, "%s:%d: %lld %lld %lld %lld", __FUNCTION__, __LINE__, dataLen, length, XBMC->GetFileLength(m_inputHandle) ,XBMC->GetFilePosition(m_inputHandle));
      }
      return dataLen;
    }

    int64_t Seek(int64_t position, int whence) override;

    void StreamStop(void);

  };
}
