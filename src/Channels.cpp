/*
 *  Copyright (C) 2020 Team Kodi
 *  https://kodi.tv
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
  std::string stream;
  std::map<int, std::pair<bool, bool>>::iterator  itr = m_channelDetails.begin();
  while (itr != m_channelDetails.end())
  {
    if (itr->second.second == (radio == true))
      itr = m_channelDetails.erase(itr);
    else
      ++itr;
  }

  if (radio)
    m_radioGroups.clear();
  else
    m_tvGroups.clear();

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

      buffer.clear();
      if (XMLUtils::GetAdditiveString(pChannelNode->FirstChildElement("groups"), "group", "\t", buffer, true))
      {
        std::vector<std::string> groups = kodi::tools::StringUtils::Split(buffer, '\t');
        for(auto const& group : groups)
        {
          if (!radio && m_tvGroups.find(group) == m_tvGroups.end())
            m_tvGroups.insert(group);
          else if (radio && m_radioGroups.find(group) == m_radioGroups.end())
            m_radioGroups.insert(group);
        }
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
  int groups = 0;
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest("channel.groups", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* groupsNode = doc.RootElement()->FirstChildElement("groups");
    tinyxml2::XMLNode* pGroupNode;
    for( pGroupNode = groupsNode->FirstChildElement("group"); pGroupNode; pGroupNode=pGroupNode->NextSiblingElement())
    {
      groups++;
    }
  }
  amount = groups;
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
  // radioGroups.size() will be zero before 5.2 API
  if (radio && m_radioGroups.size() == 0)
    return PVR_ERROR_NO_ERROR;

  if (m_settings.m_backendVersion >= 50200)
  {
    // empty groups will not be added
    for (auto const& group : radio ? m_radioGroups : m_tvGroups)
    {
      kodi::addon::PVRChannelGroup tag;
      tag.SetIsRadio(radio);
      tag.SetPosition(0);
      tag.SetGroupName(group);
      results.Add(tag);
    }
    return PVR_ERROR_NO_ERROR;
  }

  // for tv, use the groups returned by nextpvr
  tinyxml2::XMLDocument doc;
  if (m_request.DoMethodRequest("channel.groups", doc) == tinyxml2::XML_SUCCESS)
  {
    tinyxml2::XMLNode* groupsNode = doc.RootElement()->FirstChildElement("groups");
    tinyxml2::XMLNode* pGroupNode;
    for (pGroupNode = groupsNode->FirstChildElement("group"); pGroupNode; pGroupNode = pGroupNode->NextSiblingElement())
    {
      kodi::addon::PVRChannelGroup tag;
      tag.SetIsRadio(radio);
      tag.SetPosition(0); // groups default order, unused
      std::string group;
      if (XMLUtils::GetString(pGroupNode, "name", group))
      {
        // tell XBMC about channel, ignoring "All Channels" since xbmc has an built in group with effectively the same function
        tag.SetGroupName(group);
        if (group != "All Channels")
        {
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
