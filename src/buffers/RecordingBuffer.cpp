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
  return PVR_ERROR_NO_ERROR;
}

int RecordingBuffer::Duration(void)
{
  if (m_isRecording.load())
  {
    time_t endTime =  time(nullptr);
    int diff = (int) (endTime - m_startTime);
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
  if (!XBMC->GetSetting("chunkrecording", &m_chunkSize))
  {
    m_chunkSize = 32;
  }
  if (recording.iDuration + recording.recordingTime > time(nullptr))
  {
    m_startTime = recording.recordingTime;
    XBMC->Log(LOG_DEBUG, "RecordingBuffer::Open In Progress %d %lld", recording.iDuration, recording.recordingTime);
    m_isRecording.store(true);
  }
  else
  {
    m_isRecording.store(false);
  }
  if (recording.strDirectory[0] != 0)
  {
    char strDirectory [PVR_ADDON_URL_STRING_LENGTH];
    strcpy(strDirectory,recording.strDirectory);
    int i = 0;
    int j = 0;
    for(; i <= strlen(recording.strDirectory); i++, j++)
    {
      if (recording.strDirectory[i] == '\\')
      {
        if (i==0 && recording.strDirectory[1] == '\\')
        {
          strcpy(strDirectory,"smb://");
          i = 1;
          j = 5;
        }
        else
        {
          strDirectory[j] = '/';
        }
      }
      else
      {
          strDirectory[j] = recording.strDirectory[i];
      }
    }
    if ( XBMC->FileExists(strDirectory,false))
    {
      XBMC->Log(LOG_DEBUG, "Native playback %s", strDirectory);
        return Buffer::Open(std::string(strDirectory),0);
    }
  }
  return Buffer::Open(inputUrl,0);
}

int RecordingBuffer::Read(byte *buffer, size_t length)
{
  int dataRead = (int) XBMC->ReadFile(m_inputHandle, buffer, length);
  if (dataRead==0 && m_isRecording.load())
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
