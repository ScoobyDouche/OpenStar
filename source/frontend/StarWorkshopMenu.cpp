#include "StarWorkshopMenu.hpp"
#include "StarWorkshopPreview.hpp"
#include "StarWorkshopDependencyDialog.hpp"
#include "StarRoot.hpp"
#include "StarAssets.hpp"
#include "StarFile.hpp"
#include "StarGuiReader.hpp"
#include "StarGuiContext.hpp"
#include "StarPaneManager.hpp"
#include "StarLabelWidget.hpp"
#include "StarButtonWidget.hpp"
#include "StarListWidget.hpp"
#include "StarTextBoxWidget.hpp"

#include <algorithm>

namespace Star {

static uint32_t const WorkshopPageSize = 50;
static size_t const WorkshopRowTitleLimit = 26;
static size_t const WorkshopDetailTitleLimit = 32;
// A detail row's value shares its strip with the label beside it.
static size_t const WorkshopDetailValueLimit = 30;

static bool pathStartsWith(String const& path, String const& prefix) {
  std::string const& full = path.utf8();
  std::string const& start = prefix.utf8();
  return !start.empty() && full.size() >= start.size() && full.compare(0, start.size(), start) == 0;
}

WorkshopMenu::WorkshopMenu(PaneManager* manager, UserGeneratedContentServicePtr service, std::function<void()> requestApply)
  : m_manager(manager), m_service(std::move(service)), m_requestApply(std::move(requestApply)),
    m_sort(WorkshopSort::Popular), m_page(1), m_pageCount(1), m_hasQueried(false), m_showSubscribed(false) {
  auto assets = Root::singleton().assets();

  m_previewCache = make_shared<WorkshopPreviewCache>();
  m_dependencyDialog = make_shared<WorkshopDependencyDialog>();

  GuiReader reader;
  reader.registerCallback("search", [this](Widget*) { search(); });
  reader.registerCallback("items", [this](Widget*) { updateDetails(); });
  reader.registerCallback("statusRetry", [this](Widget*) {
      auto retry = std::move(m_retry);
      setStatus("");
      if (retry)
        retry();
    });
  reader.registerCallback("sortPopular", [this](Widget*) { setSort(WorkshopSort::Popular); });
  reader.registerCallback("sortRecent", [this](Widget*) { setSort(WorkshopSort::Recent); });
  reader.registerCallback("sortSubscribed", [this](Widget*) { showSubscribed(); });
  reader.registerCallback("prevPage", [this](Widget*) {
      if (m_page > 1)
        requestPage(m_page - 1);
    });
  reader.registerCallback("nextPage", [this](Widget*) {
      if (m_page < m_pageCount)
        requestPage(m_page + 1);
    });
  reader.registerCallback("subscribe", [this](Widget*) { toggleSubscription(); });
  reader.registerCallback("retryDownload", [this](Widget*) { retryDownload(); });
  reader.registerCallback("openSteam", [this](Widget*) { openLink(); });
  reader.registerCallback("apply", [this](Widget*) {
      if (m_applyState.canApply())
        m_requestApply();
    });
  reader.construct(assets->json("/interface/workshopmenu/workshopmenu.config:paneLayout"), this);

  m_searchBox = fetchChild<TextBoxWidget>("searchBox");
  m_list = fetchChild<ListWidget>("itemsArea.list");
  m_status = fetchChild<LabelWidget>("status");
  m_statusRetry = fetchChild<ButtonWidget>("statusRetry");
  m_sortPopular = fetchChild<ButtonWidget>("sortPopular");
  m_sortRecent = fetchChild<ButtonWidget>("sortRecent");
  m_sortSubscribed = fetchChild<ButtonWidget>("sortSubscribed");
  m_prevPage = fetchChild<ButtonWidget>("prevPage");
  m_nextPage = fetchChild<ButtonWidget>("nextPage");
  m_pageLabel = fetchChild<LabelWidget>("pageLabel");

  m_title = fetchChild<LabelWidget>("title");
  m_author = fetchChild<LabelWidget>("author");
  m_subscribersLabel = fetchChild<LabelWidget>("subscribersLabel");
  m_subscribers = fetchChild<LabelWidget>("subscribers");
  m_itemState = fetchChild<LabelWidget>("itemState");
  m_sourceLabel = fetchChild<LabelWidget>("sourceLabel");
  m_source = fetchChild<LabelWidget>("source");
  m_description = fetchChild<LabelWidget>("descriptionArea.description");
  m_subscribe = fetchChild<ButtonWidget>("subscribe");
  m_retryDownload = fetchChild<ButtonWidget>("retryDownload");
  m_openLink = fetchChild<ButtonWidget>("openSteam");
  m_applyLabel = fetchChild<LabelWidget>("applyLabel");
  m_apply = fetchChild<ButtonWidget>("apply");

  // Sits on the flat band the detail background gained between its label
  // strips and the description well.
  m_preview = make_shared<WorkshopPreviewWidget>();
  m_preview->setPosition(Vec2I(220, 153));
  m_preview->setSize(Vec2I(84, 84));
  addChild("preview", m_preview);

  m_prevPage->setEnabled(false);
  m_nextPage->setEnabled(false);
}

void WorkshopMenu::update(float dt) {
  Pane::update(dt);

  if (!m_hasQueried)
    requestPage(1);

  pollQuery();
  pollResolver();
  pollActions();
  m_previewCache->update();

  updateRows();
  updateDetails();
  updateApply();
}

void WorkshopMenu::search() {
  m_searchText = m_searchBox->getText();
  // Searching browses the Workshop rather than what the player already has.
  if (!m_searchText.empty())
    m_showSubscribed = false;
  updateModeButtons();
  requestPage(1);
}

void WorkshopMenu::requestPage(uint32_t page) {
  if (m_query)
    m_service->takeQueryResult(*m_query);

  m_hasQueried = true;
  m_page = page;

  m_items.clear();
  m_pendingLocals.clear();
  m_list->clear();
  m_prevPage->setEnabled(false);
  m_nextPage->setEnabled(false);

  if (!m_showSubscribed) {
    m_query = m_service->queryItems(m_searchText, m_sort, m_page);
    setStatus("Loading...");
    return;
  }

  // Re-read both lists each time so subscribing and unsubscribing show up.
  refreshSubscribedLists();
  size_t total = m_subscribedIds.size() + m_localSources.size();
  m_pageCount = std::max<uint32_t>(1, (uint32_t)((total + WorkshopPageSize - 1) / WorkshopPageSize));
  if (m_page > m_pageCount)
    m_page = m_pageCount;

  if (total == 0) {
    m_query = {};
    populateList();
    setStatus("Nothing subscribed or installed");
    return;
  }

  size_t start = (size_t)(m_page - 1) * WorkshopPageSize;
  size_t end = std::min(total, start + (size_t)WorkshopPageSize);

  StringList pageIds;
  for (size_t i = start; i < end; ++i) {
    if (i < m_subscribedIds.size())
      pageIds.append(m_subscribedIds[i]);
    else
      m_pendingLocals.append(localItem(m_localSources[i - m_subscribedIds.size()]));
  }

  if (pageIds.empty()) {
    // This page is only installed mods, so there is nothing to ask Steam for.
    m_query = {};
    m_items = m_pendingLocals;
    m_pendingLocals.clear();
    setStatus("");
    populateList();
    return;
  }

  m_query = m_service->queryItemDetails(pageIds);
  setStatus("Loading...");
}

void WorkshopMenu::refreshSubscribedLists() {
  m_subscribedIds = m_service->subscribedContentIds();

  StringList workshopDirectories;
  for (auto const& id : m_subscribedIds) {
    if (auto directory = m_service->contentDownloadDirectory(id))
      workshopDirectories.append(*directory);
  }

  // Everything loaded that did not come from a subscription: local mods in the
  // mods folder plus the base game assets.
  m_localSources.clear();
  for (auto const& source : Root::singleton().assets()->assetSources()) {
    bool fromWorkshop = false;
    for (auto const& directory : workshopDirectories) {
      if (pathStartsWith(source, directory)) {
        fromWorkshop = true;
        break;
      }
    }
    if (!fromWorkshop)
      m_localSources.append(source);
  }
}

WorkshopMenu::MenuItem WorkshopMenu::localItem(String const& sourcePath) const {
  auto metadata = Root::singleton().assets()->assetSourceMetadata(sourcePath);

  MenuItem entry;
  entry.local = true;
  entry.name = bestModName(metadata, sourcePath);
  entry.author = metadata.value("author", "").toString();
  entry.description = metadata.value("description", "").toString();
  entry.path = sourcePath;
  entry.link = metadata.value("link", "").toString();
  if (auto version = metadata.ptr("version"))
    entry.version = version->printString();
  return entry;
}

String WorkshopMenu::bestModName(JsonObject const& metadata, String const& sourcePath) {
  if (auto name = metadata.ptr("friendlyName"))
    return name->toString();
  if (auto name = metadata.ptr("name"))
    return name->toString();

  String baseName = File::baseName(sourcePath);
  if (baseName.contains("."))
    baseName.rextract(".");
  return baseName;
}

void WorkshopMenu::setSort(WorkshopSort sort) {
  m_sort = sort;
  m_showSubscribed = false;
  updateModeButtons();
  requestPage(1);
}

void WorkshopMenu::showSubscribed() {
  m_showSubscribed = true;
  m_searchText = "";
  m_searchBox->setText("", false);
  updateModeButtons();
  requestPage(1);
}

void WorkshopMenu::updateModeButtons() {
  m_sortPopular->setChecked(!m_showSubscribed && m_sort == WorkshopSort::Popular);
  m_sortRecent->setChecked(!m_showSubscribed && m_sort == WorkshopSort::Recent);
  m_sortSubscribed->setChecked(m_showSubscribed);
}

void WorkshopMenu::pollQuery() {
  if (!m_query)
    return;

  auto status = m_service->requestStatus(*m_query);
  if (status == WorkshopRequestStatus::Pending)
    return;

  auto result = m_service->takeQueryResult(*m_query);
  m_query = {};

  if (status == WorkshopRequestStatus::Failed || !result) {
    uint32_t page = m_page;
    m_pendingLocals.clear();
    setStatus("Couldn't reach Workshop", [this, page]() { requestPage(page); });
    return;
  }

  m_items.clear();
  for (auto const& item : result->items) {
    MenuItem entry;
    entry.id = item.id;
    // Removed items come back from Steam with no title.
    entry.name = item.title.empty() ? item.id : item.title;
    entry.author = item.authorId;
    entry.description = item.description;
    entry.previewUrl = item.previewUrl;
    entry.subscriberCount = item.subscriberCount;
    entry.available = item.available;
    m_items.append(std::move(entry));
  }

  if (m_showSubscribed) {
    m_items.appendAll(m_pendingLocals);
    m_pendingLocals.clear();
  } else {
    m_pageCount = std::max<uint32_t>(1, (result->totalResults + WorkshopPageSize - 1) / WorkshopPageSize);
  }

  if (m_items.empty())
    setStatus(m_showSubscribed ? "Nothing subscribed or installed" : "No mods found");
  else
    setStatus("");
  populateList();
}

void WorkshopMenu::populateList() {
  m_list->clear();
  for (auto const& item : m_items) {
    auto row = m_list->addItem();
    String title = item.name;
    if (title.size() > WorkshopRowTitleLimit)
      title = title.substr(0, WorkshopRowTitleLimit - 3) + "...";
    row->fetchChild<LabelWidget>("name")->setText(title);
  }

  m_pageLabel->setText(strf("Page {} of {}", m_page, m_pageCount));
  m_prevPage->setEnabled(m_page > 1);
  m_nextPage->setEnabled(m_page < m_pageCount);
}

void WorkshopMenu::toggleSubscription() {
  auto item = selectedItem();
  if (!item || item->local || m_resolver)
    return;

  if (m_service->itemStatus(item->id).state == WorkshopItemState::NotSubscribed)
    startSubscribe(item->id);
  else
    startAction(item->id, false);
}

void WorkshopMenu::startSubscribe(String const& id) {
  StringSet subscribed;
  for (auto const& subscribedId : m_service->subscribedContentIds())
    subscribed.add(subscribedId);
  m_resolver = WorkshopDependencyResolver(id, subscribed);
  m_resolverRequest = {};
}

void WorkshopMenu::pollResolver() {
  if (!m_resolver)
    return;

  if (m_resolverRequest) {
    auto status = m_service->requestStatus(*m_resolverRequest);
    if (status == WorkshopRequestStatus::Pending)
      return;

    auto result = m_service->takeQueryResult(*m_resolverRequest);
    m_resolverRequest = {};
    if (status == WorkshopRequestStatus::Succeeded && result)
      m_resolver->supplyBatch(result->items);
    else
      m_resolver->fail();
  }

  StringList batch = m_resolver->nextBatch();
  if (!batch.empty()) {
    m_resolverRequest = m_service->queryItemDetails(batch);
    return;
  }

  finishResolver();
}

void WorkshopMenu::finishResolver() {
  WorkshopDependencyResolver resolver = m_resolver.take();
  String rootId = resolver.rootId();

  if (resolver.failed()) {
    m_dependencyDialog->displayResolveFailure([this, rootId](WorkshopDependencyChoice choice) {
        if (choice != WorkshopDependencyChoice::Cancel)
          subscribeIds({rootId});
      });
    m_manager->displayPane(PaneLayer::ModalWindow, m_dependencyDialog);
    return;
  }

  if (resolver.toSubscribe().empty() && resolver.unavailable().empty()) {
    subscribeIds({rootId});
    return;
  }

  StringList dependencyIds = resolver.toSubscribe();
  StringList requiredTitles;
  for (auto const& id : dependencyIds)
    requiredTitles.append(resolver.titleFor(id));
  StringList unavailableTitles;
  for (auto const& id : resolver.unavailable())
    unavailableTitles.append(resolver.titleFor(id));

  m_dependencyDialog->displayRequirements(requiredTitles, unavailableTitles, [this, rootId, dependencyIds](WorkshopDependencyChoice choice) {
      if (choice == WorkshopDependencyChoice::SubscribeAll) {
        StringList ids = {rootId};
        ids.appendAll(dependencyIds);
        subscribeIds(ids);
      } else if (choice == WorkshopDependencyChoice::JustThis) {
        subscribeIds({rootId});
      }
    });
  m_manager->displayPane(PaneLayer::ModalWindow, m_dependencyDialog);
}

void WorkshopMenu::subscribeIds(StringList const& ids) {
  for (auto const& id : ids)
    startAction(id, true);
}

void WorkshopMenu::startAction(String const& id, bool subscribe) {
  WorkshopRequestId request = subscribe ? m_service->subscribe(id) : m_service->unsubscribe(id);
  m_actions.append(PendingAction{request, id, subscribe});
}

void WorkshopMenu::pollActions() {
  for (size_t i = 0; i < m_actions.size();) {
    PendingAction action = m_actions[i];
    auto status = m_service->requestStatus(action.request);
    if (status == WorkshopRequestStatus::Pending) {
      ++i;
      continue;
    }

    m_service->releaseRequest(action.request);
    m_actions.eraseAt(i);

    if (status == WorkshopRequestStatus::Succeeded) {
      m_applyState.markChanged(action.id);
    } else {
      setStatus(action.subscribe ? "Subscribe failed" : "Unsubscribe failed", [this, action]() {
          startAction(action.id, action.subscribe);
        });
    }
  }
}

void WorkshopMenu::retryDownload() {
  auto item = selectedItem();
  if (item && !item->local)
    m_service->retryDownload(item->id);
}

void WorkshopMenu::openLink() {
  auto item = selectedItem();
  if (!item)
    return;

  String url = item->local ? item->link : strf("https://steamcommunity.com/sharedfiles/filedetails/?id={}", item->id);
  if (url.empty())
    return;

  auto& guiContext = GuiContext::singleton();
  if (auto desktopService = guiContext.applicationController()->desktopService())
    desktopService->openUrl(url);
  else
    guiContext.setClipboard(url);
}

void WorkshopMenu::updateRows() {
  for (size_t i = 0; i < m_items.size() && i < m_list->listSize(); ++i) {
    auto stateLabel = m_list->itemAt(i)->fetchChild<LabelWidget>("state");
    if (m_items[i].local)
      stateLabel->setText("Installed");
    else
      stateLabel->setText(stateText(m_service->itemStatus(m_items[i].id)));
  }
}

void WorkshopMenu::updateDetails() {
  auto item = selectedItem();

  if (!item) {
    if (!m_detailsItemId.empty()) {
      m_detailsItemId = "";
      m_title->setText("");
      m_author->setText("");
      m_subscribers->setText("");
      m_itemState->setText("");
      m_source->setText("");
      m_description->setText("");
      m_preview->setImage({});
    }
    m_subscribe->setEnabled(false);
    m_openLink->setEnabled(false);
    m_retryDownload->setVisibility(false);
    return;
  }

  String itemKey = item->local ? item->path : item->id;
  if (m_detailsItemId != itemKey) {
    m_detailsItemId = itemKey;

    // The title band is one line across the whole detail panel.
    String title = item->name;
    if (title.size() > WorkshopDetailTitleLimit)
      title = title.substr(0, WorkshopDetailTitleLimit - 3) + "...";
    m_title->setText(title);

    m_subscribersLabel->setText(item->local ? "VERSION" : "SUBSCRIBERS");
    if (item->local) {
      m_subscribers->setText(item->version.empty() ? "-" : item->version);
      m_description->setText(item->description);
    } else {
      m_subscribers->setText(toString(item->subscriberCount));
      m_description->setText(item->description);
    }
  }

  if (item->local) {
    m_author->setText(item->author.empty() ? "-" : item->author);
    m_itemState->setText("Installed");
    // The old Mods menu showed where each source was loaded from.
    m_sourceLabel->setText("PATH");
    m_source->setText(elideFront(item->path, WorkshopDetailValueLimit));
    m_preview->setImage({});
    m_subscribe->setEnabled(false);
    m_subscribe->setText("Subscribe");
    m_retryDownload->setVisibility(false);
    m_openLink->setText("Link");
    m_openLink->setEnabled(!item->link.empty());
    return;
  }

  auto status = m_service->itemStatus(item->id);
  m_author->setText(m_service->personaName(item->author).value(item->author));
  m_itemState->setText(status.state == WorkshopItemState::NotSubscribed ? "Not subscribed" : stateText(status));
  m_sourceLabel->setText("WORKSHOP ID");
  m_source->setText(item->id);
  m_preview->setImage(m_previewCache->get(item->id, item->previewUrl));

  bool checking = m_resolver && m_resolver->rootId() == item->id;
  m_subscribe->setEnabled(!m_resolver);
  if (checking)
    m_subscribe->setText("Checking...");
  else
    m_subscribe->setText(status.state == WorkshopItemState::NotSubscribed ? "Subscribe" : "Unsubscribe");
  m_openLink->setText("Steam");
  m_openLink->setEnabled(true);
  m_retryDownload->setVisibility(status.state == WorkshopItemState::DownloadFailed);
}

void WorkshopMenu::updateApply() {
  for (auto const& id : m_applyState.changedIds())
    m_applyState.setDownloading(id, m_service->itemStatus(id).state == WorkshopItemState::Downloading);

  bool hasChanges = m_applyState.hasChanges();
  size_t pending = m_applyState.pendingDownloadCount();

  m_apply->setVisibility(hasChanges);
  m_apply->setEnabled(m_applyState.canApply());

  if (!hasChanges)
    m_applyLabel->setText("");
  else if (pending > 0)
    m_applyLabel->setText(strf("Waiting for {} download{}", pending, pending == 1 ? "" : "s"));
  else
    m_applyLabel->setText("Changes ready");
}

void WorkshopMenu::setStatus(String const& message, std::function<void()> retry) {
  m_status->setText(message);
  m_retry = std::move(retry);
  m_statusRetry->setVisibility((bool)m_retry);
}

WorkshopMenu::MenuItem const* WorkshopMenu::selectedItem() const {
  size_t index = m_list->selectedItem();
  if (index == NPos || index >= m_items.size())
    return nullptr;
  return &m_items[index];
}

String WorkshopMenu::stateText(WorkshopItemStatus const& status) {
  switch (status.state) {
    case WorkshopItemState::NotSubscribed:
      return "";
    case WorkshopItemState::Downloading:
      return strf("{}%", (int)(status.downloadProgress * 100.0f));
    case WorkshopItemState::Installed:
      return "^green;Subscribed";
    case WorkshopItemState::NeedsUpdate:
      return "Update";
    case WorkshopItemState::DownloadFailed:
      return "^red;Failed";
  }
  return "";
}

String WorkshopMenu::elideFront(String const& text, size_t limit) {
  if (text.size() <= limit)
    return text;
  return "..." + text.substr(text.size() - (limit - 3));
}

}
