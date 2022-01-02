/*
 *  Copyright (C) 2020-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */


#pragma once

#include  "../BackendRequest.h"
#include "DummyBuffer.h"
#include <sstream>

namespace timeshift {

  class ATTR_DLL_LOCAL TranscodedBuffer : public DummyBuffer
  {
  public:
    TranscodedBuffer() : DummyBuffer()
    {
      kodi::Log(ADDON_LOG_INFO, "TranscodedBuffer created");
    }

    ~TranscodedBuffer() {}

    bool Open(const std::string inputUrl);

    void Close();

    int TranscodeStatus();

    enum LeaseStatus Lease();

    bool CheckStatus();

    bool GetStreamInfo();

  private:

  };

}
