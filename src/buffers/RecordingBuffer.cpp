/*
 *  Copyright (C) 2015-2020 Team Kodi
 *  Copyright (C) 2015 Sam Stenvall
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
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
  if (m_recordingTime)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    time_t endTime =  time(nullptr);
    int diff = (int) (endTime - m_recordingTime -10);
    if (diff > 0)
      {
      int64_t bps = XBMC->GetFileLength(m_inputHandle) / diff;
      if ((XBMC->GetFileLength(m_inputHandle) - XBMC->GetFilePosition(m_inputHandle)) * bps < 10)
      {
        m_isLive = false;
      }
      else
      {
        m_isLive = true;
      }
      return diff;
    }
    else
    {
      m_isLive = false;
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
  XBMC->Log(LOG_DEBUG, "RecordingBuffer::Open In Progress %d %lld", recording.iDuration, recording.recordingTime);
  if (recording.iDuration + recording.recordingTime > time(nullptr))
  {
    m_recordingTime = recording.recordingTime + g_ServerTimeOffset;
    XBMC->Log(LOG_DEBUG, "RecordingBuffer::Open In Progress %d %lld", recording.iDuration, recording.recordingTime);
    m_isLive = true;
  }
  else
  {
    m_recordingTime = 0;
    m_isLive = false;
  }
  m_recordingURL = inputUrl;
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
      //m_recordingURL = strDirectory;
    }
  }
  return Buffer::Open(m_recordingURL,0);
}

int RecordingBuffer::Read(byte *buffer, size_t length)
{
  if (m_recordingTime)
    std::unique_lock<std::mutex> lock(m_mutex);
  int dataRead = (int) XBMC->ReadFile(m_inputHandle, buffer, length);
  if (dataRead == 0 && m_isLive)
  {
    XBMC->Log(LOG_DEBUG, "%s:%d: %lld %lld", __FUNCTION__, __LINE__, XBMC->GetFileLength(m_inputHandle) ,XBMC->GetFilePosition(m_inputHandle));
    int64_t position = XBMC->GetFilePosition(m_inputHandle);
    Buffer::Close();
    Buffer::Open(m_recordingURL,0);
    Seek(position,0);
    dataRead = (int) XBMC->ReadFile(m_inputHandle, buffer, length);
    XBMC->Log(LOG_DEBUG, "%s:%d: %lld %lld", __FUNCTION__, __LINE__, XBMC->GetFileLength(m_inputHandle) ,XBMC->GetFilePosition(m_inputHandle));
  }
  return dataRead;
}
