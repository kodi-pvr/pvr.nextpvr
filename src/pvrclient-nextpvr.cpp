/*
 *      Copyright (C) 2005-2011 Team XBMC
 *      http://www.xbmc.org
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <ctime>
#include <stdio.h>
#include <stdlib.h>
#include <memory>

#include <p8-platform/util/StringUtils.h>


#include "client.h"
#include "pvrclient-nextpvr.h"
#include "BackendRequest.h"

#include "md5.h"

#include "tinyxml.h"

#if defined(TARGET_WINDOWS)
  #define atoll(S) _atoi64(S) 
#else
  #define MAXINT64 ULONG_MAX
#endif 

#include <algorithm>


using namespace std;
using namespace ADDON;

/* Globals */
int g_iNextPVRXBMCBuild = 0;
extern bool g_bDownloadGuideArtwork;

/* PVR client version (don't forget to update also the addon.xml and the Changelog.txt files) */
#define PVRCLIENT_NEXTPVR_VERSION_STRING    "1.0.0.0"
#define NEXTPVRC_MIN_VERSION_STRING         "3.6.0"

#define HTTP_OK 200
#define HTTP_NOTFOUND 404
#define HTTP_BADREQUEST 400

#define DEBUGGING_XML 0
#if DEBUGGING_XML
void dump_to_log( TiXmlNode* pParent, unsigned int indent);
#else
#define dump_to_log(x, y)
#endif


#define DEBUGGING_API 0
#if DEBUGGING_API
#define LOG_API_CALL(f) XBMC->Log(LOG_ERROR, "%s:  called!", f)
#define LOG_API_IRET(f,i) XBMC->Log(LOG_ERROR, "%s: returns %d", f, i)
#else
#define LOG_API_CALL(f)
#define LOG_API_IRET(f,i)
#endif

const char SAFE[256] =
{
    /*      0 1 2 3  4 5 6 7  8 9 A B  C D E F */
    /* 0 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* 1 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* 2 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* 3 */ 1,1,1,1, 1,1,1,1, 1,1,0,0, 0,0,0,0,
    
    /* 4 */ 0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
    /* 5 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,0,0,
    /* 6 */ 0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
    /* 7 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,0,0,
    
    /* 8 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* 9 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* A */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* B */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    
    /* C */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* D */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* E */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
    /* F */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
};

std::string UriEncode(const std::string sSrc)
{
  const char DEC2HEX[16 + 1] = "0123456789ABCDEF";
  const unsigned char * pSrc = (const unsigned char *)sSrc.c_str();
  const int SRC_LEN = sSrc.length();
  unsigned char * const pStart = new unsigned char[SRC_LEN * 3];
  unsigned char * pEnd = pStart;
  const unsigned char * const SRC_END = pSrc + SRC_LEN;

  for (; pSrc < SRC_END; ++pSrc)
  {
    if (SAFE[*pSrc]) 
    {
      *pEnd++ = *pSrc;
    }
    else
    {
      // escape this char
      *pEnd++ = '%';
      *pEnd++ = DEC2HEX[*pSrc >> 4];
      *pEnd++ = DEC2HEX[*pSrc & 0x0F];
    }
  }

  std::string sResult((char *)pStart, (char *)pEnd);
  delete [] pStart;
  return sResult;
}



/************************************************************/
/** Class interface */

cPVRClientNextPVR::cPVRClientNextPVR()
{
  m_iCurrentChannel        = -1;
  m_tcpclient              = new NextPVR::Socket(NextPVR::af_inet, NextPVR::pf_inet, NextPVR::sock_stream, NextPVR::tcp);
  m_streamingclient        = new NextPVR::Socket(NextPVR::af_inet, NextPVR::pf_inet, NextPVR::sock_stream, NextPVR::tcp);
  m_bConnected             = false;
  m_iChannelCount          = 0;
  m_currentRecordingLength = 0;

  m_supportsLiveTimeshift  = false;
  m_currentLiveLength      = 0;
  m_currentLivePosition    = 0;

  m_defaultLimit = NEXTPVR_LIMIT_ASMANY;
  m_defaultShowType = NEXTPVR_SHOWTYPE_ANY;

  m_lastRecordingUpdateTime = MAXINT64;  // time of last recording check - force forever
  m_timeshiftBuffer = new timeshift::DummyBuffer();
  m_recordingBuffer = new timeshift::RecordingBuffer();
	m_radioBuffer = new timeshift::DummyBuffer();
  
  CreateThread(false);
}

cPVRClientNextPVR::~cPVRClientNextPVR()
{
  StopThread();

  XBMC->Log(LOG_DEBUG, "->~cPVRClientNextPVR()");
  if (m_bConnected)
    Disconnect();
  SAFE_DELETE(m_tcpclient);  
}

std::vector<std::string> cPVRClientNextPVR::split(const std::string& s, const std::string& delim, const bool keep_empty)
{
  std::vector<std::string> result;
  if (delim.empty()) 
  {
    result.push_back(s);
    return result;
  }
  std::string::const_iterator substart = s.begin(), subend;
  while (true)
  {
    subend = search(substart, s.end(), delim.begin(), delim.end());
    std::string temp(substart, subend);
    if (keep_empty || !temp.empty())
    {
      result.push_back(temp);
    }
    if (subend == s.end())
    {
      break;
    }
    substart = subend + delim.size();
  }
  return result;
}

bool cPVRClientNextPVR::Connect()
{
  string result;

  // initiate session
  std::string response;
  if (DoRequest("/service?method=session.initiate&ver=1.0&device=xbmc", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* saltNode = doc.RootElement()->FirstChildElement("salt");
      TiXmlElement* sidNode = doc.RootElement()->FirstChildElement("sid");

      if (saltNode != NULL && sidNode != NULL)
      {
        // extract and store sid
        PVR_STRCPY(m_sid, sidNode->FirstChild()->Value());

        // extract salt
        char salt[64];
        PVR_STRCPY(salt, saltNode->FirstChild()->Value());

        // a bit of debug
        XBMC->Log(LOG_DEBUG, "session.initiate returns: sid=%s salt=%s", m_sid, salt);
        

        std::string pinMD5 = PVRXBMC::XBMC_MD5::GetMD5(g_szPin);
        StringUtils::ToLower(pinMD5);

        // calculate combined MD5
        std::string combinedMD5;
        combinedMD5.append(":");
        combinedMD5.append(pinMD5);
        combinedMD5.append(":");
        combinedMD5.append(salt);

        // get digest
        std::string md5 = PVRXBMC::XBMC_MD5::GetMD5(combinedMD5);

        // login session
        std::string loginResponse;
        char request[512];
        sprintf(request, "/service?method=session.login&sid=%s&md5=%s", m_sid, md5.c_str());
        if (DoRequest(request, loginResponse) == HTTP_OK)
        {
          if (strstr(loginResponse.c_str(), "<rsp stat=\"ok\">"))
          {
            // check server version
            std::string settings;
            if (DoRequest("/service?method=setting.list", settings) == HTTP_OK)
            {
              // if it's a NextPVR server, check the verions. WinTV Extend servers work a slightly different way.
              TiXmlDocument settingsDoc;
              if (settingsDoc.Parse(settings.c_str()) != NULL)
              {
                //XBMC->Log(LOG_NOTICE, "Settings:\n");
                //dump_to_log(&settingsDoc, 0);
                TiXmlElement* versionNode = settingsDoc.RootElement()->FirstChildElement("NextPVRVersion");
                if (versionNode == NULL)
                {
                  // WinTV Extend server
                }
                else 
                {
                  // NextPVR server
                  int version = atoi(versionNode->FirstChild()->Value());
                  XBMC->Log(LOG_DEBUG, "NextPVR version: %d", version);

                  // is the server new enough
                  if (version < 30600)
                  {
                    XBMC->Log(LOG_ERROR, "Your NextPVR version '%d' is too old. Please upgrade to '%s' or higher!", version, NEXTPVRC_MIN_VERSION_STRING);
                    XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30050));
                    XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30051), NEXTPVRC_MIN_VERSION_STRING);
                    return false;
                  }
                }
                TiXmlElement* liveTimeshiftNode = settingsDoc.RootElement()->FirstChildElement("LiveTimeshift");
                if (liveTimeshiftNode != NULL)
                {
                  m_supportsLiveTimeshift = true;
									g_timeShiftBufferSeconds = atoi(settingsDoc.RootElement()->FirstChildElement("SlipSeconds")->FirstChild()->Value());
									XBMC->Log(LOG_NOTICE, "time shift buffer in seconds == %d\n", g_timeShiftBufferSeconds);
									if (g_livestreamingmethod == Timeshift)
                  {
                    XBMC->Log(LOG_NOTICE, "Timeshift is true!!");
                    delete m_timeshiftBuffer;
                    m_timeshiftBuffer = new timeshift::TimeshiftBuffer();
                  }
									else if (g_livestreamingmethod == EPG_Based)
									{
										XBMC->Log(LOG_NOTICE, "EPG Based Buffering");
										delete m_timeshiftBuffer;
										m_timeshiftBuffer = new timeshift::EpgBasedBuffer();
										m_timeshiftBuffer->SetSid(m_sid);
									}


                }

                // load padding defaults
                m_iDefaultPrePadding = 1;
                m_iDefaultPostPadding = 2;
                if ( settingsDoc.RootElement()->FirstChildElement("PrePadding") != NULL &&  settingsDoc.RootElement()->FirstChildElement("PrePadding")->FirstChild() != NULL)
                {
                  m_iDefaultPrePadding = atoi(settingsDoc.RootElement()->FirstChildElement("PrePadding")->FirstChild()->Value());
                  m_iDefaultPostPadding = atoi( settingsDoc.RootElement()->FirstChildElement("PostPadding")->FirstChild()->Value());
                }
                
                if ( settingsDoc.RootElement()->FirstChildElement("RecordingDirectories") != NULL &&  settingsDoc.RootElement()->FirstChildElement("RecordingDirectories")->FirstChild() != NULL)
                {
                  vector<std::string> directories = split(settingsDoc.RootElement()->FirstChildElement("RecordingDirectories")->FirstChild()->Value(), ",", false);
                  for (size_t i = 0; i < directories.size(); i++)
                  {
                    m_recordingDirectories.push_back(directories[i]);
                  }
                }
              }
            }

            m_bConnected = true;
            XBMC->Log(LOG_DEBUG, "session.login successful");
            return true;
          }
          else
          {
            XBMC->Log(LOG_DEBUG, "session.login failed");
            XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30052));
            m_bConnected = false;
          }
        }
      }
    }  
  }

  return false;
}

void cPVRClientNextPVR::Disconnect()
{
  string result;

  m_bConnected = false;
}

/* IsUp()
 * \brief   Check if we have a valid session to nextpvr
 * \return  True when a session is active
 */
bool cPVRClientNextPVR::IsUp()
{
  // check time since last time Recordings were updated, update if it has been awhile
  if (m_bConnected == true && m_lastRecordingUpdateTime != MAXINT64 &&  time(0) > (m_lastRecordingUpdateTime + 60 ))
  {
    TiXmlDocument doc;
    char request[512];
    sprintf(request, "/service?method=recording.lastupdated");
    std::string response;
    if (DoRequest(request, response) == HTTP_OK)
    {
      if (doc.Parse(response.c_str()) != NULL)
      {
        TiXmlElement* last_update = doc.RootElement()->FirstChildElement("last_update");
        if (last_update != NULL)
        {
          int64_t update_time = atoll(last_update->GetText());
          if (update_time > m_lastRecordingUpdateTime)
          {
            m_lastRecordingUpdateTime = MAXINT64;
            PVR->TriggerRecordingUpdate();
            PVR->TriggerTimerUpdate();
          }
          else
          {            
            m_lastRecordingUpdateTime = time(0);
          }
        }
        else
        {
          m_lastRecordingUpdateTime = MAXINT64;
        }
      }
    }
    else
    {
      m_lastRecordingUpdateTime = time(0);
    }
  }
  return m_bConnected;
}

void *cPVRClientNextPVR::Process(void)
{
  while (!IsStopped())
  {    
    IsUp();
    Sleep(2500);
  }
  return NULL;
}

/************************************************************/
/** General handling */

// Used among others for the server name string in the "Recordings" view
const char* cPVRClientNextPVR::GetBackendName(void)
{
  LOG_API_CALL(__FUNCTION__);
  if (!m_bConnected)
  {
    return g_szHostname.c_str();
  }

  XBMC->Log(LOG_DEBUG, "->GetBackendName()");
  
  if (m_BackendName.length() == 0)
  {
    m_BackendName = "NextPVR  (";
    m_BackendName += g_szHostname.c_str();
    m_BackendName += ")";
  }

  return m_BackendName.c_str();
}

const char* cPVRClientNextPVR::GetBackendVersion(void)
{
  LOG_API_CALL(__FUNCTION__);
  if (!IsUp())
    return "0.0";

  return "1.0";
}

const char* cPVRClientNextPVR::GetConnectionString(void)
{
  static std::string strConnectionString = "connected";
  return strConnectionString.c_str();
}

PVR_ERROR cPVRClientNextPVR::GetDriveSpace(long long *iTotal, long long *iUsed)
{

  string result;
  vector<string> fields;

  *iTotal = 0;
  *iUsed = 0;

  if (!IsUp())
    return PVR_ERROR_SERVER_ERROR;

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientNextPVR::GetBackendTime(time_t *localTime, int *gmtOffset)
{
  LOG_API_CALL(__FUNCTION__);
  if (!IsUp())
    return PVR_ERROR_SERVER_ERROR;

  return PVR_ERROR_NO_ERROR;
}

/************************************************************/
/** EPG handling */

PVR_ERROR cPVRClientNextPVR::GetEpg(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  EPG_TAG broadcast;

  std::string response;
  char request[512];
  LOG_API_CALL(__FUNCTION__);
  sprintf(request, "/service?method=channel.listings&channel_id=%d&start=%d&end=%d", channel.iUniqueId, (int)iStart, (int)iEnd);
  if (DoRequest(request, response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* listingsNode = doc.RootElement()->FirstChildElement("listings");
      TiXmlElement* pListingNode;
      for( pListingNode = listingsNode->FirstChildElement("l"); pListingNode; pListingNode=pListingNode->NextSiblingElement())
      {
        memset(&broadcast, 0, sizeof(EPG_TAG));

        char title[128];
        char description[1024];

        strncpy(title, pListingNode->FirstChildElement("name")->FirstChild()->Value(), sizeof title);
        if (pListingNode->FirstChildElement("description") != NULL && pListingNode->FirstChildElement("description")->FirstChild() != NULL)
        {
          PVR_STRCPY(description, pListingNode->FirstChildElement("description")->FirstChild()->Value());
        }
        else
        {
          description[0] = '\0';
        }

        char start[32];
        strncpy(start, pListingNode->FirstChildElement("start")->FirstChild()->Value(), sizeof start);
        start[10] = '\0';

        char end[32];
        strncpy(end, pListingNode->FirstChildElement("end")->FirstChild()->Value(), sizeof end);
        end[10] = '\0';

        broadcast.iUniqueBroadcastId  = atoi(pListingNode->FirstChildElement("id")->FirstChild()->Value());
        broadcast.strTitle            = title;
        broadcast.iUniqueChannelId    = channel.iUniqueId;
        broadcast.startTime           = atol(start);
        broadcast.endTime             = atol(end);
        broadcast.strPlotOutline      = NULL; //unused
        broadcast.strPlot             = description;
        broadcast.strOriginalTitle    = NULL; // unused
        broadcast.strCast             = NULL; // unused
        broadcast.strDirector         = NULL; // unused
        broadcast.strWriter           = NULL; // unused
        broadcast.iYear               = 0;    // unused
        broadcast.strIMDBNumber       = NULL; // unused

        // artwork URL 
        char artworkPath[128];
        artworkPath[0] = '\0';
        if (g_bDownloadGuideArtwork)
        {
          snprintf(artworkPath, sizeof(artworkPath), "http://%s:%d/service?method=channel.show.artwork&sid=%s&event_id=%d", g_szHostname.c_str(), g_iPort, m_sid, broadcast.iUniqueBroadcastId);
          broadcast.strIconPath         = artworkPath;
        }

        char genre[128];
        genre[0] = '\0';
        if (pListingNode->FirstChildElement("genre") != NULL && pListingNode->FirstChildElement("genre")->FirstChild() != NULL)
        {
          broadcast.iGenreType          = EPG_GENRE_USE_STRING;
          PVR_STRCPY(genre, pListingNode->FirstChildElement("genre")->FirstChild()->Value());
          broadcast.strGenreDescription = genre;
        }
        else
        {
          // genre type
          if (pListingNode->FirstChildElement("genre_type") != NULL && pListingNode->FirstChildElement("genre_type")->FirstChild() != NULL)
          {
            broadcast.iGenreType  = atoi(pListingNode->FirstChildElement("genre_type")->FirstChild()->Value());
          }

          // genre subtype
          if (pListingNode->FirstChildElement("genre_subtype") != NULL && pListingNode->FirstChildElement("genre_subtype")->FirstChild() != NULL)
          {
            broadcast.iGenreSubType  = atoi(pListingNode->FirstChildElement("genre_subtype")->FirstChild()->Value());
          }
        }

        broadcast.firstAired         = 0;  // unused
        broadcast.iParentalRating    = 0;  // unused
        broadcast.iStarRating        = 0;  // unused
        broadcast.bNotify            = false;
        broadcast.iSeriesNumber      = 0;  // unused
        broadcast.iEpisodeNumber     = 0;  // unused
        broadcast.iEpisodePartNumber = 0;  // unused
        broadcast.strEpisodeName     = ""; // unused

        PVR->TransferEpgEntry(handle, &broadcast);
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}


/************************************************************/
/** Channel handling */

int cPVRClientNextPVR::GetNumChannels(void)
{
  LOG_API_CALL(__FUNCTION__);
  if (m_iChannelCount != 0)
    return m_iChannelCount;


  // need something more optimal, but this will do for now...
  m_iChannelCount = 0;
  std::string response;
  if (DoRequest("/service?method=channel.list", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* channelsNode = doc.RootElement()->FirstChildElement("channels");
      TiXmlElement* pChannelNode;
      for( pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode=pChannelNode->NextSiblingElement())
      {
        m_iChannelCount++;
      }
    }
  }

  return m_iChannelCount;
}

std::string cPVRClientNextPVR::GetChannelIcon(int channelID)
{
  char filename[64];
  LOG_API_CALL(__FUNCTION__);
  snprintf(filename, sizeof(filename), "nextpvr-ch%d.png", channelID);

  std::string iconFilename("special://userdata/addon_data/pvr.nextpvr/");
  iconFilename += filename;

  // do we already have the icon file?
  if (XBMC->FileExists(iconFilename.c_str(), false))
  {        
    return iconFilename;    
  } 

  if (m_tcpclient->create())
  {
    if (m_tcpclient->connect(g_szHostname, g_iPort))
    {
      char line[256];
      sprintf(line, "GET /service?method=channel.icon&channel_id=%d HTTP/1.0\r\n", channelID);
      m_tcpclient->send(line, strlen(line));

      sprintf(line, "Connection: close\r\n");
      m_tcpclient->send(line, strlen(line));

      sprintf(line, "\r\n");
      m_tcpclient->send(line, strlen(line));

      char buf[1024];
      int read = m_tcpclient->receive(buf, sizeof buf, 0);
      if (read > 0)
      {
        void* fileHandle = XBMC->OpenFileForWrite(iconFilename.c_str(), true);
        if (fileHandle != NULL)
        {
          int written = 0;
          // HTTP response header
          for (int i=0; i<read; i++)
          {
            if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n')
            {
              XBMC->WriteFile(fileHandle, (unsigned char *)&buf[i+4], (read - (i + 4)));
            }
          }

          // rest of body
          bool connected = true;
          while (connected)
          {
            char buf[1024];
            int read = m_tcpclient->receive(buf, sizeof buf, 0);

            if (read == 0)
            {
              connected = false;
            }
            else if (read > 0)
            {
              XBMC->WriteFile(fileHandle, (unsigned char *)buf, read);
            }
            else if (read < 0 && written > 0)
            {
              connected = false;
            }
          }

          // close file
          XBMC->CloseFile(fileHandle);
        }
      }
    }
    m_tcpclient->close();
    return iconFilename;
  }


  return "";
}

PVR_ERROR cPVRClientNextPVR::GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  PVR_CHANNEL     tag;
  std::string      stream;  
  LOG_API_CALL(__FUNCTION__);
  
  m_iChannelCount = 0;

  std::string response;
  if (DoRequest("/service?method=channel.list", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      //XBMC->Log(LOG_NOTICE, "Channels:\n");
      //dump_to_log(&doc, 0);

      TiXmlElement* channelsNode = doc.RootElement()->FirstChildElement("channels");
      TiXmlElement* pChannelNode;
      for( pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode=pChannelNode->NextSiblingElement())
      {
        memset(&tag, 0, sizeof(PVR_CHANNEL));
        tag.iUniqueId = atoi(pChannelNode->FirstChildElement("id")->FirstChild()->Value());
        tag.iChannelNumber = atoi(pChannelNode->FirstChildElement("number")->FirstChild()->Value());

        // handle major.minor style subchannels
        if (pChannelNode->FirstChildElement("minor"))
        {
          tag.iSubChannelNumber = atoi(pChannelNode->FirstChildElement("minor")->FirstChild()->Value());
        }

        PVR_STRCPY(tag.strChannelName, pChannelNode->FirstChildElement("name")->FirstChild()->Value());

        // check if we need to download a channel icon
        if (pChannelNode->FirstChildElement("icon"))
        {
          std::string iconFile = GetChannelIcon(tag.iUniqueId);
          if (iconFile.length() > 0)
          {
            PVR_STRCPY(tag.strIconPath, iconFile.c_str());
          }
        }

        PVR_STRCPY(tag.strInputFormat, "video/mp2t");

        // check if it's a radio channel
        tag.bIsRadio = false;
        TiXmlElement* channelTypeNode = pChannelNode->FirstChildElement("type");
        if (strcmp(channelTypeNode->FirstChild()->Value(), "0xa") == 0)
        {
          tag.bIsRadio = true;
        }

        // transfer channel to XBMC
        if ((bRadio && tag.bIsRadio) || (!bRadio && !tag.bIsRadio))
          PVR->TransferChannelEntry(handle, &tag);
        
        m_iChannelCount ++;
      }
    }
  }
  return PVR_ERROR_NO_ERROR;
}

/************************************************************/
/** Channel group handling **/

int cPVRClientNextPVR::GetChannelGroupsAmount(void)
{
  LOG_API_CALL(__FUNCTION__);
  // Not directly possible at the moment
  XBMC->Log(LOG_DEBUG, "GetChannelGroupsAmount");

  int groups = 0;

  std::string response;
  if (DoRequest("/service?method=channel.groups", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* groupsNode = doc.RootElement()->FirstChildElement("groups");
      TiXmlElement* pGroupNode;
      for( pGroupNode = groupsNode->FirstChildElement("group"); pGroupNode; pGroupNode=pGroupNode->NextSiblingElement())
      {
        groups++;
      }
    }
  }

  return groups;
}

PVR_ERROR cPVRClientNextPVR::GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  PVR_CHANNEL_GROUP tag;
  LOG_API_CALL(__FUNCTION__);

  // nextpvr doesn't have a separate concept of radio channel groups
  if (bRadio)
    return PVR_ERROR_NO_ERROR;

  // for tv, use the groups returned by nextpvr
  std::string response;
  if (DoRequest("/service?method=channel.groups", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* groupsNode = doc.RootElement()->FirstChildElement("groups");
      TiXmlElement* pGroupNode;
      for( pGroupNode = groupsNode->FirstChildElement("group"); pGroupNode; pGroupNode=pGroupNode->NextSiblingElement())
      {
        memset(&tag, 0, sizeof(PVR_CHANNEL_GROUP));
        tag.bIsRadio  = false;
        tag.iPosition = 0; // groups default order, unused
        strncpy(tag.strGroupName, pGroupNode->FirstChildElement("name")->FirstChild()->Value(), sizeof tag.strGroupName);

        // tell XBMC about channel, ignoring "All Channels" since xbmc has an built in group with effectively the same function
        if (strcmp(tag.strGroupName, "All Channels") != 0)
        {
          PVR->TransferChannelGroup(handle, &tag);
        }
      }
    }
  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientNextPVR::GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  std::string encodedGroupName = UriEncode(group.strGroupName);
  LOG_API_CALL(__FUNCTION__);

  char request[512];
  sprintf(request, "/service?method=channel.list&group_id=%s", encodedGroupName.c_str());

  std::string response;
  if (DoRequest(request, response) == HTTP_OK)
  {
    PVR_CHANNEL_GROUP_MEMBER tag;

    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* channelsNode = doc.RootElement()->FirstChildElement("channels");
      TiXmlElement* pChannelNode;
      for( pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode=pChannelNode->NextSiblingElement())
      {
        memset(&tag, 0, sizeof(PVR_CHANNEL_GROUP_MEMBER));
        strncpy(tag.strGroupName, group.strGroupName, sizeof(tag.strGroupName));
        tag.iChannelUniqueId = atoi(pChannelNode->FirstChildElement("id")->FirstChild()->Value());
        tag.iChannelNumber = atoi(pChannelNode->FirstChildElement("number")->FirstChild()->Value());

        PVR->TransferChannelGroupMember(handle, &tag);
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}

/************************************************************/
/** Record handling **/

int cPVRClientNextPVR::GetNumRecordings(void)
{
  // need something more optimal, but this will do for now...
  // Return -1 on error.
  int recordingCount = -1;
  LOG_API_CALL(__FUNCTION__);

  std::string response;
  if (DoRequest("/service?method=recording.list&filter=ready", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
      if (recordingsNode != NULL)
      {
        TiXmlElement* pRecordingNode;
        recordingCount = 0;
        for( pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode=pRecordingNode->NextSiblingElement())
        {
          recordingCount++;
        }
      }
    }
  }
  LOG_API_IRET(__FUNCTION__, recordingCount);
  return recordingCount;
}

PVR_ERROR cPVRClientNextPVR::GetRecordings(ADDON_HANDLE handle)
{
  // include already-completed recordings
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  LOG_API_CALL(__FUNCTION__);
  std::string response;
  if (DoRequest("/service?method=recording.list&filter=ready", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      //XBMC->Log(LOG_NOTICE, "Recordings [ready:%d]:\n", __LINE__);
      dump_to_log(&doc, 0);
      PVR_RECORDING   tag;

      TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
      TiXmlElement* pRecordingNode;
      for( pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode=pRecordingNode->NextSiblingElement())
      {
        memset(&tag, 0, sizeof(PVR_RECORDING));
        
        PVR_STRCPY(tag.strRecordingId, pRecordingNode->FirstChildElement("id")->FirstChild()->Value());
        PVR_STRCPY(tag.strTitle, pRecordingNode->FirstChildElement("name")->FirstChild()->Value());
        PVR_STRCPY(tag.strDirectory, pRecordingNode->FirstChildElement("name")->FirstChild()->Value());

        if (pRecordingNode->FirstChildElement("desc") != NULL && pRecordingNode->FirstChildElement("desc")->FirstChild() != NULL)
        {
          PVR_STRCPY(tag.strPlot, pRecordingNode->FirstChildElement("desc")->FirstChild()->Value());
        }

        if (pRecordingNode->FirstChildElement("subtitle") != NULL && pRecordingNode->FirstChildElement("subtitle")->FirstChild() != NULL)
        {
          PVR_STRCPY(tag.strTitle, pRecordingNode->FirstChildElement("subtitle")->FirstChild()->Value());
        }


        tag.recordingTime = atoi(pRecordingNode->FirstChildElement("start_time_ticks")->FirstChild()->Value());
        tag.iDuration = atoi(pRecordingNode->FirstChildElement("duration_seconds")->FirstChild()->Value());

        if (pRecordingNode->FirstChildElement("playback_position") != NULL && pRecordingNode->FirstChildElement("playback_position")->FirstChild() != NULL)
        {
          tag.iLastPlayedPosition = atoi(pRecordingNode->FirstChildElement("playback_position")->FirstChild()->Value());
        }

        if (pRecordingNode->FirstChildElement("epg_event_oid") != NULL && pRecordingNode->FirstChildElement("epg_event_oid")->FirstChild() != NULL)
        {
          tag.iEpgEventId = atoi(pRecordingNode->FirstChildElement("epg_event_oid")->FirstChild()->Value());
          XBMC->Log(LOG_DEBUG, "Setting epg id %s %d", tag.strRecordingId, tag.iEpgEventId);
        }
    
        char artworkPath[512];
        snprintf(artworkPath, sizeof(artworkPath), "http://%s:%d/service?method=recording.artwork&sid=%s&recording_id=%s", g_szHostname.c_str(), g_iPort, m_sid, tag.strRecordingId);
        PVR_STRCPY(tag.strIconPath, artworkPath);
        PVR_STRCPY(tag.strThumbnailPath, artworkPath);

        snprintf(artworkPath, sizeof(artworkPath), "http://%s:%d/service?method=recording.fanart&sid=%s&recording_id=%s", g_szHostname.c_str(), g_iPort, m_sid, tag.strRecordingId);
        PVR_STRCPY(tag.strFanartPath, artworkPath);

        if (pRecordingNode->FirstChildElement("channel_oid") != NULL && pRecordingNode->FirstChildElement("channel_oid")->FirstChild() != NULL)
        {
          tag.iChannelUid = atoi(pRecordingNode->FirstChildElement("channel_oid")->FirstChild()->Value());
          if (tag.iChannelUid == 0)
          {
            tag.iChannelUid = PVR_CHANNEL_INVALID_UID;
          }
        }
        else
        {
          tag.iChannelUid = PVR_CHANNEL_INVALID_UID;
        }

        /* TODO: PVR API 5.1.0: Implement this */
        tag.channelType = PVR_RECORDING_CHANNEL_TYPE_UNKNOWN;

        PVR->TransferRecordingEntry(handle, &tag);
      }
    }
    XBMC->Log(LOG_DEBUG, "Updated recordings %lld", m_lastRecordingUpdateTime);
    // ...and any in-progress recordings
    if (DoRequest("/service?method=recording.list&filter=pending", response) == HTTP_OK)
    {
      TiXmlDocument doc;
      if (doc.Parse(response.c_str()) != NULL)
      {
        //XBMC->Log(LOG_NOTICE, "Recordings [pending:%d]:", __LINE__);
        //dump_to_log(&doc, 0);
        PVR_RECORDING   tag;

        TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
        TiXmlElement* pRecordingNode;
        for( pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode=pRecordingNode->NextSiblingElement())
        {
          memset(&tag, 0, sizeof(PVR_RECORDING));
          
          PVR_STRCPY(tag.strRecordingId, pRecordingNode->FirstChildElement("id")->FirstChild()->Value());
          PVR_STRCPY(tag.strTitle, pRecordingNode->FirstChildElement("name")->FirstChild()->Value());
          PVR_STRCPY(tag.strDirectory, pRecordingNode->FirstChildElement("name")->FirstChild()->Value());

          if (pRecordingNode->FirstChildElement("desc") != NULL && pRecordingNode->FirstChildElement("desc")->FirstChild() != NULL)
          {
            PVR_STRCPY(tag.strPlot, pRecordingNode->FirstChildElement("desc")->FirstChild()->Value());
          }

          tag.recordingTime = atoi(pRecordingNode->FirstChildElement("start_time_ticks")->FirstChild()->Value());
          tag.iDuration = atoi(pRecordingNode->FirstChildElement("duration_seconds")->FirstChild()->Value());

          if (pRecordingNode->FirstChildElement("playback_position") != NULL && pRecordingNode->FirstChildElement("playback_position")->FirstChild() != NULL)
          {
            tag.iLastPlayedPosition = atoi(pRecordingNode->FirstChildElement("playback_position")->FirstChild()->Value());
          }

          if (pRecordingNode->FirstChildElement("channel_oid") != NULL && pRecordingNode->FirstChildElement("channel_oid")->FirstChild() != NULL)
          {
            tag.iChannelUid = atoi(pRecordingNode->FirstChildElement("channel_oid")->FirstChild()->Value());
            if (tag.iChannelUid == 0)
            {
              tag.iChannelUid = PVR_CHANNEL_INVALID_UID;
            }
          }
          else
          {
            tag.iChannelUid = PVR_CHANNEL_INVALID_UID;
          }

          if (pRecordingNode->FirstChildElement("file") != NULL && pRecordingNode->FirstChildElement("file")->FirstChild() != NULL)
          {
            PVR_STRCPY(tag.strDirectory, pRecordingNode->FirstChildElement("file")->FirstChild()->Value());
          }
          /* TODO: PVR API 5.1.0: Implement this */
          tag.channelType = PVR_RECORDING_CHANNEL_TYPE_UNKNOWN;

          if (tag.recordingTime <= time(NULL) && (tag.recordingTime + tag.iDuration) >= time(NULL))
          {
            PVR->TransferRecordingEntry(handle, &tag);
          }
        }
      }
    }
    m_lastRecordingUpdateTime = time(0);
  }
  else
  {
    returnValue = PVR_ERROR_SERVER_ERROR;
  }
  LOG_API_IRET(__FUNCTION__, returnValue);
  return returnValue;
}

PVR_ERROR cPVRClientNextPVR::DeleteRecording(const PVR_RECORDING &recording)
{
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "DeleteRecording");
  char request[512];
  sprintf(request, "/service?method=recording.delete&recording_id=%s", recording.strRecordingId);

  std::string response;
  if (DoRequest(request, response) == HTTP_OK)
  {
    if (strstr(response.c_str(), "<rsp stat=\"ok\">"))
    {
      PVR->TriggerRecordingUpdate();
      XBMC->Log(LOG_DEBUG, "DeleteRecording failed. Returning PVR_ERROR_NO_ERROR");
      return PVR_ERROR_NO_ERROR;
    }
    else
    {
      XBMC->Log(LOG_DEBUG, "DeleteRecording failed");
    }
  }


  XBMC->Log(LOG_DEBUG, "DeleteRecording failed. Returning PVR_ERROR_FAILED");
  return PVR_ERROR_FAILED;
}

PVR_ERROR cPVRClientNextPVR::RenameRecording(const PVR_RECORDING &recording)
{
  LOG_API_CALL(__FUNCTION__);
  if (!IsUp())
    return PVR_ERROR_SERVER_ERROR;

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientNextPVR::SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition)
{
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "SetRecordingLastPlayedPosition");
  char request[512];
  sprintf(request, "/service?method=recording.watched.set&recording_id=%s&position=%d", recording.strRecordingId, lastplayedposition);

  std::string response;
  if (DoRequest(request, response) == HTTP_OK)
  {
    if (strstr(response.c_str(), "<rsp stat=\"ok\">") == NULL)
    {
      XBMC->Log(LOG_DEBUG, "SetRecordingLastPlayedPosition failed");
      return PVR_ERROR_FAILED;
    }
    PVR->TriggerRecordingUpdate();
  }
  return PVR_ERROR_NO_ERROR;
}

int cPVRClientNextPVR::GetRecordingLastPlayedPosition(const PVR_RECORDING &recording)
{
  LOG_API_CALL(__FUNCTION__);
  return recording.iLastPlayedPosition;
}

PVR_ERROR cPVRClientNextPVR::GetRecordingEdl(const PVR_RECORDING& recording, PVR_EDL_ENTRY entries[], int *size)
{
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "GetRecordingEdl");
  char request[512];
  sprintf(request, "/service?method=recording.edl&recording_id=%s", recording.strRecordingId);

  std::string response;
  if (DoRequest(request, response) == HTTP_OK)
  {
    if (strstr(response.c_str(), "<rsp stat=\"ok\">") != NULL)
    {
      TiXmlDocument doc;
      if (doc.Parse(response.c_str()) != NULL)
      {
        int index = 0;
        TiXmlElement* commercialsNode = doc.RootElement()->FirstChildElement("commercials");
        TiXmlElement* pCommercialNode;
        for( pCommercialNode = commercialsNode->FirstChildElement("commercial"); pCommercialNode; pCommercialNode=pCommercialNode->NextSiblingElement())
        {          
          PVR_EDL_ENTRY entry;
          entry.start = atoi(pCommercialNode->FirstChildElement("start")->FirstChild()->Value()) * 1000;
          entry.end = atoi(pCommercialNode->FirstChildElement("end")->FirstChild()->Value()) * 1000 ;
          entry.type = PVR_EDL_TYPE_COMBREAK;
          entries[index] = entry;
          index++;
        }
        *size = index;
        return PVR_ERROR_NO_ERROR;
      }
    }
  }
  return PVR_ERROR_FAILED;
}

/************************************************************/
/** Timer handling */


int cPVRClientNextPVR::GetNumTimers(void)
{
  LOG_API_CALL(__FUNCTION__);
  // Return -1 in case of error.
  int timerCount = -1;
  std::string response;

  // get list of recurring recordings
  if (DoRequest("/service?method=recording.recurring.list", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recurrings");
      if (recordingsNode != NULL)
      {
        TiXmlElement* pRecordingNode;
        timerCount = 0;
        for( pRecordingNode = recordingsNode->FirstChildElement("recurring"); pRecordingNode; pRecordingNode=pRecordingNode->NextSiblingElement())
        {
          timerCount++;
        }
      }
    }
  }


  // get list of pending recordings
  response = "";
  if (DoRequest("/service?method=recording.list&filter=pending", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
      if (recordingsNode != NULL)
      {
        TiXmlElement* pRecordingNode;
        if (timerCount == -1)
          timerCount = 0;
        for( pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode=pRecordingNode->NextSiblingElement())
        {
          timerCount++;
        }
      }
    }
  }
  LOG_API_IRET(__FUNCTION__, timerCount);
  return timerCount;
}

PVR_ERROR cPVRClientNextPVR::GetTimers(ADDON_HANDLE handle)
{
  std::string response;
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  LOG_API_CALL(__FUNCTION__);

  // first add the recurring recordings
  if (DoRequest("/service?method=recording.recurring.list&filter=pending", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      PVR_TIMER tag;
      TiXmlElement* recurringsNode = doc.RootElement()->FirstChildElement("recurrings");
      TiXmlElement* pRecurringNode;
      for( pRecurringNode = recurringsNode->FirstChildElement("recurring"); pRecurringNode; pRecurringNode=pRecurringNode->NextSiblingElement())
      {
        memset(&tag, 0, sizeof(tag));

        TiXmlElement* pMatchRulesNode = pRecurringNode->FirstChildElement("matchrules");// ->FirstChildElement("Rules");
        TiXmlElement* pRulesNode = pMatchRulesNode->FirstChildElement("Rules");// ->FirstChildElement("Rules");
                
        tag.iClientIndex = atoi(pRecurringNode->FirstChildElement("id")->FirstChild()->Value());
        tag.iClientChannelUid = atoi(pRulesNode->FirstChildElement("ChannelOID")->FirstChild()->Value());

        tag.iTimerType = pRulesNode->FirstChildElement("EPGTitle") ? TIMER_REPEATING_EPG : TIMER_REPEATING_MANUAL;

        // start/end time
        if (pRulesNode->FirstChildElement("StartTimeTicks") != NULL)
        {
          tag.startTime = atol(pRulesNode->FirstChildElement("StartTimeTicks")->FirstChild()->Value());
          tag.endTime = atol(pRulesNode->FirstChildElement("EndTimeTicks")->FirstChild()->Value());
        }

        // keyword recordings
        if (pRulesNode->FirstChildElement("AdvancedRules") != NULL)
        {
          std::string advancedRulesText = pRulesNode->FirstChildElement("AdvancedRules")->FirstChild()->Value();
          if (advancedRulesText.find("KEYWORD: ") != string::npos)
          {
            tag.iTimerType = TIMER_REPEATING_KEYWORD;
            tag.startTime = 0;
            tag.endTime = 0;
            tag.bStartAnyTime = true;
            tag.bEndAnyTime = true;
            strncpy(tag.strEpgSearchString, &advancedRulesText.c_str()[9], sizeof(tag.strEpgSearchString) - 1);
          }
        }

        // days
        tag.iWeekdays = PVR_WEEKDAY_ALLDAYS;
        if (pRulesNode->FirstChildElement("Days") != NULL)
        {
          std::string daysText = pRulesNode->FirstChildElement("Days")->FirstChild()->Value();
          tag.iWeekdays = PVR_WEEKDAY_NONE;
          if (daysText.find("SUN") != string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_SUNDAY;
          if (daysText.find("MON") != string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_MONDAY;
          if (daysText.find("TUE") != string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_TUESDAY;
          if (daysText.find("WED") != string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_WEDNESDAY;
          if (daysText.find("THU") != string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_THURSDAY;
          if (daysText.find("FRI") != string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_FRIDAY;
          if (daysText.find("SAT") != string::npos)
            tag.iWeekdays |= PVR_WEEKDAY_SATURDAY;
        }

        // pre/post padding
        if (pRulesNode->FirstChildElement("PrePadding") != NULL)
        {
          tag.iMarginStart = atoi(pRulesNode->FirstChildElement("PrePadding")->FirstChild()->Value());
          tag.iMarginEnd = atoi(pRulesNode->FirstChildElement("PostPadding")->FirstChild()->Value());
        }

        // number of recordings to keep
        if (pRulesNode->FirstChildElement("Keep") != NULL)
        {
          tag.iMaxRecordings = atoi(pRulesNode->FirstChildElement("Keep")->FirstChild()->Value());
        }

        // prevent duplicates
        if (pRulesNode->FirstChildElement("OnlyNewEpisodes") != NULL)
        {
          if (strcmp(pRulesNode->FirstChildElement("OnlyNewEpisodes")->FirstChild()->Value(), "true") == 0)
          {
            tag.iPreventDuplicateEpisodes = 1;
          }
        }

        // recordings directory ID
        if (pRulesNode->FirstChildElement("RecordingDirectoryID") != NULL)
        {
          tag.iRecordingGroup = 0;
          if (pRulesNode->FirstChildElement("RecordingDirectoryID")->FirstChild() != NULL)
          {
            std::string recordingDirectoryID = pRulesNode->FirstChildElement("RecordingDirectoryID")->FirstChild()->Value();
            int i = 0;
            for (auto it = m_recordingDirectories.begin(); it != m_recordingDirectories.end(); ++it, i++)
            {
              std::string bracketed = "[" + m_recordingDirectories[i] + "]";
              if (bracketed == recordingDirectoryID)
              {
                tag.iRecordingGroup = i;
                break;
              }
            }
          }
        }

        char strTitle[PVR_ADDON_NAME_STRING_LENGTH];
        strncpy(strTitle, pRecurringNode->FirstChildElement("name")->FirstChild()->Value(), sizeof(strTitle)-1);
        strncat(tag.strTitle, XBMC->GetLocalizedString(30054), sizeof(tag.strTitle) - 1);
        strncat(tag.strTitle, " ", sizeof(tag.strTitle) - 1);
        strncat(tag.strTitle, strTitle, sizeof(tag.strTitle) - 1);

        tag.state = PVR_TIMER_STATE_SCHEDULED;
        
        PVR_STRCPY(tag.strSummary, "summary");

        // pass timer to xbmc
        PVR->TransferTimerEntry(handle, &tag);
      }
    }
    // next add the one-off recordings.
    response = "";
    if (DoRequest("/service?method=recording.list&filter=pending", response) == HTTP_OK)
    {
      TiXmlDocument doc;
      if (doc.Parse(response.c_str()) != NULL)
      {
        PVR_TIMER tag;

        TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
        TiXmlElement* pRecordingNode;
        for( pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode=pRecordingNode->NextSiblingElement())
        {
          memset(&tag, 0, sizeof(tag));

          tag.iTimerType = pRecordingNode->FirstChildElement("epg_event_oid") ? TIMER_ONCE_EPG : TIMER_ONCE_MANUAL;

          tag.iClientIndex = atoi(pRecordingNode->FirstChildElement("id")->FirstChild()->Value());
          tag.iClientChannelUid = atoi(pRecordingNode->FirstChildElement("channel_id")->FirstChild()->Value());

          if (pRecordingNode->FirstChildElement("recurring_parent") != NULL)
          {
            tag.iParentClientIndex = atoi(pRecordingNode->FirstChildElement("recurring_parent")->FirstChild()->Value());
            if (tag.iParentClientIndex != PVR_TIMER_NO_PARENT)
            {
              if (tag.iTimerType == TIMER_ONCE_EPG)
              {
                tag.iTimerType = TIMER_ONCE_EPG_CHILD;
              }
              else
              {
                tag.iTimerType = TIMER_ONCE_MANUAL_CHILD;
              }
            }
          }

          // pre-padding
          if (pRecordingNode->FirstChildElement("pre_padding") != NULL)
          {
            tag.iMarginStart = atoi(pRecordingNode->FirstChildElement("pre_padding")->FirstChild()->Value());
          }

          // post-padding
          if (pRecordingNode->FirstChildElement("post_padding") != NULL)
          {
            tag.iMarginEnd = atoi(pRecordingNode->FirstChildElement("post_padding")->FirstChild()->Value());
          }

          // name
          PVR_STRCPY(tag.strTitle, pRecordingNode->FirstChildElement("name")->FirstChild()->Value());

          // description
          if (pRecordingNode->FirstChildElement("desc") != NULL && pRecordingNode->FirstChildElement("desc")->FirstChild() != NULL)
          {
            PVR_STRCPY(tag.strSummary, pRecordingNode->FirstChildElement("desc")->FirstChild()->Value());
          }

          tag.state = PVR_TIMER_STATE_SCHEDULED;
          if (pRecordingNode->FirstChildElement("status") != NULL && pRecordingNode->FirstChildElement("status")->FirstChild() != NULL)
          {
            char status[32];
            PVR_STRCPY(status, pRecordingNode->FirstChildElement("status")->FirstChild()->Value());
            if (strcmp(status, "Recording") == 0)
            {
              tag.state = PVR_TIMER_STATE_RECORDING;
            }
          }


          // start/end time
          char start[32];
          strncpy(start, pRecordingNode->FirstChildElement("start_time_ticks")->FirstChild()->Value(), sizeof start);
          start[10] = '\0';
          tag.startTime           = atol(start);
          tag.endTime             = tag.startTime + atoi(pRecordingNode->FirstChildElement("duration_seconds")->FirstChild()->Value());

          // pass timer to xbmc
          PVR->TransferTimerEntry(handle, &tag);
        }
      }
    }
  }
  else
  {
    returnValue = PVR_ERROR_SERVER_ERROR;
  }

  LOG_API_IRET(__FUNCTION__, returnValue);
  return returnValue;  
}

namespace
{
  struct TimerType : PVR_TIMER_TYPE
  {
    TimerType(unsigned int id,
    unsigned int attributes,
    const std::string &description,
    const std::vector< std::pair<int, std::string> > &maxRecordingsValues,
    int maxRecordingsDefault,
    const std::vector< std::pair<int, std::string> > &dupEpisodesValues,
    int dupEpisodesDefault,
    const std::vector< std::pair<int, std::string> > &recordingGroupsValues,
    int recordingGroupDefault
    )
    {
      memset(this, 0, sizeof(PVR_TIMER_TYPE));

      iId = id;
      iAttributes = attributes;
      iMaxRecordingsSize = maxRecordingsValues.size();
      iMaxRecordingsDefault = maxRecordingsDefault;
      iPreventDuplicateEpisodesSize = dupEpisodesValues.size();
      iPreventDuplicateEpisodesDefault = dupEpisodesDefault;
      iRecordingGroupSize = recordingGroupsValues.size();
      iRecordingGroupDefault = recordingGroupDefault;
      strncpy(strDescription, description.c_str(), sizeof(strDescription)-1);

      int i = 0;
      for (auto it = maxRecordingsValues.begin(); it != maxRecordingsValues.end(); ++it, ++i)
      {
        maxRecordings[i].iValue = it->first;
        strncpy(maxRecordings[i].strDescription, it->second.c_str(), sizeof(maxRecordings[i].strDescription) - 1);
      }

      i = 0;
      for (auto it = dupEpisodesValues.begin(); it != dupEpisodesValues.end(); ++it, ++i)
      {
        preventDuplicateEpisodes[i].iValue = it->first;
        strncpy(preventDuplicateEpisodes[i].strDescription, it->second.c_str(), sizeof(preventDuplicateEpisodes[i].strDescription) - 1);
      }

      i = 0;
      for (auto it = recordingGroupsValues.begin(); it != recordingGroupsValues.end(); ++it, ++i)
      {
        recordingGroup[i].iValue = it->first;
        strncpy(recordingGroup[i].strDescription, it->second.c_str(), sizeof(recordingGroup[i].strDescription) - 1);
      }
    }
  };

} // unnamed namespace

PVR_ERROR cPVRClientNextPVR::GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
  LOG_API_CALL(__FUNCTION__);
  static const int MSG_ONETIME_MANUAL = 30140;
  static const int MSG_ONETIME_GUIDE = 30141;
  static const int MSG_REPEATING_MANUAL = 30142;
  static const int MSG_REPEATING_GUIDE = 30143;
  static const int MSG_REPEATING_CHILD = 30144;
  static const int MSG_REPEATING_KEYWORD = 30145;

  static const int MSG_KEEPALL = 30150;
  static const int MSG_KEEP1 = 30151;
  static const int MSG_KEEP2 = 30152;
  static const int MSG_KEEP3 = 30153;
  static const int MSG_KEEP4 = 30154;
  static const int MSG_KEEP5 = 30155;
  static const int MSG_KEEP6 = 30156;
  static const int MSG_KEEP7 = 30157;
  static const int MSG_KEEP10 = 30158;

  static const int MSG_SHOWTYPE_FIRSTRUNONLY = 30160;
  static const int MSG_SHOWTYPE_ANY = 30161;

  /* PVR_Timer.iMaxRecordings values and presentation. */
  static std::vector< std::pair<int, std::string> > recordingLimitValues;
  if (recordingLimitValues.size() == 0)
  {
    recordingLimitValues.push_back(std::make_pair(NEXTPVR_LIMIT_ASMANY, XBMC->GetLocalizedString(MSG_KEEPALL)));
    recordingLimitValues.push_back(std::make_pair(NEXTPVR_LIMIT_1, XBMC->GetLocalizedString(MSG_KEEP1)));
    recordingLimitValues.push_back(std::make_pair(NEXTPVR_LIMIT_2, XBMC->GetLocalizedString(MSG_KEEP2)));
    recordingLimitValues.push_back(std::make_pair(NEXTPVR_LIMIT_3, XBMC->GetLocalizedString(MSG_KEEP3)));
    recordingLimitValues.push_back(std::make_pair(NEXTPVR_LIMIT_4, XBMC->GetLocalizedString(MSG_KEEP4)));
    recordingLimitValues.push_back(std::make_pair(NEXTPVR_LIMIT_5, XBMC->GetLocalizedString(MSG_KEEP5)));
    recordingLimitValues.push_back(std::make_pair(NEXTPVR_LIMIT_6, XBMC->GetLocalizedString(MSG_KEEP6)));
    recordingLimitValues.push_back(std::make_pair(NEXTPVR_LIMIT_7, XBMC->GetLocalizedString(MSG_KEEP7)));
    recordingLimitValues.push_back(std::make_pair(NEXTPVR_LIMIT_10, XBMC->GetLocalizedString(MSG_KEEP10)));
  }

  /* PVR_Timer.iPreventDuplicateEpisodes values and presentation.*/
  static std::vector< std::pair<int, std::string> > showTypeValues;
  if (showTypeValues.size() == 0)
  {
    showTypeValues.push_back(std::make_pair(NEXTPVR_SHOWTYPE_FIRSTRUNONLY, XBMC->GetLocalizedString(MSG_SHOWTYPE_FIRSTRUNONLY)));
    showTypeValues.push_back(std::make_pair(NEXTPVR_SHOWTYPE_ANY, XBMC->GetLocalizedString(MSG_SHOWTYPE_ANY)));
  }

  /* PVR_Timer.iRecordingGroup values and presentation */
  int i = 0; 
  static std::vector< std::pair<int, std::string> > recordingGroupValues;
  for (auto it = m_recordingDirectories.begin(); it != m_recordingDirectories.end(); ++it, i++)
  {
    recordingGroupValues.push_back(std::make_pair(i, m_recordingDirectories[i]));
  }

  static const unsigned int TIMER_MANUAL_ATTRIBS
    = PVR_TIMER_TYPE_IS_MANUAL |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_START_TIME |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;

  static const unsigned int TIMER_EPG_ATTRIBS
    = PVR_TIMER_TYPE_REQUIRES_EPG_TAG_ON_CREATE |
      PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;

  static const unsigned int TIMER_REPEATING_MANUAL_ATTRIBS
    = PVR_TIMER_TYPE_IS_REPEATING |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS |
      PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

  static const unsigned int TIMER_REPEATING_EPG_ATTRIBS
    = PVR_TIMER_TYPE_IS_REPEATING |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN |
      PVR_TIMER_TYPE_SUPPORTS_WEEKDAYS |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES |
      PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

  static const unsigned int TIMER_CHILD_ATTRIBUTES
    = PVR_TIMER_TYPE_SUPPORTS_START_TIME |
      PVR_TIMER_TYPE_SUPPORTS_END_TIME |
      PVR_TIMER_TYPE_FORBIDS_NEW_INSTANCES;

  static const unsigned int TIMER_KEYWORD_ATTRIBS
    = PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_TITLE_EPG_MATCH |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;

  static const unsigned int TIMER_REPEATING_KEYWORD_ATTRIBS
      = PVR_TIMER_TYPE_IS_REPEATING |
      PVR_TIMER_TYPE_SUPPORTS_RECORD_ONLY_NEW_EPISODES |
      PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;


  /* Timer types definition.*/
  static std::vector< std::unique_ptr< TimerType > > timerTypes;
  if (timerTypes.size() == 0)
  {
    timerTypes.push_back(
      /* One-shot manual (time and channel based) */
      std::unique_ptr<TimerType>(new TimerType(
      /* Type id. */
      TIMER_ONCE_MANUAL,
      /* Attributes. */
      TIMER_MANUAL_ATTRIBS,
      /* Description. */
      XBMC->GetLocalizedString(MSG_ONETIME_MANUAL), // "One time (manual)",
      /* Values definitions for attributes. */
      recordingLimitValues, m_defaultLimit,
      showTypeValues, m_defaultShowType,
      recordingGroupValues, 0)));

    timerTypes.push_back(
      /* One-shot epg based */
      std::unique_ptr<TimerType>(new TimerType(
      /* Type id. */
      TIMER_ONCE_EPG,
      /* Attributes. */
      TIMER_EPG_ATTRIBS,
      /* Description. */
      XBMC->GetLocalizedString(MSG_ONETIME_GUIDE), // "One time (guide)",
      /* Values definitions for attributes. */
      recordingLimitValues, m_defaultLimit,
      showTypeValues, m_defaultShowType,
      recordingGroupValues, 0)));

    timerTypes.push_back(
      /* Repeating manual (time and channel based) Parent */
      std::unique_ptr<TimerType>(new TimerType(
      /* Type id. */
      TIMER_REPEATING_MANUAL,
      /* Attributes. */
      TIMER_MANUAL_ATTRIBS | TIMER_REPEATING_MANUAL_ATTRIBS,
      /* Description. */
      XBMC->GetLocalizedString(MSG_REPEATING_MANUAL), // "Repeating (manual)"
      /* Values definitions for attributes. */
      recordingLimitValues, m_defaultLimit,
      showTypeValues, m_defaultShowType,
      recordingGroupValues, 0)));

    timerTypes.push_back(
      /* Repeating epg based Parent*/
      std::unique_ptr<TimerType>(new TimerType(
      /* Type id. */
      TIMER_REPEATING_EPG,
      /* Attributes. */
      TIMER_EPG_ATTRIBS | TIMER_REPEATING_EPG_ATTRIBS,
      /* Description. */
      XBMC->GetLocalizedString(MSG_REPEATING_GUIDE), // "Repeating (guide)"
      /* Values definitions for attributes. */
      recordingLimitValues, m_defaultLimit,
      showTypeValues, m_defaultShowType,
      recordingGroupValues, 0)));

    timerTypes.push_back(
      /* Read-only one-shot for timers generated by timerec */
      std::unique_ptr<TimerType>(new TimerType(
      /* Type id. */
      TIMER_ONCE_MANUAL_CHILD,
      /* Attributes. */
      TIMER_MANUAL_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
      /* Description. */
      XBMC->GetLocalizedString(MSG_REPEATING_CHILD), // "Created by Repeating Timer"
      /* Values definitions for attributes. */
      recordingLimitValues, m_defaultLimit,
      showTypeValues, m_defaultShowType,
      recordingGroupValues, 0)));

    timerTypes.push_back(
      /* Read-only one-shot for timers generated by autorec */
      std::unique_ptr<TimerType>(new TimerType(
      /* Type id. */
      TIMER_ONCE_EPG_CHILD,
      /* Attributes. */
      TIMER_EPG_ATTRIBS | TIMER_CHILD_ATTRIBUTES,
      /* Description. */
      XBMC->GetLocalizedString(MSG_REPEATING_CHILD), // "Created by Repeating Timer"
      /* Values definitions for attributes. */
      recordingLimitValues, m_defaultLimit,
      showTypeValues, m_defaultShowType,
      recordingGroupValues, 0)));

    timerTypes.push_back(
      /* Repeating epg based Parent*/
      std::unique_ptr<TimerType>(new TimerType(
      /* Type id. */
      TIMER_REPEATING_KEYWORD,
      /* Attributes. */
      TIMER_KEYWORD_ATTRIBS | TIMER_REPEATING_KEYWORD_ATTRIBS,
      /* Description. */
      XBMC->GetLocalizedString(MSG_REPEATING_KEYWORD), // "Repeating (keyword)"
      /* Values definitions for attributes. */
      recordingLimitValues, m_defaultLimit,
      showTypeValues, m_defaultShowType,
      recordingGroupValues, 0)));
  }

  /* Copy data to target array. */
  i = 0;
  for (auto it = timerTypes.begin(); it != timerTypes.end(); ++it, ++i)
    types[i] = **it;

  *size = timerTypes.size();

  return PVR_ERROR_NO_ERROR;
}

std::string cPVRClientNextPVR::GetDayString(int dayMask)
{
  std::string days;
  if (dayMask == (PVR_WEEKDAY_SATURDAY | PVR_WEEKDAY_SUNDAY))
  {
    days = "WEEKENDS";
  }
  else if (dayMask == (PVR_WEEKDAY_MONDAY | PVR_WEEKDAY_TUESDAY | PVR_WEEKDAY_WEDNESDAY | PVR_WEEKDAY_THURSDAY | PVR_WEEKDAY_FRIDAY))
  {
    days = "WEEKDAYS";
  } 
  else
  {
    if (dayMask & PVR_WEEKDAY_SATURDAY)
      days += "SAT:";
    if (dayMask & PVR_WEEKDAY_SUNDAY)
      days += "SUN:";
    if (dayMask & PVR_WEEKDAY_MONDAY)
      days += "MON:";
    if (dayMask & PVR_WEEKDAY_TUESDAY)
      days += "TUE:";
    if (dayMask & PVR_WEEKDAY_WEDNESDAY)
      days += "WED:";
    if (dayMask & PVR_WEEKDAY_THURSDAY)
      days += "THU:";
    if (dayMask & PVR_WEEKDAY_FRIDAY)
      days += "FRI:";
  }

  return days;
}

PVR_ERROR cPVRClientNextPVR::AddTimer(const PVR_TIMER &timerinfo)
{
  // editing recording is not supported
  //if (timerinfo.iClientIndex != PVR_TIMER_NO_CLIENT_INDEX)
  //{
  //  return PVR_ERROR_NOT_IMPLEMENTED;
  //}

  char request[1024];

  char preventDuplicates[16];
  LOG_API_CALL(__FUNCTION__);
  if (timerinfo.iPreventDuplicateEpisodes)
    strcpy(preventDuplicates, "true");
  else
    strcpy(preventDuplicates, "false");

  std::string encodedName = UriEncode(timerinfo.strTitle);
  std::string encodedKeyword = UriEncode(timerinfo.strEpgSearchString);
  std::string days = GetDayString(timerinfo.iWeekdays);
  switch (timerinfo.iTimerType)
  {
  case TIMER_ONCE_MANUAL:
    XBMC->Log(LOG_DEBUG, "TIMER_ONCE_MANUAL");
    // build one-off recording request
    snprintf(request, sizeof(request), "/service?method=recording.save&name=%s&channel=%d&time_t=%d&duration=%d&pre_padding=%d&post_padding=%d&directory_id=%s",
      encodedName.c_str(),
      timerinfo.iClientChannelUid,
      (int)timerinfo.startTime,
      (int)(timerinfo.endTime - timerinfo.startTime),
      (int)timerinfo.iMarginStart,
      (int)timerinfo.iMarginEnd,
      m_recordingDirectories[timerinfo.iRecordingGroup].c_str()
      );
    break;

  case TIMER_ONCE_EPG:
    XBMC->Log(LOG_DEBUG, "TIMER_ONCE_EPG");
    // build one-off recording request
    snprintf(request, sizeof(request), "/service?method=recording.save&recording_id=%d&event_id=%d&pre_padding=%d&post_padding=%d&directory_id=%s",
      timerinfo.iClientIndex,
      timerinfo.iEpgUid,
      (int)timerinfo.iMarginStart,
      (int)timerinfo.iMarginEnd,
      m_recordingDirectories[timerinfo.iRecordingGroup].c_str());
    break;

  case TIMER_REPEATING_EPG:
    XBMC->Log(LOG_DEBUG, "TIMER_REPEATING_EPG");
    // build recurring recording request
    snprintf(request, sizeof(request), "/service?method=recording.recurring.save&recurring_id=%d&event_id=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s&only_new=%s",
      timerinfo.iClientIndex,
      timerinfo.iEpgUid,
      (int)timerinfo.iMaxRecordings,
      (int)timerinfo.iMarginStart,
      (int)timerinfo.iMarginEnd,
      days.c_str(),
      m_recordingDirectories[timerinfo.iRecordingGroup].c_str(),
      preventDuplicates
      );
    break;

  case TIMER_REPEATING_MANUAL:
    XBMC->Log(LOG_DEBUG, "TIMER_REPEATING_EPG");
    // build manual recurring request
    snprintf(request, sizeof(request), "/service?method=recording.recurring.save&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s",
      timerinfo.iClientIndex,
      encodedName.c_str(),
      timerinfo.iClientChannelUid,
      (int)timerinfo.startTime,
      (int)timerinfo.endTime,
      (int)timerinfo.iMaxRecordings,
      (int)timerinfo.iMarginStart,
      (int)timerinfo.iMarginEnd,
      days.c_str(),
      m_recordingDirectories[timerinfo.iRecordingGroup].c_str()
      );
    break;

  case TIMER_REPEATING_KEYWORD:
    XBMC->Log(LOG_DEBUG, "TIMER_REPEATING_KEYWORD");
    // build manual recurring request
    snprintf(request, sizeof(request), "/service?method=recording.recurring.save&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&directory_id=%s&keyword=%s&only_new=%s",
      timerinfo.iClientIndex,
      encodedName.c_str(),
      timerinfo.iClientChannelUid,
      (int)timerinfo.startTime,
      (int)timerinfo.endTime,
      (int)timerinfo.iMaxRecordings,
      (int)timerinfo.iMarginStart,
      (int)timerinfo.iMarginEnd,
      m_recordingDirectories[timerinfo.iRecordingGroup].c_str(),
      encodedKeyword.c_str(),
      preventDuplicates
      );
    break;
  }

  // send request to NextPVR
  std::string response;
  if (DoRequest(request, response) == HTTP_OK)
  {
    if (strstr(response.c_str(), "<rsp stat=\"ok\">"))
    {
      PVR->TriggerTimerUpdate();
      return PVR_ERROR_NO_ERROR;
    }
  }

  return PVR_ERROR_FAILED;
}

PVR_ERROR cPVRClientNextPVR::DeleteTimer(const PVR_TIMER &timer, bool bForceDelete)
{
  char request[512];
  LOG_API_CALL(__FUNCTION__);
  sprintf(request, "/service?method=recording.delete&recording_id=%d", timer.iClientIndex);

  // handle recurring recordings
  if (timer.iTimerType >= TIMER_REPEATING_MIN && timer.iTimerType <= TIMER_REPEATING_MAX)
  {
    sprintf(request, "/service?method=recording.recurring.delete&recurring_id=%d", timer.iClientIndex);
  }

  std::string response;
  if (DoRequest(request, response) == HTTP_OK)
  {
    if (strstr(response.c_str(), "<rsp stat=\"ok\">"))
    {
      PVR->TriggerTimerUpdate();
      return PVR_ERROR_NO_ERROR;
    }
  }

  return PVR_ERROR_FAILED;
}

PVR_ERROR cPVRClientNextPVR::UpdateTimer(const PVR_TIMER &timerinfo)
{
  LOG_API_CALL(__FUNCTION__);
  // not supported
  return AddTimer(timerinfo);
}


/************************************************************/
/** Live stream handling */
bool cPVRClientNextPVR::OpenLiveStream(const PVR_CHANNEL &channelinfo)
{
  char line[256];
  LOG_API_CALL(__FUNCTION__);
  if (channelinfo.bIsRadio == false && m_supportsLiveTimeshift && g_livestreamingmethod == Timeshift)
  {
    sprintf(line, "GET /live?channeloid=%d&mode=liveshift&client=XBMC-%s HTTP/1.0\r\n", channelinfo.iUniqueId, m_sid);
  }
  else
  {
    sprintf(line, "http://%s:%d/live?channeloid=%d&client=XBMC-%s", g_szHostname.c_str(), g_iPort, channelinfo.iUniqueId, m_sid);
    if (channelinfo.bIsRadio == false && g_livestreamingmethod == EPG_Based)
    {
      strcat(line,"&epgmode=true");
    }
  }
  XBMC->Log(LOG_NOTICE, "Calling Open(%s) on tsb!", line);
  if (channelinfo.bIsRadio == true && m_radioBuffer->Open(line))
  {
    return true;
  }
  else if (m_timeshiftBuffer->Open(line))
  {
    return true;
  }
  return false;
}

int cPVRClientNextPVR::ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  return m_timeshiftBuffer->Read(pBuffer, iBufferSize);
}

void cPVRClientNextPVR::CloseLiveStream(void)
{
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "CloseLiveStream");
  m_timeshiftBuffer->Close();
  XBMC->Log(LOG_DEBUG, "CloseLiveStream@exit");
}


long long cPVRClientNextPVR::SeekLiveStream(long long iPosition, int iWhence)
{
  long long retVal;
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "calling seek(%lli %d)", iPosition, iWhence);
  retVal = m_timeshiftBuffer->Seek(iPosition, iWhence);
  XBMC->Log(LOG_DEBUG, "returned from seek()");
  return retVal;
}


long long cPVRClientNextPVR::LengthLiveStream(void)
{
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "seek length(%lli)", m_timeshiftBuffer->Length());
  return m_timeshiftBuffer->Length();
}

PVR_ERROR cPVRClientNextPVR::SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  LOG_API_CALL(__FUNCTION__);
  // Not supported yet
  return PVR_ERROR_NO_ERROR;
}


bool cPVRClientNextPVR::CanPauseStream(void)
{
//  LOG_API_CALL(__FUNCTION__);
  if (m_recordingBuffer->GetDuration())
    return true;
  else
  {
    bool retval = m_timeshiftBuffer->CanPauseStream();
    return retval;
  }
}

void cPVRClientNextPVR::PauseStream(bool bPaused)
{
  m_timeshiftBuffer->PauseStream(bPaused);
}

bool cPVRClientNextPVR::CanSeekStream(void)
{
  LOG_API_CALL(__FUNCTION__);
  if (m_recordingBuffer->GetDuration())
    return true;
  else
    return m_timeshiftBuffer->CanSeekStream();
}

void Tokenize(const string& str, vector<string>& tokens, const string& delimiters = " ")
{
  string::size_type start_pos = 0;
  string::size_type delim_pos = 0;
  LOG_API_CALL(__FUNCTION__);

  while (string::npos != delim_pos)
  {
    delim_pos = str.find_first_of(delimiters, start_pos);
    tokens.push_back(str.substr(start_pos, delim_pos - start_pos));
    start_pos = delim_pos + 1;
    // Find next "non-delimiter"
  }
}


/************************************************************/
/** Record stream handling */


bool cPVRClientNextPVR::OpenRecordedStream(const PVR_RECORDING &recording)
{
  
  LOG_API_CALL(__FUNCTION__);
  m_currentRecordingLength = 0;
  m_currentRecordingPosition = 0;
  PVR_STRCLR(m_currentRecordingID);

  PVR_STRCPY(m_currentRecordingID, recording.strRecordingId);
  char line[1024];
  snprintf(line, sizeof(line), "http://%s:%d/live?recording=%s&client=XBMC", g_szHostname.c_str(), g_iPort, m_currentRecordingID);
  return m_recordingBuffer->Open(line,recording);
}

void cPVRClientNextPVR::CloseRecordedStream(void)
{
  LOG_API_CALL(__FUNCTION__);
  m_recordingBuffer->Close();
  m_recordingBuffer->SetDuration(0);
  m_currentRecordingLength = 0;
  m_currentRecordingPosition = 0;
  
}

int cPVRClientNextPVR::ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  LOG_API_CALL(__FUNCTION__);
  iBufferSize = m_recordingBuffer->Read(pBuffer, iBufferSize);
  return iBufferSize;
}

long long cPVRClientNextPVR::SeekRecordedStream(long long iPosition, int iWhence)
{
  LOG_API_CALL(__FUNCTION__);
  return m_recordingBuffer->Seek(iPosition, iWhence);
}

long long  cPVRClientNextPVR::LengthRecordedStream(void)
{
  LOG_API_CALL(__FUNCTION__);
  return m_recordingBuffer->Length();
}

/************************************************************/
/** http handling */
int cPVRClientNextPVR::DoRequest(const char *resource, std::string &response)
{
  LOG_API_CALL(__FUNCTION__);
  P8PLATFORM::CLockObject lock(m_mutex);
  NextPVR::Request *request;
  request = new NextPVR::Request();
  int resultCode =  request->DoRequest(resource, response,m_sid);
  delete request;
  LOG_API_IRET(__FUNCTION__, resultCode);
  return resultCode;
}

bool cPVRClientNextPVR::IsTimeshifting()
{
  //LOG_API_CALL(__FUNCTION__);
  return m_timeshiftBuffer->IsTimeshifting();
}

bool cPVRClientNextPVR::IsRealTimeStream()
{
  LOG_API_CALL(__FUNCTION__);
  if (m_recordingBuffer->GetDuration())
    return false;
  else
    return m_timeshiftBuffer->IsRealTimeStream();
}

PVR_ERROR cPVRClientNextPVR::GetStreamTimes(PVR_STREAM_TIMES *stimes)
{
  PVR_ERROR rez;
  LOG_API_CALL(__FUNCTION__);
  if (m_recordingBuffer->GetDuration())
    rez = m_recordingBuffer->GetStreamTimes(stimes);
  else
    rez = m_timeshiftBuffer->GetStreamTimes(stimes);
#if DEBUGGING_API
  XBMC->Log(LOG_ERROR, "GetStreamTimes: start: %d", stimes->startTime);
  XBMC->Log(LOG_ERROR, "             ptsStart: %lld", stimes->ptsStart);
  XBMC->Log(LOG_ERROR, "             ptsBegin: %lld", stimes->ptsBegin);
  XBMC->Log(LOG_ERROR, "               ptsEnd: %lld", stimes->ptsEnd);
#endif
  return rez;
}

PVR_ERROR cPVRClientNextPVR::GetStreamReadChunkSize(int* chunksize)
{
  PVR_ERROR rez;
  if (m_recordingBuffer->GetDuration())
    rez = m_recordingBuffer->GetStreamReadChunkSize(chunksize);
  else
    rez = m_timeshiftBuffer->GetStreamReadChunkSize(chunksize);
  LOG_API_IRET(__FUNCTION__, *chunksize);
  return rez;
}

#if DEBUGGING_XML

// ----------------------------------------------------------------------
// LOG dump and indenting utility functions
// ----------------------------------------------------------------------
const unsigned int NUM_INDENTS_PER_SPACE=2;

const char * getIndent( unsigned int numIndents )
{
  static const char * pINDENT="                                      + ";
  static const unsigned int LENGTH=strlen( pINDENT );
  unsigned int n=numIndents*NUM_INDENTS_PER_SPACE;
  if ( n > LENGTH ) n = LENGTH;

  return &pINDENT[ LENGTH-n ];
}

// same as getIndent but no "+" at the end
const char * getIndentAlt( unsigned int numIndents )
{
  static const char * pINDENT="                                        ";
  static const unsigned int LENGTH=strlen( pINDENT );
  unsigned int n=numIndents*NUM_INDENTS_PER_SPACE;
  if ( n > LENGTH ) n = LENGTH;

  return &pINDENT[ LENGTH-n ];
}

int dump_attribs_to_stdout(TiXmlElement* pElement, unsigned int indent)
{
  if ( !pElement ) return 0;

  TiXmlAttribute* pAttrib=pElement->FirstAttribute();
  int i=0;
  int ival;
  double dval;
  const char* pIndent=getIndent(indent);
  // XBMC->Log(LOG_NOTICE, "\n");
  while (pAttrib)
  {
    char buf[1024];
    sprintf(buf, "%s%s: value=[%s]", pIndent, pAttrib->Name(), pAttrib->Value());
    // XBMC->Log(LOG_NOTICE,  "%s%s: value=[%s]", pIndent, pAttrib->Name(), pAttrib->Value());

    if (pAttrib->QueryIntValue(&ival)==TIXML_SUCCESS)    
    {
      sprintf(buf + strlen(buf), " int=%d", ival);
      // XBMC->Log(LOG_NOTICE,  " int=%d", ival);
    }
    if (pAttrib->QueryDoubleValue(&dval)==TIXML_SUCCESS)
    {
      sprintf(buf + strlen(buf), " d=%1.1f", dval);
      // XBMC->Log(LOG_NOTICE,  " d=%1.1f", dval);
    }
    XBMC->Log(LOG_NOTICE,  "%s\n", buf );
    i++;
    pAttrib=pAttrib->Next();
  }
  return i; 
}

void dump_to_log( TiXmlNode* pParent, unsigned int indent)
{
  if ( !pParent ) return;

  char buf[2048];
  TiXmlNode* pChild;
  TiXmlText* pText;
  int t = pParent->Type();
  sprintf(buf, "%s", getIndent(indent));
  // XBMC->Log(LOG_NOTICE,  "%s", getIndent(indent));
  int num;

  switch ( t )
  {
  case TiXmlNode::TINYXML_DOCUMENT:
    sprintf(buf + strlen(buf), "Document");
    // XBMC->Log(LOG_NOTICE,  "Document" );
    break;

  case TiXmlNode::TINYXML_ELEMENT:
    sprintf(buf + strlen(buf), "Element [%s]", pParent->Value());
    // XBMC->Log(LOG_NOTICE,  "Element [%s]", pParent->Value() );
    num=dump_attribs_to_stdout(pParent->ToElement(), indent+1);
    switch(num)
    {
      case 0:
        sprintf(buf + strlen(buf), " (No attributes)");
        // XBMC->Log(LOG_NOTICE,  " (No attributes)"); 
        break;
      case 1:
        sprintf(buf + strlen(buf), "%s1 attribute", getIndentAlt(indent));
        // XBMC->Log(LOG_NOTICE,  "%s1 attribute", getIndentAlt(indent)); 
        break;
      default:
        sprintf(buf + strlen(buf), "%s%d attributes", getIndentAlt(indent), num);
        // XBMC->Log(LOG_NOTICE,  "%s%d attributes", getIndentAlt(indent), num);
        break;
    }
    break;

  case TiXmlNode::TINYXML_COMMENT:
    sprintf(buf + strlen(buf), "Comment: [%s]", pParent->Value());
    //XBMC->Log(LOG_NOTICE,  "Comment: [%s]", pParent->Value());
    break;

  case TiXmlNode::TINYXML_UNKNOWN:
    sprintf(buf + strlen(buf), "Unknown");
    // XBMC->Log(LOG_NOTICE,  "Unknown" );
    break;

  case TiXmlNode::TINYXML_TEXT:
    pText = pParent->ToText();
    sprintf(buf + strlen(buf), "Text: [%s]", pText->Value());
    // XBMC->Log(LOG_NOTICE,  "Text: [%s]", pText->Value() );
    break;

  case TiXmlNode::TINYXML_DECLARATION:
    sprintf(buf + strlen(buf), "Declaration");
    // XBMC->Log(LOG_NOTICE,  "Declaration" );
    break;
  default:
    break;
  }
  XBMC->Log(LOG_ERROR,  "%s\n", buf );
  for ( pChild = pParent->FirstChild(); pChild != 0; pChild = pChild->NextSibling()) 
  {
    dump_to_log( pChild, indent+1 );
  }
}
#endif // DEBUGGING_XML
