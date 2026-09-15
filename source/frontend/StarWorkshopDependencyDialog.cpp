#include "StarWorkshopDependencyDialog.hpp"
#include "StarGuiReader.hpp"
#include "StarRoot.hpp"
#include "StarAssets.hpp"
#include "StarLabelWidget.hpp"
#include "StarButtonWidget.hpp"

namespace Star {

static size_t const MaxListedTitles = 6;

WorkshopDependencyDialog::WorkshopDependencyDialog() : m_replied(true) {
  setAnchor(PaneAnchor::Center);
}

void WorkshopDependencyDialog::displayRequirements(StringList const& requiredTitles, StringList const& unavailableTitles, Callback callback) {
  String message;
  if (!requiredTitles.empty())
    message = strf("This mod also needs:\n^green;{}^reset;", summarize(requiredTitles));
  if (!unavailableTitles.empty()) {
    if (!message.empty())
      message += "\n\n";
    message += strf("Unavailable (hidden or removed):\n^red;{}^reset;\nThis mod may not work.", summarize(unavailableTitles));
  }
  construct(message, !requiredTitles.empty(), std::move(callback));
}

void WorkshopDependencyDialog::displayResolveFailure(Callback callback) {
  construct("Couldn't check requirements for this mod.", false, std::move(callback));
}

void WorkshopDependencyDialog::dismissed() {
  if (!m_replied) {
    m_replied = true;
    auto callback = std::move(m_callback);
    m_callback = {};
    if (callback)
      callback(WorkshopDependencyChoice::Cancel);
  }
  Pane::dismissed();
}

void WorkshopDependencyDialog::construct(String const& message, bool allowSubscribeAll, Callback callback) {
  auto assets = Root::singleton().assets();

  removeAllChildren();
  m_replied = false;
  m_callback = std::move(callback);

  GuiReader reader;
  reader.registerCallback("subscribeAll", [this](Widget*) { reply(WorkshopDependencyChoice::SubscribeAll); });
  reader.registerCallback("justThis", [this](Widget*) { reply(WorkshopDependencyChoice::JustThis); });
  reader.registerCallback("cancel", [this](Widget*) { reply(WorkshopDependencyChoice::Cancel); });
  reader.construct(assets->json("/interface/workshopmenu/dependencies.config:paneLayout"), this);

  fetchChild<LabelWidget>("message")->setText(message);
  fetchChild<ButtonWidget>("subscribeAll")->setVisibility(allowSubscribeAll);
  fetchChild<ButtonWidget>("justThis")->setText(allowSubscribeAll ? "Just this" : "Anyway");
}

void WorkshopDependencyDialog::reply(WorkshopDependencyChoice choice) {
  m_replied = true;
  auto callback = std::move(m_callback);
  m_callback = {};
  dismiss();
  if (callback)
    callback(choice);
}

String WorkshopDependencyDialog::summarize(StringList const& titles) {
  if (titles.size() <= MaxListedTitles)
    return titles.join(", ");

  StringList listed;
  for (size_t i = 0; i < MaxListedTitles; ++i)
    listed.append(titles[i]);
  return strf("{} and {} more", listed.join(", "), titles.size() - MaxListedTitles);
}

}
