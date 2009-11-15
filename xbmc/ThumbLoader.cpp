/*
 *      Copyright (C) 2005-2008 Team XBMC
 *      http://www.xbmc.org
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
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include "FileSystem/StackDirectory.h"
#include "ThumbLoader.h"
#include "Util.h"
#include "URL.h"
#include "Picture.h"
#include "FileSystem/File.h"
#include "FileItem.h"
#include "GUISettings.h"
#include "GUIUserMessages.h"
#include "GUIWindowManager.h"
#include "TextureManager.h"
#include "VideoInfoTag.h"
#include "VideoDatabase.h"
#include "utils/log.h"
#include "utils/SingleLock.h"

#include "cores/dvdplayer/DVDFileInfo.h"

using namespace XFILE;
using namespace DIRECTORY;
using namespace std;

CThumbLoader::CThumbLoader(int nThreads) :
  CBackgroundInfoLoader(nThreads)
{
}

CThumbLoader::~CThumbLoader()
{
}

bool CThumbLoader::LoadRemoteThumb(CFileItem *pItem)
{
  // look for remote thumbs
  CStdString thumb(pItem->GetThumbnailImage());
  if (!g_TextureManager.CanLoad(thumb))
  {
    CStdString cachedThumb(pItem->GetCachedVideoThumb());
    if (CFile::Exists(cachedThumb))
      pItem->SetThumbnailImage(cachedThumb);
    else
    {
      if (CPicture::CreateThumbnail(thumb, cachedThumb))
        pItem->SetThumbnailImage(cachedThumb);
      else
        pItem->SetThumbnailImage("");
    }
  }
  return pItem->HasThumbnail();
}

CThumbExtractor::CThumbExtractor(const CFileItem& item, const CStdString& listpath, bool thumb, const CStdString& path, const CStdString& target)
{
  m_path = path;
  m_listpath = listpath;
  m_target = target;
  m_thumb = thumb;
  m_item = item;
}

CThumbExtractor::~CThumbExtractor()
{
}

bool CThumbExtractor::operator==(const CJob* job) const
{
  if (strcmp(job->GetType(),GetType()) == 0)
  {
    const CThumbExtractor* jobExtract = dynamic_cast<const CThumbExtractor*>(job);
    if (jobExtract && jobExtract->m_listpath == m_listpath)
      return true;
  }
  return false;
}

bool CThumbExtractor::DoWork()
{
  if (CUtil::IsLiveTV(m_path)
  ||  CUtil::IsUPnP(m_path)
  ||  CUtil::IsDAAP(m_path))
    return false;

  if (CUtil::IsRemote(m_path) && !CUtil::IsOnLAN(m_path))
    return false;

  if (m_thumb && g_guiSettings.GetBool("myvideos.extractflags"))
  {
    CLog::Log(LOGDEBUG,"%s - trying to extract thumb from video file %s", __FUNCTION__, m_path.c_str());
    CDVDFileInfo::ExtractThumb(m_path, m_target, &m_item.GetVideoInfoTag()->m_streamDetails);
    if (CFile::Exists(m_target))
    {
      m_item.SetProperty("HasAutoThumb", "1");
      m_item.SetProperty("AutoThumbImage", m_target);
      m_item.SetThumbnailImage(m_target);
    }
  }
  else if (m_item.HasVideoInfoTag() &&
           g_guiSettings.GetBool("myvideos.extractflags")   &&
           !m_item.GetVideoInfoTag()->HasStreamDetails())
  {
    CDVDFileInfo::GetFileStreamDetails(&m_item);
  }

  return true;
}

CVideoThumbLoader::CVideoThumbLoader() :
  CThumbLoader(1), CJobQueue(true), m_pStreamDetailsObs(NULL)
{
}

CVideoThumbLoader::~CVideoThumbLoader()
{
  StopThread();
}

void CVideoThumbLoader::OnLoaderStart()
{
}

void CVideoThumbLoader::OnLoaderFinish()
{
}

/**
* Reads watched status from the database and sets the watched overlay accordingly
*/
void CVideoThumbLoader::SetWatchedOverlay(CFileItem *item)
{
  // do this only for video files and exclude everything else.
  if (item->IsVideo() && !item->IsVideoDb() && !item->IsInternetStream()
      && !item->IsFileFolder() && !item->IsPlugin())
  {
    CVideoDatabase dbs;
    if (dbs.Open())
    {
      int file_id = dbs.GetFileId(item->m_strPath);
      if (file_id > -1)
        item->SetOverlayImage(CGUIListItem::ICON_OVERLAY_UNWATCHED, (dbs.GetPlayCount(file_id) > 0));

      dbs.Close();
    }
  }
}

/**
 * Look for a thumbnail for pItem.  If one does not exist, look for an autogenerated
 * thumbnail.  If that does not exist, attempt to autogenerate one.  Finally, check
 * for the existance of fanart and set properties accordinly.
 * @return: true if pItem has been modified
 */
bool CVideoThumbLoader::LoadItem(CFileItem* pItem)
{
  if (pItem->m_bIsShareOrDrive) return false;

  SetWatchedOverlay(pItem);

  CFileItem item(*pItem);
  CStdString cachedThumb(item.GetCachedVideoThumb());
  
  if (!pItem->HasThumbnail())
  {
    item.SetUserVideoThumb();
    if (!CFile::Exists(cachedThumb))
    {
      CStdString strPath, strFileName;
      CUtil::Split(cachedThumb, strPath, strFileName);

      // create unique thumb for auto generated thumbs
      cachedThumb = strPath + "auto-" + strFileName;
      if (CFile::Exists(cachedThumb))
      {
        pItem->SetProperty("HasAutoThumb", "1");
        pItem->SetProperty("AutoThumbImage", cachedThumb);
      }
      else if (!item.m_bIsFolder && item.IsVideo() && !item.IsInternetStream() && !item.IsPlayList())
      {
        CStdString path(item.m_strPath);
        if (item.IsStack())
          path = CStackDirectory::GetFirstStackedFile(item.m_strPath);

        CThumbExtractor* extract = new CThumbExtractor(item, pItem->m_strPath, true, path, cachedThumb);
        AddJob(extract);
      }
    }
    if (CFile::Exists(cachedThumb))
      pItem->SetThumbnailImage(cachedThumb);
  }
  else
    LoadRemoteThumb(pItem);

  if (!pItem->HasProperty("fanart_image"))
  {
    if (pItem->CacheLocalFanart())
      pItem->SetProperty("fanart_image",pItem->GetCachedFanart());
  }

  if (!pItem->m_bIsFolder && !pItem->IsInternetStream() &&
       pItem->HasVideoInfoTag() &&
       g_guiSettings.GetBool("myvideos.extractflags")   &&
       !pItem->GetVideoInfoTag()->HasStreamDetails())
  {
    CThumbExtractor* extract = new CThumbExtractor(*pItem,pItem->m_strPath,false);
    AddJob(extract);
  }
  return true;
}

void CVideoThumbLoader::OnJobComplete(unsigned int jobID, bool success, CJob* job)
{
  CThumbExtractor* loader = (CThumbExtractor*)job;
  loader->m_item.m_strPath = loader->m_listpath;
  CVideoInfoTag* info = loader->m_item.GetVideoInfoTag();
  if (m_pStreamDetailsObs)
    m_pStreamDetailsObs->OnStreamDetails(info->m_streamDetails, info->m_strFileNameAndPath, info->m_iFileId);
  if (m_pObserver)
    m_pObserver->OnItemLoaded(&loader->m_item);
  CFileItemPtr pItem(new CFileItem(loader->m_item));
  CGUIMessage msg(GUI_MSG_NOTIFY_ALL, 0, 0, GUI_MSG_UPDATE_ITEM, 0, pItem);
  g_windowManager.SendThreadMessage(msg);
  CJobQueue::OnJobComplete(jobID, success, job);
}

CProgramThumbLoader::CProgramThumbLoader()
{
}

CProgramThumbLoader::~CProgramThumbLoader()
{
}

bool CProgramThumbLoader::LoadItem(CFileItem *pItem)
{
  if (pItem->m_bIsShareOrDrive) return true;
  if (!pItem->HasThumbnail())
    pItem->SetUserProgramThumb();
  else
    LoadRemoteThumb(pItem);
  return true;
}

CMusicThumbLoader::CMusicThumbLoader()
{
}

CMusicThumbLoader::~CMusicThumbLoader()
{
}

bool CMusicThumbLoader::LoadItem(CFileItem* pItem)
{
  if (pItem->m_bIsShareOrDrive) return true;
  if (!pItem->HasThumbnail())
    pItem->SetUserMusicThumb();
  else
    LoadRemoteThumb(pItem);
  return true;
}

