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

static char const* const WorkshopBBCodeTags[] = {
  "h1", "h2", "h3", "b", "i", "u", "strike", "spoiler", "noparse", "code", "quote", "url",
  "img", "previewyoutube", "list", "olist", "*", "hr", "table", "tr", "td", "th"
};

struct WorkshopBBCodeTag {
  bool closing;
  std::string name;
  // Index just past the closing ']'.
  size_t end;
};

// Reads a known tag such as [b], [/h1] or [url=...] starting at the '[' at pos.
static Maybe<WorkshopBBCodeTag> readWorkshopBBCodeTag(std::string const& text, size_t pos) {
  WorkshopBBCodeTag tag;
  size_t i = pos + 1;
  tag.closing = i < text.size() && text[i] == '/';
  if (tag.closing)
    ++i;

  while (i < text.size() && (std::isalnum((unsigned char)text[i]) || text[i] == '*'))
    tag.name += (char)std::tolower((unsigned char)text[i++]);
  if (tag.name.empty() || i >= text.size())
    return {};

  if (text[i] == '=' && !tag.closing) {
    i = text.find_first_of("]\n", i);
    if (i == std::string::npos || text[i] != ']')
      return {};
  } else if (text[i] != ']') {
    return {};
  }
  tag.end = i + 1;

  for (auto known : WorkshopBBCodeTags) {
    if (tag.name == known)
      return tag;
  }
  return {};
}

String workshopDescriptionText(String const& bbcode) {
  std::string text;
  for (char c : bbcode.utf8()) {
    if (c != '\r')
      text += c;
  }
  std::string lowered = text;
  for (auto& c : lowered)
    c = (char)std::tolower((unsigned char)c);

  std::string out;
  auto startLine = [&]() {
    if (!out.empty() && out.back() != '\n')
      out += '\n';
  };

  size_t pos = 0;
  while (pos < text.size()) {
    auto tag = text[pos] == '[' ? readWorkshopBBCodeTag(text, pos) : Maybe<WorkshopBBCodeTag>();
    if (!tag) {
      out += text[pos++];
      continue;
    }
    pos = tag->end;

    if (tag->name == "img" || tag->name == "previewyoutube") {
      // The contents are a URL, so drop everything up to the matching close.
      if (!tag->closing) {
        size_t close = lowered.find("[/" + tag->name + "]", pos);
        if (close != std::string::npos)
          pos = close + tag->name.size() + 3;
      }
    } else if (tag->name == "h1" || tag->name == "h2" || tag->name == "h3") {
      out += tag->closing ? "^reset;" : "^orange;";
    } else if (tag->name == "b") {
      out += tag->closing ? "^reset;" : "^yellow;";
    } else if (tag->name == "*") {
      startLine();
      out += "- ";
      while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
        ++pos;
    } else if (tag->name == "list" || tag->name == "olist" || tag->name == "hr") {
      startLine();
      // The tag already ended the line, so a newline right after it would
      // leave a blank one.
      if (!out.empty() && pos < text.size() && text[pos] == '\n')
        ++pos;
    }
  }

  // Trim each line's trailing spaces, keep at most one blank line in a row, and
  // trim the whole text.
  std::string tidy;
  size_t newlines = 0;
  size_t lineStart = 0;
  while (lineStart <= out.size()) {
    size_t lineEnd = out.find('\n', lineStart);
    if (lineEnd == std::string::npos)
      lineEnd = out.size();
    std::string line = out.substr(lineStart, lineEnd - lineStart);
    line.erase(line.find_last_not_of(" \t") + 1);

    if (line.empty()) {
      ++newlines;
    } else {
      if (!tidy.empty())
        tidy.append(std::min<size_t>(newlines, 1) + 1, '\n');
      else
        line.erase(0, line.find_first_not_of(" \t"));
      tidy += line;
      newlines = 0;
    }
    lineStart = lineEnd + 1;
  }
  return tidy;
}

StringList workshopUpdatedIds(JsonObject const& seen, List<pair<String, uint64_t>> const& current) {
  StringList updated;
  for (auto const& item : current) {
    auto seenTime = seen.ptr(item.first);
    if (!seenTime || !seenTime->canConvert(Json::Type::Int))
      continue;
    if (item.second > seenTime->toUInt())
      updated.append(item.first);
  }
  return updated;
}

JsonObject workshopSeenUpdateRecord(List<pair<String, uint64_t>> const& current) {
  JsonObject record;
  for (auto const& item : current)
    record[item.first] = item.second;
  return record;
}

}
