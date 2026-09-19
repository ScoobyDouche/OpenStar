#pragma once

#include "StarPane.hpp"

namespace Star {

STAR_CLASS(ListWidget);
STAR_CLASS(WorkshopUpdatesDialog);

// Lists subscribed Workshop items that updated since the last launch, each
// with a button that opens its Steam changelog.
class WorkshopUpdatesDialog final : public Pane {
public:
  WorkshopUpdatesDialog();

  // Each entry is a Workshop id and the title to show for it.
  void setUpdates(List<pair<String, String>> const& updates);

private:
  void openChangelog(String const& id);

  String m_changelogUrl;
  ListWidgetPtr m_list;
};

}
