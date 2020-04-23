/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include  "../BackendRequest.h"
#include "DummyBuffer.h"
#include <sstream>

namespace timeshift {

  class TranscodedBuffer : public DummyBuffer
  {
  public:
    TranscodedBuffer() : DummyBuffer()
    {
      m_profile << "&profile=" << g_iResolution << "p";
      XBMC->Log(LOG_INFO, "TranscodedBuffer created! %s", m_profile.str().c_str());
    }

    ~TranscodedBuffer() {}

    bool Open(const std::string inputUrl);

    void Close();

    int TranscodeStatus();

    int Lease();

    bool CheckStatus();

    bool GetStreamInfo();

  private:
    std::ostringstream m_profile;

  };

}
