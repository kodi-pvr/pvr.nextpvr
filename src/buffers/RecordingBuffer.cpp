/*
 *  Copyright (C) 2015-2021 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2015 Sam Stenvall
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "../BackendRequest.h"
#include "../utilities/XMLUtils.h"
#include "RecordingBuffer.h"

using namespace NextPVR::utilities;
using namespace timeshift;

PVR_ERROR RecordingBuffer::GetStreamTimes(kodi::addon::PVRStreamTimes& stimes)
{
  stimes.SetStartTime(0);
  stimes.SetPTSStart(0);
  stimes.SetPTSEnd(static_cast<int64_t>(Duration()) * STREAM_TIME_BASE);
  if (CanSeekStream())
    stimes.SetPTSBegin(0);
  else
    stimes.SetPTSBegin(stimes.GetPTSEnd());
  return PVR_ERROR_NO_ERROR;
}

int RecordingBuffer::Duration(void)
{
  if (m_recordingTime)
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    int diff = static_cast<int>(time(nullptr) - m_recordingTime) - 15;
    if (diff > m_Duration)
    {
      tinyxml2::XMLDocument doc;
      if (m_request.DoMethodRequest("recording.list&recording_id=" + m_recordingID, doc) == tinyxml2::XML_SUCCESS)
      {
        tinyxml2::XMLElement* recordingNode = doc.RootElement()->FirstChildElement("recordings")->FirstChildElement("recording");
        std::string status;

        XMLUtils::GetString(recordingNode, "status", status);

        if (status != "Recording")
        {
          diff = m_Duration;
          m_recordingTime = 0;
        }
        else
        {
          m_Duration += 60;
        }
      }
    }
    else if (diff > 0)
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

  kodi::Log(ADDON_LOG_DEBUG, "RecordingBuffer::Open %d %lld", recording.GetDuration(), recording.GetRecordingTime());
  if (recording.GetDuration() + recording.GetRecordingTime() > time(nullptr))
  {
    m_recordingTime = recording.GetRecordingTime() + m_settings->m_serverTimeOffset;
    m_isLive = true;
    m_recordingID = recording.GetRecordingId();
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
