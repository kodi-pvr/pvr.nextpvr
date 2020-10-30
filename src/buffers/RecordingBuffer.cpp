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

PVR_ERROR RecordingBuffer::GetStreamTimes(kodi::addon::PVRStreamTimes& stimes)
{
  stimes.SetStartTime(0);
  stimes.SetPTSStart(0);
  stimes.SetPTSBegin(0);
  stimes.SetPTSEnd(static_cast<int64_t>(Duration()) * STREAM_TIME_BASE);
  return PVR_ERROR_NO_ERROR;
}

int RecordingBuffer::Duration(void)
{
  if (m_recordingTime)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    int diff = static_cast<int>(time(nullptr) - m_recordingTime) - 15;
    if (diff > 0)
    {
      m_isLive = true;
      diff += 15;
    }
    else
    {
      m_isLive = false;
      diff = 0;
    }
    return diff;
  }
  else
  {
    return m_Duration;
  }
}

bool RecordingBuffer::Open(const std::string inputUrl, const kodi::addon::PVRRecording& recording)
{
  m_Duration = recording.GetDuration();

  kodi::Log(ADDON_LOG_DEBUG, "RecordingBuffer::Open In Progress %d %lld", recording.GetDuration(), recording.GetRecordingTime());
  if (recording.GetDuration() + recording.GetRecordingTime() > time(nullptr))
  {
    m_recordingTime = recording.GetRecordingTime() + m_settings.m_serverTimeOffset;
    m_isLive = true;
  }
  else
  {
    m_recordingTime = 0;
    m_isLive = false;
  }
  m_recordingURL = inputUrl;
  if (!recording.GetDirectory().empty() && m_isLive == false)
  {
    std::string kodiDirectory = recording.GetDirectory();
    kodi::tools::StringUtils::Replace(kodiDirectory, '\\', '/');
    if (kodi::tools::StringUtils::StartsWith(kodiDirectory, "//"))
    {
      kodiDirectory = "smb:" + kodiDirectory;
    }
    if ( kodi::vfs::FileExists(kodiDirectory))
    {
      m_recordingURL = kodiDirectory;
    }
  }
  return Buffer::Open(m_recordingURL, ADDON_READ_NO_CACHE);
}

ssize_t RecordingBuffer::Read(byte *buffer, size_t length)
{
  if (m_recordingTime)
    std::unique_lock<std::mutex> lock(m_mutex);
  ssize_t dataRead = (int) m_inputHandle.Read(buffer, length);
  if (dataRead == 0 && m_isLive)
  {
    kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %lld %lld", __FUNCTION__, __LINE__, m_inputHandle.GetLength() , m_inputHandle.GetPosition());
    const int64_t position = m_inputHandle.GetPosition();
    const time_t startTime = time(nullptr);
    do {
      Buffer::Close();
      std::this_thread::sleep_for(std::chrono::milliseconds(2000));
      Buffer::Open(m_recordingURL);
      Seek(position, 0);
      dataRead = m_inputHandle.Read(buffer, length);
    } while (dataRead == 0 && time(nullptr) - startTime < 5);
    kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %lld %lld", __FUNCTION__, __LINE__, m_inputHandle.GetLength() , m_inputHandle.GetPosition());
  }
  return dataRead;
}
