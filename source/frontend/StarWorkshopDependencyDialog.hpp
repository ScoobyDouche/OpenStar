#pragma once

#include "StarPane.hpp"

namespace Star {

STAR_CLASS(WorkshopDependencyDialog);

enum class WorkshopDependencyChoice {
  SubscribeAll,
  JustThis,
  Cancel
};

class WorkshopDependencyDialog final : public Pane {
public:
  typedef std::function<void(WorkshopDependencyChoice)> Callback;

  WorkshopDependencyDialog();

  // Lists the dependencies that would also be subscribed and any that are
  // unavailable.  Offers All / Just this / Cancel.
  void displayRequirements(StringList const& requiredTitles, StringList const& unavailableTitles, Callback callback);
  // Dependency lookup failed.  Offers Anyway (JustThis) / Cancel.
  void displayResolveFailure(Callback callback);

  void dismissed() override;

private:
  void construct(String const& message, bool allowSubscribeAll, Callback callback);
  void reply(WorkshopDependencyChoice choice);

  static String summarize(StringList const& titles);

  bool m_replied;
  Callback m_callback;
};

}
