#include "StarUserGeneratedContentService_pc_steam.hpp"
#include "StarLogging.hpp"
#include "StarLexicalCast.hpp"
#include "StarTime.hpp"

namespace Star {

static double const WorkshopRequestTimeout = 30.0;

SteamUserGeneratedContentService::SteamUserGeneratedContentService(PcPlatformServicesStatePtr)
  : m_callbackDownloadResult(this, &SteamUserGeneratedContentService::onDownloadResult),
    m_nextRequestId(1),
    m_checkedUGC(false) {};

StringList SteamUserGeneratedContentService::subscribedContentIds() const {
  List<PublishedFileId_t> contentIds(SteamUGC()->GetNumSubscribedItems(), {});
  SteamUGC()->GetSubscribedItems(contentIds.ptr(), contentIds.size());
  return contentIds.transformed([](PublishedFileId_t id) {
      return String(toString(id));
    });
}

Maybe<String> SteamUserGeneratedContentService::contentDownloadDirectory(String const& contentId) const {
  PublishedFileId_t id = lexicalCast<PublishedFileId_t>(contentId);
  uint32 itemState = SteamUGC()->GetItemState(id);
  if (itemState & k_EItemStateInstalled) {
    char path[4096];
    if (SteamUGC()->GetItemInstallInfo(id, nullptr, path, sizeof(path), nullptr))
      return String(path);
  }
  return {};
}

Maybe<uint64_t> SteamUserGeneratedContentService::installedUpdateTime(String const& contentId) const {
  PublishedFileId_t id = lexicalCast<PublishedFileId_t>(contentId);
  if (!(SteamUGC()->GetItemState(id) & k_EItemStateInstalled))
    return {};
  char path[4096];
  uint32 timeStamp = 0;
  if (!SteamUGC()->GetItemInstallInfo(id, nullptr, path, sizeof(path), &timeStamp) || timeStamp == 0)
    return {};
  return (uint64_t)timeStamp;
}

UserGeneratedContentService::UGCState SteamUserGeneratedContentService::triggerContentDownload() {
  List<PublishedFileId_t> contentIds(SteamUGC()->GetNumSubscribedItems(), {});
  SteamUGC()->GetSubscribedItems(contentIds.ptr(), contentIds.size());

  if (!m_checkedUGC) {
    bool contentNeedsUpdate = false;
    for (uint64 contentId : contentIds) {
      uint32 itemState = SteamUGC()->GetItemState(contentId);
      if (!(itemState & k_EItemStateInstalled) || itemState & k_EItemStateNeedsUpdate) {
        // Download is needed
        contentNeedsUpdate = true;
      }
    }
    m_checkedUGC = true;
    if (!contentNeedsUpdate) {
      // No download was needed
      return UserGeneratedContentService::UGCState::NoDownload;
    }
  }

  for (uint64 contentId : contentIds) {
    if (!m_currentDownloadState.contains(contentId)) {
      uint32 itemState = SteamUGC()->GetItemState(contentId);
      if (!(itemState & k_EItemStateInstalled) || itemState & k_EItemStateNeedsUpdate) {
        // DownloadItem has a return bool if it succeeds in attempting to download.
        if (SteamUGC()->DownloadItem(contentId, true)) {
          itemState = SteamUGC()->GetItemState(contentId);
          m_currentDownloadState[contentId] = !(itemState & k_EItemStateDownloading);
        } else {
          // DownloadItem failed for some reason. Just set this to true to skip this item and prevent an infinite loop.
          m_currentDownloadState[contentId] = true;
        }
      } else {
        m_currentDownloadState[contentId] = true;
      }
    }
  }

  bool allDownloaded = true;
  for (auto const& p : m_currentDownloadState) {
    if (!p.second)
      allDownloaded = false;
  }

  if (allDownloaded) {
    return UserGeneratedContentService::UGCState::Finished;
  } else {
    return UserGeneratedContentService::UGCState::InProgress;
  }
}

void SteamUserGeneratedContentService::onDownloadResult(DownloadItemResult_t* result) {
  m_currentDownloadState[result->m_nPublishedFileId] = true;
  if (result->m_eResult == k_EResultOK)
    m_failedDownloads.remove(result->m_nPublishedFileId);
  else
    m_failedDownloads.add(result->m_nPublishedFileId);
}

SteamUserGeneratedContentService::PendingQuery::PendingQuery()
  : handle(k_UGCQueryHandleInvalid), status(WorkshopRequestStatus::Pending), startTime(0.0), page(1) {}

SteamUserGeneratedContentService::PendingQuery::~PendingQuery() {
  if (handle != k_UGCQueryHandleInvalid)
    SteamUGC()->ReleaseQueryUGCRequest(handle);
}

void SteamUserGeneratedContentService::PendingQuery::onCompleted(SteamUGCQueryCompleted_t* completed, bool ioFailure) {
  if (ioFailure || completed->m_eResult != k_EResultOK) {
    status = WorkshopRequestStatus::Failed;
    return;
  }

  WorkshopPage workshopPage;
  workshopPage.page = page;
  workshopPage.totalResults = completed->m_unTotalMatchingResults;

  for (uint32 i = 0; i < completed->m_unNumResultsReturned; ++i) {
    SteamUGCDetails_t details;
    if (!SteamUGC()->GetQueryUGCResult(handle, i, &details))
      continue;

    WorkshopItem item;
    item.id = toString(details.m_nPublishedFileId);
    item.available = details.m_eResult == k_EResultOK && !details.m_bBanned;
    item.title = details.m_rgchTitle;
    item.authorId = toString(details.m_ulSteamIDOwner);
    item.description = details.m_rgchDescription;

    char previewUrl[1024] = {};
    if (SteamUGC()->GetQueryUGCPreviewURL(handle, i, previewUrl, sizeof(previewUrl)))
      item.previewUrl = previewUrl;

    uint64 subscribers = 0;
    if (SteamUGC()->GetQueryUGCStatistic(handle, i, k_EItemStatistic_NumSubscriptions, &subscribers))
      item.subscriberCount = subscribers;

    if (details.m_unNumChildren > 0) {
      List<PublishedFileId_t> children(details.m_unNumChildren, 0);
      if (SteamUGC()->GetQueryUGCChildren(handle, i, children.ptr(), children.size())) {
        for (auto child : children)
          item.dependencyIds.append(toString(child));
      }
    }

    workshopPage.items.append(std::move(item));
  }

  // Details requests: ids Steam did not return no longer exist.
  for (auto const& requestedId : requestedIds) {
    bool returned = false;
    for (auto const& item : workshopPage.items) {
      if (item.id == requestedId) {
        returned = true;
        break;
      }
    }
    if (!returned) {
      WorkshopItem missing;
      missing.id = requestedId;
      missing.available = false;
      workshopPage.items.append(std::move(missing));
    }
  }

  result = std::move(workshopPage);
  status = WorkshopRequestStatus::Succeeded;
}

WorkshopRequestStatus SteamUserGeneratedContentService::timedStatus(WorkshopRequestStatus status, double startTime) {
  if (status == WorkshopRequestStatus::Pending && Time::monotonicTime() - startTime > WorkshopRequestTimeout)
    return WorkshopRequestStatus::Failed;
  return status;
}

WorkshopRequestId SteamUserGeneratedContentService::sendQuery(UGCQueryHandle_t handle, uint32_t page, StringList requestedIds) {
  auto query = make_shared<PendingQuery>();
  query->handle = handle;
  query->page = page;
  query->requestedIds = std::move(requestedIds);
  query->startTime = Time::monotonicTime();

  if (handle == k_UGCQueryHandleInvalid) {
    query->status = WorkshopRequestStatus::Failed;
  } else {
    SteamUGC()->SetReturnLongDescription(handle, true);
    SteamUGC()->SetReturnChildren(handle, true);
    SteamAPICall_t call = SteamUGC()->SendQueryUGCRequest(handle);
    if (call == k_uAPICallInvalid)
      query->status = WorkshopRequestStatus::Failed;
    else
      query->callResult.Set(call, query.get(), &PendingQuery::onCompleted);
  }

  WorkshopRequestId request = m_nextRequestId++;
  m_queries[request] = std::move(query);
  return request;
}

WorkshopRequestId SteamUserGeneratedContentService::queryItems(String const& searchText, WorkshopSort sort, uint32_t page) {
  EUGCQuery queryType = k_EUGCQuery_RankedByTrend;
  if (!searchText.empty())
    queryType = k_EUGCQuery_RankedByTextSearch;
  else if (sort == WorkshopSort::Recent)
    queryType = k_EUGCQuery_RankedByPublicationDate;
  else if (sort == WorkshopSort::MostSubscribed)
    queryType = k_EUGCQuery_RankedByTotalUniqueSubscriptions;

  AppId_t appId = SteamUtils()->GetAppID();
  UGCQueryHandle_t handle = SteamUGC()->CreateQueryAllUGCRequest(queryType, k_EUGCMatchingUGCType_Items, appId, appId, (uint32)page);
  if (handle != k_UGCQueryHandleInvalid && !searchText.empty())
    SteamUGC()->SetSearchText(handle, searchText.utf8Ptr());

  return sendQuery(handle, page, {});
}

WorkshopRequestId SteamUserGeneratedContentService::queryItemDetails(StringList const& ids) {
  List<PublishedFileId_t> fileIds;
  for (auto const& id : ids) {
    if (auto fileId = maybeLexicalCast<PublishedFileId_t>(id))
      fileIds.append(*fileId);
  }

  UGCQueryHandle_t handle = k_UGCQueryHandleInvalid;
  if (!fileIds.empty())
    handle = SteamUGC()->CreateQueryUGCDetailsRequest(fileIds.ptr(), fileIds.size());

  return sendQuery(handle, 1, ids);
}

WorkshopRequestStatus SteamUserGeneratedContentService::requestStatus(WorkshopRequestId request) const {
  if (auto query = m_queries.ptr(request))
    return timedStatus((*query)->status, (*query)->startTime);
  if (auto action = m_subscribes.ptr(request))
    return timedStatus((*action)->status, (*action)->startTime);
  if (auto action = m_unsubscribes.ptr(request))
    return timedStatus((*action)->status, (*action)->startTime);
  return WorkshopRequestStatus::Failed;
}

Maybe<WorkshopPage> SteamUserGeneratedContentService::takeQueryResult(WorkshopRequestId request) {
  auto query = m_queries.maybeTake(request);
  if (!query)
    return {};
  if (timedStatus((*query)->status, (*query)->startTime) != WorkshopRequestStatus::Succeeded)
    return {};
  return std::move((*query)->result);
}

WorkshopRequestId SteamUserGeneratedContentService::subscribe(String const& id) {
  auto action = make_shared<PendingSubscribe>();
  action->startTime = Time::monotonicTime();

  auto fileId = maybeLexicalCast<PublishedFileId_t>(id);
  SteamAPICall_t call = fileId ? SteamUGC()->SubscribeItem(*fileId) : k_uAPICallInvalid;
  if (call == k_uAPICallInvalid) {
    action->status = WorkshopRequestStatus::Failed;
  } else {
    m_failedDownloads.remove(*fileId);
    action->callResult.Set(call, action.get(), &PendingSubscribe::onCompleted);
  }

  WorkshopRequestId request = m_nextRequestId++;
  m_subscribes[request] = std::move(action);
  return request;
}

WorkshopRequestId SteamUserGeneratedContentService::unsubscribe(String const& id) {
  auto action = make_shared<PendingUnsubscribe>();
  action->startTime = Time::monotonicTime();

  auto fileId = maybeLexicalCast<PublishedFileId_t>(id);
  SteamAPICall_t call = fileId ? SteamUGC()->UnsubscribeItem(*fileId) : k_uAPICallInvalid;
  if (call == k_uAPICallInvalid) {
    action->status = WorkshopRequestStatus::Failed;
  } else {
    m_failedDownloads.remove(*fileId);
    action->callResult.Set(call, action.get(), &PendingUnsubscribe::onCompleted);
  }

  WorkshopRequestId request = m_nextRequestId++;
  m_unsubscribes[request] = std::move(action);
  return request;
}

void SteamUserGeneratedContentService::releaseRequest(WorkshopRequestId request) {
  m_subscribes.remove(request);
  m_unsubscribes.remove(request);
}

WorkshopItemStatus SteamUserGeneratedContentService::itemStatus(String const& id) const {
  WorkshopItemStatus status;
  auto fileId = maybeLexicalCast<PublishedFileId_t>(id);
  if (!fileId)
    return status;

  uint32 itemState = SteamUGC()->GetItemState(*fileId);
  if (!(itemState & k_EItemStateSubscribed))
    return status;

  if (m_failedDownloads.contains(*fileId)) {
    status.state = WorkshopItemState::DownloadFailed;
  } else if (itemState & (k_EItemStateDownloading | k_EItemStateDownloadPending)) {
    status.state = WorkshopItemState::Downloading;
    uint64 downloaded = 0;
    uint64 total = 0;
    if (SteamUGC()->GetItemDownloadInfo(*fileId, &downloaded, &total) && total > 0)
      status.downloadProgress = (float)downloaded / (float)total;
  } else if (itemState & k_EItemStateNeedsUpdate) {
    status.state = WorkshopItemState::NeedsUpdate;
  } else if (itemState & k_EItemStateInstalled) {
    status.state = WorkshopItemState::Installed;
  } else {
    // Subscribed, but Steam has not started downloading yet.
    status.state = WorkshopItemState::Downloading;
  }
  return status;
}

bool SteamUserGeneratedContentService::retryDownload(String const& id) {
  auto fileId = maybeLexicalCast<PublishedFileId_t>(id);
  if (!fileId)
    return false;
  m_failedDownloads.remove(*fileId);
  return SteamUGC()->DownloadItem(*fileId, true);
}

Maybe<String> SteamUserGeneratedContentService::personaName(String const& steamId) {
  auto id = maybeLexicalCast<uint64>(steamId);
  if (!id)
    return {};
  if (auto name = m_personaNames.ptr(*id))
    return *name;

  CSteamID user(*id);
  // Returns true while Steam is still fetching the user's information.
  if (SteamFriends()->RequestUserInformation(user, true))
    return {};

  String name = SteamFriends()->GetFriendPersonaName(user);
  m_personaNames[*id] = name;
  return name;
}

}
