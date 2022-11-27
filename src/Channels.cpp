/*
 *  Copyright (C) 2020-2021 Team Kodi (https://kodi.tv)
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "Channels.h"
#include "utilities/XMLUtils.h"
#include "pvrclient-nextpvr.h"

#include <kodi/tools/StringUtils.h>

using namespace NextPVR;
using namespace NextPVR::utilities;

/** Channel handling */

int Channels::GetNumChannels()
{
  // Kodi polls this while recordings are open avoid calls to backend
  int channelCount = m_channelDetails.size();
  if (channelCount == 0)
  {
    tinyxml2::XMLDocument doc;
    if (m_request.DoMethodRequest("channel.list", doc) == tinyxml2::XML_SUCCESS)
    {
      tinyxml2::XMLNode* channelsNode = doc.RootElement()->FirstChildElement("channels");
      tinyxml2::XMLNode* pChannelNode;
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
  std::string iconFilename = GetChannelIconFileName(channelID);

  // do we already have the icon file?
  if (kodi::vfs::FileExists(iconFilename))
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
  return kodi::tools::StringUtils::Format("special://userdata/addon_data/pvr.nextpvr/nextpvr-ch%d.png", channelID);
}

void  Channels::DeleteChannelIcon(int channelID)
{
  kodi::vfs::DeleteFile(GetChannelIconFileName(channelID));
}

void Channels::DeleteChannelIcons()
{
  std::vector<kodi::vfs::CDirEntry> icons;
  if (kodi::vfs::GetDirectory("special://userdata/addon_data/pvr.nextpvr/", "nextpvr-ch*.png", icons))
  {
    kodi::Log(ADDON_LOG_INFO, "Deleting %d channel icons", icons.size());
    for (auto const& it : icons)
    {
      const std::string deleteme = it.Path();
      kodi::Log(ADDON_LOG_DEBUG, "DeleteFile %s rc:%d", kodi::vfs::TranslateSpecialProtocol(deleteme).c_str(), kodi::vfs::DeleteFile(deleteme));
    }
  }
}

PVR_ERROR Channels::GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results)
{
  if (radio && !m_settings.m_showRadio)
    return PVR_ERROR_NO_ERROR;

  std::string stream;
  std::map<int, std::pair<bool, bool>>::iterator  itr = m_channelDetails.begin();
  while (itr != m_channelDetails.end())
  {
    if (itr->second.second == (radio == true))
      itr = m_channelDetails.erase(itr);
    else
      ++itr;
  }

  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest("channel.list&extras=true", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* channelsNode = doc.RootElement()->FirstChildElement("channels");
    tinyxml2::XMLNode* pChannelNode;
    for( pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode=pChannelNode->NextSiblingElement())
    {
      kodi::addon::PVRChannel tag;
      tag.SetUniqueId(XMLUtils::GetUIntValue(pChannelNode, "id"));
      std::string buffer;
      XMLUtils::GetString(pChannelNode, "type", buffer);
      if ( buffer =="0xa")
      {
        tag.SetIsRadio(true);
        tag.SetMimeType("application/octet-stream");
      }
      else
      {
        tag.SetIsRadio(false);
        tag.SetMimeType("application/octet-stream");
        if (IsChannelAPlugin(tag.GetUniqueId()))
        {
          if (kodi::tools::StringUtils::EndsWithNoCase(m_liveStreams[tag.GetUniqueId()], ".m3u8"))
            tag.SetMimeType("application/x-mpegURL");
          else
            tag.SetMimeType("video/MP2T");
        }
      }
      if (radio != tag.GetIsRadio())
        continue;

      tag.SetChannelNumber(XMLUtils::GetUIntValue(pChannelNode, "number"));
      tag.SetSubChannelNumber(XMLUtils::GetUIntValue(pChannelNode, "minor"));

      buffer.clear();
      XMLUtils::GetString(pChannelNode, "name", buffer);
      tag.SetChannelName(buffer);

      // check if we need to download a channel icon
      bool isIcon;
      if (XMLUtils::GetBoolean(pChannelNode, "icon", isIcon))
      {
        // only set when true;
        std::string iconFile = GetChannelIcon(tag.GetUniqueId());
        if (iconFile.length() > 0)
          tag.SetIconPath(iconFile);
      }

      // V5 has the EPG source type info.
      std::string epg;
      if (XMLUtils::GetString(pChannelNode, "epg", epg))
        m_channelDetails[tag.GetUniqueId()] = std::make_pair(epg == "None", tag.GetIsRadio());
      else
        m_channelDetails[tag.GetUniqueId()] = std::make_pair(false, tag.GetIsRadio());

      // transfer channel to XBMC
      results.Add(tag);
    }
  }
  return PVR_ERROR_NO_ERROR;
}

/************************************************************/
/** Channel group handling **/

PVR_ERROR Channels::GetChannelGroupsAmount(int& amount)
{
  // this could be different from the number of backend groups if radio and TV are mixed or if groups are empty
  amount = m_radioGroups.size() + m_tvGroups.size();
  return PVR_ERROR_NO_ERROR;
}

PVR_RECORDING_CHANNEL_TYPE Channels::GetChannelType(unsigned int uid)
{
  // when uid is invalid we assume TV because Kodi will
  if (m_channelDetails.count(uid) > 0 && m_channelDetails[uid].second == true)
    return PVR_RECORDING_CHANNEL_TYPE_RADIO;

  return PVR_RECORDING_CHANNEL_TYPE_TV;
}

PVR_ERROR Channels::GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results)
{
  if (radio && !m_settings.m_showRadio)
    return PVR_ERROR_NO_ERROR;

  std::unordered_set selectedGroups = radio ? m_radioGroups : m_tvGroups;

  selectedGroups.clear();
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest("channel.list&extras=true", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* channelsNode = doc.RootElement()->FirstChildElement("channels");
    tinyxml2::XMLNode* pChannelNode;
    for( pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode=pChannelNode->NextSiblingElement())
    {
      std::string buffer;
      if (XMLUtils::GetAdditiveString(pChannelNode->FirstChildElement("groups"), "group", "\t", buffer, true))
      {
        std::vector<std::string> groups = kodi::tools::StringUtils::Split(buffer, '\t');
        bool foundRadio = false;
        buffer.clear();
        XMLUtils::GetString(pChannelNode, "type", buffer);
        if ( buffer == "0xa")
        {
          foundRadio = true;
        }
        if (radio == foundRadio)
        {
          for(auto const& group : groups)
          {
            if (selectedGroups.find(group) == selectedGroups.end())
            {
              selectedGroups.insert(group);
            }
          }
        }
      }
    }
  }

  // Many users won't have radio groups
  if (selectedGroups.size() == 0)
    return PVR_ERROR_NO_ERROR;

  doc.Clear();
  if (m_request.DoMethodRequest("channel.groups", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* groupsNode = doc.RootElement()->FirstChildElement("groups");
    tinyxml2::XMLNode* pGroupNode;
    std::string group;
    int priority = 1;
    for (pGroupNode = groupsNode->FirstChildElement("group"); pGroupNode; pGroupNode = pGroupNode->NextSiblingElement())
    {
      if (XMLUtils::GetString(pGroupNode, "name", group))
      {
        // "All Channels" won't match any group, skip empty NextPVR groups
        if (selectedGroups.find(group) != selectedGroups.end())
        {
          kodi::addon::PVRChannelGroup tag;
          tag.SetIsRadio(radio);
          tag.SetPosition(priority++);
          tag.SetGroupName(group);
          results.Add(tag);
        }
      }
    }
  }
  else
  {
    kodi::Log(ADDON_LOG_DEBUG, "No Channel Group");
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR Channels::GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, kodi::addon::PVRChannelGroupMembersResultSet& results)
{
  std::string encodedGroupName = UriEncode(group.GetGroupName());
  std::string request = "channel.list&group_id=" + encodedGroupName;
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest(request, doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* channelsNode = doc.RootElement()->FirstChildElement("channels");
    tinyxml2::XMLNode* pChannelNode;
    for (pChannelNode = channelsNode->FirstChildElement("channel"); pChannelNode; pChannelNode = pChannelNode->NextSiblingElement())
    {
      kodi::addon::PVRChannelGroupMember tag;
      tag.SetChannelUniqueId(XMLUtils::GetUIntValue(pChannelNode, "id"));
      // ignore orphan channels in groups
      if (m_channelDetails.find(tag.GetChannelUniqueId()) != m_channelDetails.end()
        && group.GetIsRadio() == m_channelDetails[tag.GetChannelUniqueId()].second)
      {
        tag.SetGroupName(group.GetGroupName());
        tag.SetChannelNumber(XMLUtils::GetUIntValue(pChannelNode, "number"));
        tag.SetSubChannelNumber(XMLUtils::GetUIntValue(pChannelNode, "minor"));
        results.Add(tag);
      }
    }
  }
  return PVR_ERROR_NO_ERROR;
}

bool Channels::IsChannelAPlugin(int uid)
{
  if (m_liveStreams.count(uid) != 0)
    if (kodi::tools::StringUtils::StartsWith(m_liveStreams[uid], "plugin:") || kodi::tools::StringUtils::EndsWithNoCase(m_liveStreams[uid], ".m3u8"))
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
    tinyxml2::XMLDocument doc;
    std::string liveStreams = kodi::vfs::TranslateSpecialProtocol("special://userdata/addon_data/pvr.nextpvr/LiveStreams.xml");
    kodi::Log(ADDON_LOG_DEBUG, "Loading LiveStreams.xml %s", liveStreams.c_str());
    if (doc.LoadFile(liveStreams.c_str()) == tinyxml2::XML_SUCCESS)
    {
      tinyxml2::XMLNode* streamsNode = doc.FirstChildElement("streams");
      if (streamsNode)
      {
        tinyxml2::XMLElement* streamNode;
        for (streamNode = streamsNode->FirstChildElement("stream"); streamNode; streamNode = streamNode->NextSiblingElement())
        {
          const char* attrib = streamNode->Attribute("id");
          if (attrib != nullptr)
          {
            try
            {
              int channelID = std::atoi(attrib);
              kodi::Log(ADDON_LOG_DEBUG, "%d %s", channelID, streamNode->FirstChild()->Value());
              m_liveStreams[channelID] = streamNode->FirstChild()->Value();
            }
            catch (...)
            {
              kodi::Log(ADDON_LOG_DEBUG, "%s:%d", __FUNCTION__, __LINE__);
            }
          }
        }
      }
    }
  }
}
