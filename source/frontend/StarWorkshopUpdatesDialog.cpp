#include "StarWorkshopUpdatesDialog.hpp"
#include "StarGuiReader.hpp"
#include "StarGuiContext.hpp"
#include "StarRoot.hpp"
#include "StarAssets.hpp"
#include "StarLabelWidget.hpp"
#include "StarListWidget.hpp"

namespace Star {

static size_t const UpdateRowTitleLimit = 26;

WorkshopUpdatesDialog::WorkshopUpdatesDialog() {
  auto assets = Root::singleton().assets();
  m_changelogUrl = assets->json("/interface/workshopmenu/updates.config:changelogUrl").toString();

  GuiReader reader;
  reader.registerCallback("close", [this](Widget*) { dismiss(); });
  reader.registerCallback("openChangelog", [this](Widget*) { openSelectedChangelog(); });
  reader.construct(assets->json("/interface/workshopmenu/updates.config:paneLayout"), this);

  m_list = fetchChild<ListWidget>("modsArea.list");
  setAnchor(PaneAnchor::Center);
}

void WorkshopUpdatesDialog::setUpdates(List<pair<String, String>> const& updates) {
  m_list->clear();
  m_ids.clear();
  for (auto const& update : updates) {
    auto row = m_list->addItem();

    String title = update.second;
    if (title.size() > UpdateRowTitleLimit)
      title = title.substr(0, UpdateRowTitleLimit - 3) + "...";
    row->fetchChild<LabelWidget>("name")->setText(title);
    m_ids.append(update.first);
  }
}

void WorkshopUpdatesDialog::openSelectedChangelog() {
  size_t index = m_list->selectedItem();
  if (index >= m_ids.size())
    return;
  // Deselect so clicking the same row again opens it again.  This re-enters
  // with no selection, which the check above ignores.
  m_list->clearSelected();

  String url = m_changelogUrl.replace("{}", m_ids[index]);

  auto& guiContext = GuiContext::singleton();
  if (auto desktopService = guiContext.applicationController()->desktopService())
    desktopService->openUrl(url);
  else
    guiContext.setClipboard(url);
}

}
