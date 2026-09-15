#include "StarWorkshopLogic.hpp"

#include <cctype>

namespace Star {

static char const* const AllowedWorkshopPreviewHosts[] = {
  "steamuserimages-a.akamaihd.net",
  "images.steamusercontent.com"
};

bool isAllowedWorkshopPreviewUrl(String const& url) {
  std::string const& text = url.utf8();
  std::string const scheme = "https://";
  if (text.size() <= scheme.size() || text.compare(0, scheme.size(), scheme) != 0)
    return false;

  size_t hostEnd = text.find_first_of("/?#", scheme.size());
  std::string host = hostEnd == std::string::npos
    ? text.substr(scheme.size())
    : text.substr(scheme.size(), hostEnd - scheme.size());
  if (host.empty() || host.find('@') != std::string::npos || host.find(':') != std::string::npos)
    return false;

  for (auto& c : host)
    c = (char)std::tolower((unsigned char)c);

  for (auto allowedHost : AllowedWorkshopPreviewHosts) {
    if (host == allowedHost)
      return true;
  }
  return false;
}

WorkshopDependencyResolver::WorkshopDependencyResolver(String rootId, StringSet alreadySubscribed)
  : m_rootId(std::move(rootId)), m_subscribed(std::move(alreadySubscribed)), m_awaiting(false), m_failed(false) {
  m_visited.add(m_rootId);
  m_frontier.append(m_rootId);
}

String const& WorkshopDependencyResolver::rootId() const {
  return m_rootId;
}

StringList WorkshopDependencyResolver::nextBatch() {
  if (m_awaiting || m_failed || m_frontier.empty())
    return {};
  m_awaiting = true;
  return m_frontier;
}

void WorkshopDependencyResolver::supplyBatch(List<WorkshopItem> const& items) {
  if (!m_awaiting)
    return;

  StringMap<WorkshopItem const*> itemsById;
  for (auto const& item : items)
    itemsById[item.id] = &item;

  StringList nextFrontier;
  for (auto const& id : m_frontier) {
    bool isRoot = id == m_rootId;
    WorkshopItem const* item = itemsById.value(id, nullptr);
    if (!item || !item->available) {
      if (!isRoot)
        m_unavailable.append(id);
      continue;
    }

    m_titles[id] = item->title;
    if (!isRoot) {
      if (m_subscribed.contains(id))
        m_alreadySubscribed.append(id);
      else
        m_toSubscribe.append(id);
    }

    for (auto const& dependencyId : item->dependencyIds) {
      if (m_visited.add(dependencyId))
        nextFrontier.append(dependencyId);
    }
  }

  m_frontier = std::move(nextFrontier);
  m_awaiting = false;
}

void WorkshopDependencyResolver::fail() {
  m_failed = true;
  m_awaiting = false;
}

bool WorkshopDependencyResolver::finished() const {
  return !m_failed && !m_awaiting && m_frontier.empty();
}

bool WorkshopDependencyResolver::failed() const {
  return m_failed;
}

StringList const& WorkshopDependencyResolver::toSubscribe() const {
  return m_toSubscribe;
}

StringList const& WorkshopDependencyResolver::unavailable() const {
  return m_unavailable;
}

StringList const& WorkshopDependencyResolver::alreadySubscribed() const {
  return m_alreadySubscribed;
}

String WorkshopDependencyResolver::titleFor(String const& id) const {
  return m_titles.value(id, id);
}

void WorkshopApplyState::markChanged(String const& id) {
  m_changed.add(id);
}

void WorkshopApplyState::setDownloading(String const& id, bool downloading) {
  if (!m_changed.contains(id))
    return;
  if (downloading)
    m_downloading.add(id);
  else
    m_downloading.remove(id);
}

bool WorkshopApplyState::hasChanges() const {
  return !m_changed.empty();
}

size_t WorkshopApplyState::pendingDownloadCount() const {
  return m_downloading.size();
}

bool WorkshopApplyState::canApply() const {
  return hasChanges() && pendingDownloadCount() == 0;
}

StringList WorkshopApplyState::changedIds() const {
  StringList result;
  for (auto const& id : m_changed)
    result.append(id);
  return result;
}

void WorkshopApplyState::reset() {
  m_changed.clear();
  m_downloading.clear();
}

}
