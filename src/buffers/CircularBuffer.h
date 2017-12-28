#pragma once
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
// Dead simple circular buffer
//

#include "../client.h"

namespace timeshift {

  class CircularBuffer {
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
