/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Channels.h"

#include "kodi/util/XMLUtils.h"

#include "pvrclient-nextpvr.h"

#include <p8-platform/util/StringUtils.h>

using namespace NextPVR;

/** Channel handling */

int Channels::GetNumChannels(void)
{
  LOG_API_CALL(__FUNCTION__);
  // need something more optimal, but this will do for now...
  std::string response;
  int channelCount = 0;
  if (m_request.DoRequest("/service?method=channel.list", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
    {
      TiXmlElement* channelsNode = doc.RootElement()->FirstChildElement("channels");
      TiXmlElement* pChannelNode;
      for( pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode=pChannelNode->NextSiblingElement())
      {
        channelCount++;
      }
    }
  }

  return channelCount;
}

std::string Channels::GetChannelIcon(int channelID)
{
  LOG_API_CALL(__FUNCTION__);

  std::string iconFilename = GetChannelIconFileName(channelID);

  // do we already have the icon file?
  if (XBMC->FileExists(XBMC->TranslateSpecialProtocol(iconFilename.c_str()), false))
  {
    return iconFilename;
  }
  const std::string URL = "/service?method=channel.icon&channel_id=" + std::to_string(channelID);
  if (m_request.FileCopy(URL.c_str(), iconFilename) == HTTP_OK)
  {
    return iconFilename;
  }

  return "";
}

std::string Channels::GetChannelIconFileName(int channelID)
{
  return StringUtils::Format("special://userdata/addon_data/pvr.nextpvr/nextpvr-ch%d.png",channelID);
}

void  Channels::DeleteChannelIcon(int channelID)
{
  #if defined(TARGET_WINDOWS)
    #undef DeleteFile
  #endif
  XBMC->DeleteFile(GetChannelIconFileName(channelID).c_str());
}

void  Channels::DeleteChannelIcons()
{
  VFSDirEntry* icons;
  unsigned int count;
  if (XBMC->GetDirectory("special://userdata/addon_data/pvr.nextpvr/","nextpvr-ch*.png", &icons, &count))
  {
    XBMC->Log(LOG_INFO, "Deleting %d channel icons", count);
    for (unsigned int i = 0; i < count; ++i)
    {
      VFSDirEntry& f = icons[i];
      const std::string deleteme = f.path;
      XBMC->Log(LOG_DEBUG,"DeleteFile %s rc:%d",XBMC->TranslateSpecialProtocol(f.path),remove(XBMC->TranslateSpecialProtocol(f.path)));
    }
  }
}

PVR_ERROR Channels::GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  PVR_CHANNEL     tag;
  std::string      stream;
  LOG_API_CALL(__FUNCTION__);

  std::map<int, std::pair<bool, bool>>::iterator  itr = m_channelDetails.begin();
  while (itr != m_channelDetails.end())
  {
    if (itr->second.second == (bRadio == true))
      itr = m_channelDetails.erase(itr);
    else
      ++itr;
  }

  std::string response;
  if (m_request.DoRequest("/service?method=channel.list&extras=true", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
    {
      TiXmlElement* channelsNode = doc.RootElement()->FirstChildElement("channels");
      TiXmlElement* pChannelNode;
      for( pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode=pChannelNode->NextSiblingElement())
      {
        tag = {0};
        tag.iUniqueId = g_pvrclient->XmlGetUInt(pChannelNode, "id");
        std::string buffer;
        XMLUtils::GetString(pChannelNode,"type",buffer);
        if ( buffer =="0xa")
        {
          tag.bIsRadio = true;
          strcpy(tag.strInputFormat, "application/octet-stream");
        }
        else
        {
          tag.bIsRadio = false;
          if (IsChannelAPlugin(tag.iUniqueId))
            strcpy(tag.strInputFormat, "video/MP2T");
        }
        if (bRadio != tag.bIsRadio)
          continue;

        tag.iChannelNumber = g_pvrclient->XmlGetUInt(pChannelNode, "number");
        tag.iSubChannelNumber = g_pvrclient->XmlGetUInt(pChannelNode, "minor");

        buffer.clear();
        XMLUtils::GetString(pChannelNode,"name",buffer);
        buffer.copy(tag.strChannelName,sizeof(tag.strChannelName)-1);

        // check if we need to download a channel icon
        bool isIcon;
        if (XMLUtils::GetBoolean(pChannelNode,"icon",isIcon))
        {
          // only set when true;
          std::string iconFile = GetChannelIcon(tag.iUniqueId);
          if (iconFile.length() > 0)
            iconFile.copy(tag.strIconPath,sizeof(tag.strIconPath)-1);
        }

        // V5 has the EPG source type info.
        std::string epg;
        if (XMLUtils::GetString(pChannelNode, "epg", epg))
          m_channelDetails[tag.iUniqueId] = std::make_pair(epg == "None", tag.bIsRadio);
        else
          m_channelDetails[tag.iUniqueId] = std::make_pair(false, tag.bIsRadio);

        // transfer channel to XBMC
        PVR->TransferChannelEntry(handle, &tag);
      }
    }
  }
  return PVR_ERROR_NO_ERROR;
}

/************************************************************/
/** Channel group handling **/

int Channels::GetChannelGroupsAmount(void)
{
  LOG_API_CALL(__FUNCTION__);
  // Not directly possible at the moment
  XBMC->Log(LOG_DEBUG, "GetChannelGroupsAmount");

  int groups = 0;

  std::string response;
  if (m_request.DoRequest("/service?method=channel.groups", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
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

PVR_RECORDING_CHANNEL_TYPE Channels::GetChannelType(unsigned int uid)
{
  // when uid is invalid we assume TV because Kodi will
  if (m_channelDetails.count(uid) > 0 && m_channelDetails[uid].second == true)
    return PVR_RECORDING_CHANNEL_TYPE_RADIO;

  return PVR_RECORDING_CHANNEL_TYPE_TV;
}

PVR_ERROR Channels::GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  PVR_CHANNEL_GROUP tag;
  LOG_API_CALL(__FUNCTION__);

  // nextpvr doesn't have a separate concept of radio channel groups
  if (bRadio)
    return PVR_ERROR_NO_ERROR;

  // for tv, use the groups returned by nextpvr
  std::string response;
  if (m_request.DoRequest("/service?method=channel.groups", response) == HTTP_OK)
  {
    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
    {
      TiXmlElement* groupsNode = doc.RootElement()->FirstChildElement("groups");
      TiXmlElement* pGroupNode;
      for( pGroupNode = groupsNode->FirstChildElement("group"); pGroupNode; pGroupNode=pGroupNode->NextSiblingElement())
      {
        tag = {0};
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


PVR_ERROR Channels::GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  std::string encodedGroupName = UriEncode(group.strGroupName);
  LOG_API_CALL(__FUNCTION__);

  std::string request = "/service?method=channel.list&group_id=" + encodedGroupName;
  std::string response;
  if (m_request.DoRequest(request.c_str(), response) == HTTP_OK)
  {
    PVR_CHANNEL_GROUP_MEMBER tag;

    TiXmlDocument doc;
    if (doc.Parse(response.c_str()) != nullptr)
    {
      TiXmlElement* channelsNode = doc.RootElement()->FirstChildElement("channels");
      TiXmlElement* pChannelNode;
      for( pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode=pChannelNode->NextSiblingElement())
      {
        tag = {0};
        strcpy(tag.strGroupName, group.strGroupName);
        tag.iChannelNumber = g_pvrclient->XmlGetUInt(pChannelNode, "id");
        tag.iChannelNumber = g_pvrclient->XmlGetUInt(pChannelNode, "number");
        tag.iSubChannelNumber = g_pvrclient->XmlGetUInt(pChannelNode, "minor");
        ;
        PVR->TransferChannelGroupMember(handle, &tag);
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}

bool Channels::IsChannelAPlugin(int uid)
{
  if (m_liveStreams.count(uid) != 0)
    if (StringUtils::StartsWith(m_liveStreams[uid], "plugin:") || StringUtils::EndsWithNoCase(m_liveStreams[uid], ".m3u8"))
      return true;

  return false;
}

/************************************************************/
void Channels::LoadLiveStreams()
{
  const std::string URL = "/public/LiveStreams.xml";
  m_liveStreams.clear();
  if (m_request.FileCopy(URL.c_str(), "special://userdata/addon_data/pvr.nextpvr/LiveStreams.xml") == HTTP_OK)
  {
    TiXmlDocument doc;
    char* liveStreams = XBMC->TranslateSpecialProtocol("special://userdata/addon_data/pvr.nextpvr/LiveStreams.xml");
    XBMC->Log(LOG_DEBUG, "Loading LiveStreams.xml %s", liveStreams);
    if (doc.LoadFile(liveStreams))
    {
      TiXmlElement* streamsNode = doc.FirstChildElement("streams");
      if (streamsNode)
      {
        TiXmlElement* streamNode;
        for (streamNode = streamsNode->FirstChildElement("stream"); streamNode; streamNode = streamNode->NextSiblingElement())
        {
          std::string key_value;
          if (streamNode->QueryStringAttribute("id", &key_value) == TIXML_SUCCESS)
          {
            try
            {
              if (streamNode->FirstChild())
              {
                int channelID = std::stoi(key_value);
                XBMC->Log(LOG_DEBUG, "%d %s", channelID, streamNode->FirstChild()->Value());
                m_liveStreams[channelID] = streamNode->FirstChild()->Value();
              }
            }
            catch (...)
            {
              XBMC->Log(LOG_DEBUG, "%s:%d", __FUNCTION__, __LINE__);
            }
          }
        }
      }
      XBMC->FreeString(liveStreams);
    }
  }
}
