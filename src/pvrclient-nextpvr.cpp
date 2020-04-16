/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include <ctime>
#include <stdio.h>
#include <stdlib.h>
#include <regex>
#include <memory>

#include <p8-platform/util/StringUtils.h>
#include "kodi/util/XMLUtils.h"

#include "client.h"
#include "pvrclient-nextpvr.h"
#include "BackendRequest.h"

#include "md5.h"

#if defined(TARGET_WINDOWS)
  #define atoll(S) _atoi64(S)
#else
  #define MAXINT64 ULONG_MAX
#endif

#include <algorithm>


using namespace std;
using namespace ADDON;

extern "C"
{
  ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue);
}

/* Globals */
int g_iNextPVRXBMCBuild = 0;
extern bool g_bDownloadGuideArtwork;
int g_ServerTimeOffset = 0;

/* PVR client version (don't forget to update also the addon.xml and the Changelog.txt files) */
#define PVRCLIENT_NEXTPVR_VERSION_STRING    "1.0.0.0"
#define NEXTPVRC_MIN_VERSION_STRING         "4.2.4"

#define DEBUGGING_XML 0
#if DEBUGGING_XML
void dump_to_log( TiXmlNode* pParent, unsigned int indent);
#else
#define dump_to_log(x, y)
#endif


#define DEBUGGING_API 0
#if DEBUGGING_API
#define LOG_API_CALL(f) XBMC->Log(LOG_INFO, "%s:  called!", f)
#define LOG_API_IRET(f,i) XBMC->Log(LOG_INFO, "%s: returns %d", f, i)
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
  NextPVR::m_backEnd       = new NextPVR::Request();
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
  m_realTimeBuffer = new timeshift::DummyBuffer();
  m_livePlayer = nullptr;
  m_backendVersion = 0;
  CreateThread();
}

cPVRClientNextPVR::~cPVRClientNextPVR()
{
  StopThread();

  XBMC->Log(LOG_DEBUG, "->~cPVRClientNextPVR()");
  if (m_bConnected)
    Disconnect();
  SAFE_DELETE(m_tcpclient);
  delete m_timeshiftBuffer;
  delete m_recordingBuffer;
  delete m_realTimeBuffer;
  m_epgOidLookup.clear();
  m_hostFilenames.clear();
  m_channelTypes.clear();
  m_liveStreams.clear();
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

ADDON_STATUS cPVRClientNextPVR::Connect()
{
  string result;
  m_bConnected = false;
  ADDON_STATUS status = ADDON_STATUS_UNKNOWN;
  // initiate session
  std::string response;

  SendWakeOnLan();
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
        PVR_STRCLR(m_sid);
        PVR_STRCPY(m_sid, sidNode->FirstChild()->Value());
        NextPVR::m_backEnd->setSID(m_sid);
        // extract salt
        char salt[64];
        PVR_STRCLR(salt);
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
              TiXmlDocument settingsDoc;
              if (settingsDoc.Parse(settings.c_str()) != NULL)
              {
                //XBMC->Log(LOG_INFO, "Settings:\n");
                //dump_to_log(&settingsDoc, 0);
                if (XMLUtils::GetInt(settingsDoc.RootElement(), "NextPVRVersion", m_backendVersion))
                {
                  // NextPVR server
                  XBMC->Log(LOG_INFO, "NextPVR version: %d", m_backendVersion);

                  // is the server new enough
                  if (m_backendVersion < 40204)
                  {
                    XBMC->Log(LOG_ERROR, "Your NextPVR version '%d' is too old. Please upgrade to '%s' or higher!", m_backendVersion, NEXTPVRC_MIN_VERSION_STRING);
                    XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30050));
                    XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30051), NEXTPVRC_MIN_VERSION_STRING);
                    return ADDON_STATUS_PERMANENT_FAILURE;
                  }
                }

                // load padding defaults
                m_iDefaultPrePadding = 1;
                XMLUtils::GetInt(settingsDoc.RootElement(), "PrePadding", m_iDefaultPrePadding);

                m_iDefaultPostPadding = 2;
                XMLUtils::GetInt(settingsDoc.RootElement(), "PostPadding", m_iDefaultPostPadding);

                m_showNew = false;
                XMLUtils::GetBoolean(settingsDoc.RootElement(),"ShowNewInGuide",m_showNew);

                std::string recordingDirectories;
                if (XMLUtils::GetString(settingsDoc.RootElement(),"RecordingDirectories",recordingDirectories))
                {
                  vector<std::string> directories = split(recordingDirectories, ",", false);
                  for (size_t i = 0; i < directories.size(); i++)
                  {
                    m_recordingDirectories.push_back(directories[i]);
                  }
                }

                int serverTimestamp;
                if (XMLUtils::GetInt(settingsDoc.RootElement(), "TimeEpoch", serverTimestamp))
                {
                  g_ServerTimeOffset = time(nullptr) - serverTimestamp;
                  XBMC->Log(LOG_NOTICE, "Server time offset in seconds: %d", g_ServerTimeOffset);
                }

                if (XMLUtils::GetInt(settingsDoc.RootElement(), "SlipSeconds", g_timeShiftBufferSeconds))
                  XBMC->Log(LOG_NOTICE, "time shift buffer in seconds == %d\n", g_timeShiftBufferSeconds);

                std::string serverMac;
                if (XMLUtils::GetString(settingsDoc.RootElement(),"ServerMAC",serverMac))
                {
                  // only available from Windows backend
                  char rawMAC[13];
                  PVR_STRCPY(rawMAC,serverMac.c_str());
                  if (strlen(rawMAC)==12)
                  {
                    char mac[18];
                    sprintf(mac,"%2.2s:%2.2s:%2.2s:%2.2s:%2.2s:%2.2s",rawMAC,&rawMAC[2],&rawMAC[4],&rawMAC[6],&rawMAC[8],&rawMAC[10]);
                    XBMC->Log(LOG_DEBUG, "Server MAC addres %4.4s...",mac);
                    std::string smac = mac;
                    if (g_host_mac != smac)
                    {
                      SaveSettings("host_mac", smac);
                    }
                  }
                }
              }
            }
            // set additional options are based on the backend

            SetVersionSpecificSettings();

            m_bConnected = true;
            XBMC->Log(LOG_DEBUG, "session.login successful");
            status = ADDON_STATUS_OK;
          }
        }
        else
        {
          XBMC->Log(LOG_DEBUG, "session.login failed");
          PVR->ConnectionStateChange( "Access denied", PVR_CONNECTION_STATE_ACCESS_DENIED, XBMC->GetLocalizedString(30052));
          m_bConnected = false;
          status = ADDON_STATUS_LOST_CONNECTION;
        }
      }
    }
  }
  else
  {
    PVR->ConnectionStateChange( "Could not connect to server", PVR_CONNECTION_STATE_SERVER_UNREACHABLE ,NULL);
    status = ADDON_STATUS_NEED_SETTINGS;
  }

  return status;
}

void cPVRClientNextPVR::Disconnect()
{
  string result;

  m_bConnected = false;
}

void cPVRClientNextPVR::SetVersionSpecificSettings()
{
  char buffer[1024];
  //NextPVR::m_backEnd->Discovery();
  g_livestreamingmethod = DEFAULT_LIVE_STREAM;

  if (XBMC->GetSetting("livestreamingmethod", &g_livestreamingmethod))
  {
    // has v4 setting
    if (m_backendVersion < 50000)
    {
      // previous Matrix clients had a transcoding option
      if (g_livestreamingmethod == eStreamingMethod::Transcoded)
      {
        g_livestreamingmethod = eStreamingMethod::RealTime;
        XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30051), "5");
      }
    }
    else  if (m_backendVersion < 50002)
    {
      g_livestreamingmethod = eStreamingMethod::RealTime;
      XBMC->QueueNotification(QUEUE_ERROR, XBMC->GetLocalizedString(30051), "5.0.2+");
    }
    else
    {
      // check for new v5 setting
      if (!XBMC->GetSetting("livestreamingmethod5", &g_livestreamingmethod))
        if (g_livestreamingmethod == RollingFile)
          g_livestreamingmethod = eStreamingMethod::Timeshift;
    }
  }
  if (g_livestreamingmethod != eStreamingMethod::RealTime)
  {
    if (m_backendVersion >= 50000)
    {
      delete m_timeshiftBuffer;
      if (g_livestreamingmethod == eStreamingMethod::Transcoded)
      {
        m_timeshiftBuffer = new timeshift::TranscodedBuffer();
      }
      else
      {
        m_supportsLiveTimeshift = true;
        g_livestreamingmethod = eStreamingMethod::ClientTimeshift;
        XBMC->Log(LOG_NOTICE, "Client Timeshift Based Buffering");
        m_timeshiftBuffer = new timeshift::ClientTimeShift();
      }
    }
    else
    {
      delete m_timeshiftBuffer;
      m_supportsLiveTimeshift = true;
      if (g_livestreamingmethod != eStreamingMethod::Timeshift)
      {
        m_timeshiftBuffer = new timeshift::RollingFile();
      }
      else
      {
        XBMC->Log(LOG_NOTICE, "Timeshift is true");
        m_timeshiftBuffer = new timeshift::TimeshiftBuffer();
      }
    }
  }

  if (m_backendVersion >= 50000)
  {
    g_sendSidWithMetadata = false;
    bool remote;
    if (g_szPin != "0000" && XBMC->GetSetting("remoteaccess", &remote))
    {
      if (remote)
      {
        g_bDownloadGuideArtwork = false;
        g_sendSidWithMetadata = true;
      }
    }
  }
  else
  {
    g_sendSidWithMetadata = true;
  }

  if (!XBMC->GetSetting("recordingsize", &m_recordingSize))
    m_recordingSize = false;

  bool liveStreams;
  if (XBMC->GetSetting("uselivestreams", &liveStreams))
    if (liveStreams)
      LoadLiveStreams();

  bool resetIcons;
  if (XBMC->GetSetting("reseticons", &resetIcons))
  if (resetIcons)
      DeleteChannelIcons();

}

/* IsUp()
 * \brief   Check if we have a valid session to nextpvr
 * \return  True when a session is active
 */
bool cPVRClientNextPVR::IsUp()
{
  LOG_API_CALL(__FUNCTION__);
  // check time since last time Recordings were updated, update if it has been awhile
  if (m_bConnected == true && g_NowPlaying == NotPlaying && m_lastRecordingUpdateTime != MAXINT64 &&  time(0) > (m_lastRecordingUpdateTime + 60 ))
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
  else if (g_NowPlaying == Transcoding)
  {
    if ( m_livePlayer->IsRealTimeStream() == false)
    {
      //m_livePlayer->Close();
      g_NowPlaying = NotPlaying;
      m_livePlayer = nullptr;
    }
  }
  return m_bConnected;
}

void *cPVRClientNextPVR::Process(void)
{
  LOG_API_CALL(__FUNCTION__);
  while (!IsStopped())
  {
    IsUp();
    Sleep(2500);
  }
  return NULL;
}

void cPVRClientNextPVR::OnSystemSleep()
{
  m_lastRecordingUpdateTime = MAXINT64;
  Disconnect();
  PVR->ConnectionStateChange( "sleeping", PVR_CONNECTION_STATE_DISCONNECTED, NULL);
  Sleep(1000);
}

void cPVRClientNextPVR::OnSystemWake()
{
  PVR->ConnectionStateChange( "waking", PVR_CONNECTION_STATE_CONNECTING, NULL);
  int count = 0;
  for (;count < 5; count++)
  {
    if (Connect())
    {
      PVR->ConnectionStateChange( "connected", PVR_CONNECTION_STATE_CONNECTED, NULL);
      break;
    }
    Sleep(500);
  }

  XBMC->Log(LOG_INFO, "On NextPVR Wake %d %d",m_bConnected, count);
}

void cPVRClientNextPVR::SendWakeOnLan()
{
  if (g_wol_enabled == true )
  {
    if (g_szHostname == "127.0.0.1" || g_szHostname == "localhost" || g_szHostname == "::1")
    {
      g_wol_enabled = false;
      return;
    }
    int count = 0;
    for (;count < g_wol_timeout; count++)
    {
      if (NextPVR::m_backEnd->PingBackend())
      {
        return;
      }
      XBMC->WakeOnLan(g_host_mac.c_str());
      XBMC->Log(LOG_DEBUG, "WOL sent %d",count);
      Sleep(1000);
    }
  }
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
    m_BackendName = "NextPVR (";
    m_BackendName += g_szHostname.c_str();
    m_BackendName += ")";
  }

  return m_BackendName.c_str();
}

const char* cPVRClientNextPVR::GetBackendVersionString()
{
  LOG_API_CALL(__FUNCTION__);
  if (!m_bConnected)
    return "Unknown";

  XBMC->Log(LOG_DEBUG, "->GetBackendVersion()");
  std::string version = to_string(m_backendVersion);
  return version.c_str();
}

const char* cPVRClientNextPVR::GetConnectionString(void)
{
  static std::string strConnectionString = "connected";
  return strConnectionString.c_str();
}

PVR_ERROR cPVRClientNextPVR::GetDriveSpace(long long *iTotal, long long *iUsed)
{
  LOG_API_CALL(__FUNCTION__);
  string result;
  vector<string> fields;

  *iTotal = 0;
  *iUsed = 0;

  if (!m_bConnected)
    return PVR_ERROR_SERVER_ERROR;

  return PVR_ERROR_NO_ERROR;
}

/************************************************************/
/** EPG handling */

PVR_ERROR cPVRClientNextPVR::GetEpg(ADDON_HANDLE handle, int iChannelUid, time_t iStart, time_t iEnd)
{
  EPG_TAG broadcast;

  std::string response;
  char request[512];
  LOG_API_CALL(__FUNCTION__);
  if ( iEnd < (time(nullptr) - 24 * 3600))
  {
      XBMC->Log(LOG_DEBUG, "Skipping expired EPG data %d %ld %lld",iChannelUid,iStart, iEnd);
      return PVR_ERROR_INVALID_PARAMETERS;
  }
  sprintf(request, "/service?method=channel.listings&channel_id=%d&start=%d&end=%d&genre=all", iChannelUid, (int)iStart, (int)iEnd);
  if (DoRequest(request, response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      TiXmlElement* listingsNode = doc.RootElement()->FirstChildElement("listings");
      TiXmlElement* pListingNode;
      for( pListingNode = listingsNode->FirstChildElement("l"); pListingNode; pListingNode=pListingNode->NextSiblingElement())
      {
        broadcast = {};

        string title;
        string description;
        string subtitle;
        XMLUtils::GetString(pListingNode,"name",title);
        XMLUtils::GetString(pListingNode,"description",description);

        if (XMLUtils::GetString(pListingNode,"subtitle",subtitle))
        {
          if (description != subtitle + ":" && StringUtils::StartsWith(description,subtitle + ": "))
          {
            description = description.substr(subtitle.length()+2);
          }
        }

        broadcast.iYear = XmlGetInt(pListingNode,"year");;

        string startTime, endTime;
        XMLUtils::GetString(pListingNode,"start",startTime);
        startTime.resize(10);
        XMLUtils::GetString(pListingNode,"end",endTime);
        endTime.resize(10);

        const std::string oidLookup(endTime + ":" + to_string(iChannelUid));

        const int epgOid = XmlGetInt(pListingNode,"id");
        m_epgOidLookup[oidLookup] = epgOid;

        broadcast.strTitle            = title.c_str();
        broadcast.strEpisodeName      = subtitle.c_str();
        broadcast.iUniqueChannelId    = iChannelUid;
        broadcast.startTime           = stol(startTime);
        broadcast.iUniqueBroadcastId  = stoi(endTime);
        broadcast.endTime             = stol(endTime);
        broadcast.strPlotOutline      = NULL; //unused
        broadcast.strPlot             = description.c_str();
        broadcast.strOriginalTitle    = NULL; // unused
        broadcast.strCast             = NULL; // unused
        broadcast.strDirector         = NULL; // unused
        broadcast.strWriter           = NULL; // unused
        broadcast.strIMDBNumber       = NULL; // unused

        if (g_bDownloadGuideArtwork)
        {
          // artwork URL
          char artworkPath[512];
          if (m_backendVersion < 50000)
            snprintf(artworkPath, sizeof(artworkPath), "http://%s:%d/service?method=channel.show.artwork&sid=%s&event_id=%d", g_szHostname.c_str(), g_iPort, m_sid, epgOid);
          else
          {
            if (g_sendSidWithMetadata)
              snprintf(artworkPath, sizeof(artworkPath), "http://%s:%d/service?method=channel.show.artwork&sid=%s&name=%s", g_szHostname.c_str(), g_iPort, m_sid,UriEncode(title).c_str());
            else
              snprintf(artworkPath, sizeof(artworkPath), "http://%s:%d/service?method=channel.show.artwork&name=%s", g_szHostname.c_str(), g_iPort, UriEncode(title).c_str());
            if (g_eventArtFormat == Portrait)
              strcat(artworkPath, "&prefer=poster");
          }
          broadcast.strIconPath = artworkPath;
        }
        std::string sGenre;
        if (XMLUtils::GetString(pListingNode,"genre",sGenre))
        {
          broadcast.strGenreDescription = sGenre.c_str();
          broadcast.iGenreType = EPG_GENRE_USE_STRING;
        }
        else
        {
          // genre type
          broadcast.iGenreType = XmlGetInt(pListingNode,"genre_type");
          broadcast.iGenreSubType = XmlGetInt(pListingNode,"genre_subtype");
        }
        if (pListingNode->FirstChildElement("genres") != NULL)
        {
          std::string  allGenres;
          if (GetAdditiveString(pListingNode->FirstChildElement("genres"), "genre", EPG_STRING_TOKEN_SEPARATOR, allGenres, true))
          {
            if (allGenres.find(EPG_STRING_TOKEN_SEPARATOR) != std::string::npos)
            {
              if (broadcast.iGenreType != EPG_GENRE_USE_STRING)
              {
                broadcast.iGenreSubType = EPG_GENRE_USE_STRING;
              }
              sGenre = allGenres;
              broadcast.strGenreDescription = sGenre.c_str();
            }
          }
        }
        int value = 0;
        XMLUtils::GetInt(pListingNode,"season",value);
        if ( value == 0 )
          broadcast.iSeriesNumber = EPG_TAG_INVALID_SERIES_EPISODE;
        else
          broadcast.iSeriesNumber = value;

        value = 0;
        XMLUtils::GetInt(pListingNode,"episode",value);
        if ( value == 0 )
          broadcast.iEpisodeNumber = EPG_TAG_INVALID_SERIES_EPISODE;
        else
          broadcast.iEpisodeNumber = value;

        broadcast.iEpisodePartNumber = EPG_TAG_INVALID_SERIES_EPISODE;

        std::string original;
        XMLUtils::GetString(pListingNode,"original",original);
        broadcast.strFirstAired = original.c_str();

        bool firstrun;
        if (XMLUtils::GetBoolean(pListingNode,"firstrun",firstrun))
        {
          if (firstrun)
          {
            std::string significance;
            XMLUtils::GetString(pListingNode,"significance",significance);
            if (significance == "Live")
            {
              broadcast.iFlags = EPG_TAG_FLAG_IS_LIVE;
            }
            else if (significance.find("Premiere") != string::npos)
            {
              broadcast.iFlags = EPG_TAG_FLAG_IS_PREMIERE;
            }
            else if (significance.find("Finale") != string::npos)
            {
              broadcast.iFlags = EPG_TAG_FLAG_IS_FINALE;
            }
            else if (m_showNew)
            {
              broadcast.iFlags = EPG_TAG_FLAG_IS_NEW;
            }
          }
        }
        PVR->TransferEpgEntry(handle, &broadcast);
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}

int cPVRClientNextPVR::XmlGetInt(TiXmlElement * node, const char* name)
{
  int retval = 0;
  XMLUtils::GetInt(node,name,retval);
  return retval;
}


/************************************************************/
/** Channel handling */

int cPVRClientNextPVR::GetNumChannels(void)
{
  LOG_API_CALL(__FUNCTION__);
  if (m_iChannelCount != -1)
    return m_iChannelCount;


  // need something more optimal, but this will do for now...
  std::string response;
  if (DoRequest("/service?method=channel.list", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      m_iChannelCount = 0;
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
  LOG_API_CALL(__FUNCTION__);

  std::string iconFilename = GetChannelIconFileName(channelID);

  // do we already have the icon file?
  if (XBMC->FileExists(iconFilename.c_str(), false))
  {
    return iconFilename;
  }
  char strURL[256];
  sprintf(strURL, "/service?method=channel.icon&channel_id=%d", channelID);
  if (NextPVR::m_backEnd->FileCopy(strURL, iconFilename) == HTTP_OK)
  {
    return iconFilename;
  }

  return "";
}

std::string cPVRClientNextPVR::GetChannelIconFileName(int channelID)
{
  char filename[64];
  snprintf(filename, sizeof(filename), "nextpvr-ch%d.png", channelID);
  std::string iconFilePath("special://userdata/addon_data/pvr.nextpvr/");

  return iconFilePath + filename;
}

void  cPVRClientNextPVR::DeleteChannelIcons()
{
  VFSDirEntry* icons;
  unsigned int count;
  if (XBMC->GetDirectory("special://userdata/addon_data/pvr.nextpvr/","nextpvr-ch*.png",&icons,&count))
  {
    XBMC->Log(LOG_INFO, "Deleting %d channel icons", count);
    for (int i = 0; i < count; ++i)
    {
      VFSDirEntry& f = icons[i];
      std::string deleteme = f.path;
      XBMC->Log(LOG_DEBUG,"DeleteFile %s rc:%d",XBMC->TranslateSpecialProtocol(f.path),remove(XBMC->TranslateSpecialProtocol(f.path)));
    }
  }
  SaveSettings("reseticons", "false");
}

void cPVRClientNextPVR::LoadLiveStreams()
{
  char strURL[256];
  sprintf(strURL, "/public/LiveStreams.xml");
  m_liveStreams.clear();
  if (NextPVR::m_backEnd->FileCopy(strURL, "special://userdata/addon_data/pvr.nextpvr/LiveStreams.xml") == HTTP_OK)
  {
    TiXmlDocument doc;
    char *liveStreams = XBMC->TranslateSpecialProtocol("special://userdata/addon_data/pvr.nextpvr/LiveStreams.xml");
    XBMC->Log(LOG_DEBUG, "Loading LiveStreams.xml %s", liveStreams);
    if (doc.LoadFile(liveStreams))
    {
      TiXmlElement* streamsNode = doc.FirstChildElement("streams");
      if (streamsNode)
      {
        TiXmlElement* streamNode;
        for( streamNode = streamsNode->FirstChildElement("stream"); streamNode; streamNode=streamNode->NextSiblingElement())
        {
          std::string key_value;
          if ( streamNode->QueryStringAttribute("id", &key_value)==TIXML_SUCCESS)
          {
            try {
              if (streamNode->FirstChild())
              {
                int channelID = std::stoi(key_value);
                XBMC->Log(LOG_DEBUG, "%d %s",channelID, streamNode->FirstChild()->Value());
                m_liveStreams[channelID] = streamNode->FirstChild()->Value();
              }
            } catch (...)
            {
                XBMC->Log(LOG_DEBUG, "%s:%d",__FUNCTION__,__LINE__);
            }
          }
        }
      }
    }
  }
}

PVR_ERROR cPVRClientNextPVR::GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  PVR_CHANNEL     tag;
  std::string      stream;
  LOG_API_CALL(__FUNCTION__);

  m_channelTypes.clear();
  int channelCount = 0;
  std::string response;
  if (DoRequest("/service?method=channel.list", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      //XBMC->Log(LOG_INFO, "Channels:\n");
      //dump_to_log(&doc, 0);
      channelCount = 0;
      TiXmlElement* channelsNode = doc.RootElement()->FirstChildElement("channels");
      TiXmlElement* pChannelNode;
      for( pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode=pChannelNode->NextSiblingElement())
      {
        memset(&tag, 0, sizeof(PVR_CHANNEL));
        TiXmlElement* channelTypeNode = pChannelNode->FirstChildElement("type");
        tag.iUniqueId = atoi(pChannelNode->FirstChildElement("id")->FirstChild()->Value());
        if (strcmp(channelTypeNode->FirstChild()->Value(), "0xa") == 0)
        {
          tag.bIsRadio = true;
          PVR_STRCPY(tag.strInputFormat, "application/octet-stream");
        }
        else
        {
          tag.bIsRadio = false;
          if (!IsChannelAPlugin(tag.iUniqueId))
            PVR_STRCPY(tag.strInputFormat, "video/MP2T");
        }
        if (bRadio != tag.bIsRadio)
          continue;

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
        if ( !m_channelTypes[tag.iUniqueId])
        {
          m_channelTypes[tag.iUniqueId] = tag.bIsRadio;
        }
        // transfer channel to XBMC
        PVR->TransferChannelEntry(handle, &tag);
        channelCount++;
      }
    }
    m_iChannelCount = channelCount;
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
        std::string group;
        if ( XMLUtils::GetString(pGroupNode,"name",group) )
        {
          // tell XBMC about channel, ignoring "All Channels" since xbmc has an built in group with effectively the same function
          strcpy(tag.strGroupName,group.c_str());
          if (strcmp(tag.strGroupName, "All Channels") != 0 && tag.strGroupName[0]!=0)
          {
            PVR->TransferChannelGroup(handle, &tag);
          }
        }
      }
    }
    else
    {
      XBMC->Log(LOG_DEBUG, "GetChannelGroupsAmount");
    }

  }
  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR cPVRClientNextPVR::GetChannelStreamProperties(const PVR_CHANNEL &channel, PVR_NAMED_VALUE *properties, unsigned int *iPropertiesCount)
{
  LOG_API_CALL(__FUNCTION__);
    XBMC->Log(LOG_DEBUG, "%s:%d:", __FUNCTION__, __LINE__);
  bool liveStream = IsChannelAPlugin(channel.iUniqueId);
  if (liveStream || ( g_livestreamingmethod == Transcoded && !channel.bIsRadio) )
  {
    strncpy(properties[0].strName, PVR_STREAM_PROPERTY_STREAMURL,sizeof(properties[0].strName) - 1);
    if (liveStream)
    {
      strncpy(properties[0].strValue,m_liveStreams[channel.iUniqueId].c_str(), sizeof(properties[0].strValue) - 1);
      strcpy(properties[1].strName, PVR_STREAM_PROPERTY_ISREALTIMESTREAM);
      strcpy(properties[1].strValue,"true");
      *iPropertiesCount = 2;
    }
    else
    {
      char line[256];
      if (m_livePlayer != nullptr)
      {
        m_livePlayer->Close();
        g_NowPlaying = NotPlaying;
        m_livePlayer = nullptr;
      }
      sprintf(line, "http://%s:%d/services/service?method=channel.transcode.m3u8&sid=%s", g_szHostname.c_str(), g_iPort,m_sid);
      m_livePlayer = m_timeshiftBuffer;
      m_livePlayer->Channel(channel.iUniqueId);
      if (m_livePlayer->Open(line))
      {
        g_NowPlaying = Transcoding;
      }
      else
      {
        XBMC->Log(LOG_ERROR, "Transcoding Error");
        return PVR_ERROR_FAILED;
      }
      strncpy(properties[0].strValue,line, sizeof(properties[0].strValue) - 1);
      strcpy(properties[1].strName, PVR_STREAM_PROPERTY_ISREALTIMESTREAM);
      strcpy(properties[1].strValue,"true");
      strcpy(properties[2].strName, PVR_STREAM_PROPERTY_MIMETYPE );
      strcpy(properties[2].strValue,"application/x-mpegURL");
      *iPropertiesCount = 3;
    }
    return PVR_ERROR_NO_ERROR;
  }
  return PVR_ERROR_NOT_IMPLEMENTED;
}


bool cPVRClientNextPVR::IsChannelAPlugin(int uid)
{
  if (m_liveStreams.count(uid)!= 0)
    if (StringUtils::StartsWith(m_liveStreams[uid],"plugin:") || StringUtils::EndsWithNoCase(m_liveStreams[uid],".m3u8"))
      return true;

  return false;
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

  LOG_API_CALL(__FUNCTION__);
  if (m_iRecordingCount != 0)
    return m_iRecordingCount;

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
        m_iRecordingCount = 0;
        for( pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode=pRecordingNode->NextSiblingElement())
        {
          m_iRecordingCount++;
        }
      }
    }
  }
  LOG_API_IRET(__FUNCTION__, m_iRecordingCount);
  return m_iRecordingCount;
}

PVR_ERROR cPVRClientNextPVR::GetRecordings(ADDON_HANDLE handle)
{
  // include already-completed recordings
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  m_hostFilenames.clear();
  LOG_API_CALL(__FUNCTION__);
  int recordingCount = 0;
  std::string response;
  if (DoRequest("/service?method=recording.list&filter=all", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != NULL)
    {
      dump_to_log(&doc, 0);
      PVR_RECORDING   tag;
      TiXmlElement* recordingsNode = doc.RootElement()->FirstChildElement("recordings");
      TiXmlElement* pRecordingNode;
      for( pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode=pRecordingNode->NextSiblingElement())
      {
       tag = {};
        if (UpdatePvrRecording(pRecordingNode, &tag))
        {
          recordingCount++;
          PVR->TransferRecordingEntry(handle, &tag);
        }
      }
    }
    m_iRecordingCount = recordingCount;
    XBMC->Log(LOG_DEBUG, "Updated recordings %lld", m_lastRecordingUpdateTime);
  }
  else
  {
    returnValue = PVR_ERROR_SERVER_ERROR;
  }
  m_lastRecordingUpdateTime = time(0);
  LOG_API_IRET(__FUNCTION__, returnValue);
  return returnValue;
}

bool cPVRClientNextPVR::UpdatePvrRecording(TiXmlElement* pRecordingNode, PVR_RECORDING *tag)
{

  tag->recordingTime = atol(pRecordingNode->FirstChildElement("start_time_ticks")->FirstChild()->Value());

  std::string status = pRecordingNode->FirstChildElement("status")->FirstChild()->Value();
  if (status=="Pending"  && tag->recordingTime > time(nullptr) + g_ServerTimeOffset)
  {
    // skip timers
    return false;
  }
  tag->iDuration = atoi(pRecordingNode->FirstChildElement("duration_seconds")->FirstChild()->Value());

  if (status == "Ready" || status == "Pending" || status == "Recording")
  {
    snprintf(tag->strDirectory,sizeof(tag->strDirectory),"/%s",pRecordingNode->FirstChildElement("name")->FirstChild()->Value());
    if (pRecordingNode->FirstChildElement("desc") != NULL && pRecordingNode->FirstChildElement("desc")->FirstChild() != NULL)
    {
      PVR_STRCPY(tag->strPlot, pRecordingNode->FirstChildElement("desc")->FirstChild()->Value());
    }
  }
  else if (status == "Failed")
  {
    snprintf(tag->strDirectory,sizeof(tag->strDirectory),"/%s/%s",XBMC->GetLocalizedString(30166),pRecordingNode->FirstChildElement("name")->FirstChild()->Value());
    if (pRecordingNode->FirstChildElement("reason") != NULL && pRecordingNode->FirstChildElement("reason")->FirstChild() != NULL)
    {
      PVR_STRCPY(tag->strPlot, pRecordingNode->FirstChildElement("reason")->FirstChild()->Value());
    }
    if (tag->iDuration < 0)
    {
      tag->iDuration = 0;
    }
  }
  else if (status == "Conflict")
  {
    // shouldn't happen;
    return false;
  }
  else
  {
     XBMC->Log(LOG_ERROR, "Unknown status %s",status.c_str());
     return false;
  }

  if (status == "Recording" || status == "Pending"  )
  {
    if (pRecordingNode->FirstChildElement("epg_event_oid"))
    {
      // EPG Event ID is likely not valid on most older recordings so current or pending only
      // Linked tags need to be on end time because recordingTime can change because of pre-padding

      tag->iEpgEventId = tag->recordingTime + tag->iDuration;
    }
  }

  PVR_STRCPY(tag->strRecordingId, pRecordingNode->FirstChildElement("id")->FirstChild()->Value());
  PVR_STRCPY(tag->strTitle, pRecordingNode->FirstChildElement("name")->FirstChild()->Value());

  tag->iSeriesNumber = PVR_RECORDING_INVALID_SERIES_EPISODE;
  tag->iEpisodeNumber = PVR_RECORDING_INVALID_SERIES_EPISODE;

  if (pRecordingNode->FirstChildElement("subtitle") != NULL && pRecordingNode->FirstChildElement("subtitle")->FirstChild() != NULL)
  {
    if (g_KodiLook)
    {
      ParseNextPVRSubtitle(pRecordingNode->FirstChildElement("subtitle")->FirstChild()->Value(), tag);
    }
    else
    {
      PVR_STRCPY(tag->strTitle, pRecordingNode->FirstChildElement("subtitle")->FirstChild()->Value());
    }
  }

  if (pRecordingNode->FirstChildElement("playback_position") != NULL && pRecordingNode->FirstChildElement("playback_position")->FirstChild() != NULL)
  {
    tag->iLastPlayedPosition = atoi(pRecordingNode->FirstChildElement("playback_position")->FirstChild()->Value());
  }

  if (pRecordingNode->FirstChildElement("channel_id") != NULL && pRecordingNode->FirstChildElement("channel_id")->FirstChild() != NULL)
  {
    tag->iChannelUid = atoi(pRecordingNode->FirstChildElement("channel_id")->FirstChild()->Value());
    if (tag->iChannelUid == 0)
    {
      tag->iChannelUid = PVR_CHANNEL_INVALID_UID;
    }
    else
    {
      strcpy(tag->strIconPath,GetChannelIconFileName(tag->iChannelUid).c_str());
    }
  }
  else
  {
    tag->iChannelUid = PVR_CHANNEL_INVALID_UID;
  }

  if (pRecordingNode->FirstChildElement("channel") != NULL && pRecordingNode->FirstChildElement("channel")->FirstChild() != NULL)
  {
    PVR_STRCPY(tag->strChannelName,pRecordingNode->FirstChildElement("channel")->FirstChild()->Value());
  }

  std::string recordingFile;
  if (XMLUtils::GetString(pRecordingNode, "file", recordingFile))
  {
    if (m_recordingSize)
    {
      StringUtils::Replace(recordingFile, '\\', '/');
      if (StringUtils::StartsWith(recordingFile, "//"))
      {
        recordingFile = "smb:" + recordingFile;
      }
      if (XBMC->FileExists(recordingFile.c_str(), READ_NO_CACHE))
      {
        void* fileHandle = XBMC->OpenFile(recordingFile.c_str(), READ_NO_CACHE);
        tag->sizeInBytes = XBMC->GetFileLength(fileHandle);
        XBMC->CloseFile(fileHandle);
      }
      else
      {
        // don't play recording as file
        recordingFile.clear();
      }
    }
  }

  m_hostFilenames[tag->strRecordingId] = recordingFile;

  // if we use unknown Kodi logs warning and turns it to TV so save some steps
  tag->channelType = PVR_RECORDING_CHANNEL_TYPE_TV;
  if ( tag->iChannelUid != PVR_CHANNEL_INVALID_UID)
  {
    if ( m_channelTypes[tag->iChannelUid])
    {
      if ( m_channelTypes[tag->iChannelUid] == true)
      {
        tag->channelType = PVR_RECORDING_CHANNEL_TYPE_RADIO;
      }
    }
  }
  if (tag->channelType != PVR_RECORDING_CHANNEL_TYPE_RADIO)
  {
    char artworkPath[512];
    if (m_backendVersion < 50000)
    {
      snprintf(artworkPath, sizeof(artworkPath), "http://%s:%d/service?method=recording.artwork&sid=%s&recording_id=%s", g_szHostname.c_str(), g_iPort, m_sid, tag->strRecordingId);
      PVR_STRCPY(tag->strThumbnailPath, artworkPath);
      snprintf(artworkPath, sizeof(artworkPath), "http://%s:%d/service?method=recording.fanart&sid=%s&recording_id=%s", g_szHostname.c_str(), g_iPort, m_sid, tag->strRecordingId);
      PVR_STRCPY(tag->strFanartPath, artworkPath);
    }
    else
    {
      if (g_sendSidWithMetadata)
        snprintf(artworkPath, sizeof(artworkPath), "http://%s:%d/service?method=channel.show.artwork&sid=%s&name=%s", g_szHostname.c_str(), g_iPort, m_sid, UriEncode(tag->strTitle).c_str());
      else
        snprintf(artworkPath, sizeof(artworkPath), "http://%s:%d/service?method=channel.show.artwork&name=%s", g_szHostname.c_str(), g_iPort, UriEncode(tag->strTitle).c_str());
      PVR_STRCPY(tag->strFanartPath, artworkPath);
      strcat(artworkPath, "&prefer=poster");
      PVR_STRCPY(tag->strThumbnailPath, artworkPath);
    }
  }
  if (pRecordingNode->FirstChildElement("genres") != NULL)
  {
    std::string genres;
    TiXmlElement* pGenresNode = pRecordingNode->FirstChildElement("genres");
    GetAdditiveString(pGenresNode,"genre",EPG_STRING_TOKEN_SEPARATOR,genres,true);
    tag->iGenreType = EPG_GENRE_USE_STRING;
    tag->iGenreSubType = 0;
    PVR_STRCPY(tag->strGenreDescription,genres.c_str());
  }

  std::string significance;
  XMLUtils::GetString(pRecordingNode,"significance",significance);
  if (significance.find("Premiere") != string::npos)
  {
    tag->iFlags = PVR_RECORDING_FLAG_IS_PREMIERE;
  }
  else if (significance.find("Finale") != string::npos)
  {
    tag->iFlags = PVR_RECORDING_FLAG_IS_FINALE;
  }

  bool played = false;
  if (XMLUtils::GetBoolean(pRecordingNode, "played", played))
  {
    tag->iPlayCount = 1;
  }

  return true;
}

bool cPVRClientNextPVR::GetAdditiveString(const TiXmlNode* pRootNode, const char* strTag,
      const std::string& strSeparator, std::string& strStringValue,
      bool clear)
{
  std::string strTemp;
  const TiXmlElement* node = pRootNode->FirstChildElement(strTag);
  bool bResult=false;
  if (node && node->FirstChild() && clear)
    strStringValue.clear();
  while (node)
  {
    if (node->FirstChild())
    {
      bResult = true;
      strTemp = node->FirstChild()->Value();
      const char* clear=node->Attribute("clear");
      if (strStringValue.empty() || (clear && StringUtils::CompareNoCase(clear,"true")==0))
        strStringValue = strTemp;
      else
        strStringValue += strSeparator+strTemp;
    }
    node = node->NextSiblingElement(strTag);
  }

  return bResult;
}

void cPVRClientNextPVR::ParseNextPVRSubtitle( const char *episodeName, PVR_RECORDING   *tag)
{
    string strEpisodeName =  episodeName;
    std::regex base_regex("S(\\d\\d)E(\\d+) - ?(.+)?");
    std::smatch base_match;
    // note NextPVR does not support S0 for specials
    if (std::regex_match(strEpisodeName , base_match, base_regex))
    {
      if (base_match.size() == 3 || base_match.size() == 4)
      {
        std::ssub_match base_sub_match = base_match[1];
        tag->iSeriesNumber  = std::stoi(base_sub_match.str());
        base_sub_match = base_match[2];
        tag->iEpisodeNumber = std::stoi(base_sub_match.str());
        if (base_match.size() == 4)
        {
          base_sub_match = base_match[3];
          strcpy(tag->strEpisodeName,base_sub_match.str().c_str());
        }
      }
    }
    else
    {
      PVR_STRCPY(tag->strEpisodeName , strEpisodeName.c_str());
    }
}

PVR_ERROR cPVRClientNextPVR::DeleteRecording(const PVR_RECORDING &recording)
{
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "DeleteRecording");
  char request[512];
  if (recording.recordingTime < time(nullptr) && recording.recordingTime + recording.iDuration > time(nullptr))
    return PVR_ERROR_RECORDING_RUNNING;

  sprintf(request, "/service?method=recording.delete&recording_id=%s", recording.strRecordingId);

  std::string response;
  if (DoRequest(request, response) == HTTP_OK)
  {
    if (strstr(response.c_str(), "<rsp stat=\"ok\">"))
    {
      return PVR_ERROR_NO_ERROR;
    }
  }
  else
  {
    XBMC->Log(LOG_DEBUG, "DeleteRecording failed");
  }
  return PVR_ERROR_FAILED;
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
    m_lastRecordingUpdateTime = 0;
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
  if (m_iTimerCount != -1)
    return m_iTimerCount;

  std::string response;
  int timerCount = -1;
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
        for( pRecordingNode = recordingsNode->FirstChildElement("recording"); pRecordingNode; pRecordingNode=pRecordingNode->NextSiblingElement())
        {
          timerCount++;
        }
      }
    }
  }
  if (timerCount > -1)
  {
    m_iTimerCount = timerCount + 1;
  }
  LOG_API_IRET(__FUNCTION__, timerCount);
  return m_iTimerCount;
}

PVR_ERROR cPVRClientNextPVR::GetTimers(ADDON_HANDLE handle)
{
  std::string response;
  PVR_ERROR returnValue = PVR_ERROR_NO_ERROR;
  LOG_API_CALL(__FUNCTION__);
  int timerCount = 0;
  // first add the recurring recordings
  if (DoRequest("/service?method=recording.recurring.list", response) == HTTP_OK)
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
          if (tag.startTime < time(nullptr))
          {
            tag.startTime = 0;
          }
          else
          {
            tag.endTime = atol(pRulesNode->FirstChildElement("EndTimeTicks")->FirstChild()->Value());
          }
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
          else
          {
            tag.iTimerType = TIMER_REPEATING_ADVANCED;
            tag.startTime = 0;
            tag.endTime = 0;
            tag.bStartAnyTime = true;
            tag.bEndAnyTime = true;
            tag.bFullTextEpgSearch = true;
            strncpy(tag.strEpgSearchString, advancedRulesText.c_str(), sizeof(tag.strEpgSearchString) - 1);
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

        PVR_STRCPY(tag.strTitle,pRecurringNode->FirstChildElement("name")->FirstChild()->Value());

        tag.state = PVR_TIMER_STATE_SCHEDULED;

        PVR_STRCPY(tag.strSummary, "summary");

        // pass timer to xbmc
        timerCount++;
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
          UpdatePvrTimer(pRecordingNode, &tag);
          // pass timer to xbmc
          timerCount++;
          PVR->TransferTimerEntry(handle, &tag);
        }
      }
      response = "";
      if (DoRequest("/service?method=recording.list&filter=conflict", response) == HTTP_OK)
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
            UpdatePvrTimer(pRecordingNode, &tag);
            // pass timer to xbmc
            timerCount++;
            PVR->TransferTimerEntry(handle, &tag);
          }
          m_iTimerCount = timerCount;
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

bool cPVRClientNextPVR::UpdatePvrTimer(TiXmlElement* pRecordingNode, PVR_TIMER *tag)
{
  tag->iTimerType = pRecordingNode->FirstChildElement("epg_event_oid") ? TIMER_ONCE_EPG : TIMER_ONCE_MANUAL;

  tag->iClientIndex = atoi(pRecordingNode->FirstChildElement("id")->FirstChild()->Value());
  tag->iClientChannelUid = atoi(pRecordingNode->FirstChildElement("channel_id")->FirstChild()->Value());

  if (pRecordingNode->FirstChildElement("recurring_parent") != NULL)
  {
    tag->iParentClientIndex = atoi(pRecordingNode->FirstChildElement("recurring_parent")->FirstChild()->Value());
    if (tag->iParentClientIndex != PVR_TIMER_NO_PARENT)
    {
      if (tag->iTimerType == TIMER_ONCE_EPG)
      {
        tag->iTimerType = TIMER_ONCE_EPG_CHILD;
      }
      else
      {
        tag->iTimerType = TIMER_ONCE_MANUAL_CHILD;
      }
    }
    if (pRecordingNode->FirstChildElement("epg_event_oid") != NULL && pRecordingNode->FirstChildElement("epg_event_oid")->FirstChild() != NULL)
    {
      tag->iEpgUid = atoi(pRecordingNode->FirstChildElement("epg_event_oid")->FirstChild()->Value());
      XBMC->Log(LOG_DEBUG, "Setting timer epg id %d %d", tag->iClientIndex, tag->iEpgUid);
    }
  }

  // pre-padding
  if (pRecordingNode->FirstChildElement("pre_padding") != NULL)
  {
    tag->iMarginStart = atoi(pRecordingNode->FirstChildElement("pre_padding")->FirstChild()->Value());
  }

  // post-padding
  if (pRecordingNode->FirstChildElement("post_padding") != NULL)
  {
    tag->iMarginEnd = atoi(pRecordingNode->FirstChildElement("post_padding")->FirstChild()->Value());
  }

  // name
  PVR_STRCPY(tag->strTitle, pRecordingNode->FirstChildElement("name")->FirstChild()->Value());

  // description
  if (pRecordingNode->FirstChildElement("desc") != NULL && pRecordingNode->FirstChildElement("desc")->FirstChild() != NULL)
  {
    PVR_STRCPY(tag->strSummary, pRecordingNode->FirstChildElement("desc")->FirstChild()->Value());
  }

  // start/end time
  char start[32];
  strncpy(start, pRecordingNode->FirstChildElement("start_time_ticks")->FirstChild()->Value(), sizeof start);
  start[10] = '\0';
  tag->startTime           = atol(start);
  tag->endTime             = tag->startTime + atoi(pRecordingNode->FirstChildElement("duration_seconds")->FirstChild()->Value());

  tag->state = PVR_TIMER_STATE_SCHEDULED;
  if (pRecordingNode->FirstChildElement("status") != NULL && pRecordingNode->FirstChildElement("status")->FirstChild() != NULL)
  {
    std::string status = pRecordingNode->FirstChildElement("status")->FirstChild()->Value();
    if (status == "Recording" || (status == "Pending"  && tag->startTime < time(nullptr) + g_ServerTimeOffset) )
    {
      tag->state = PVR_TIMER_STATE_RECORDING;
    }
    else if (status == "Conflict")
    {
      tag->state = PVR_TIMER_STATE_CONFLICT_NOK;
    }
  }

  return true;
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
  static const int MSG_REPEATING_ADVANCED = 30171;

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
      PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL |
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
      PVR_TIMER_TYPE_SUPPORTS_ANY_CHANNEL |
      PVR_TIMER_TYPE_SUPPORTS_MAX_RECORDINGS;

  static const unsigned int TIMER_ADVANCED_ATTRIBS
    = PVR_TIMER_TYPE_SUPPORTS_CHANNELS |
      PVR_TIMER_TYPE_SUPPORTS_FULLTEXT_EPG_MATCH  |
      PVR_TIMER_TYPE_SUPPORTS_RECORDING_GROUP |
      PVR_TIMER_TYPE_SUPPORTS_START_END_MARGIN;



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

    timerTypes.push_back(
      /* Repeating epg based Parent*/
      std::unique_ptr<TimerType>(new TimerType(
      /* Type id. */
      TIMER_REPEATING_ADVANCED,
      /* Attributes. */
      TIMER_ADVANCED_ATTRIBS | TIMER_REPEATING_KEYWORD_ATTRIBS,
      /* Description. */
      XBMC->GetLocalizedString(MSG_REPEATING_ADVANCED), // "Repeating (advanced)"
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

  const std::string encodedName = UriEncode(timerinfo.strTitle);
  const std::string encodedKeyword = UriEncode(timerinfo.strEpgSearchString);
  const std::string days = GetDayString(timerinfo.iWeekdays);
  const std::string directory = UriEncode( m_recordingDirectories[timerinfo.iRecordingGroup]);
  const std::string oidKey = to_string(timerinfo.iEpgUid) + ":" + to_string(timerinfo.iClientChannelUid);
  const int epgOid = m_epgOidLookup[oidKey];
  XBMC->Log(LOG_DEBUG, "TIMER_%d %s",epgOid,oidKey.c_str());
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
      directory.c_str()
      );
    break;

  case TIMER_ONCE_EPG:
    XBMC->Log(LOG_DEBUG, "TIMER_ONCE_EPG");
    // build one-off recording request
    snprintf(request, sizeof(request), "/service?method=recording.save&recording_id=%d&event_id=%d&pre_padding=%d&post_padding=%d&directory_id=%s",
      timerinfo.iClientIndex,
      epgOid,
      (int)timerinfo.iMarginStart,
      (int)timerinfo.iMarginEnd,
      directory.c_str());
    break;

  case TIMER_REPEATING_EPG:
    if (timerinfo.iClientChannelUid == PVR_TIMER_ANY_CHANNEL)
    {
      // Fake a manual recording
      XBMC->Log(LOG_DEBUG, "TIMER_REPEATING_EPG ANY CHANNEL");
      string title = encodedName + "%";
      snprintf(request, sizeof(request), "/service?method=recording.recurring.save&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s,&keyword=%s",
        encodedName.c_str(),
        timerinfo.iClientChannelUid,
        (int)timerinfo.startTime,
        (int)timerinfo.endTime,
        (int)timerinfo.iMaxRecordings,
        (int)timerinfo.iMarginStart,
        (int)timerinfo.iMarginEnd,
        days.c_str(),
        directory.c_str(),
        title.c_str()
        );
    }
    else
    {
      XBMC->Log(LOG_DEBUG, "TIMER_REPEATING_EPG");
      // build recurring recording request
      snprintf(request, sizeof(request), "/service?method=recording.recurring.save&recurring_id=%d&event_id=%d&keep=%d&pre_padding=%d&post_padding=%d&day_mask=%s&directory_id=%s&only_new=%s",
        timerinfo.iClientIndex,
        epgOid,
        (int)timerinfo.iMaxRecordings,
        (int)timerinfo.iMarginStart,
        (int)timerinfo.iMarginEnd,
        days.c_str(),
        directory.c_str(),
        preventDuplicates
        );
    }
    break;

  case TIMER_REPEATING_MANUAL:
    XBMC->Log(LOG_DEBUG, "TIMER_REPEATING_MANUAL");
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
      directory.c_str()
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
      directory.c_str(),
      encodedKeyword.c_str(),
      preventDuplicates
      );
    break;

  case TIMER_REPEATING_ADVANCED:
    XBMC->Log(LOG_DEBUG, "TIMER_REPEATING_ADVANCED");
    // build manual advanced recurring request
    snprintf(request, sizeof(request), "/service?method=recording.recurring.save&recurring_type=advanced&recurring_id=%d&name=%s&channel_id=%d&start_time=%d&end_time=%d&keep=%d&pre_padding=%d&post_padding=%d&directory_id=%s&advanced=%s&only_new=%s",
      timerinfo.iClientIndex,
      encodedName.c_str(),
      timerinfo.iClientChannelUid,
      (int)timerinfo.startTime,
      (int)timerinfo.endTime,
      (int)timerinfo.iMaxRecordings,
      (int)timerinfo.iMarginStart,
      (int)timerinfo.iMarginEnd,
      directory.c_str(),
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
      if (timerinfo.startTime <= time(nullptr) && timerinfo.endTime > time(nullptr))
        PVR->TriggerRecordingUpdate();
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
      if (timer.startTime <= time(nullptr) && timer.endTime > time(nullptr))
        PVR->TriggerRecordingUpdate();
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
  if (channelinfo.bIsRadio == false)
  {
    g_NowPlaying = TV;
  }
  else
  {
    g_NowPlaying = Radio;
  }
  if (m_liveStreams.count(channelinfo.iUniqueId)!= 0)
  {
    snprintf(line,sizeof(line),"%s",m_liveStreams[channelinfo.iUniqueId].c_str());
    m_livePlayer = m_realTimeBuffer;
  }
  else if (channelinfo.bIsRadio == false && m_supportsLiveTimeshift && g_livestreamingmethod == Timeshift)
  {
    sprintf(line, "GET /live?channeloid=%d&mode=liveshift&client=XBMC-%s HTTP/1.0\r\n", channelinfo.iUniqueId, m_sid);
    m_livePlayer = m_timeshiftBuffer;
  }
  else if (g_livestreamingmethod == RollingFile)
  {
    sprintf(line, "http://%s:%d/live?channeloid=%d&client=XBMC-%s&epgmode=true", g_szHostname.c_str(), g_iPort, channelinfo.iUniqueId, m_sid);
    m_livePlayer = m_timeshiftBuffer;
  }
  else if (g_livestreamingmethod == ClientTimeshift)
  {
    sprintf(line, "http://%s:%d/live?channeloid=%d&client=%s&sid=%s", g_szHostname.c_str(), g_iPort, channelinfo.iUniqueId, m_sid,m_sid);
    m_livePlayer = m_timeshiftBuffer;
    m_livePlayer->Channel(channelinfo.iUniqueId);
  }
  else
  {
    sprintf(line, "http://%s:%d/live?channeloid=%d&client=XBMC-%s", g_szHostname.c_str(), g_iPort, channelinfo.iUniqueId, m_sid);
    m_livePlayer = m_realTimeBuffer;
  }
  XBMC->Log(LOG_INFO, "Calling Open(%s) on tsb!", line);
  if (m_livePlayer->Open(line))
  {
    return true;
  }
  return false;
}

int cPVRClientNextPVR::ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  LOG_API_CALL(__FUNCTION__);
  return m_livePlayer->Read(pBuffer, iBufferSize);
}

void cPVRClientNextPVR::CloseLiveStream(void)
{
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "CloseLiveStream");
  if (m_livePlayer != nullptr)
  {
    XBMC->Log(LOG_DEBUG, "CloseLiveStream");
    m_livePlayer->Close();
    m_livePlayer = nullptr;
  }
  XBMC->Log(LOG_DEBUG, "CloseLiveStream@exit");
  g_NowPlaying = NotPlaying;
}


long long cPVRClientNextPVR::SeekLiveStream(long long iPosition, int iWhence)
{
  long long retVal;
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "calling seek(%lli %d)", iPosition, iWhence);
  retVal = m_livePlayer->Seek(iPosition, iWhence);
  XBMC->Log(LOG_DEBUG, "returned from seek()");
  return retVal;
}


long long cPVRClientNextPVR::LengthLiveStream(void)
{
  LOG_API_CALL(__FUNCTION__);
  XBMC->Log(LOG_DEBUG, "seek length(%lli)", m_livePlayer->Length());
  return m_livePlayer->Length();
}

PVR_ERROR cPVRClientNextPVR::GetSignalStatus(PVR_SIGNAL_STATUS *signalStatus)
{
  LOG_API_CALL(__FUNCTION__);
  // Not supported yet
  if (g_NowPlaying == Transcoding)
  {
    m_livePlayer->Lease();
  }
  return PVR_ERROR_NO_ERROR;
}


bool cPVRClientNextPVR::CanPauseStream(void)
{
  LOG_API_CALL(__FUNCTION__);
  if (g_NowPlaying == Recording )
  {
    return true;
  }
  else
  {
    bool retval = m_livePlayer->CanPauseStream();
    return retval;
  }
}

void cPVRClientNextPVR::PauseStream(bool bPaused)
{
  LOG_API_CALL(__FUNCTION__);
  if (g_NowPlaying == Recording )
    m_recordingBuffer->PauseStream(bPaused);
  else
    m_livePlayer->PauseStream(bPaused);
}

bool cPVRClientNextPVR::CanSeekStream(void)
{
  LOG_API_CALL(__FUNCTION__);
  if (g_NowPlaying == Recording )
    return true;
  else
    return m_livePlayer->CanSeekStream();
}

/************************************************************/
/** Record stream handling */


bool cPVRClientNextPVR::OpenRecordedStream(const PVR_RECORDING &recording)
{
  PVR_RECORDING copyRecording = recording;
  LOG_API_CALL(__FUNCTION__);
  m_currentRecordingLength = 0;
  m_currentRecordingPosition = 0;

  char line[1024];
  g_NowPlaying = Recording;
  strcpy(copyRecording.strDirectory,m_hostFilenames[recording.strRecordingId].c_str());
  snprintf(line, sizeof(line), "http://%s:%d/live?recording=%s&client=XBMC-%s", g_szHostname.c_str(), g_iPort, recording.strRecordingId, m_sid);
  return m_recordingBuffer->Open(line,copyRecording);
}

void cPVRClientNextPVR::CloseRecordedStream(void)
{
  LOG_API_CALL(__FUNCTION__);
  m_recordingBuffer->Close();
  m_recordingBuffer->SetDuration(0);
  m_currentRecordingLength = 0;
  m_currentRecordingPosition = 0;
  g_NowPlaying = NotPlaying;
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
  int resultCode =  NextPVR::m_backEnd->DoRequest(resource, response);
  return resultCode;
}

bool cPVRClientNextPVR::IsTimeshifting()
{
  LOG_API_CALL(__FUNCTION__);
  if (g_NowPlaying == Recording )
    return false;
  else
    return m_livePlayer->IsTimeshifting();
}

bool cPVRClientNextPVR::IsRealTimeStream()
{
  LOG_API_CALL(__FUNCTION__);
  bool retval;
  if (g_NowPlaying == Recording )
  {
    retval = m_recordingBuffer->IsRealTimeStream();
  }
  else
  {
    retval = m_livePlayer->IsRealTimeStream();
  }
  return retval;
}

PVR_ERROR cPVRClientNextPVR::GetStreamTimes(PVR_STREAM_TIMES *stimes)
{
  PVR_ERROR rez;
  LOG_API_CALL(__FUNCTION__);
  if (g_NowPlaying == Recording )
    rez = m_recordingBuffer->GetStreamTimes(stimes);
  else
    rez = m_livePlayer->GetStreamTimes(stimes);
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
  if (g_NowPlaying == Recording )
    rez = m_recordingBuffer->GetStreamReadChunkSize(chunksize);
  else
    rez = m_livePlayer->GetStreamReadChunkSize(chunksize);
  LOG_API_IRET(__FUNCTION__, *chunksize);
  return rez;
}

bool cPVRClientNextPVR::SaveSettings(std::string name, std::string value)
{
  bool found = false;
  TiXmlDocument doc;

  char *settings = XBMC->TranslateSpecialProtocol("special://profile/addon_data/pvr.nextpvr/settings.xml");
  if (doc.LoadFile(settings))
  {
    //Get Root Node
    TiXmlElement* rootNode = doc.FirstChildElement("settings");
    if (rootNode)
    {
      TiXmlElement* childNode;
      std::string key_value;
      for( childNode = rootNode->FirstChildElement("setting"); childNode; childNode=childNode->NextSiblingElement())
      {
        if ( childNode->QueryStringAttribute("id", &key_value)==TIXML_SUCCESS)
        {
          if (key_value == name)
          {
            if (childNode->FirstChild() != NULL)
            {
              childNode->FirstChild()->SetValue( value );
              found = true;
              break;
            }
            return false;
          }
        }
      }
      if (found == false)
      {
        TiXmlElement *newSetting = new TiXmlElement( "setting" );
        TiXmlText *newvalue = new TiXmlText( value );
        newSetting->SetAttribute("id", name);
        newSetting->LinkEndChild( newvalue );
        rootNode->LinkEndChild( newSetting);
      }
      ADDON_SetSetting(name.c_str(),value.c_str());
      doc.SaveFile(settings);
    }
  }
  else
  {
    XBMC->Log(LOG_ERROR, "Error loading settings.xml %s", settings);
  }
  XBMC->FreeString(settings);
  return true;
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
  // XBMC->Log(LOG_INFO, "\n");
  while (pAttrib)
  {
    char buf[1024];
    sprintf(buf, "%s%s: value=[%s]", pIndent, pAttrib->Name(), pAttrib->Value());
    // XBMC->Log(LOG_INFO,  "%s%s: value=[%s]", pIndent, pAttrib->Name(), pAttrib->Value());

    if (pAttrib->QueryIntValue(&ival)==TIXML_SUCCESS)
    {
      sprintf(buf + strlen(buf), " int=%d", ival);
      // XBMC->Log(LOG_INFO,  " int=%d", ival);
    }
    if (pAttrib->QueryDoubleValue(&dval)==TIXML_SUCCESS)
    {
      sprintf(buf + strlen(buf), " d=%1.1f", dval);
      // XBMC->Log(LOG_INFO,  " d=%1.1f", dval);
    }
    XBMC->Log(LOG_INFO,  "%s", buf );
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
  // XBMC->Log(LOG_INFO,  "%s", getIndent(indent));
  int num;

  switch ( t )
  {
  case TiXmlNode::TINYXML_DOCUMENT:
    sprintf(buf + strlen(buf), "Document");
    // XBMC->Log(LOG_INFO,  "Document" );
    break;

  case TiXmlNode::TINYXML_ELEMENT:
    sprintf(buf + strlen(buf), "Element [%s]", pParent->Value());
    // XBMC->Log(LOG_INFO,  "Element [%s]", pParent->Value() );
    num=dump_attribs_to_stdout(pParent->ToElement(), indent+1);
    switch(num)
    {
      case 0:
        sprintf(buf + strlen(buf), " (No attributes)");
        // XBMC->Log(LOG_INFO,  " (No attributes)");
        break;
      case 1:
        sprintf(buf + strlen(buf), "%s1 attribute", getIndentAlt(indent));
        // XBMC->Log(LOG_INFO,  "%s1 attribute", getIndentAlt(indent));
        break;
      default:
        sprintf(buf + strlen(buf), "%s%d attributes", getIndentAlt(indent), num);
        // XBMC->Log(LOG_INFO,  "%s%d attributes", getIndentAlt(indent), num);
        break;
    }
    break;

  case TiXmlNode::TINYXML_COMMENT:
    sprintf(buf + strlen(buf), "Comment: [%s]", pParent->Value());
    //XBMC->Log(LOG_INFO,  "Comment: [%s]", pParent->Value());
    break;

  case TiXmlNode::TINYXML_UNKNOWN:
    sprintf(buf + strlen(buf), "Unknown");
    // XBMC->Log(LOG_INFO,  "Unknown" );
    break;

  case TiXmlNode::TINYXML_TEXT:
    pText = pParent->ToText();
    sprintf(buf + strlen(buf), "Text: [%s]", pText->Value());
    // XBMC->Log(LOG_INFO,  "Text: [%s]", pText->Value() );
    break;

  case TiXmlNode::TINYXML_DECLARATION:
    sprintf(buf + strlen(buf), "Declaration");
    // XBMC->Log(LOG_INFO,  "Declaration" );
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
