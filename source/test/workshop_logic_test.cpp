#include "StarWorkshopLogic.hpp"

#include "gtest/gtest.h"

#include <string>
#include <vector>

using namespace Star;

static std::vector<std::string> ids(StringList const& list) {
  std::vector<std::string> result;
  for (auto const& id : list)
    result.push_back(id.utf8());
  return result;
}

static WorkshopItem makeItem(String const& id, StringList dependencies = {}, bool available = true) {
  WorkshopItem item;
  item.id = id;
  item.title = String("Title ") + id;
  item.dependencyIds = std::move(dependencies);
  item.available = available;
  return item;
}

TEST(WorkshopDependencyResolverTest, NoDependencies) {
  WorkshopDependencyResolver resolver("A", {});
  EXPECT_EQ(ids(resolver.nextBatch()), std::vector<std::string>({"A"}));
  resolver.supplyBatch({makeItem("A")});
  EXPECT_TRUE(resolver.finished());
  EXPECT_TRUE(resolver.nextBatch().empty());
  EXPECT_TRUE(resolver.toSubscribe().empty());
  EXPECT_TRUE(resolver.unavailable().empty());
}

TEST(WorkshopDependencyResolverTest, OneLevel) {
  WorkshopDependencyResolver resolver("A", {});
  resolver.nextBatch();
  resolver.supplyBatch({makeItem("A", {"B", "C"})});
  EXPECT_FALSE(resolver.finished());
  EXPECT_EQ(ids(resolver.nextBatch()), std::vector<std::string>({"B", "C"}));
  resolver.supplyBatch({makeItem("B"), makeItem("C")});
  EXPECT_TRUE(resolver.finished());
  EXPECT_EQ(ids(resolver.toSubscribe()), std::vector<std::string>({"B", "C"}));
  EXPECT_EQ(resolver.titleFor("B").utf8(), "Title B");
}

TEST(WorkshopDependencyResolverTest, MultipleLevels) {
  WorkshopDependencyResolver resolver("A", {});
  resolver.nextBatch();
  resolver.supplyBatch({makeItem("A", {"B"})});
  EXPECT_EQ(ids(resolver.nextBatch()), std::vector<std::string>({"B"}));
  resolver.supplyBatch({makeItem("B", {"C"})});
  EXPECT_EQ(ids(resolver.nextBatch()), std::vector<std::string>({"C"}));
  resolver.supplyBatch({makeItem("C")});
  EXPECT_TRUE(resolver.finished());
  EXPECT_EQ(ids(resolver.toSubscribe()), std::vector<std::string>({"B", "C"}));
}

TEST(WorkshopDependencyResolverTest, DiamondVisitsSharedDependencyOnce) {
  WorkshopDependencyResolver resolver("A", {});
  resolver.nextBatch();
  resolver.supplyBatch({makeItem("A", {"B", "C"})});
  resolver.nextBatch();
  resolver.supplyBatch({makeItem("B", {"D"}), makeItem("C", {"D"})});
  EXPECT_EQ(ids(resolver.nextBatch()), std::vector<std::string>({"D"}));
  resolver.supplyBatch({makeItem("D")});
  EXPECT_TRUE(resolver.finished());
  EXPECT_EQ(ids(resolver.toSubscribe()), std::vector<std::string>({"B", "C", "D"}));
}

TEST(WorkshopDependencyResolverTest, CycleTerminates) {
  WorkshopDependencyResolver resolver("A", {});
  resolver.nextBatch();
  resolver.supplyBatch({makeItem("A", {"B"})});
  resolver.nextBatch();
  resolver.supplyBatch({makeItem("B", {"A"})});
  EXPECT_TRUE(resolver.finished());
  EXPECT_TRUE(resolver.nextBatch().empty());
  EXPECT_EQ(ids(resolver.toSubscribe()), std::vector<std::string>({"B"}));
}

TEST(WorkshopDependencyResolverTest, AlreadySubscribedExcluded) {
  StringSet subscribed;
  subscribed.add("B");
  WorkshopDependencyResolver resolver("A", subscribed);
  resolver.nextBatch();
  resolver.supplyBatch({makeItem("A", {"B", "C"})});
  resolver.nextBatch();
  resolver.supplyBatch({makeItem("B"), makeItem("C")});
  EXPECT_TRUE(resolver.finished());
  EXPECT_EQ(ids(resolver.toSubscribe()), std::vector<std::string>({"C"}));
  EXPECT_EQ(ids(resolver.alreadySubscribed()), std::vector<std::string>({"B"}));
}

TEST(WorkshopDependencyResolverTest, MissingAndHiddenReportedUnavailable) {
  WorkshopDependencyResolver resolver("A", {});
  resolver.nextBatch();
  resolver.supplyBatch({makeItem("A", {"B", "C", "D"})});
  resolver.nextBatch();
  resolver.supplyBatch({makeItem("C", {}, false), makeItem("D")});
  EXPECT_TRUE(resolver.finished());
  EXPECT_EQ(ids(resolver.unavailable()), std::vector<std::string>({"B", "C"}));
  EXPECT_EQ(ids(resolver.toSubscribe()), std::vector<std::string>({"D"}));
  EXPECT_EQ(resolver.titleFor("B").utf8(), "B");
}

TEST(WorkshopDependencyResolverTest, FailStopsResolution) {
  WorkshopDependencyResolver resolver("A", {});
  resolver.nextBatch();
  resolver.fail();
  EXPECT_TRUE(resolver.failed());
  EXPECT_FALSE(resolver.finished());
  EXPECT_TRUE(resolver.nextBatch().empty());
}

TEST(WorkshopDependencyResolverTest, NoNewBatchWhileAwaiting) {
  WorkshopDependencyResolver resolver("A", {});
  EXPECT_FALSE(resolver.nextBatch().empty());
  EXPECT_TRUE(resolver.nextBatch().empty());
  EXPECT_FALSE(resolver.finished());
}

TEST(WorkshopApplyStateTest, NoChangesCannotApply) {
  WorkshopApplyState state;
  EXPECT_FALSE(state.hasChanges());
  EXPECT_FALSE(state.canApply());
}

TEST(WorkshopApplyStateTest, WaitsForDownloads) {
  WorkshopApplyState state;
  state.markChanged("A");
  state.setDownloading("A", true);
  EXPECT_TRUE(state.hasChanges());
  EXPECT_EQ(state.pendingDownloadCount(), 1u);
  EXPECT_FALSE(state.canApply());

  state.setDownloading("A", false);
  EXPECT_EQ(state.pendingDownloadCount(), 0u);
  EXPECT_TRUE(state.canApply());
}

TEST(WorkshopApplyStateTest, IgnoresUnchangedIds) {
  WorkshopApplyState state;
  state.markChanged("A");
  state.setDownloading("B", true);
  EXPECT_EQ(state.pendingDownloadCount(), 0u);
  EXPECT_TRUE(state.canApply());
  EXPECT_EQ(ids(state.changedIds()), std::vector<std::string>({"A"}));
}

TEST(WorkshopApplyStateTest, ResetClearsEverything) {
  WorkshopApplyState state;
  state.markChanged("A");
  state.setDownloading("A", true);
  state.reset();
  EXPECT_FALSE(state.hasChanges());
  EXPECT_EQ(state.pendingDownloadCount(), 0u);
}

TEST(WorkshopPreviewUrlTest, AllowsSteamImageHosts) {
  EXPECT_TRUE(isAllowedWorkshopPreviewUrl("https://steamuserimages-a.akamaihd.net/ugc/123/ABC/"));
  EXPECT_TRUE(isAllowedWorkshopPreviewUrl("https://images.steamusercontent.com/ugc/1/2/?imw=512"));
  EXPECT_TRUE(isAllowedWorkshopPreviewUrl("https://IMAGES.steamusercontent.com/ugc/1/2/"));
}

TEST(WorkshopPreviewUrlTest, RejectsEverythingElse) {
  EXPECT_FALSE(isAllowedWorkshopPreviewUrl(""));
  EXPECT_FALSE(isAllowedWorkshopPreviewUrl("http://images.steamusercontent.com/ugc/1/2/"));
  EXPECT_FALSE(isAllowedWorkshopPreviewUrl("https://evil.example/steamuserimages-a.akamaihd.net/"));
  EXPECT_FALSE(isAllowedWorkshopPreviewUrl("https://steamuserimages-a.akamaihd.net.evil.example/x"));
  EXPECT_FALSE(isAllowedWorkshopPreviewUrl("https://user@images.steamusercontent.com/x"));
  EXPECT_FALSE(isAllowedWorkshopPreviewUrl("https://images.steamusercontent.com:8080/x"));
  EXPECT_FALSE(isAllowedWorkshopPreviewUrl("https://"));
}
