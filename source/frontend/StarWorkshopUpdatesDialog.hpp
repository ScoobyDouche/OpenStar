#pragma once

#include "StarPane.hpp"

namespace Star {

STAR_CLASS(ListWidget);
STAR_CLASS(WorkshopUpdatesDialog);

// Lists subscribed Workshop items that updated since the last launch.
// Clicking a row opens that item's Steam changelog.
class WorkshopUpdatesDialog final : public Pane {
public:
  WorkshopUpdatesDialog();

  // Each entry is a Workshop id and the title to show for it.
  void setUpdates(List<pair<String, String>> const& updates);

private:
  void openSelectedChangelog();

  String m_changelogUrl;
  ListWidgetPtr m_list;
  StringList m_ids;
};

}
