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
  struct PendingAction {
    WorkshopRequestId request;
    String id;
    bool subscribe;
  };

  void search();
  void requestPage(uint32_t page);
  void setSort(WorkshopSort sort);
  // Lists the player's own subscriptions instead of browsing the Workshop.
  void showMine();
  void updateModeButtons();
  void pollQuery();
  void populateList();

  void toggleSubscription();
  void startSubscribe(WorkshopItem const& item);
  void pollResolver();
  void finishResolver();
  void subscribeIds(StringList const& ids);
  void startAction(String const& id, bool subscribe);
  void pollActions();

  void retryDownload();
  void openInSteam();

  void updateRows();
  void updateDetails();
  void updateApply();

  void setStatus(String const& message, std::function<void()> retry = {});
  WorkshopItem const* selectedItem() const;
  static String stateText(WorkshopItemStatus const& status);

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
  bool m_showMine;
  // Every subscribed id while showing Mine, paged through in blocks of 50.
  StringList m_mineIds;
  Maybe<WorkshopRequestId> m_query;
  List<WorkshopItem> m_items;

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
  ButtonWidgetPtr m_sortMine;
  ButtonWidgetPtr m_prevPage;
  ButtonWidgetPtr m_nextPage;
  LabelWidgetPtr m_pageLabel;

  WorkshopPreviewWidgetPtr m_preview;
  LabelWidgetPtr m_title;
  LabelWidgetPtr m_author;
  LabelWidgetPtr m_subscribers;
  LabelWidgetPtr m_itemState;
  LabelWidgetPtr m_description;
  ButtonWidgetPtr m_subscribe;
  ButtonWidgetPtr m_retryDownload;
  ButtonWidgetPtr m_openSteam;
  LabelWidgetPtr m_applyLabel;
  ButtonWidgetPtr m_apply;
};

}
