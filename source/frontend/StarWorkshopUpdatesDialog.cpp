#include "StarWorkshopUpdatesDialog.hpp"
#include "StarGuiReader.hpp"
#include "StarGuiContext.hpp"
#include "StarRoot.hpp"
#include "StarAssets.hpp"
#include "StarLabelWidget.hpp"
#include "StarButtonWidget.hpp"
#include "StarListWidget.hpp"

namespace Star {

static size_t const UpdateRowTitleLimit = 26;

WorkshopUpdatesDialog::WorkshopUpdatesDialog() {
  auto assets = Root::singleton().assets();
  m_changelogUrl = assets->json("/interface/workshopmenu/updates.config:changelogUrl").toString();

  GuiReader reader;
  reader.registerCallback("close", [this](Widget*) { dismiss(); });
  reader.construct(assets->json("/interface/workshopmenu/updates.config:paneLayout"), this);

  m_list = fetchChild<ListWidget>("modsArea.list");
  setAnchor(PaneAnchor::Center);
}

void WorkshopUpdatesDialog::setUpdates(List<pair<String, String>> const& updates) {
  m_list->clear();
  for (auto const& update : updates) {
    auto row = m_list->addItem();

    String title = update.second;
    if (title.size() > UpdateRowTitleLimit)
      title = title.substr(0, UpdateRowTitleLimit - 3) + "...";
    row->fetchChild<LabelWidget>("name")->setText(title);

    String id = update.first;
    row->fetchChild<ButtonWidget>("changelog")->setCallback([this, id](Widget*) { openChangelog(id); });
  }
}

void WorkshopUpdatesDialog::openChangelog(String const& id) {
  String url = m_changelogUrl.replace("{}", id);

  auto& guiContext = GuiContext::singleton();
  if (auto desktopService = guiContext.applicationController()->desktopService())
    desktopService->openUrl(url);
  else
    guiContext.setClipboard(url);
}

}
