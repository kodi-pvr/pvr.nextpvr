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
  class DummyBuffer : public Buffer
  {
  public:
    DummyBuffer() : Buffer() { kodi::Log(ADDON_LOG_INFO, "DummyBuffer created!"); }
    virtual ~DummyBuffer() {}

    virtual ssize_t Read(byte *buffer, size_t length) override
    {
      return m_inputHandle.Read(buffer, length);
    }

    virtual int64_t Seek(int64_t position, int whence) override
    {
      return -1; // we can't seek without a real buffer
    }

    virtual int64_t Position() const override
    {
      return m_inputHandle.GetPosition();
    }

    virtual int64_t Length() const override
    {
      return m_inputHandle.GetLength();
    }

    PVR_ERROR GetStreamTimes(kodi::addon::PVRStreamTimes& stimes) override;
  };
}
