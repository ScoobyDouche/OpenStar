#include "StarWorkshopMenu.hpp"
#include "StarWorkshopPreview.hpp"
#include "StarWorkshopDependencyDialog.hpp"
#include "StarRoot.hpp"
#include "StarAssets.hpp"
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

WorkshopMenu::WorkshopMenu(PaneManager* manager, UserGeneratedContentServicePtr service, std::function<void()> requestApply)
  : m_manager(manager), m_service(std::move(service)), m_requestApply(std::move(requestApply)),
    m_sort(WorkshopSort::Popular), m_page(1), m_pageCount(1), m_hasQueried(false), m_showMine(false) {
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
  reader.registerCallback("sortMine", [this](Widget*) { showMine(); });
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
  reader.registerCallback("openSteam", [this](Widget*) { openInSteam(); });
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
  m_sortMine = fetchChild<ButtonWidget>("sortMine");
  m_prevPage = fetchChild<ButtonWidget>("prevPage");
  m_nextPage = fetchChild<ButtonWidget>("nextPage");
  m_pageLabel = fetchChild<LabelWidget>("pageLabel");

  m_title = fetchChild<LabelWidget>("title");
  m_author = fetchChild<LabelWidget>("author");
  m_subscribers = fetchChild<LabelWidget>("subscribers");
  m_itemState = fetchChild<LabelWidget>("itemState");
  m_description = fetchChild<LabelWidget>("descriptionArea.description");
  m_subscribe = fetchChild<ButtonWidget>("subscribe");
  m_retryDownload = fetchChild<ButtonWidget>("retryDownload");
  m_openSteam = fetchChild<ButtonWidget>("openSteam");
  m_applyLabel = fetchChild<LabelWidget>("applyLabel");
  m_apply = fetchChild<ButtonWidget>("apply");

  m_preview = make_shared<WorkshopPreviewWidget>();
  m_preview->setPosition(Vec2I(180, 124));
  m_preview->setSize(Vec2I(76, 76));
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
  // Searching browses the Workshop rather than the player's own subscriptions.
  if (!m_searchText.empty())
    m_showMine = false;
  updateModeButtons();
  requestPage(1);
}

void WorkshopMenu::requestPage(uint32_t page) {
  if (m_query)
    m_service->takeQueryResult(*m_query);

  m_hasQueried = true;
  m_page = page;

  m_items.clear();
  m_list->clear();
  m_prevPage->setEnabled(false);
  m_nextPage->setEnabled(false);

  if (m_showMine) {
    // Re-read the subscriptions each time so unsubscribes drop out.
    m_mineIds = m_service->subscribedContentIds();
    m_pageCount = std::max<uint32_t>(1, (uint32_t)((m_mineIds.size() + WorkshopPageSize - 1) / WorkshopPageSize));
    if (m_page > m_pageCount)
      m_page = m_pageCount;

    if (m_mineIds.empty()) {
      m_query = {};
      m_pageLabel->setText(strf("Page {} of {}", m_page, m_pageCount));
      setStatus("You aren't subscribed to any mods");
      return;
    }

    StringList pageIds;
    size_t start = (size_t)(m_page - 1) * WorkshopPageSize;
    for (size_t i = start; i < m_mineIds.size() && i < start + WorkshopPageSize; ++i)
      pageIds.append(m_mineIds[i]);
    m_query = m_service->queryItemDetails(pageIds);
  } else {
    m_query = m_service->queryItems(m_searchText, m_sort, m_page);
  }

  setStatus("Loading...");
}

void WorkshopMenu::setSort(WorkshopSort sort) {
  m_sort = sort;
  m_showMine = false;
  updateModeButtons();
  requestPage(1);
}

void WorkshopMenu::showMine() {
  m_showMine = true;
  m_searchText = "";
  m_searchBox->setText("", false);
  updateModeButtons();
  requestPage(1);
}

void WorkshopMenu::updateModeButtons() {
  m_sortPopular->setChecked(!m_showMine && m_sort == WorkshopSort::Popular);
  m_sortRecent->setChecked(!m_showMine && m_sort == WorkshopSort::Recent);
  m_sortMine->setChecked(m_showMine);
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
    setStatus("Couldn't reach Workshop", [this, page]() { requestPage(page); });
    return;
  }

  m_items = std::move(result->items);
  // In Mine the page count comes from the subscription list, not the query.
  if (!m_showMine)
    m_pageCount = std::max<uint32_t>(1, (result->totalResults + WorkshopPageSize - 1) / WorkshopPageSize);
  if (m_items.empty())
    setStatus(m_showMine ? "You aren't subscribed to any mods" : "No mods found");
  else
    setStatus("");
  populateList();
}

void WorkshopMenu::populateList() {
  m_list->clear();
  for (auto const& item : m_items) {
    auto row = m_list->addItem();
    // Removed items come back from Steam with no title.
    String title = item.title.empty() ? item.id : item.title;
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
  if (!item || m_resolver)
    return;

  if (m_service->itemStatus(item->id).state == WorkshopItemState::NotSubscribed)
    startSubscribe(*item);
  else
    startAction(item->id, false);
}

void WorkshopMenu::startSubscribe(WorkshopItem const& item) {
  StringSet subscribed;
  for (auto const& id : m_service->subscribedContentIds())
    subscribed.add(id);
  m_resolver = WorkshopDependencyResolver(item.id, subscribed);
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
  if (auto item = selectedItem())
    m_service->retryDownload(item->id);
}

void WorkshopMenu::openInSteam() {
  auto item = selectedItem();
  if (!item)
    return;

  String url = strf("https://steamcommunity.com/sharedfiles/filedetails/?id={}", item->id);
  auto& guiContext = GuiContext::singleton();
  if (auto desktopService = guiContext.applicationController()->desktopService())
    desktopService->openUrl(url);
  else
    guiContext.setClipboard(url);
}

void WorkshopMenu::updateRows() {
  for (size_t i = 0; i < m_items.size() && i < m_list->listSize(); ++i) {
    auto status = m_service->itemStatus(m_items[i].id);
    m_list->itemAt(i)->fetchChild<LabelWidget>("state")->setText(stateText(status));
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
      m_description->setText("");
      m_preview->setImage({});
    }
    m_subscribe->setEnabled(false);
    m_openSteam->setEnabled(false);
    m_retryDownload->setVisibility(false);
    return;
  }

  if (m_detailsItemId != item->id) {
    m_detailsItemId = item->id;
    // Kept to one line so it never runs into the labels beside the preview.
    String title = item->title.empty() ? item->id : item->title;
    if (title.size() > WorkshopDetailTitleLimit)
      title = title.substr(0, WorkshopDetailTitleLimit - 3) + "...";
    m_title->setText(title);
    m_subscribers->setText(toString(item->subscriberCount));
    m_description->setText(item->description);
  }

  auto status = m_service->itemStatus(item->id);
  m_author->setText(m_service->personaName(item->authorId).value(item->authorId));
  m_itemState->setText(status.state == WorkshopItemState::NotSubscribed ? "Not subscribed" : stateText(status));
  m_preview->setImage(m_previewCache->get(item->id, item->previewUrl));

  bool checking = m_resolver && m_resolver->rootId() == item->id;
  m_subscribe->setEnabled(!m_resolver);
  if (checking)
    m_subscribe->setText("Checking...");
  else
    m_subscribe->setText(status.state == WorkshopItemState::NotSubscribed ? "Subscribe" : "Unsubscribe");
  m_openSteam->setEnabled(true);
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

WorkshopItem const* WorkshopMenu::selectedItem() const {
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

}
