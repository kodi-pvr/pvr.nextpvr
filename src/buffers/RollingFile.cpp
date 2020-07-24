/*
 *  Copyright (C) 2015-2020 Team Kodi
 *  Copyright (C) 2015 Sam Stenvall
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "RollingFile.h"
#include  "../BackendRequest.h"
#include  "../pvrclient-nextpvr.h"
#include "../utilities/XMLUtils.h"
#include <regex>
#include <mutex>
#include "tinyxml.h"

//#define TESTURL "d:/downloads/abc.ts"

using namespace NextPVR::utilities;
using namespace timeshift;

/* Rolling File mode functions */

bool RollingFile::Open(const std::string inputUrl)
{
  m_isPaused = false;
  m_nextLease = 0;
  m_nextStreamInfo = 0;
  m_nextRoll = 0;
  m_complete = false;
  m_isRadio = g_pvrclient->IsRadio();

  m_stream_duration = 0;
  m_bytesPerSecond = 0;
  m_activeFilename.clear();
  m_isLive = true;

  slipFiles.clear();
  std::stringstream ss;

  ss << inputUrl ;//<< "|connection-timeout=" << 15;
  if (ss.str().find("&epgmode=true") != std::string::npos)
  {
    m_isEpgBased = true;
  }
  else
  {
    m_isEpgBased = false;
  }
  if (!m_slipHandle.OpenFile(ss.str(), ADDON_READ_NO_CACHE))
  {
    kodi::Log(ADDON_LOG_ERROR, "Could not open slipHandle file");
    return false;
  }
  int waitTime = 0;
  if (m_isRadio == false)
  {
    waitTime = m_prebuffer;
  }
  do
  {
    // epgmode=true requires a 10 second pause changing channels
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    waitTime--;
    if ( RollingFile::GetStreamInfo())
    {
      m_lastClose = 0;
    }
  }  while ((m_lastClose + 10) > time(nullptr));

  if ( !RollingFile::GetStreamInfo())
  {
    kodi::Log(ADDON_LOG_ERROR, "Could not read rolling file");
    return false;
  }
  m_rollingStartSeconds = m_streamStart = time(nullptr);
  kodi::Log(ADDON_LOG_DEBUG, "RollingFile::Open in Rolling File Mode: %d", m_isEpgBased);
  m_activeFilename = slipFiles.back().filename;
  m_activeLength = -1;
  m_isLeaseRunning = true;
  m_leaseThread = std::thread([this]()
  {
    LeaseWorker();
  });

  while (m_stream_length < waitTime)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    RollingFile::GetStreamInfo();
  };
  return  RollingFile::RollingFileOpen();
}

bool RollingFile::RollingFileOpen()
{
  kodi::addon::PVRRecording recording;
  recording.SetRecordingTime(time(nullptr));
  recording.SetDuration(5 * 60 * 60);
  #if !defined(TESTURL)
  recording.SetDirectory(m_activeFilename);
  #endif

  #if defined(TESTURL)
  const std::string URL = TESTURL;
  #else
  std::string URL = StringUtils::Format("http://%s:%d/stream?f=%s&mode=http&sid=%s", m_settings.m_hostname.c_str(), m_settings.m_port, UriEncode(m_activeFilename).c_str(), m_request.getSID());
  if (m_isRadio && m_activeLength == -1)
  {
    // reduce buffer for radio when playing in-progess slip file
    URL += "&bufsize=32768&wait=true";
  }
  #endif
  return RecordingBuffer::Open(URL.c_str(), recording);
}

bool RollingFile::GetStreamInfo()
{
  enum infoReturns
  {
    OK,
    XML_PARSE,
    HTTP_ERROR
  };
  int64_t  stream_length;
  int64_t duration;
  infoReturns infoReturn;
  infoReturn = HTTP_ERROR;
  std::string response;

  if (m_nextRoll == LLONG_MAX)
  {
    kodi::Log(ADDON_LOG_ERROR, "NextPVR not updating completed rolling file");
    return ( m_stream_length != 0 );
  }
  if (m_request.DoRequest("/services/service?method=channel.stream.info", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
    {
      TiXmlElement* filesNode = doc.FirstChildElement("Files");
      if (filesNode != nullptr)
      {
        stream_length = strtoll(filesNode->FirstChildElement("Length")->GetText(), nullptr, 0);
        duration = strtoll(filesNode->FirstChildElement("Duration")->GetText(), nullptr, 0);
        XMLUtils::GetBoolean(filesNode, "Complete", m_complete);
        kodi::Log(ADDON_LOG_DEBUG, "channel.stream.info %lld %lld %d %d", stream_length, duration, m_complete, m_bytesPerSecond.load());
        if (m_complete == true)
        {
          if ( slipFiles.empty() )
          {
            return false;
          }
          m_stream_length = stream_length-500000;
          slipFiles.back().length = stream_length - slipFiles.back().offset;
          m_nextStreamInfo = m_nextRoll = LLONG_MAX;
          return true;
        }

        infoReturn = OK;
        if (duration!=0)
        {
          m_bytesPerSecond = stream_length/duration * 1000;
        }
        m_stream_length = stream_length;
        m_stream_duration = duration/1000;
        TiXmlElement* pFileNode;
        for( pFileNode = filesNode->FirstChildElement("File"); pFileNode; pFileNode=pFileNode->NextSiblingElement("File"))
        {
          int64_t offset = strtoll(pFileNode->Attribute("offset"), nullptr, 0);

          if (!slipFiles.empty())
          {
            if ( slipFiles.back().offset == offset)
            {
              // already have this file on top
              time_t now = time(nullptr);
              if (now >= m_nextRoll)
              {
                m_nextRoll = now + 1;
              }
              if (slipFiles.size() == 4)
              {
                slipFiles.front().offset = slipFiles.front().offset + stream_length - offset;
                if (!m_isEpgBased)
                {
                  duration = slipFiles.front().seconds - duration;
                  m_rollingStartSeconds = now - m_settings.m_timeshiftBufferSeconds;
                }
              }
              break;
            }
            slipFiles.back().length = offset - slipFiles.back().offset;
            if (m_activeLength == -1)
            {
              m_activeLength = slipFiles.back().length;
            }
          }
          else
          {
            m_activeLength = -1;
          }
          struct slipFile newFile;
          newFile.filename = pFileNode->GetText();
          newFile.offset =  offset;
          newFile.length = -1;
          newFile.seconds = time(nullptr);
          slipFiles.push_back(newFile);
          if (m_isEpgBased)
          {
            std::regex base_regex(".+_20.+_(\\d{4})(\\d{4})\\.ts");
            std::smatch base_match;
            if (std::regex_match(newFile.filename , base_match, base_regex))
            {
              // The first sub_match is the whole string; the next
              // sub_match is the first parenthesized expression.
              if (base_match.size() == 3)
              {
                std::ssub_match base_sub_match = base_match[1];
                int startTime = std::stoi(base_sub_match.str());
                base_sub_match = base_match[2];
                int endTime = std::stoi(base_sub_match.str());
                kodi::Log(ADDON_LOG_DEBUG, "channel.stream.info %d %d", startTime, endTime);
                if (startTime < endTime)
                {
                  m_nextRoll = (time(nullptr) / 60) * 60 + (endTime - startTime) * 60 - 3 + m_settings.m_serverTimeOffset;
                }
                else
                {
                  m_nextRoll = (time(nullptr) / 60) * 60 + (2400 - startTime + endTime) * 60 - 3  + m_settings.m_serverTimeOffset;
                }
              }
            }
            if (m_nextRoll == 0)
            {
              m_isEpgBased = false;
              kodi::Log(ADDON_LOG_DEBUG, "Reset to Time-based %s", newFile.filename.c_str());
            }
          }
          if (!m_isEpgBased)
          {
            m_nextRoll = time(nullptr) + m_settings.m_timeshiftBufferSeconds/3 - 3 + m_settings.m_serverTimeOffset;
          }
          if (slipFiles.size() == 5)
          {
            time_t slipDuration = slipFiles.front().seconds;
            slipFiles.pop_front();
            if (m_isEpgBased)
            {
              slipDuration = slipFiles.front().seconds - slipDuration;
              m_rollingStartSeconds += slipDuration;
            }
            else
            {
              m_rollingStartSeconds = time(nullptr) - m_settings.m_timeshiftBufferSeconds;
            }

          }
          for (auto File : slipFiles )
          {
            kodi::Log(ADDON_LOG_DEBUG, "<Files> %s %lld %lld", File.filename.c_str(), File.offset, File.length);
          }
          break;
        }
      }
    }
  }

  if (infoReturn != OK)
  {
    kodi::Log(ADDON_LOG_ERROR, "NextPVR not updating rolling file %d", infoReturn );
    m_nextStreamInfo = time(nullptr) + 1;
    return false;
  }
  m_nextStreamInfo = time(nullptr) + 10;
  return true;
}
PVR_ERROR RollingFile::GetStreamTimes(kodi::addon::PVRStreamTimes& stimes)
{
  if (m_isLive == false)
    return RecordingBuffer::GetStreamTimes(stimes);
  stimes.SetStartTime(m_streamStart);
  stimes.SetPTSStart(0);
  stimes.SetPTSBegin((m_rollingStartSeconds - m_streamStart) * DVD_TIME_BASE);
  stimes.SetPTSEnd((time(nullptr) - m_streamStart) * DVD_TIME_BASE);
  return PVR_ERROR_NO_ERROR;
}

void RollingFile::Close()
{
  if (m_slipHandle.IsOpen())
  {
    RecordingBuffer::Close();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    m_slipHandle.Close();
    kodi::Log(ADDON_LOG_DEBUG, "%s:%d:", __FUNCTION__, __LINE__);
  }
  m_isLeaseRunning = false;
  if (m_leaseThread.joinable())
    m_leaseThread.join();

  m_lastClose = time(nullptr);
}
int RollingFile::Read(byte *buffer, size_t length)
{
  std::unique_lock<std::mutex> lock(m_mutex);
  bool foundFile = false;
  int dataRead = (int) m_inputHandle.Read(buffer, length);
  if (dataRead == 0)
  {
    RollingFile::GetStreamInfo();
    if (m_inputHandle.GetPosition() == m_activeLength)
    {
      RecordingBuffer::Close();
      for (std::list<slipFile>::reverse_iterator File=slipFiles.rbegin(); File!=slipFiles.rend(); ++File)
      {
        if (File->filename == m_activeFilename)
        {
          foundFile = true;
          if (File==slipFiles.rbegin())
          {
            // still waiting for new filename
            kodi::Log(ADDON_LOG_ERROR, "%s:%d: waiting %s  %s", __FUNCTION__, __LINE__, File->filename.c_str(), m_activeFilename.c_str());
          }
          else
          {
            --File;
            m_activeFilename = File->filename;
            m_activeLength = File->length;
          }
          break;
        }
      }
      if (foundFile == false)
      {
        // file removed from slip file
        m_activeFilename = slipFiles.front().filename;
        m_activeLength = slipFiles.front().length;
      }
      RollingFile::RollingFileOpen();
      dataRead = (int) m_inputHandle.Read(buffer, length);
    }
    else
    {
      while( m_inputHandle.GetPosition() == m_inputHandle.GetLength())
      {
        RollingFile::GetStreamInfo();
        if (m_nextRoll == LLONG_MAX)
        {
          kodi::Log(ADDON_LOG_DEBUG, "should exit %s:%d: %lld %lld %lld", __FUNCTION__, __LINE__, Length(),  m_inputHandle.GetLength() , m_inputHandle.GetPosition());
          return 0;
        }
        kodi::Log(ADDON_LOG_DEBUG, "should exit %s:%d: %lld %lld %lld", __FUNCTION__, __LINE__, Length(),  m_inputHandle.GetLength() , m_inputHandle.GetPosition());
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    }
    kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %d %d %lld %lld", __FUNCTION__, __LINE__, length, dataRead, m_inputHandle.GetLength() , m_inputHandle.GetPosition());
  }
  else if (dataRead < length)
  {
    //kodi::Log(ADDON_LOG_DEBUG, "short read %s:%d: %lld %d", __FUNCTION__, __LINE__, length, dataRead);
  }
  return dataRead;
}

int64_t RollingFile::Seek(int64_t position, int whence)
{
  slipFile prevFile;
  int64_t adjust;
  RollingFile::GetStreamInfo();
  prevFile = slipFiles.front();
  if (slipFiles.back().offset <= position)
  {
    // seek on head
    if ( m_activeFilename != slipFiles.back().filename)
    {
      RecordingBuffer::Close();
      m_activeFilename = slipFiles.back().filename;
      m_activeLength = slipFiles.back().length;
      RollingFile::RollingFileOpen();
    }
    adjust = slipFiles.back().offset;
  }
  else
  {
    for (auto File : slipFiles )
    {
      if (position < File.offset)
      {
        kodi::Log(ADDON_LOG_INFO, "Found slip file %s %lld", prevFile.filename.c_str(), prevFile.offset);
        adjust = prevFile.offset;
        if ( m_activeFilename != prevFile.filename)
        {
          RecordingBuffer::Close();
          m_activeFilename = prevFile.filename;
          m_activeLength = prevFile.length;
          RollingFile::RollingFileOpen();
        }
        break;
      }
      else
      {
        adjust = File.offset;
      }
      prevFile = File;
    }
  }
  if (position-adjust < 0)
  {
    adjust = position;
  }
  int64_t seekval = RecordingBuffer::Seek(position - adjust, whence);
  kodi::Log(ADDON_LOG_DEBUG, "%s:%d: %lld %d %lld", __FUNCTION__, __LINE__, position, adjust, seekval);
  return seekval;
}
