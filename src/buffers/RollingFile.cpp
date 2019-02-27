/*
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

#include "RollingFile.h"
#include  "../BackendRequest.h"
#include "Filesystem.h"
#include <regex>
#include <mutex>
#include "tinyxml.h"


#define HTTP_OK 200

#if defined(TARGET_WINDOWS)
#define SLEEP(ms) Sleep(ms)
#else
#define SLEEP(ms) usleep(ms*1000)
#endif

using namespace timeshift;

/* Rolling File mode functions */

bool RollingFile::Open(const std::string inputUrl)
{
  m_sd.isPaused = false;
  m_sd.lastPauseAdjust = 0;
  m_sd.lastBufferTime = 0;
  m_sd.lastKnownLength.store(0);
  m_activeFilename.clear();
  m_isRecording.store(true);
  slipFiles.clear();
  std::stringstream ss;
  m_nextRoll = 0;

  if (g_NowPlaying == TV)
  {
    m_chunkSize = m_liveChunkSize;
  }
  else
    m_chunkSize = 4;

  XBMC->Log(LOG_DEBUG, "%s:%d: %d", __FUNCTION__, __LINE__, m_chunkSize);
  ss << inputUrl << "|connection-timeout=" << 15;
  if (ss.str().find("&epgmode=true") != std::string::npos)
  {
    m_isEpgBased = true;
  }
  else
  {
    m_isEpgBased = false;
  }
  m_slipHandle = XBMC->OpenFile(ss.str().c_str(), READ_NO_CACHE );
  if (m_slipHandle == nullptr)
  {
    XBMC->Log(LOG_ERROR,"Could not open slip file");
    return false;
  }
  SLEEP(500);
  if ( !RollingFile::GetStreamInfo())
  {
    XBMC->Log(LOG_ERROR,"Could not read slip file");
    return false;
  }
  m_rollingBegin = m_slipStart = time(nullptr);
  XBMC->Log(LOG_DEBUG, "RollingFile::Open in Rolling File Mode: %d", m_isEpgBased);
  m_activeFilename = slipFiles.back().filename;
  m_activeLength = -1;
  m_tsbThread = std::thread([this]()
  {
    TSBTimerProc();
  });

  int waitTime = 0;
  if (g_NowPlaying == TV)
  {
    waitTime = m_prebuffer;
  }
  while (m_sd.tsbStart.load() < waitTime)
  {
    SLEEP(500);
    RollingFile::GetStreamInfo();
  };
  return  RollingFile::RollingFileOpen();
}

bool RollingFile::RollingFileOpen()
{
  struct PVR_RECORDING recording;
  recording.recordingTime = time(nullptr);
  recording.iDuration = 5 * 60 * 60;
    memset(recording.strDirectory,0,sizeof(recording.strDirectory));
    #if !defined(TESTURL)
    strcpy(recording.strDirectory, m_activeFilename.c_str());
    #endif

  char strURL[1024];
  #if defined(TESTURL)
    strcpy(strURL,TESTURL);
  #else
  snprintf(strURL,sizeof(strURL),"http://%s:%d/stream?f=%s&sid=%s", g_szHostname.c_str(), g_iPort, UriEncode(m_activeFilename).c_str(), NextPVR::m_backEnd->getSID());
  #endif
  return RecordingBuffer::Open(strURL,recording);
}

void RollingFile::TSBTimerProc(void)
{
  while (m_slipHandle != nullptr)
  {
    time_t now = time(nullptr);
    //XBMC->Log(LOG_DEBUG,"TSB %lld %lld %lld %lld",now,m_nextRoll, m_sd.lastPauseAdjust, m_sd.lastBufferTime  );
    if ( m_sd.lastPauseAdjust <= now )
    {
      std::this_thread::yield();
      std::unique_lock<std::mutex> lock(m_mutex);
      std::string response;
      if (NextPVR::m_backEnd->DoRequest("/service?method=channel.transcode.lease", response) == HTTP_OK)
      {
        m_sd.lastPauseAdjust = now + 7;
      }
      else
      {
        XBMC->Log(LOG_ERROR, "channel.transcode.lease failed %lld", m_sd.lastPauseAdjust );
        m_sd.lastPauseAdjust = now + 1;
      }
    }
    if (m_sd.lastBufferTime <= now || m_nextRoll <= now)
    {
      std::this_thread::yield();
      std::unique_lock<std::mutex> lock(m_mutex);
      RollingFile::GetStreamInfo();
    }
    SLEEP(1000);
  }
}

bool RollingFile::GetStreamInfo()
{
  enum infoReturns
  {
    OK,
    XML_PARSE,
    HTTP_ERROR
  };
  int64_t  length;
  int64_t duration;
  int complete;
  infoReturns infoReturn;
  infoReturn = HTTP_ERROR;
  std::string response;
  if (m_nextRoll == LLONG_MAX)
  {
    XBMC->Log(LOG_ERROR, "NextPVR not updating completed rolling file");
    return true;
  }
  if (NextPVR::m_backEnd->DoRequest("/services/service?method=channel.stream.info", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* filesNode = doc.FirstChildElement("Files");
      if (filesNode != NULL)
      {
        length = strtoll(filesNode->FirstChildElement("Length")->GetText(),nullptr,0);
        duration = strtoll(filesNode->FirstChildElement("Duration")->GetText(),nullptr,0);
        m_sd.tsbStart.store(duration/1000);
        complete = atoi(filesNode->FirstChildElement("Complete")->GetText());
        XBMC->Log(LOG_DEBUG,"channel.stream.info %lld %lld %d %d",length, duration,complete, m_sd.iBytesPerSecond);
        if (complete == 1)
        {
          m_sd.lastKnownLength.store(length);
          slipFiles.back().length = length - slipFiles.back().offset;
          m_sd.lastBufferTime = m_nextRoll = LLONG_MAX;
          return true;
        }

        infoReturn = OK;
        if (duration!=0)
        {
          m_sd.iBytesPerSecond = length/duration * 1000;
        }
        m_sd.lastKnownLength.store(length);
        TiXmlElement* pFileNode;
        for( pFileNode = filesNode->FirstChildElement("File"); pFileNode; pFileNode=pFileNode->NextSiblingElement("File"))
        {
          int64_t offset = strtoll(pFileNode->Attribute("offset"),nullptr,0);
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
                XBMC->Log(LOG_DEBUG,"channel.stream.info %d %d",startTime,endTime);
                if (startTime < endTime)
                {
                  m_nextRoll = (time(nullptr) / 60) * 60 + (endTime - startTime) * 60 - 3 + g_ServerTimeOffset;
                }
                else
                {
                  m_nextRoll = (time(nullptr) / 60) * 60 + (2400 - startTime + endTime) * 60 - 3  + g_ServerTimeOffset;
                }
              }
            }
            if (m_nextRoll == 0)
            {
              m_isEpgBased = false;
              XBMC->Log(LOG_DEBUG,"Reset to Time-based %s",newFile.filename.c_str());
            }
          }
          if (!m_isEpgBased)
          {
            m_nextRoll = time(nullptr) + g_timeShiftBufferSeconds/3 - 3 + g_ServerTimeOffset;
            if (slipFiles.size() == 5)
            {
              slipFiles.pop_front();
              m_rollingBegin += g_timeShiftBufferSeconds/3;
            }
          }
          for (auto File : slipFiles )
          {
            XBMC->Log(LOG_DEBUG,"<Files> %s %lld %lld",File.filename.c_str(),File.offset, File.length);
          }
          break;
        }
      }
    }
  }

  if (infoReturn != OK)
  {
    XBMC->Log(LOG_ERROR, "NextPVR not updating rolling file %d", infoReturn );
    m_sd.lastBufferTime = time(nullptr) + 1;
    return false;
  }
  m_sd.lastBufferTime = time(nullptr) + 10;
  return true;
}
PVR_ERROR RollingFile::GetStreamTimes(PVR_STREAM_TIMES *stimes)
{
  if (m_isRecording.load()==false)
    return RecordingBuffer::GetStreamTimes(stimes);

  stimes->startTime = m_slipStart;
  stimes->ptsStart = 0;
  stimes->ptsBegin = (m_rollingBegin - m_slipStart)  * DVD_TIME_BASE;
  stimes->ptsEnd = (time(nullptr) - m_slipStart) * DVD_TIME_BASE;
  return PVR_ERROR_NO_ERROR;
}

void RollingFile::Close()
{
  if (m_slipHandle != nullptr)
  {
    RecordingBuffer::Close();
    SLEEP(500);
    XBMC->CloseFile(m_slipHandle);
    XBMC->Log(LOG_DEBUG, "%s:%d:", __FUNCTION__, __LINE__);
    m_slipHandle = nullptr;
  }
  if (m_tsbThread.joinable())
    m_tsbThread.join();
}
int RollingFile::Read(byte *buffer, size_t length)
{
  int dataRead = (int) XBMC->ReadFile(m_inputHandle,buffer, length);
  bool foundFile = false;
  if (dataRead == 0)
  {
    RollingFile::GetStreamInfo();
    if (XBMC->GetFilePosition(m_inputHandle) == m_activeLength)
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
            XBMC->Log(LOG_ERROR, "%s:%d: waiting %s  %s", __FUNCTION__, __LINE__,File->filename.c_str(),m_activeFilename.c_str());
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
      dataRead = (int) XBMC->ReadFile(m_inputHandle, buffer, length);
    }
    else
    {
      while( XBMC->GetFilePosition(m_inputHandle) == Length())
      {
        RollingFile::GetStreamInfo();
        if (m_nextRoll == LLONG_MAX)
        {
          XBMC->Log(LOG_DEBUG, "should exit %s:%d: %lld %lld %lld", __FUNCTION__, __LINE__,Length(),  XBMC->GetFileLength(m_inputHandle) ,XBMC->GetFilePosition(m_inputHandle));
          return 0;
        }
        SLEEP(200);
      }
    }
    XBMC->Log(LOG_DEBUG, "%s:%d: %lld %d %lld %lld", __FUNCTION__, __LINE__,length, dataRead, XBMC->GetFileLength(m_inputHandle) ,XBMC->GetFilePosition(m_inputHandle));
  }
  else if (dataRead < length)
  {
    //XBMC->Log(LOG_DEBUG, "short read %s:%d: %lld %d", __FUNCTION__, __LINE__,length, dataRead);
  }
  return dataRead;
}

int64_t RollingFile::Seek(int64_t position, int whence)
{
  slipFile prevFile;
  int64_t adjust;
  RollingFile::GetStreamInfo();
  if (!m_isEpgBased)
  {
    // catch deleted files
    prevFile = slipFiles.front();
  }
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
        XBMC->Log(LOG_INFO,"Found slip file %s %lld",prevFile.filename.c_str(),prevFile.offset);
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
  XBMC->Log(LOG_DEBUG, "%s:%d: %lld %d", __FUNCTION__, __LINE__, position, adjust);
  return RecordingBuffer::Seek(position - adjust,whence);
}
