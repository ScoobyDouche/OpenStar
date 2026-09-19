#pragma once

#include "StarString.hpp"
#include "StarJson.hpp"
#include "StarUserGeneratedContentService.hpp"

namespace Star {

// True only for https URLs on Steam's own Workshop image hosts.
bool isAllowedWorkshopPreviewUrl(String const& url);

// Turns a Steam Workshop description written in BBCode into text for a
// Starbound label: headings and bold become color codes, list items become
// bullets, images and videos are dropped, and other known tags are removed
// with their text kept.  Brackets that are not BBCode tags are left alone.
String workshopDescriptionText(String const& bbcode);

// Subscribed items, in subscription order, whose update time is newer than the
// one recorded in seen (id -> time).  Items with no valid recorded time were
// subscribed since the last check and are not reported.
StringList workshopUpdatedIds(JsonObject const& seen, List<pair<String, uint64_t>> const& current);
// The record to store for the next check: every current item's update time.
JsonObject workshopSeenUpdateRecord(List<pair<String, uint64_t>> const& current);

// Walks Workshop dependencies breadth-first without doing any I/O.  The caller
// fetches the details for each nextBatch() and passes them to supplyBatch().
class WorkshopDependencyResolver {
public:
  WorkshopDependencyResolver(String rootId, StringSet alreadySubscribed);

  String const& rootId() const;

  // Ids whose details are needed next.  Empty when finished, failed, or while
  // the previous batch has not been supplied yet.
  StringList nextBatch();
  // Details for the batch last returned by nextBatch().  Requested ids that are
  // missing from items, or not available, are reported as unavailable.
  void supplyBatch(List<WorkshopItem> const& items);
  void fail();

  bool finished() const;
  bool failed() const;

  // Dependencies to subscribe to, in discovery order; never contains the root.
  StringList const& toSubscribe() const;
  StringList const& unavailable() const;
  StringList const& alreadySubscribed() const;
  // The item's title if its details were seen, otherwise the id.
  String titleFor(String const& id) const;

private:
  String m_rootId;
  StringSet m_subscribed;
  StringSet m_visited;
  StringList m_frontier;
  bool m_awaiting;
  bool m_failed;

  StringList m_toSubscribe;
  StringList m_unavailable;
  StringList m_alreadySubscribed;
  StringMap<String> m_titles;
};

// Tracks subscription changes made in the Workshop menu and whether any of
// those items are still downloading.
class WorkshopApplyState {
public:
  void markChanged(String const& id);
  // Ignored for ids that were not marked changed.
  void setDownloading(String const& id, bool downloading);

  bool hasChanges() const;
  size_t pendingDownloadCount() const;
  bool canApply() const;
  StringList changedIds() const;

  void reset();

private:
  StringSet m_changed;
  StringSet m_downloading;
};

}
