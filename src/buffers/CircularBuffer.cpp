/*
*      Copyright (C) 2017 Mike Burgett
*
*  This Program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 2, or (at your option)
*  any later version.
*
*  This Program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with XBMC; see the file COPYING.  If not, write to
*  the Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
*  MA 02110-1301  USA
*  http://www.gnu.org/copyleft/gpl.html
*
*/

//
//  Dead simple circular buffer
//

#include "CircularBuffer.h"

using namespace timeshift;
using namespace ADDON;

bool CircularBuffer::WriteBytes(const byte *buffer, int length)
{
  if (length > m_iSize - m_iBytes)
  {
    XBMC->Log(LOG_DEBUG, "WriteBytes: returning false %d [%d] [%d] [%d]", length, m_iSize, m_iBytes, m_iSize - m_iBytes);
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
  XBMC->Log(LOG_DEBUG, "WriteBytes: wrote %d bytes, returning true. [%d] [%d] [%d]", length, m_iSize, m_iBytes, m_iSize - m_iBytes);
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
  XBMC->Log(LOG_DEBUG, "ReadBytes: returning %d\n", length);
  return length;
}

int CircularBuffer::AdjustBytes(int delta)
{ 
  XBMC->Log(LOG_DEBUG, "AdjustBytes(%d): before: %d [%d]\n", delta, m_iReadPos, m_iBytes);
  m_iReadPos += delta;
  if (m_iReadPos < 0)
    m_iReadPos += m_iSize;
  if (m_iReadPos > m_iSize)
    m_iReadPos -= m_iSize;
  m_iBytes -= delta; 
  XBMC->Log(LOG_DEBUG, "AdjustBytes(%d): after: %d [%d]\n", delta, m_iReadPos, m_iBytes);
  return m_iBytes; 
}
