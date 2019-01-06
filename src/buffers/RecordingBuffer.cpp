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

using namespace timeshift;

PVR_ERROR RecordingBuffer::GetStreamTimes(PVR_STREAM_TIMES *stimes)
{
  stimes->startTime = 0;
  stimes->ptsStart = 0;
  stimes->ptsBegin = 0;
  stimes->ptsEnd = ((int64_t ) Duration() ) * DVD_TIME_BASE;
  XBMC->Log(LOG_DEBUG, "RecordingBuffer::GetStreamTimes called!");
  return PVR_ERROR_NO_ERROR;
}

int RecordingBuffer::Duration(void)
{
  if (m_isRecording)
  {
    time_t endTime =  time(nullptr);
    int diff = (int) (endTime - m_startTime - 10);
    if (diff > 0)
    {
      return diff;
    }
    else
    {
      return 0;
    }
  }
  else
  {
    return m_Duration;
  }
}

bool RecordingBuffer::Open(const std::string inputUrl,const PVR_RECORDING &recording)
{
  m_Duration = recording.iDuration;
  if (recording.iDuration + recording.recordingTime > time(nullptr))
  {
    m_startTime = recording.recordingTime;
    XBMC->Log(LOG_DEBUG, "RecordingBuffer::Open In Progress %d %lld", recording.iDuration, recording.recordingTime);
    m_isRecording = true;
  }
  else
  {
    m_isRecording = false;
  }

  return Buffer::Open(inputUrl);
}

int RecordingBuffer::Read(byte *buffer, size_t length)
{
  int dataRead = (int) XBMC->ReadFile(m_inputHandle, buffer, length);
  if (dataRead==0 && m_isRecording)
  {
    XBMC->Log(LOG_DEBUG, "%s:%d: %lld %lld", __FUNCTION__, __LINE__, XBMC->GetFileLength(m_inputHandle) ,XBMC->GetFilePosition(m_inputHandle));
    if (XBMC->GetFileLength(m_inputHandle) == XBMC->GetFilePosition(m_inputHandle))
    {
      int64_t where = XBMC->GetFileLength(m_inputHandle);
      Seek(where - length,SEEK_SET);
      Seek(where,SEEK_SET);
      if (where != Length())
      {
        XBMC->Log(LOG_INFO, "%s:%d: Before %lld After %lld", __FUNCTION__, __LINE__, where, Length());
      }
    }
  }
  return dataRead;
}
