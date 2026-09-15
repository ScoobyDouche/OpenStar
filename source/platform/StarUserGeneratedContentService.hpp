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
	
  ~UserGeneratedContentService() = default;

  // Returns a list of the content the user is currently subscribed to.
  virtual StringList subscribedContentIds() const = 0;

  // If the content has been downloaded successfully, returns the path to the
  // downloaded content directory on the filesystem, otherwise nothing.
  virtual Maybe<String> contentDownloadDirectory(String const& contentId) const = 0;

  // Start downloading subscribed content in the background, returns true when
  // all content is synchronized.
  virtual UserGeneratedContentService::UGCState triggerContentDownload() = 0;
};

}
