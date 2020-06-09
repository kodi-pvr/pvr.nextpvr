/*
 *  Copyright (C) 2017-2020 Team Kodi
 *  Copyright (C) 2017 Mike Burgett
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

//
//  Dead simple circular buffer
//

#include "CircularBuffer.h"

using namespace timeshift;


bool CircularBuffer::WriteBytes(const byte *buffer, int length)
{
  if (length > m_iSize - m_iBytes)
  {
    kodi::Log(ADDON_LOG_DEBUG, "WriteBytes: returning false %d [%d] [%d] [%d]", length, m_iSize, m_iBytes, m_iSize - m_iBytes);
    return false;
  }
  if (length + m_iWritePos > m_iSize)
  {
    unsigned int chunk = m_iSize - m_iWritePos;
    memcpy(m_cBuffer + m_iWritePos, buffer, chunk);
    memcpy(m_cBuffer, buffer + chunk, length - chunk);
    m_iWritePos = length - chunk;
  }
  else
  {
    memcpy(m_cBuffer + m_iWritePos, buffer, length);
    m_iWritePos += length;
  }
  if (m_iWritePos == m_iSize)
    m_iWritePos = 0;
  m_iBytes += length;
  kodi::Log(ADDON_LOG_DEBUG, "WriteBytes: wrote %d bytes, returning true. [%d] [%d] [%d]", length, m_iSize, m_iBytes, m_iSize - m_iBytes);
  return true;
}

int  CircularBuffer::ReadBytes(byte *buffer, int length)
{
  if (length + m_iReadPos > m_iSize)
  {
    unsigned int chunk = m_iSize - m_iReadPos;
    memcpy(buffer, m_cBuffer + m_iReadPos, chunk);
    memcpy(buffer + chunk, m_cBuffer, length - chunk);
    m_iReadPos = length - chunk;
  }
  else
  {
    memcpy(buffer, m_cBuffer + m_iReadPos, length);
    m_iReadPos += length;
  }
  if (m_iReadPos == m_iSize)
    m_iReadPos = 0;
  m_iBytes -= length;
  kodi::Log(ADDON_LOG_DEBUG, "ReadBytes: returning %d\n", length);
  return length;
}

int CircularBuffer::AdjustBytes(int delta)
{
  kodi::Log(ADDON_LOG_DEBUG, "AdjustBytes(%d): before: %d [%d]\n", delta, m_iReadPos, m_iBytes);
  m_iReadPos += delta;
  if (m_iReadPos < 0)
    m_iReadPos += m_iSize;
  if (m_iReadPos > m_iSize)
    m_iReadPos -= m_iSize;
  m_iBytes -= delta;
  kodi::Log(ADDON_LOG_DEBUG, "AdjustBytes(%d): after: %d [%d]\n", delta, m_iReadPos, m_iBytes);
  return m_iBytes;
}
