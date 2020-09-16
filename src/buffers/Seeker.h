/*
 *  Copyright (C) 2017-2020 Team Kodi
 *  Copyright (C) 2017 Mike Burgett
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#pragma once

#if defined(TARGET_WINDOWS)
#include <algorithm>
#endif
#include "CircularBuffer.h"
#include "session.h"

namespace timeshift {

  class ATTRIBUTE_HIDDEN Seeker
  {
  public:
    Seeker(session_data_t *sd, CircularBuffer *cirBuf) :
      m_pSd(sd), m_cirBuf(cirBuf), m_xStreamOffset(0), m_iBlockOffset(0), m_bSeeking(false),
      m_bSeekBlockRequested(false), m_bSeekBlockReceived(false), m_streamPositionSet(false) {}
    ~Seeker() {}
    bool InitSeek(int64_t offset, int whence);
    bool Active() { return m_bSeeking; }
    bool BlockRequested() { return m_bSeekBlockRequested; }
    bool PreprocessSeek();
    void ProcessRequests();
    bool PostprocessSeek(int64_t);
    int64_t SeekStreamOffset()  { if (m_bSeeking) return m_xStreamOffset; return -1; }
    void Clear() { m_xStreamOffset = 0; m_iBlockOffset = 0; m_bSeeking = m_bSeekBlockRequested = m_bSeekBlockReceived = m_streamPositionSet = false; }


  private:
    session_data_t  *m_pSd;
    CircularBuffer  *m_cirBuf;
    int64_t          m_xStreamOffset;
    int32_t          m_iBlockOffset;
    bool             m_bSeeking;
    bool             m_bSeekBlockRequested;
    bool             m_bSeekBlockReceived;
    bool             m_streamPositionSet;

  };
}
