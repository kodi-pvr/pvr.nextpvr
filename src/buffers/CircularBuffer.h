/*
 *  Copyright (C) 2017-2023 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2017 Mike Burgett
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

//
// Dead simple circular buffer
//

#include "Buffer.h"

namespace timeshift {

  class ATTR_DLL_LOCAL CircularBuffer {
  public:
    CircularBuffer(int size) : m_iBytes(0), m_iReadPos(0), m_iWritePos(0), m_iSize(size) { m_cBuffer = new byte[m_iSize]; }
    ~CircularBuffer() { delete[] m_cBuffer; }

    void Reset() { m_iBytes = m_iReadPos = m_iWritePos = 0; }

    bool WriteBytes(const byte *, int);
    int ReadBytes(byte *, int);
    int BytesFree() { return m_iSize - m_iBytes; }
    int BytesAvailable() { return m_iBytes; }
    int AdjustBytes(int);
    int Size() { return m_iSize; }

  private:
    byte     *m_cBuffer;
    int32_t  m_iReadPos;
    int32_t  m_iWritePos;
    int32_t  m_iSize;
    int32_t  m_iBytes;
  };
}
