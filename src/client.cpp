/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "client.h"
#include "kodi/xbmc_pvr_dll.h"
#include "pvrclient-nextpvr.h"
#include "uri.h"

using namespace std;
using namespace ADDON;

#define PVR_MIN_API_VERSION "1.2.0"

/* User adjustable settings are saved here.
 * Default values are defined inside client.h
 * and exported to the other source files.
 */
std::string      g_szHostname             = DEFAULT_HOST;                  ///< The Host name or IP of the NextPVR server
std::string      g_szPin                  = DEFAULT_PIN;                   ///< The PIN for the NextPVR server
int              g_iPort                  = DEFAULT_PORT;                  ///< The web listening port (default: 8866)
int g_timeShiftBufferSeconds = 0;
std::string      g_host_mac = "";
eStreamingMethod g_livestreamingmethod = RealTime;
eNowPlaying      g_NowPlaying = NotPlaying;
int              g_wol_timeout;
bool             g_wol_enabled;
bool             g_KodiLook;

/* Client member variables */
ADDON_STATUS           m_CurStatus    = ADDON_STATUS_UNKNOWN;
cPVRClientNextPVR     *g_client       = NULL;
std::string            g_szUserPath   = "";
std::string            g_szClientPath = "";

CHelper_libXBMC_addon *XBMC           = NULL;
CHelper_libXBMC_pvr   *PVR            = NULL;
bool                   g_bDownloadGuideArtwork = false;

int g_backendVersion{ 0 };
bool g_sendSidWithMetadata{ false };
bool g_readLiveStreams{ false };
bool g_showRecordingSize{ false };
eEventArt g_eventArtFormat{ eEventArt::Landscape };

extern "C" {

void ADDON_ReadSettings();

/***********************************************************
 * Standard AddOn related public library functions
 ***********************************************************/

//-- Create -------------------------------------------------------------------
// Called after loading of the dll, all steps to become Client functional
// must be performed here.
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!hdl || !props)
    return ADDON_STATUS_UNKNOWN;

  AddonProperties_PVR* pvrprops = (AddonProperties_PVR*)props;

  XBMC = new CHelper_libXBMC_addon;
  if (!XBMC->RegisterMe(hdl))
  {
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  PVR = new CHelper_libXBMC_pvr;
  if (!PVR->RegisterMe(hdl))
  {
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
    return ADDON_STATUS_PERMANENT_FAILURE;
  }

  XBMC->Log(LOG_INFO, "Creating NextPVR PVR-Client");

  m_CurStatus    = ADDON_STATUS_UNKNOWN;
  g_szUserPath   = pvrprops->strUserPath;
  g_szClientPath = pvrprops->strClientPath;

  ADDON_ReadSettings();

  /* Create connection to NextPVR KODI TV client */
  g_client       = new cPVRClientNextPVR();
  m_CurStatus = g_client->Connect();
  if (m_CurStatus == ADDON_STATUS_NEED_SETTINGS && g_szHostname == DEFAULT_HOST)
  {
    // if there is no settings.xml try discovery
    if (!XBMC->FileExists("special://profile/addon_data/pvr.nextpvr/settings.xml",false))
    {
      XBMC->Log(LOG_ERROR, "Couldn't connect default connection");
      std::vector<std::string> foundAddress = NextPVR::m_backEnd->Discovery();
      if (!foundAddress.empty())
      {
        g_szHostname = foundAddress[0];
        g_iPort = stoi(foundAddress[1]);
        m_CurStatus = g_client->Connect();
        if (m_CurStatus == ADDON_STATUS_OK)
        {
          XBMC->QueueNotification(QUEUE_INFO,XBMC->GetLocalizedString(30182),g_szHostname.c_str(),g_iPort);
        }
      }
    }
  }

  if (m_CurStatus != ADDON_STATUS_OK)
  {
    SAFE_DELETE(g_client);
    SAFE_DELETE(PVR);
    SAFE_DELETE(XBMC);
  }

  return m_CurStatus;
}

//-- Destroy ------------------------------------------------------------------
// Used during destruction of the client, all steps to do clean and safe Create
// again must be done.
//-----------------------------------------------------------------------------
void ADDON_Destroy()
{
  SAFE_DELETE(g_client);
  SAFE_DELETE(PVR);
  SAFE_DELETE(XBMC);

  m_CurStatus = ADDON_STATUS_UNKNOWN;
}

//-- GetStatus ----------------------------------------------------------------
// Report the current Add-On Status to XBMC
// Note currently not called but needed to load
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_GetStatus()
{
  /* check whether we're still connected */
  if (m_CurStatus == ADDON_STATUS_OK && g_client && !g_client->IsUp())
    m_CurStatus = ADDON_STATUS_LOST_CONNECTION;

  return m_CurStatus;
}

void ADDON_ReadSettings(void)
{
  /* Read setting "host" from settings.xml */
  char buffer[1024];

  if (!XBMC)
    return;

  /* Connection settings */
  /***********************/
  if (XBMC->GetSetting("host", &buffer))
  {
    g_szHostname = buffer;
    uri::decode(g_szHostname);
  }
  else
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'host' setting, falling back to '127.0.0.1' as default");
    g_szHostname = DEFAULT_HOST;
  }

  /* Read setting "port" from settings.xml */
  if (!XBMC->GetSetting("port", &g_iPort))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'port' setting, falling back to '8866' as default");
    g_iPort = DEFAULT_PORT;
  }

  /* Read setting "pin" from settings.xml */
  if (XBMC->GetSetting("pin", &buffer))
  {
    g_szPin = buffer;
  }
  else
  {
    g_szPin = DEFAULT_PIN;
  }

  /* Read setting "guideartwork" from settings.xml */
  if (!XBMC->GetSetting("guideartwork", &g_bDownloadGuideArtwork))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'guideartwork' setting, falling back to 'false' as default");
    g_bDownloadGuideArtwork = DEFAULT_GUIDE_ARTWORK;
  }

  if (XBMC->GetSetting("host_mac", &buffer))
  {
    g_host_mac = buffer;
  }

  if (!XBMC->GetSetting("wolenable", &g_wol_enabled))
  {
    g_wol_enabled = false;
  }

  if (!XBMC->GetSetting("woltimeout", &g_wol_timeout))
  {
    g_wol_timeout = 20;
  }

  if (!XBMC->GetSetting("kodilook", &g_KodiLook))
  {
    g_KodiLook = false;
  }

  /* Log the current settings for debugging purposes */
  XBMC->Log(LOG_DEBUG, "settings: host='%s', port=%i, mac=%4.4s...", g_szHostname.c_str(), g_iPort, g_host_mac.c_str());

}

//-- SetSetting ---------------------------------------------------------------
// Called everytime a setting is changed by the user and to inform AddOn about
// new setting and to do required stuff to apply it.
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
  string str = settingName;

  // SetSetting can occur when the addon is enabled, but TV support still
  // disabled. In that case the addon is not loaded, so we should not try
  // to change its settings.
  if (!XBMC)
    return ADDON_STATUS_OK;

  if (str == "host")
  {
    string tmp_sHostname = (const char*) settingValue;
    if (tmp_sHostname != g_szHostname)
    {
      XBMC->Log(LOG_INFO, "Changed Setting 'host' from %s to %s", g_szHostname.c_str(), tmp_sHostname.c_str());
      g_szHostname = tmp_sHostname;
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (str == "port")
  {
    if (g_iPort != *(int*) settingValue)
    {
      XBMC->Log(LOG_INFO, "Changed Setting 'port' from %u to %u", g_iPort, *(int*) settingValue);
      g_iPort = *(int*) settingValue;
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (str == "pin")
  {
    string tmp_sPin = (const char*) settingValue;
    if (tmp_sPin != g_szPin)
    {
      XBMC->Log(LOG_INFO, "Changed Setting 'pin'");
      g_szPin = tmp_sPin;
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (str == "guideartwork")
  {
    if ( g_bDownloadGuideArtwork != *(bool*)settingValue)
    {
      XBMC->Log(LOG_INFO, "Changed setting 'guideartwork' from %u to %u", g_bDownloadGuideArtwork, *(bool*)settingValue);
      g_bDownloadGuideArtwork = *(bool*)settingValue;
    }
  }
  else if (str == "kodilook")
  {
    if ( g_KodiLook != *(bool*)settingValue)
    {
      XBMC->Log(LOG_INFO, "Changed setting 'kodilook' from %u to %u", g_KodiLook, *(bool*)settingValue);
      g_KodiLook = *(bool*)settingValue;
      if (g_client)
        PVR->TriggerRecordingUpdate();
    }
  }
  else if (str == "livestreamingmethod")
  {
    if (g_client)
    {
      if (g_client->GetBackendVersion() < 50000)
      {
        eStreamingMethod  setting_livestreamingmethod = *(eStreamingMethod*)settingValue;
        if (g_livestreamingmethod != setting_livestreamingmethod)
        {
          g_livestreamingmethod = setting_livestreamingmethod;
          return ADDON_STATUS_NEED_RESTART;
        }
      }
    }
  }
  else if (str ==  "livestreamingmethod5")
  {
    if (g_client)
    {
      if (g_client->GetBackendVersion() >= 50000)
      {
        eStreamingMethod  setting_livestreamingmethod = *(eStreamingMethod*)settingValue;
        if (g_livestreamingmethod != setting_livestreamingmethod)
        {
          g_livestreamingmethod = setting_livestreamingmethod;
          return ADDON_STATUS_NEED_RESTART;
        }
      }
    }
  }
  else if (str == "host_mac")
  {
    if ( g_host_mac != (const char *)settingValue )
    {
      XBMC->Log(LOG_INFO, "Changed setting 'host_mac' from %4.4s... to %4.4s...", g_host_mac.c_str(), (const char *)settingValue );
      g_host_mac = (const char *) settingValue;
      return ADDON_STATUS_OK ;
    }
  }
  else if (str == "reseticons")
  {
    if (*(bool*)settingValue == true)
    {
      XBMC->Log(LOG_INFO, "Flagging icon reset");
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else
  {
    XBMC->Log(LOG_DEBUG, "Skipping setting %s",str.c_str());
  }

  return ADDON_STATUS_OK;
}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/

void OnSystemSleep()
{
  if (g_client)
    g_client->OnSystemSleep();
}

void OnSystemWake()
{
  if (g_client)
    g_client->OnSystemWake();
}

void OnPowerSavingActivated()
{
}

void OnPowerSavingDeactivated()
{
}

//-- GetCapabilities -----------------------------------------------------
// Tell XBMC our requirements
//-----------------------------------------------------------------------------
PVR_ERROR GetCapabilities(PVR_ADDON_CAPABILITIES *pCapabilities)
{
  XBMC->Log(LOG_DEBUG, "->GetProperties()");

  pCapabilities->bSupportsEPG                = true;
  pCapabilities->bSupportsRecordings         = true;
  pCapabilities->bSupportsRecordingsUndelete = false;
  pCapabilities->bSupportsTimers             = true;
  pCapabilities->bSupportsTV                 = true;
  pCapabilities->bSupportsRadio              = true;
  pCapabilities->bSupportsChannelGroups      = true;
  pCapabilities->bHandlesInputStream         = true;
  pCapabilities->bHandlesDemuxing            = false;
  pCapabilities->bSupportsChannelScan        = false;
  pCapabilities->bSupportsLastPlayedPosition = true;
  pCapabilities->bSupportsRecordingEdl       = true;
  pCapabilities->bSupportsRecordingsRename   = false;
  pCapabilities->bSupportsRecordingsLifetimeChange = false;
  pCapabilities->bSupportsDescrambleInfo = false;

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES *pProperties)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

//-- GetBackendName -----------------------------------------------------------
// Return the Name of the Backend
//-----------------------------------------------------------------------------
const char * GetBackendName(void)
{
  if (g_client)
    return g_client->GetBackendName();
  else
    return "";
}

//-- GetBackendVersion --------------------------------------------------------
// Return the Version of the Backend as String
//-----------------------------------------------------------------------------
const char * GetBackendVersion(void)
{
  if (g_client)
    return g_client->GetBackendVersionString();
  else
    return "";
}

//-- GetConnectionString ------------------------------------------------------
// Return a String with connection info, if available
//-----------------------------------------------------------------------------
const char * GetConnectionString(void)
{
  if (g_client)
    return g_client->GetConnectionString();
  else
    return "addon error!";
}

//-- GetBackendHostname -------------------------------------------------------
// Return a String with the backend host name
//-----------------------------------------------------------------------------
const char * GetBackendHostname(void)
{
  return g_szHostname.c_str();
}

//-- GetDriveSpace ------------------------------------------------------------
// Return the Total and Free Drive space on the PVR Backend
//-----------------------------------------------------------------------------
PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetDriveSpace(iTotal, iUsed);
}

PVR_ERROR OpenDialogChannelScan()
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook, const PVR_MENUHOOK_DATA &item)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}


/*******************************************/
/** PVR EPG Functions                     **/

PVR_ERROR GetEPGForChannel(ADDON_HANDLE handle, int iChannelUid, time_t iStart, time_t iEnd)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetEpg(handle, iChannelUid, iStart, iEnd);
}


/*******************************************/
/** PVR Channel Functions                 **/

int GetChannelsAmount()
{
  if (!g_client)
    return 0;
  else
    return g_client->GetNumChannels();
}

PVR_ERROR GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetChannels(handle, bRadio);
}

PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR RenameChannel(const PVR_CHANNEL &channel)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR OpenDialogChannelSettings(const PVR_CHANNEL &channelinfo)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR OpenDialogChannelAdd(const PVR_CHANNEL &channelinfo)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}


/*******************************************/
/** PVR Channel group Functions           **/

int GetChannelGroupsAmount(void)
{
  if (!g_client)
    return 0;
  else
    return g_client->GetChannelGroupsAmount();
}

PVR_ERROR GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetChannelGroups(handle, bRadio);
}

PVR_ERROR GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetChannelGroupMembers(handle, group);
}


/*******************************************/
/** PVR Recording Functions               **/

int GetRecordingsAmount(bool deleted)
{
  if (!g_client)
    return 0;
  else
    return g_client->GetNumRecordings();
}

PVR_ERROR GetRecordings(ADDON_HANDLE handle, bool deleted)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetRecordings(handle);
}

PVR_ERROR DeleteRecording(const PVR_RECORDING &recording)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->DeleteRecording(recording);
}

PVR_ERROR RenameRecording(const PVR_RECORDING &recording)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}


/*******************************************/
/** PVR Timer Functions                   **/

PVR_ERROR GetTimerTypes(PVR_TIMER_TYPE types[], int *size)
{
  if (g_client)
    return g_client->GetTimerTypes(types, size);
  return PVR_ERROR_SERVER_ERROR;
}

int GetTimersAmount(void)
{
  if (!g_client)
    return 0;
  else
    return g_client->GetNumTimers();
}

PVR_ERROR GetTimers(ADDON_HANDLE handle)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetTimers(handle);
}

PVR_ERROR AddTimer(const PVR_TIMER &timer)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->AddTimer(timer);
}

PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->DeleteTimer(timer, bForceDelete);
}

PVR_ERROR UpdateTimer(const PVR_TIMER &timer)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->UpdateTimer(timer);
}


/*******************************************/
/** PVR Live Stream Functions             **/

bool OpenLiveStream(const PVR_CHANNEL &channelinfo)
{
  if (!g_client)
    return false;
  else
    return g_client->OpenLiveStream(channelinfo);
}

void CloseLiveStream()
{
  if (g_client)
    g_client->CloseLiveStream();
}

int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  if (!g_client)
    return 0;
  else
    return g_client->ReadLiveStream(pBuffer, iBufferSize);
}

long long SeekLiveStream(long long iPosition, int iWhence)
{
  if (!g_client)
    return -1;
  else
    return g_client->SeekLiveStream(iPosition, iWhence);
}

long long LengthLiveStream(void)
{
  if (!g_client)
    return -1;
  else
    return g_client->LengthLiveStream();
}

PVR_ERROR GetSignalStatus(int channelUid, PVR_SIGNAL_STATUS *signalStatus)
{
  if (!g_client)
    return PVR_ERROR_SERVER_ERROR;
  else
    return g_client->GetSignalStatus(signalStatus);
}

PVR_ERROR GetChannelStreamProperties(const PVR_CHANNEL* channel, PVR_NAMED_VALUE* properties, unsigned int* iPropertiesCount)
{
  if (g_client)
    return g_client->GetChannelStreamProperties(*channel,properties,iPropertiesCount);
  return PVR_ERROR_NOT_IMPLEMENTED;
}

/*******************************************/
/** PVR Recording Stream Functions        **/

bool OpenRecordedStream(const PVR_RECORDING &recording)
{
  if (!g_client)
    return false;
  else
    return g_client->OpenRecordedStream(recording);
}

void CloseRecordedStream(void)
{
  if (g_client)
    g_client->CloseRecordedStream();
}

int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  if (!g_client)
    return 0;
  else
    return g_client->ReadRecordedStream(pBuffer, iBufferSize);
}

long long SeekRecordedStream(long long iPosition, int iWhence)
{
  if (!g_client)
    return -1;
  else
    return g_client->SeekRecordedStream(iPosition, iWhence);
}

long long LengthRecordedStream(void)
{
  if (!g_client)
    return -1;
  else
    return g_client->LengthRecordedStream();
}

bool CanPauseStream(void)
{
  if (g_client)
    return g_client->CanPauseStream();
  return false;
}

void PauseStream(bool bPaused)
{
  if (g_client)
    g_client->PauseStream(bPaused);
}

bool CanSeekStream(void)
{
  if (g_client)
    return g_client->CanPauseStream();
  return false;
}

PVR_ERROR SetRecordingLastPlayedPosition(const PVR_RECORDING &recording, int lastplayedposition)
{
  if (g_client)
    return g_client->SetRecordingLastPlayedPosition(recording, lastplayedposition);
  return PVR_ERROR_SERVER_ERROR;
}

int GetRecordingLastPlayedPosition(const PVR_RECORDING &recording)
{
  if (g_client)
    return g_client->GetRecordingLastPlayedPosition(recording);
  return -1;
}

PVR_ERROR GetRecordingEdl(const PVR_RECORDING &recording, PVR_EDL_ENTRY entries[], int *size)
{
  if (g_client)
    return g_client->GetRecordingEdl(recording, entries, size);
  return PVR_ERROR_SERVER_ERROR;
}

bool IsRealTimeStream(void)
{
  if (g_client)
    return g_client->IsRealTimeStream();
  return false;
}

PVR_ERROR GetStreamTimes(PVR_STREAM_TIMES *stimes)
{
  if (g_client)
    return g_client->GetStreamTimes(stimes);
  return PVR_ERROR_SERVER_ERROR;
}

PVR_ERROR GetStreamReadChunkSize(int* chunksize)
{
  if (g_client)
    return g_client->GetStreamReadChunkSize(chunksize);
  return PVR_ERROR_SERVER_ERROR;
}

/** UNUSED API FUNCTIONS */
DemuxPacket* DemuxRead(void) { return NULL; }
void DemuxAbort(void) {}
void DemuxReset(void) {}
void DemuxFlush(void) {}
void FillBuffer(bool mode) {}

PVR_ERROR SetRecordingPlayCount(const PVR_RECORDING &recording, int count) { return PVR_ERROR_NOT_IMPLEMENTED; }

bool SeekTime(double,bool,double*) { return false; }
void SetSpeed(int) {};
PVR_ERROR UndeleteRecording(const PVR_RECORDING& recording) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR DeleteAllRecordingsFromTrash() { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetRecordingSize(const PVR_RECORDING* recording, int64_t* sizeInBytes) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetEPGTimeFrame(int) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetDescrambleInfo(int, PVR_DESCRAMBLE_INFO*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR SetRecordingLifetime(const PVR_RECORDING*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetRecordingStreamProperties(const PVR_RECORDING*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR IsEPGTagRecordable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR IsEPGTagPlayable(const EPG_TAG*, bool*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetEPGTagStreamProperties(const EPG_TAG*, PVR_NAMED_VALUE*, unsigned int*) { return PVR_ERROR_NOT_IMPLEMENTED; }
PVR_ERROR GetEPGTagEdl(const EPG_TAG* epgTag, PVR_EDL_ENTRY edl[], int *size) { return PVR_ERROR_NOT_IMPLEMENTED; }

} //end extern "C"
