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

#include "RecordingBuffer.h"
//#include <thread>
//#include <condition_variable>
#include <mutex>
#include "session.h"

using namespace ADDON;
namespace timeshift {

  /**
   * Dummy buffer that just passes all calls through to the input file
   * handle without actually buffering anything
   */
  class EpgBasedBuffer : public RecordingBuffer
  {
  private:
    mutable std::mutex m_mutex;
    session_data_t m_sd;
    char m_sid[64];
    //int NextPVR::DoRequest(const char *resource, std::string &response);

  public:
    EpgBasedBuffer() : RecordingBuffer() { XBMC->Log(LOG_NOTICE, "EPG Based Buffer created!");}

    virtual ~EpgBasedBuffer() {}

    virtual bool Open(const std::string inputUrl) override;

    virtual bool CanPauseStream() override;

    virtual void PauseStream(bool bPause) override
    {
      if ((m_sd.isPaused = bPause))
        m_sd.lastPauseAdjust = m_sd.pauseStart = time(nullptr);
      else
        m_sd.lastPauseAdjust = m_sd.pauseStart = 0;
    }
    virtual bool IsTimeshifting() const override
    {
      XBMC->Log(LOG_DEBUG, "%s:%d: %lld %lld", __FUNCTION__, __LINE__, XBMC->GetFileLength(m_inputHandle) ,XBMC->GetFilePosition(m_inputHandle));
      return true;
    }

    virtual bool IsRealTimeStream() const
    {
      return false;
    }

    /*virtual int64_t Length() const
    {
      return 0;
      int64_t length;
      if ( XBMC->GetFileLength(m_inputHandle) == 0 )
      {
          return XBMC->GetFilePosition(m_inputHandle);
          NextPVR::DoRequest(NextPVR::af_inet, NextPVR::pf_inet, NextPVR::sock_stream, NextPVR::tcp)
      }
      return length;
    }*/


    void SetSid(char *sid) override { strcpy(m_sid,sid); }

  };
}
