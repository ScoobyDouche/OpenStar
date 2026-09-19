#pragma once

#include "StarString.hpp"
#include "StarMaybe.hpp"

namespace Star {

STAR_CLASS(UserGeneratedContentService);

enum class WorkshopSort {
  Popular,
  Recent,
  MostSubscribed
};

struct WorkshopItem {
  String id;
  String title;
  // Steam ID of the owner, resolve a display name with personaName()
  String authorId;
  String description;
  String previewUrl;
  uint64_t subscriberCount = 0;
  StringList dependencyIds;
  // False if the item is hidden, removed, or banned
  bool available = true;
};

struct WorkshopPage {
  List<WorkshopItem> items;
  uint32_t page = 1;
  uint32_t totalResults = 0;
};

enum class WorkshopItemState {
  NotSubscribed,
  Downloading,
  Installed,
  NeedsUpdate,
  DownloadFailed
};

struct WorkshopItemStatus {
  WorkshopItemState state = WorkshopItemState::NotSubscribed;
  // 0..1, only meaningful while Downloading
  float downloadProgress = 0.0f;
};

enum class WorkshopRequestStatus {
  Pending,
  Succeeded,
  Failed
};

typedef uint64_t WorkshopRequestId;

class UserGeneratedContentService {
public:
  enum class UGCState {
    NoDownload = 0,
    InProgress = 1,
    Finished = 2
  };
	
  virtual ~UserGeneratedContentService() = default;

  // Returns a list of the content the user is currently subscribed to.
  virtual StringList subscribedContentIds() const = 0;

  // If the content has been downloaded successfully, returns the path to the
  // downloaded content directory on the filesystem, otherwise nothing.
  virtual Maybe<String> contentDownloadDirectory(String const& contentId) const = 0;
  // When the installed copy of the content was last updated on the Workshop,
  // in seconds since the epoch, or nothing if it is not installed.
  virtual Maybe<uint64_t> installedUpdateTime(String const& contentId) const = 0;

  // Start downloading subscribed content in the background, returns true when
  // all content is synchronized.
  virtual UserGeneratedContentService::UGCState triggerContentDownload() = 0;

  // Starts a Workshop search.  A non-empty searchText ranks by relevance and
  // ignores sort.  Pages start at 1 and hold up to 50 items.
  virtual WorkshopRequestId queryItems(String const& searchText, WorkshopSort sort, uint32_t page) = 0;
  // Starts a details request for specific items.  Requested ids missing from
  // the result are returned with available == false.
  virtual WorkshopRequestId queryItemDetails(StringList const& ids) = 0;
  // Status of any request.  Requests still pending after 30 seconds, and
  // unknown ids, report Failed.
  virtual WorkshopRequestStatus requestStatus(WorkshopRequestId request) const = 0;
  // Removes a query request, cancelling it if still pending.  Returns the page
  // only if the query succeeded.
  virtual Maybe<WorkshopPage> takeQueryResult(WorkshopRequestId request) = 0;

  virtual WorkshopRequestId subscribe(String const& id) = 0;
  virtual WorkshopRequestId unsubscribe(String const& id) = 0;
  // Removes a subscribe or unsubscribe request, cancelling it if still pending.
  virtual void releaseRequest(WorkshopRequestId request) = 0;

  virtual WorkshopItemStatus itemStatus(String const& id) const = 0;
  // Clears a failed download and asks Steam to download the item again.
  virtual bool retryDownload(String const& id) = 0;
  // Display name for a Steam ID, or nothing while it is still being fetched.
  virtual Maybe<String> personaName(String const& steamId) = 0;
};

}
