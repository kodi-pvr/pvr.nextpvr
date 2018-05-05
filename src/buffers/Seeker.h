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
#if defined(TARGET_WINDOWS)
#include <algorithm>
#endif
#include "../client.h"
#include "CircularBuffer.h"
#include "session.h"

namespace timeshift {

  class Seeker
  {
  public:
    Seeker(session_data_t *sd, CircularBuffer *cirBuf) : 
      m_pSd(sd), m_cirBuf(cirBuf), m_xStreamOffset(0), m_iBlockOffset(0), m_bSeeking(false), 
      m_bSeekBlockRequested(false), m_bSeekBlockReceived(false) {}
    ~Seeker() {}
    bool InitSeek(int64_t offset, int whence);
    bool Active() { return m_bSeeking; }
    bool BlockRequested() { return m_bSeekBlockRequested; }
    bool PreprocessSeek();
    void ProcessRequests();
    bool PostprocessSeek(int64_t);
    int64_t SeekStreamOffset()  { if (m_bSeeking) return m_xStreamOffset; return -1; }  
    void Clear() { m_xStreamOffset = 0; m_iBlockOffset = 0; m_bSeeking = m_bSeekBlockRequested = m_bSeekBlockReceived = false; }
    
    
  private:
    session_data_t  *m_pSd;
    CircularBuffer  *m_cirBuf;
    int64_t          m_xStreamOffset;
    int32_t          m_iBlockOffset;
    bool             m_bSeeking;
    bool             m_bSeekBlockRequested;
    bool             m_bSeekBlockReceived;
  };
}
