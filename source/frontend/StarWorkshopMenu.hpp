#pragma once

#include "StarPane.hpp"
#include "StarUserGeneratedContentService.hpp"
#include "StarWorkshopLogic.hpp"

namespace Star {

STAR_CLASS(PaneManager);
STAR_CLASS(ListWidget);
STAR_CLASS(LabelWidget);
STAR_CLASS(ButtonWidget);
STAR_CLASS(TextBoxWidget);
STAR_CLASS(WorkshopPreviewCache);
STAR_CLASS(WorkshopPreviewWidget);
STAR_CLASS(WorkshopDependencyDialog);
STAR_CLASS(WorkshopMenu);

class WorkshopMenu : public Pane {
public:
  WorkshopMenu(PaneManager* manager, UserGeneratedContentServicePtr service, std::function<void()> requestApply);

  void update(float dt) override;

private:
  // A row in the list: either a Workshop item or a loaded asset source that
  // did not come from the Workshop.
  struct MenuItem {
    bool local = false;
    // Workshop id; empty for local asset sources.
    String id;
    String name;
    // Workshop: the owner's Steam id, resolved to a name for display.
    // Local: the author from the mod's metadata.
    String author;
    String description;
    String previewUrl;
    uint64_t subscriberCount = 0;
    bool available = true;
    // Local only.
    String path;
    String version;
    String link;
  };

  struct PendingAction {
    WorkshopRequestId request;
    String id;
    bool subscribe;
  };

  void search();
  void requestPage(uint32_t page);
  void setSort(WorkshopSort sort);
  // Lists the player's subscriptions followed by their other installed mods.
  void showSubscribed();
  void updateModeButtons();
  void pollQuery();
  void populateList();

  void refreshSubscribedLists();
  MenuItem localItem(String const& sourcePath) const;
  static String bestModName(JsonObject const& metadata, String const& sourcePath);

  void toggleSubscription();
  void startSubscribe(String const& id);
  void pollResolver();
  void finishResolver();
  void subscribeIds(StringList const& ids);
  void startAction(String const& id, bool subscribe);
  void pollActions();

  void retryDownload();
  void openLink();

  void updateRows();
  void updateDetails();
  void updateApply();

  void setStatus(String const& message, std::function<void()> retry = {});
  MenuItem const* selectedItem() const;
  static String stateText(WorkshopItemStatus const& status);
  // Detail rows are one line wide, so long values keep their tail.
  static String elideFront(String const& text, size_t limit);

  PaneManager* m_manager;
  UserGeneratedContentServicePtr m_service;
  std::function<void()> m_requestApply;

  WorkshopApplyState m_applyState;
  WorkshopPreviewCachePtr m_previewCache;
  WorkshopDependencyDialogPtr m_dependencyDialog;

  String m_searchText;
  WorkshopSort m_sort;
  uint32_t m_page;
  uint32_t m_pageCount;
  bool m_hasQueried;
  bool m_showSubscribed;

  // While showing Subscribed: every subscribed Workshop id, then every loaded
  // asset source that is not one of them.  Paged through together.
  StringList m_subscribedIds;
  StringList m_localSources;
  // Local rows for the page whose Workshop details are still loading.
  List<MenuItem> m_pendingLocals;

  Maybe<WorkshopRequestId> m_query;
  List<MenuItem> m_items;

  Maybe<WorkshopDependencyResolver> m_resolver;
  Maybe<WorkshopRequestId> m_resolverRequest;
  List<PendingAction> m_actions;

  std::function<void()> m_retry;
  String m_detailsItemId;

  TextBoxWidgetPtr m_searchBox;
  ListWidgetPtr m_list;
  LabelWidgetPtr m_status;
  ButtonWidgetPtr m_statusRetry;
  ButtonWidgetPtr m_sortPopular;
  ButtonWidgetPtr m_sortRecent;
  ButtonWidgetPtr m_sortSubscribed;
  ButtonWidgetPtr m_prevPage;
  ButtonWidgetPtr m_nextPage;
  LabelWidgetPtr m_pageLabel;

  WorkshopPreviewWidgetPtr m_preview;
  LabelWidgetPtr m_title;
  LabelWidgetPtr m_author;
  LabelWidgetPtr m_subscribersLabel;
  LabelWidgetPtr m_subscribers;
  LabelWidgetPtr m_itemState;
  LabelWidgetPtr m_sourceLabel;
  LabelWidgetPtr m_source;
  LabelWidgetPtr m_description;
  ButtonWidgetPtr m_subscribe;
  ButtonWidgetPtr m_retryDownload;
  ButtonWidgetPtr m_openLink;
  LabelWidgetPtr m_applyLabel;
  ButtonWidgetPtr m_apply;
};

}
