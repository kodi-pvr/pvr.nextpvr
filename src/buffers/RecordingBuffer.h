#pragma once
/*
*      Copyright (C) 2015 Sam Stenvall
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

#include "Buffer.h"

using namespace ADDON;
namespace timeshift {

  /**
   * Dummy buffer that just passes all calls through to the input file
   * handle without actually buffering anything
   */
  class RecordingBuffer : public Buffer
  {
  private:
    int m_Duration;
    bool m_isRecording;
    time_t m_startTime;

  public:
    RecordingBuffer() : Buffer() { m_Duration = 0; XBMC->Log(LOG_NOTICE, "RecordingBuffer created!"); }
    virtual ~RecordingBuffer() {}
    
    virtual int Read(byte *buffer, size_t length) override;

    virtual int64_t Seek(int64_t position, int whence) override
    {
      return XBMC->SeekFile(m_inputHandle, position, whence);
    }

    virtual bool CanPauseStream() override
    {
      return true;
    }

    virtual bool CanSeekStream() const override
    {
      return true;
    }

    virtual int64_t Position() const override
    {
      return XBMC->GetFilePosition(m_inputHandle);
    }

    virtual int64_t Length() const override
    {
      return XBMC->GetFileLength(m_inputHandle);
    }

    PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES *) override;

    void SetDuration(int duration) { m_Duration = duration; XBMC->Log(LOG_ERROR, "XXXXX Duration set to %d XXXXX", m_Duration); }
    int GetDuration(void) { return m_Duration; XBMC->Log(LOG_ERROR, "XXXXX Duration set to %d XXXXX", m_Duration); }
    int Duration(void);
    bool Open(const std::string inputUrl,const PVR_RECORDING &recording);

  };
}
