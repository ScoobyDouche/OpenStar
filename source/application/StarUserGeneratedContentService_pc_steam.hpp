#pragma once

#include "StarPlatformServices_pc.hpp"

namespace Star {

STAR_CLASS(SteamUserGeneratedContentService);

class SteamUserGeneratedContentService final : public UserGeneratedContentService {
public:
  SteamUserGeneratedContentService(PcPlatformServicesStatePtr state);

  StringList subscribedContentIds() const override;
  Maybe<String> contentDownloadDirectory(String const& contentId) const override;
  UserGeneratedContentService::UGCState triggerContentDownload() override;

  WorkshopRequestId queryItems(String const& searchText, WorkshopSort sort, uint32_t page) override;
  WorkshopRequestId queryItemDetails(StringList const& ids) override;
  WorkshopRequestStatus requestStatus(WorkshopRequestId request) const override;
  Maybe<WorkshopPage> takeQueryResult(WorkshopRequestId request) override;

  WorkshopRequestId subscribe(String const& id) override;
  WorkshopRequestId unsubscribe(String const& id) override;
  void releaseRequest(WorkshopRequestId request) override;

  WorkshopItemStatus itemStatus(String const& id) const override;
  bool retryDownload(String const& id) override;
  Maybe<String> personaName(String const& steamId) override;

private:
  struct PendingQuery {
    PendingQuery();
    ~PendingQuery();

    void onCompleted(SteamUGCQueryCompleted_t* completed, bool ioFailure);

    UGCQueryHandle_t handle;
    CCallResult<PendingQuery, SteamUGCQueryCompleted_t> callResult;
    WorkshopRequestStatus status;
    double startTime;
    uint32_t page;
    StringList requestedIds;
    Maybe<WorkshopPage> result;
  };

  template <typename ResultType>
  struct PendingAction {
    void onCompleted(ResultType* completed, bool ioFailure) {
      status = (!ioFailure && completed->m_eResult == k_EResultOK) ? WorkshopRequestStatus::Succeeded : WorkshopRequestStatus::Failed;
    }

    CCallResult<PendingAction<ResultType>, ResultType> callResult;
    WorkshopRequestStatus status = WorkshopRequestStatus::Pending;
    double startTime = 0.0;
  };

  typedef PendingAction<RemoteStorageSubscribePublishedFileResult_t> PendingSubscribe;
  typedef PendingAction<RemoteStorageUnsubscribePublishedFileResult_t> PendingUnsubscribe;

  static WorkshopRequestStatus timedStatus(WorkshopRequestStatus status, double startTime);
  WorkshopRequestId sendQuery(UGCQueryHandle_t handle, uint32_t page, StringList requestedIds);

  STEAM_CALLBACK(SteamUserGeneratedContentService, onDownloadResult, DownloadItemResult_t, m_callbackDownloadResult);

  HashMap<PublishedFileId_t, bool> m_currentDownloadState;
  HashSet<PublishedFileId_t> m_failedDownloads;

  // shared_ptr rather than unique_ptr because HashMap copies its values, and
  // the pending structs must keep a stable address for their CCallResult.
  HashMap<WorkshopRequestId, shared_ptr<PendingQuery>> m_queries;
  HashMap<WorkshopRequestId, shared_ptr<PendingSubscribe>> m_subscribes;
  HashMap<WorkshopRequestId, shared_ptr<PendingUnsubscribe>> m_unsubscribes;
  WorkshopRequestId m_nextRequestId;

  HashMap<uint64, String> m_personaNames;

  bool m_checkedUGC;
};

}
