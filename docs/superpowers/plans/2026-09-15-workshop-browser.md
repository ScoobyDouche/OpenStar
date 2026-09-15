# In-Game Steam Workshop Browser Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let players search, subscribe to (with dependencies), and unsubscribe from Steam Workshop mods from the OpenStarbound title screen, then apply the changes without restarting.

**Architecture:** The platform-neutral `UserGeneratedContentService` gains async Workshop query/subscribe methods implemented with the Steam UGC API. Steam-free logic (dependency resolution, apply state, URL allowlist) lives in `StarWorkshopLogic` and is unit tested. A native C++ `WorkshopMenu` pane (plus a dependency dialog and a texture-backed preview widget) drives the service, and Apply reuses `ClientApplication`'s existing `MainAppState::Mods` reload path.

**Tech Stack:** C++17, CMake presets + vcpkg, Steamworks SDK (bundled in `lib/`), cpr via `HttpClient`, stb_image (vcpkg `stb`), gtest (vendored in `source/test/gtest`), Starbound JSON GUI layouts, Lua asset patches.

**Spec:** `docs/superpowers/specs/2026-09-15-workshop-browser-design.md`

## Global Constraints

- C++17 (`CMAKE_CXX_STANDARD 17` in `source/CMakePresets.json`). Code style: 2-space indent, everything in `namespace Star`, `STAR_CLASS(Name)` forward declarations, match surrounding files.
- Steam Workshop only. Steam API calls only inside `source/application/*_pc_steam.*`.
- No Lua bindings for any Workshop functionality.
- Preview URLs: `https://` only, host exactly `steamuserimages-a.akamaihd.net` or `images.steamusercontent.com`, no `@` or port.
- Request timeout: 30 seconds. Workshop page size: 50. Preview cache: 64 entries, images shrunk to at most 512px per side.
- Workshop button hidden when `userGeneratedContentService()` is null.
- Frontend code never calls `Root::loadMods`; Apply goes through `ClientApplication` via `MainAppState::Mods`.
- All commits end with the trailer `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- Work on branch `feature/workshop-browser`.

## File Structure

| File | Responsibility |
|---|---|
| `source/platform/StarUserGeneratedContentService.hpp` | Workshop types + async service interface |
| `source/application/StarUserGeneratedContentService_pc_steam.{hpp,cpp}` | Steam UGC implementation |
| `source/game/StarWorkshopLogic.{hpp,cpp}` | Dependency resolver, apply state, preview URL allowlist (no I/O) |
| `source/core/StarImageDecode.{hpp,cpp}` | stb_image decoding + downscaling into `Star::Image` |
| `source/frontend/StarWorkshopPreview.{hpp,cpp}` | Preview fetch/decode cache + texture-drawing widget |
| `source/frontend/StarWorkshopDependencyDialog.{hpp,cpp}` | Modal "required mods" dialog |
| `source/frontend/StarWorkshopMenu.{hpp,cpp}` | Workshop browser pane |
| `assets/opensb/interface/workshopmenu/workshopmenu.config` | Browser layout |
| `assets/opensb/interface/workshopmenu/dependencies.config` | Dialog layout |
| `assets/opensb/interface/windowconfig/title.config.patch.lua` | Adds Workshop main-menu button |
| `source/frontend/StarTitleScreen.{hpp,cpp}` | Workshop state, button, pane registration, reload request flag |
| `source/client/StarClientApplication.{hpp,cpp}` | Turns reload request into a mods reload |
| `source/test/workshop_logic_test.cpp` | Logic unit tests (`workshop_tests`) |
| `source/test/image_decode_test.cpp` | Decode unit tests (`core_tests`) |

Paths in commands below are relative to the repo root `OpenStar/` unless they start with `source/`-relative `cd`.

---

### Task 1: Build environment and baseline

Nothing is built on this machine yet: there is no `build/` directory and no `VCPKG_ROOT`. This task gets the existing tests passing before any change.

**Files:** none

- [ ] **Step 1: Install system packages (same list as CI)**

```bash
sudo apt-get update
sudo apt-get install -y pkgconf libxmu-dev libgl-dev libglu1-mesa-dev libasound2-dev libpulse-dev \
  libaudio-dev libjack-dev libsndio-dev libx11-dev libxext-dev \
  libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
  libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev \
  libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev libpipewire-0.3-dev libwayland-dev libdecor-0-dev liburing-dev \
  autoconf autoconf-archive automake libtool patchelf tarlz
```

- [ ] **Step 2: Install vcpkg**

```bash
git clone https://github.com/microsoft/vcpkg ~/vcpkg
~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
echo 'export VCPKG_ROOT="$HOME/vcpkg"' >> ~/.bashrc
export VCPKG_ROOT="$HOME/vcpkg"
```

- [ ] **Step 3: Configure (first run builds all vcpkg dependencies; expect 20–60 minutes)**

```bash
cd source && cmake --preset linux-release
```

Expected: ends with `-- Build files have been written to: .../OpenStar/build/linux-release`, and the log contains `Using Steam platform services: ON` or `TRUE`.

- [ ] **Step 4: Build everything**

```bash
cd source && cmake --build --preset linux-release
```

Expected: exit code 0. The binaries (`starbound`, `asset_packer`, `core_tests`, `game_tests`) are in `OpenStar/dist/`.

- [ ] **Step 5: Run the baseline tests**

```bash
cd source && ctest --preset linux-release
```

Expected: `100% tests passed` (the preset only runs `NoAssets`-labelled tests, currently `core_tests`).

- [ ] **Step 6: Confirm the build left the tree clean**

```bash
git status --short
```

Expected: no output. If `build/` or `dist/` show up, stop and report; do not commit build outputs.

---

### Task 2: Workshop types and Steam-free logic

**Files:**
- Modify: `source/platform/StarUserGeneratedContentService.hpp` (types only in this task)
- Create: `source/game/StarWorkshopLogic.hpp`
- Create: `source/game/StarWorkshopLogic.cpp`
- Modify: `source/game/CMakeLists.txt`
- Create: `source/test/workshop_logic_test.cpp`
- Modify: `source/test/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - Types `WorkshopSort`, `WorkshopItem{id,title,authorId,description,previewUrl,subscriberCount,dependencyIds,available}`, `WorkshopPage{items,page,totalResults}`, `WorkshopItemState`, `WorkshopItemStatus{state,downloadProgress}`, `WorkshopRequestStatus`, `typedef uint64_t WorkshopRequestId` — all in `StarUserGeneratedContentService.hpp`.
  - `bool isAllowedWorkshopPreviewUrl(String const& url);`
  - `class WorkshopDependencyResolver { WorkshopDependencyResolver(String rootId, StringSet alreadySubscribed); String const& rootId() const; StringList nextBatch(); void supplyBatch(List<WorkshopItem> const& items); void fail(); bool finished() const; bool failed() const; StringList const& toSubscribe() const; StringList const& unavailable() const; StringList const& alreadySubscribed() const; String titleFor(String const& id) const; };`
  - `class WorkshopApplyState { void markChanged(String const& id); void setDownloading(String const& id, bool downloading); bool hasChanges() const; size_t pendingDownloadCount() const; bool canApply() const; StringList changedIds() const; void reset(); };`

- [ ] **Step 1: Add the Workshop types to the platform header**

Replace the top of `source/platform/StarUserGeneratedContentService.hpp`, from `#pragma once` through `class UserGeneratedContentService {`, with:

```cpp
#pragma once

#include "StarString.hpp"
#include "StarMaybe.hpp"

namespace Star {

STAR_CLASS(UserGeneratedContentService);

enum class WorkshopSort {
  Popular,
  Recent,
  MostSubscribed
};

struct WorkshopItem {
  String id;
  String title;
  // Steam ID of the owner, resolve a display name with personaName()
  String authorId;
  String description;
  String previewUrl;
  uint64_t subscriberCount = 0;
  StringList dependencyIds;
  // False if the item is hidden, removed, or banned
  bool available = true;
};

struct WorkshopPage {
  List<WorkshopItem> items;
  uint32_t page = 1;
  uint32_t totalResults = 0;
};

enum class WorkshopItemState {
  NotSubscribed,
  Downloading,
  Installed,
  NeedsUpdate,
  DownloadFailed
};

struct WorkshopItemStatus {
  WorkshopItemState state = WorkshopItemState::NotSubscribed;
  // 0..1, only meaningful while Downloading
  float downloadProgress = 0.0f;
};

enum class WorkshopRequestStatus {
  Pending,
  Succeeded,
  Failed
};

typedef uint64_t WorkshopRequestId;

class UserGeneratedContentService {
```

Leave the rest of the class unchanged in this task.

- [ ] **Step 2: Write the failing tests**

Create `source/test/workshop_logic_test.cpp`:

```cpp
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
```

- [ ] **Step 3: Add the test executable**

In `source/test/CMakeLists.txt`, insert this block directly before the line `SET_TESTS_PROPERTIES(core_tests PROPERTIES`:

```cmake
SET (star_workshop_tests_SOURCES
      gtest/gtest-all.cc

      core_tests_main.cpp

      workshop_logic_test.cpp
      ${PROJECT_SOURCE_DIR}/game/StarWorkshopLogic.cpp
    )
ADD_EXECUTABLE (workshop_tests
  $<TARGET_OBJECTS:star_extern> $<TARGET_OBJECTS:star_core>
  ${star_workshop_tests_SOURCES})
TARGET_LINK_LIBRARIES (workshop_tests ${STAR_EXT_LIBS})
ADD_TEST (NAME workshop_tests WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/workshop_tests)

SET_TESTS_PROPERTIES(workshop_tests PROPERTIES
                     LABELS "NoAssets")

```

- [ ] **Step 4: Create an empty-bodied header so the test compiles but fails to link**

Create `source/game/StarWorkshopLogic.hpp`:

```cpp
#pragma once

#include "StarString.hpp"
#include "StarUserGeneratedContentService.hpp"

namespace Star {

// True only for https URLs on Steam's own Workshop image hosts.
bool isAllowedWorkshopPreviewUrl(String const& url);

// Walks Workshop dependencies breadth-first without doing any I/O.  The caller
// fetches the details for each nextBatch() and passes them to supplyBatch().
class WorkshopDependencyResolver {
public:
  WorkshopDependencyResolver(String rootId, StringSet alreadySubscribed);

  String const& rootId() const;

  // Ids whose details are needed next.  Empty when finished, failed, or while
  // the previous batch has not been supplied yet.
  StringList nextBatch();
  // Details for the batch last returned by nextBatch().  Requested ids that are
  // missing from items, or not available, are reported as unavailable.
  void supplyBatch(List<WorkshopItem> const& items);
  void fail();

  bool finished() const;
  bool failed() const;

  // Dependencies to subscribe to, in discovery order; never contains the root.
  StringList const& toSubscribe() const;
  StringList const& unavailable() const;
  StringList const& alreadySubscribed() const;
  // The item's title if its details were seen, otherwise the id.
  String titleFor(String const& id) const;

private:
  String m_rootId;
  StringSet m_subscribed;
  StringSet m_visited;
  StringList m_frontier;
  bool m_awaiting;
  bool m_failed;

  StringList m_toSubscribe;
  StringList m_unavailable;
  StringList m_alreadySubscribed;
  StringMap<String> m_titles;
};

// Tracks subscription changes made in the Workshop menu and whether any of
// those items are still downloading.
class WorkshopApplyState {
public:
  void markChanged(String const& id);
  // Ignored for ids that were not marked changed.
  void setDownloading(String const& id, bool downloading);

  bool hasChanges() const;
  size_t pendingDownloadCount() const;
  bool canApply() const;
  StringList changedIds() const;

  void reset();

private:
  StringSet m_changed;
  StringSet m_downloading;
};

}
```

Create `source/game/StarWorkshopLogic.cpp` with only:

```cpp
#include "StarWorkshopLogic.hpp"

namespace Star {

}
```

- [ ] **Step 5: Run the tests to verify they fail**

```bash
cd source && cmake --preset linux-release && cmake --build --preset linux-release --target workshop_tests
```

Expected: FAIL at link time with `undefined reference to 'Star::WorkshopDependencyResolver::WorkshopDependencyResolver(...)'` (and the other logic symbols).

- [ ] **Step 6: Implement the logic**

Replace `source/game/StarWorkshopLogic.cpp` with:

```cpp
#include "StarWorkshopLogic.hpp"

#include <cctype>

namespace Star {

static char const* const AllowedWorkshopPreviewHosts[] = {
  "steamuserimages-a.akamaihd.net",
  "images.steamusercontent.com"
};

bool isAllowedWorkshopPreviewUrl(String const& url) {
  std::string const& text = url.utf8();
  std::string const scheme = "https://";
  if (text.size() <= scheme.size() || text.compare(0, scheme.size(), scheme) != 0)
    return false;

  size_t hostEnd = text.find_first_of("/?#", scheme.size());
  std::string host = hostEnd == std::string::npos
    ? text.substr(scheme.size())
    : text.substr(scheme.size(), hostEnd - scheme.size());
  if (host.empty() || host.find('@') != std::string::npos || host.find(':') != std::string::npos)
    return false;

  for (auto& c : host)
    c = (char)std::tolower((unsigned char)c);

  for (auto allowedHost : AllowedWorkshopPreviewHosts) {
    if (host == allowedHost)
      return true;
  }
  return false;
}

WorkshopDependencyResolver::WorkshopDependencyResolver(String rootId, StringSet alreadySubscribed)
  : m_rootId(std::move(rootId)), m_subscribed(std::move(alreadySubscribed)), m_awaiting(false), m_failed(false) {
  m_visited.add(m_rootId);
  m_frontier.append(m_rootId);
}

String const& WorkshopDependencyResolver::rootId() const {
  return m_rootId;
}

StringList WorkshopDependencyResolver::nextBatch() {
  if (m_awaiting || m_failed || m_frontier.empty())
    return {};
  m_awaiting = true;
  return m_frontier;
}

void WorkshopDependencyResolver::supplyBatch(List<WorkshopItem> const& items) {
  if (!m_awaiting)
    return;

  StringMap<WorkshopItem const*> itemsById;
  for (auto const& item : items)
    itemsById[item.id] = &item;

  StringList nextFrontier;
  for (auto const& id : m_frontier) {
    bool isRoot = id == m_rootId;
    WorkshopItem const* item = itemsById.value(id, nullptr);
    if (!item || !item->available) {
      if (!isRoot)
        m_unavailable.append(id);
      continue;
    }

    m_titles[id] = item->title;
    if (!isRoot) {
      if (m_subscribed.contains(id))
        m_alreadySubscribed.append(id);
      else
        m_toSubscribe.append(id);
    }

    for (auto const& dependencyId : item->dependencyIds) {
      if (m_visited.add(dependencyId))
        nextFrontier.append(dependencyId);
    }
  }

  m_frontier = std::move(nextFrontier);
  m_awaiting = false;
}

void WorkshopDependencyResolver::fail() {
  m_failed = true;
  m_awaiting = false;
}

bool WorkshopDependencyResolver::finished() const {
  return !m_failed && !m_awaiting && m_frontier.empty();
}

bool WorkshopDependencyResolver::failed() const {
  return m_failed;
}

StringList const& WorkshopDependencyResolver::toSubscribe() const {
  return m_toSubscribe;
}

StringList const& WorkshopDependencyResolver::unavailable() const {
  return m_unavailable;
}

StringList const& WorkshopDependencyResolver::alreadySubscribed() const {
  return m_alreadySubscribed;
}

String WorkshopDependencyResolver::titleFor(String const& id) const {
  return m_titles.value(id, id);
}

void WorkshopApplyState::markChanged(String const& id) {
  m_changed.add(id);
}

void WorkshopApplyState::setDownloading(String const& id, bool downloading) {
  if (!m_changed.contains(id))
    return;
  if (downloading)
    m_downloading.add(id);
  else
    m_downloading.remove(id);
}

bool WorkshopApplyState::hasChanges() const {
  return !m_changed.empty();
}

size_t WorkshopApplyState::pendingDownloadCount() const {
  return m_downloading.size();
}

bool WorkshopApplyState::canApply() const {
  return hasChanges() && pendingDownloadCount() == 0;
}

StringList WorkshopApplyState::changedIds() const {
  StringList result;
  for (auto const& id : m_changed)
    result.append(id);
  return result;
}

void WorkshopApplyState::reset() {
  m_changed.clear();
  m_downloading.clear();
}

}
```

Note: `HashSet::add` returns `true` only when the value was newly inserted, which is what makes each dependency visited once.

- [ ] **Step 7: Add the logic to the game library**

In `source/game/CMakeLists.txt`, add `    StarWorkshopLogic.hpp` as the first entry after the line `SET (star_game_HEADERS`, and `    StarWorkshopLogic.cpp` as the first entry after the line `SET (star_game_SOURCES`.

- [ ] **Step 8: Run the tests to verify they pass**

```bash
cd source && cmake --preset linux-release && cmake --build --preset linux-release --target workshop_tests && ctest --preset linux-release -R workshop_tests
```

Expected: `100% tests passed, 0 tests failed out of 1`. For detail run `../dist/workshop_tests` and expect `[  PASSED  ] 15 tests.`

- [ ] **Step 9: Build the whole project to confirm the header change compiles everywhere**

```bash
cd source && cmake --build --preset linux-release
```

Expected: exit code 0.

- [ ] **Step 10: Commit**

```bash
git add source/platform/StarUserGeneratedContentService.hpp source/game/StarWorkshopLogic.hpp source/game/StarWorkshopLogic.cpp source/game/CMakeLists.txt source/test/workshop_logic_test.cpp source/test/CMakeLists.txt
git commit -m "Add Workshop types, dependency resolver and apply state

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: Image decoding with stb_image

**Files:**
- Modify: `source/vcpkg.json`
- Modify: `source/CMakeLists.txt`
- Create: `source/core/StarImageDecode.hpp`
- Create: `source/core/StarImageDecode.cpp`
- Modify: `source/core/CMakeLists.txt`
- Create: `source/test/image_decode_test.cpp`
- Modify: `source/test/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `Maybe<Image> decodeImage(ByteArray const& bytes);` — RGBA32, rows bottom-up (y = 0 is the bottom row, like `Image::readPng`).
  - `Image fitImage(Image const& image, unsigned maxSide);`

- [ ] **Step 1: Add the stb dependency**

In `source/vcpkg.json`, change the line `    "abseil",` to:

```json
    "abseil",
    "stb",
```

In `source/CMakeLists.txt`, after the line `find_package(imgui CONFIG REQUIRED)` add:

```cmake
find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)
```

and in the `include_directories(SYSTEM` block right below it, change

```cmake
include_directories(SYSTEM
    ${FREETYPE_INCLUDE_DIRS}
    ${OGGVORBIS_INCLUDE_DIR}
)
```

to

```cmake
include_directories(SYSTEM
    ${FREETYPE_INCLUDE_DIRS}
    ${OGGVORBIS_INCLUDE_DIR}
    ${STB_INCLUDE_DIRS}
)
```

- [ ] **Step 2: Write the failing test**

Create `source/test/image_decode_test.cpp`:

```cpp
#include "StarImageDecode.hpp"

#include "gtest/gtest.h"

using namespace Star;

// 2x2 24-bit BMP. Top row: red, green. Bottom row: blue, white.
static ByteArray makeTestBmp() {
  unsigned char const bytes[] = {
    // BITMAPFILEHEADER: "BM", file size 70, reserved, pixel offset 54
    'B', 'M', 70, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
    // BITMAPINFOHEADER: size 40, width 2, height 2, planes 1, 24 bpp, no compression, image size 16
    40, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0,
    0, 0, 0, 0, 16, 0, 0, 0, 0x13, 0x0B, 0, 0, 0x13, 0x0B, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    // Pixel rows are bottom-up, BGR, padded to 4 bytes
    0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0, 0,  // bottom: blue, white
    0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0, 0   // top: red, green
  };
  return ByteArray((char const*)bytes, sizeof(bytes));
}

TEST(ImageDecodeTest, DecodesBmpBottomUp) {
  auto image = decodeImage(makeTestBmp());
  ASSERT_TRUE(image.isValid());
  EXPECT_EQ(image->width(), 2u);
  EXPECT_EQ(image->height(), 2u);
  EXPECT_EQ(image->pixelFormat(), PixelFormat::RGBA32);
  EXPECT_EQ(image->get(0, 0), Vec4B(0, 0, 255, 255));
  EXPECT_EQ(image->get(1, 0), Vec4B(255, 255, 255, 255));
  EXPECT_EQ(image->get(0, 1), Vec4B(255, 0, 0, 255));
  EXPECT_EQ(image->get(1, 1), Vec4B(0, 255, 0, 255));
}

TEST(ImageDecodeTest, RejectsGarbage) {
  EXPECT_FALSE(decodeImage(ByteArray("not an image", 12)).isValid());
  EXPECT_FALSE(decodeImage(ByteArray()).isValid());
}

TEST(ImageDecodeTest, FitImageShrinksLargeImages) {
  Image large = Image::filled(Vec2U(1000, 500), Vec4B(10, 20, 30, 255));
  Image fitted = fitImage(large, 512);
  EXPECT_EQ(fitted.width(), 512u);
  EXPECT_EQ(fitted.height(), 256u);
  EXPECT_EQ(fitted.get(100, 100), Vec4B(10, 20, 30, 255));
}

TEST(ImageDecodeTest, FitImageKeepsSmallImages) {
  Image small = Image::filled(Vec2U(100, 50), Vec4B(1, 2, 3, 4));
  Image fitted = fitImage(small, 512);
  EXPECT_EQ(fitted.width(), 100u);
  EXPECT_EQ(fitted.height(), 50u);
}
```

In `source/test/CMakeLists.txt`, in `star_core_tests_SOURCES`, add the line `      image_decode_test.cpp` directly after `      host_address_test.cpp`.

- [ ] **Step 3: Add the header and an empty source, then verify the test fails**

Create `source/core/StarImageDecode.hpp`:

```cpp
#pragma once

#include "StarImage.hpp"
#include "StarByteArray.hpp"
#include "StarMaybe.hpp"

namespace Star {

// Decodes PNG, JPEG, GIF (first frame) or BMP bytes into an RGBA32 image with
// rows stored bottom-up, like Image::readPng.  Returns nothing on failure.
Maybe<Image> decodeImage(ByteArray const& bytes);

// Nearest-neighbour downscale of an RGBA32 image so that neither side exceeds
// maxSide, preserving aspect ratio.  Smaller images are returned unchanged.
Image fitImage(Image const& image, unsigned maxSide);

}
```

Create `source/core/StarImageDecode.cpp`:

```cpp
#include "StarImageDecode.hpp"

namespace Star {

}
```

In `source/core/CMakeLists.txt`, add `    StarImageDecode.hpp` on the line after `    StarImage.hpp`, and `    StarImageDecode.cpp` on the line after `    StarImage.cpp`.

```bash
cd source && cmake --preset linux-release && cmake --build --preset linux-release --target core_tests
```

Expected: FAIL at link time with `undefined reference to 'Star::decodeImage(Star::ByteArray const&)'`. If configure fails with `STB_INCLUDE_DIRS-NOTFOUND`, vcpkg did not install `stb`; rerun `cmake --preset linux-release` and check the vcpkg output for `stb`.

- [ ] **Step 4: Implement decoding**

Replace `source/core/StarImageDecode.cpp` with:

```cpp
#include "StarImageDecode.hpp"

#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#include "stb_image.h"

namespace Star {

Maybe<Image> decodeImage(ByteArray const& bytes) {
  if (bytes.empty())
    return {};

  int width = 0;
  int height = 0;
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory((stbi_uc const*)bytes.ptr(), (int)bytes.size(), &width, &height, &channels, 4);
  if (!pixels)
    return {};

  Image image((unsigned)width, (unsigned)height, PixelFormat::RGBA32);
  size_t stride = (size_t)width * 4;
  // stb_image returns rows top-down, Star::Image stores them bottom-up.
  for (int row = 0; row < height; ++row)
    std::memcpy(image.data() + (size_t)(height - row - 1) * stride, pixels + (size_t)row * stride, stride);

  stbi_image_free(pixels);
  return image;
}

Image fitImage(Image const& image, unsigned maxSide) {
  unsigned width = image.width();
  unsigned height = image.height();
  if (width <= maxSide && height <= maxSide)
    return image;

  double scale = (double)maxSide / (double)std::max(width, height);
  unsigned newWidth = std::max(1u, (unsigned)(width * scale));
  unsigned newHeight = std::max(1u, (unsigned)(height * scale));

  Image result(newWidth, newHeight, PixelFormat::RGBA32);
  for (unsigned y = 0; y < newHeight; ++y) {
    unsigned sourceY = std::min(height - 1, (unsigned)(y / scale));
    for (unsigned x = 0; x < newWidth; ++x) {
      unsigned sourceX = std::min(width - 1, (unsigned)(x / scale));
      result.set(x, y, image.get(sourceX, sourceY));
    }
  }
  return result;
}

}
```

- [ ] **Step 5: Run the tests to verify they pass**

```bash
cd source && cmake --build --preset linux-release --target core_tests && ctest --preset linux-release
```

Expected: `100% tests passed` for both `core_tests` and `workshop_tests`. `../dist/core_tests --gtest_filter=ImageDecodeTest.*` shows `[  PASSED  ] 4 tests.`

- [ ] **Step 6: Commit**

```bash
git add source/vcpkg.json source/CMakeLists.txt source/core/StarImageDecode.hpp source/core/StarImageDecode.cpp source/core/CMakeLists.txt source/test/image_decode_test.cpp source/test/CMakeLists.txt
git commit -m "Add stb_image based image decoding

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: Workshop service interface and Steam implementation

There is no Steam in the test environment, so this task is verified by compiling. Behaviour is checked manually in Task 9.

**Files:**
- Modify: `source/platform/StarUserGeneratedContentService.hpp`
- Modify: `source/application/StarUserGeneratedContentService_pc_steam.hpp`
- Modify: `source/application/StarUserGeneratedContentService_pc_steam.cpp`

**Interfaces:**
- Consumes: Workshop types from Task 2.
- Produces (virtual methods on `UserGeneratedContentService`):
  - `WorkshopRequestId queryItems(String const& searchText, WorkshopSort sort, uint32_t page)`
  - `WorkshopRequestId queryItemDetails(StringList const& ids)`
  - `WorkshopRequestStatus requestStatus(WorkshopRequestId request) const`
  - `Maybe<WorkshopPage> takeQueryResult(WorkshopRequestId request)`
  - `WorkshopRequestId subscribe(String const& id)`, `WorkshopRequestId unsubscribe(String const& id)`, `void releaseRequest(WorkshopRequestId request)`
  - `WorkshopItemStatus itemStatus(String const& id) const`, `bool retryDownload(String const& id)`, `Maybe<String> personaName(String const& steamId)`

- [ ] **Step 1: Extend the interface**

In `source/platform/StarUserGeneratedContentService.hpp`, change `  ~UserGeneratedContentService() = default;` to `  virtual ~UserGeneratedContentService() = default;`, and replace

```cpp
  virtual UserGeneratedContentService::UGCState triggerContentDownload() = 0;
};
```

with

```cpp
  virtual UserGeneratedContentService::UGCState triggerContentDownload() = 0;

  // Starts a Workshop search.  A non-empty searchText ranks by relevance and
  // ignores sort.  Pages start at 1 and hold up to 50 items.
  virtual WorkshopRequestId queryItems(String const& searchText, WorkshopSort sort, uint32_t page) = 0;
  // Starts a details request for specific items.  Requested ids missing from
  // the result are returned with available == false.
  virtual WorkshopRequestId queryItemDetails(StringList const& ids) = 0;
  // Status of any request.  Requests still pending after 30 seconds, and
  // unknown ids, report Failed.
  virtual WorkshopRequestStatus requestStatus(WorkshopRequestId request) const = 0;
  // Removes a query request, cancelling it if still pending.  Returns the page
  // only if the query succeeded.
  virtual Maybe<WorkshopPage> takeQueryResult(WorkshopRequestId request) = 0;

  virtual WorkshopRequestId subscribe(String const& id) = 0;
  virtual WorkshopRequestId unsubscribe(String const& id) = 0;
  // Removes a subscribe or unsubscribe request, cancelling it if still pending.
  virtual void releaseRequest(WorkshopRequestId request) = 0;

  virtual WorkshopItemStatus itemStatus(String const& id) const = 0;
  // Clears a failed download and asks Steam to download the item again.
  virtual bool retryDownload(String const& id) = 0;
  // Display name for a Steam ID, or nothing while it is still being fetched.
  virtual Maybe<String> personaName(String const& steamId) = 0;
};
```

- [ ] **Step 2: Build to confirm the Steam class is now abstract**

```bash
cd source && cmake --build --preset linux-release --target starbound
```

Expected: FAIL with an error like `invalid new-expression of abstract class type 'Star::SteamUserGeneratedContentService'` in `StarPlatformServices_pc.cpp`.

- [ ] **Step 3: Replace the Steam header**

Replace `source/application/StarUserGeneratedContentService_pc_steam.hpp` with:

```cpp
#pragma once

#include "StarPlatformServices_pc.hpp"

namespace Star {

STAR_CLASS(SteamUserGeneratedContentService);

class SteamUserGeneratedContentService final : public UserGeneratedContentService {
public:
  SteamUserGeneratedContentService(PcPlatformServicesStatePtr state);

  StringList subscribedContentIds() const override;
  Maybe<String> contentDownloadDirectory(String const& contentId) const override;
  UserGeneratedContentService::UGCState triggerContentDownload() override;

  WorkshopRequestId queryItems(String const& searchText, WorkshopSort sort, uint32_t page) override;
  WorkshopRequestId queryItemDetails(StringList const& ids) override;
  WorkshopRequestStatus requestStatus(WorkshopRequestId request) const override;
  Maybe<WorkshopPage> takeQueryResult(WorkshopRequestId request) override;

  WorkshopRequestId subscribe(String const& id) override;
  WorkshopRequestId unsubscribe(String const& id) override;
  void releaseRequest(WorkshopRequestId request) override;

  WorkshopItemStatus itemStatus(String const& id) const override;
  bool retryDownload(String const& id) override;
  Maybe<String> personaName(String const& steamId) override;

private:
  struct PendingQuery {
    PendingQuery();
    ~PendingQuery();

    void onCompleted(SteamUGCQueryCompleted_t* completed, bool ioFailure);

    UGCQueryHandle_t handle;
    CCallResult<PendingQuery, SteamUGCQueryCompleted_t> callResult;
    WorkshopRequestStatus status;
    double startTime;
    uint32_t page;
    StringList requestedIds;
    Maybe<WorkshopPage> result;
  };

  template <typename ResultType>
  struct PendingAction {
    void onCompleted(ResultType* completed, bool ioFailure) {
      status = (!ioFailure && completed->m_eResult == k_EResultOK) ? WorkshopRequestStatus::Succeeded : WorkshopRequestStatus::Failed;
    }

    CCallResult<PendingAction<ResultType>, ResultType> callResult;
    WorkshopRequestStatus status = WorkshopRequestStatus::Pending;
    double startTime = 0.0;
  };

  typedef PendingAction<RemoteStorageSubscribePublishedFileResult_t> PendingSubscribe;
  typedef PendingAction<RemoteStorageUnsubscribePublishedFileResult_t> PendingUnsubscribe;

  static WorkshopRequestStatus timedStatus(WorkshopRequestStatus status, double startTime);
  WorkshopRequestId sendQuery(UGCQueryHandle_t handle, uint32_t page, StringList requestedIds);

  STEAM_CALLBACK(SteamUserGeneratedContentService, onDownloadResult, DownloadItemResult_t, m_callbackDownloadResult);

  HashMap<PublishedFileId_t, bool> m_currentDownloadState;
  HashSet<PublishedFileId_t> m_failedDownloads;

  HashMap<WorkshopRequestId, unique_ptr<PendingQuery>> m_queries;
  HashMap<WorkshopRequestId, unique_ptr<PendingSubscribe>> m_subscribes;
  HashMap<WorkshopRequestId, unique_ptr<PendingUnsubscribe>> m_unsubscribes;
  WorkshopRequestId m_nextRequestId;

  HashMap<uint64, String> m_personaNames;

  bool m_checkedUGC;
};

}
```

- [ ] **Step 4: Update the Steam source**

In `source/application/StarUserGeneratedContentService_pc_steam.cpp`:

1. Add `#include "StarTime.hpp"` after `#include "StarLexicalCast.hpp"`.
2. Replace the constructor with:

```cpp
static double const WorkshopRequestTimeout = 30.0;

SteamUserGeneratedContentService::SteamUserGeneratedContentService(PcPlatformServicesStatePtr)
  : m_callbackDownloadResult(this, &SteamUserGeneratedContentService::onDownloadResult),
    m_nextRequestId(1),
    m_checkedUGC(false) {};
```

3. Replace `onDownloadResult` at the end of the file (before the closing `}` of the namespace) with:

```cpp
void SteamUserGeneratedContentService::onDownloadResult(DownloadItemResult_t* result) {
  m_currentDownloadState[result->m_nPublishedFileId] = true;
  if (result->m_eResult == k_EResultOK)
    m_failedDownloads.remove(result->m_nPublishedFileId);
  else
    m_failedDownloads.add(result->m_nPublishedFileId);
}

SteamUserGeneratedContentService::PendingQuery::PendingQuery()
  : handle(k_UGCQueryHandleInvalid), status(WorkshopRequestStatus::Pending), startTime(0.0), page(1) {}

SteamUserGeneratedContentService::PendingQuery::~PendingQuery() {
  if (handle != k_UGCQueryHandleInvalid)
    SteamUGC()->ReleaseQueryUGCRequest(handle);
}

void SteamUserGeneratedContentService::PendingQuery::onCompleted(SteamUGCQueryCompleted_t* completed, bool ioFailure) {
  if (ioFailure || completed->m_eResult != k_EResultOK) {
    status = WorkshopRequestStatus::Failed;
    return;
  }

  WorkshopPage workshopPage;
  workshopPage.page = page;
  workshopPage.totalResults = completed->m_unTotalMatchingResults;

  for (uint32 i = 0; i < completed->m_unNumResultsReturned; ++i) {
    SteamUGCDetails_t details;
    if (!SteamUGC()->GetQueryUGCResult(handle, i, &details))
      continue;

    WorkshopItem item;
    item.id = toString(details.m_nPublishedFileId);
    item.available = details.m_eResult == k_EResultOK && !details.m_bBanned;
    item.title = details.m_rgchTitle;
    item.authorId = toString(details.m_ulSteamIDOwner);
    item.description = details.m_rgchDescription;

    char previewUrl[1024] = {};
    if (SteamUGC()->GetQueryUGCPreviewURL(handle, i, previewUrl, sizeof(previewUrl)))
      item.previewUrl = previewUrl;

    uint64 subscribers = 0;
    if (SteamUGC()->GetQueryUGCStatistic(handle, i, k_EItemStatistic_NumSubscriptions, &subscribers))
      item.subscriberCount = subscribers;

    if (details.m_unNumChildren > 0) {
      List<PublishedFileId_t> children(details.m_unNumChildren, 0);
      if (SteamUGC()->GetQueryUGCChildren(handle, i, children.ptr(), children.size())) {
        for (auto child : children)
          item.dependencyIds.append(toString(child));
      }
    }

    workshopPage.items.append(std::move(item));
  }

  for (auto const& requestedId : requestedIds) {
    bool returned = false;
    for (auto const& item : workshopPage.items) {
      if (item.id == requestedId) {
        returned = true;
        break;
      }
    }
    if (!returned) {
      WorkshopItem missing;
      missing.id = requestedId;
      missing.available = false;
      workshopPage.items.append(std::move(missing));
    }
  }

  result = std::move(workshopPage);
  status = WorkshopRequestStatus::Succeeded;
}

WorkshopRequestStatus SteamUserGeneratedContentService::timedStatus(WorkshopRequestStatus status, double startTime) {
  if (status == WorkshopRequestStatus::Pending && Time::monotonicTime() - startTime > WorkshopRequestTimeout)
    return WorkshopRequestStatus::Failed;
  return status;
}

WorkshopRequestId SteamUserGeneratedContentService::sendQuery(UGCQueryHandle_t handle, uint32_t page, StringList requestedIds) {
  auto query = make_unique<PendingQuery>();
  query->handle = handle;
  query->page = page;
  query->requestedIds = std::move(requestedIds);
  query->startTime = Time::monotonicTime();

  if (handle == k_UGCQueryHandleInvalid) {
    query->status = WorkshopRequestStatus::Failed;
  } else {
    SteamUGC()->SetReturnLongDescription(handle, true);
    SteamUGC()->SetReturnChildren(handle, true);
    SteamAPICall_t call = SteamUGC()->SendQueryUGCRequest(handle);
    if (call == k_uAPICallInvalid)
      query->status = WorkshopRequestStatus::Failed;
    else
      query->callResult.Set(call, query.get(), &PendingQuery::onCompleted);
  }

  WorkshopRequestId request = m_nextRequestId++;
  m_queries[request] = std::move(query);
  return request;
}

WorkshopRequestId SteamUserGeneratedContentService::queryItems(String const& searchText, WorkshopSort sort, uint32_t page) {
  EUGCQuery queryType = k_EUGCQuery_RankedByTrend;
  if (!searchText.empty())
    queryType = k_EUGCQuery_RankedByTextSearch;
  else if (sort == WorkshopSort::Recent)
    queryType = k_EUGCQuery_RankedByPublicationDate;
  else if (sort == WorkshopSort::MostSubscribed)
    queryType = k_EUGCQuery_RankedByTotalUniqueSubscriptions;

  AppId_t appId = SteamUtils()->GetAppID();
  UGCQueryHandle_t handle = SteamUGC()->CreateQueryAllUGCRequest(queryType, k_EUGCMatchingUGCType_Items, appId, appId, (uint32)page);
  if (handle != k_UGCQueryHandleInvalid && !searchText.empty())
    SteamUGC()->SetSearchText(handle, searchText.utf8Ptr());

  return sendQuery(handle, page, {});
}

WorkshopRequestId SteamUserGeneratedContentService::queryItemDetails(StringList const& ids) {
  List<PublishedFileId_t> fileIds;
  for (auto const& id : ids) {
    if (auto fileId = maybeLexicalCast<PublishedFileId_t>(id))
      fileIds.append(*fileId);
  }

  UGCQueryHandle_t handle = k_UGCQueryHandleInvalid;
  if (!fileIds.empty())
    handle = SteamUGC()->CreateQueryUGCDetailsRequest(fileIds.ptr(), fileIds.size());

  return sendQuery(handle, 1, ids);
}

WorkshopRequestStatus SteamUserGeneratedContentService::requestStatus(WorkshopRequestId request) const {
  if (auto query = m_queries.ptr(request))
    return timedStatus((*query)->status, (*query)->startTime);
  if (auto action = m_subscribes.ptr(request))
    return timedStatus((*action)->status, (*action)->startTime);
  if (auto action = m_unsubscribes.ptr(request))
    return timedStatus((*action)->status, (*action)->startTime);
  return WorkshopRequestStatus::Failed;
}

Maybe<WorkshopPage> SteamUserGeneratedContentService::takeQueryResult(WorkshopRequestId request) {
  auto query = m_queries.maybeTake(request);
  if (!query)
    return {};
  if (timedStatus((*query)->status, (*query)->startTime) != WorkshopRequestStatus::Succeeded)
    return {};
  return std::move((*query)->result);
}

WorkshopRequestId SteamUserGeneratedContentService::subscribe(String const& id) {
  auto action = make_unique<PendingSubscribe>();
  action->startTime = Time::monotonicTime();

  auto fileId = maybeLexicalCast<PublishedFileId_t>(id);
  SteamAPICall_t call = fileId ? SteamUGC()->SubscribeItem(*fileId) : k_uAPICallInvalid;
  if (call == k_uAPICallInvalid) {
    action->status = WorkshopRequestStatus::Failed;
  } else {
    m_failedDownloads.remove(*fileId);
    action->callResult.Set(call, action.get(), &PendingSubscribe::onCompleted);
  }

  WorkshopRequestId request = m_nextRequestId++;
  m_subscribes[request] = std::move(action);
  return request;
}

WorkshopRequestId SteamUserGeneratedContentService::unsubscribe(String const& id) {
  auto action = make_unique<PendingUnsubscribe>();
  action->startTime = Time::monotonicTime();

  auto fileId = maybeLexicalCast<PublishedFileId_t>(id);
  SteamAPICall_t call = fileId ? SteamUGC()->UnsubscribeItem(*fileId) : k_uAPICallInvalid;
  if (call == k_uAPICallInvalid) {
    action->status = WorkshopRequestStatus::Failed;
  } else {
    m_failedDownloads.remove(*fileId);
    action->callResult.Set(call, action.get(), &PendingUnsubscribe::onCompleted);
  }

  WorkshopRequestId request = m_nextRequestId++;
  m_unsubscribes[request] = std::move(action);
  return request;
}

void SteamUserGeneratedContentService::releaseRequest(WorkshopRequestId request) {
  m_subscribes.remove(request);
  m_unsubscribes.remove(request);
}

WorkshopItemStatus SteamUserGeneratedContentService::itemStatus(String const& id) const {
  WorkshopItemStatus status;
  auto fileId = maybeLexicalCast<PublishedFileId_t>(id);
  if (!fileId)
    return status;

  uint32 itemState = SteamUGC()->GetItemState(*fileId);
  if (!(itemState & k_EItemStateSubscribed))
    return status;

  if (m_failedDownloads.contains(*fileId)) {
    status.state = WorkshopItemState::DownloadFailed;
  } else if (itemState & (k_EItemStateDownloading | k_EItemStateDownloadPending)) {
    status.state = WorkshopItemState::Downloading;
    uint64 downloaded = 0;
    uint64 total = 0;
    if (SteamUGC()->GetItemDownloadInfo(*fileId, &downloaded, &total) && total > 0)
      status.downloadProgress = (float)downloaded / (float)total;
  } else if (itemState & k_EItemStateNeedsUpdate) {
    status.state = WorkshopItemState::NeedsUpdate;
  } else if (itemState & k_EItemStateInstalled) {
    status.state = WorkshopItemState::Installed;
  } else {
    // Subscribed, but Steam has not started downloading yet.
    status.state = WorkshopItemState::Downloading;
  }
  return status;
}

bool SteamUserGeneratedContentService::retryDownload(String const& id) {
  auto fileId = maybeLexicalCast<PublishedFileId_t>(id);
  if (!fileId)
    return false;
  m_failedDownloads.remove(*fileId);
  return SteamUGC()->DownloadItem(*fileId, true);
}

Maybe<String> SteamUserGeneratedContentService::personaName(String const& steamId) {
  auto id = maybeLexicalCast<uint64>(steamId);
  if (!id)
    return {};
  if (auto name = m_personaNames.ptr(*id))
    return *name;

  CSteamID user(*id);
  // Returns true while Steam is still fetching the user's information.
  if (SteamFriends()->RequestUserInformation(user, true))
    return {};

  String name = SteamFriends()->GetFriendPersonaName(user);
  m_personaNames[*id] = name;
  return name;
}
```

- [ ] **Step 5: Build**

```bash
cd source && cmake --build --preset linux-release
```

Expected: exit code 0, with no new warnings from `StarUserGeneratedContentService_pc_steam.cpp` (check with `cmake --build --preset linux-release 2>&1 | grep -i "pc_steam.*warning"`, expecting no output).

- [ ] **Step 6: Run the tests to confirm nothing regressed**

```bash
cd source && ctest --preset linux-release
```

Expected: `100% tests passed`.

- [ ] **Step 7: Commit**

```bash
git add source/platform/StarUserGeneratedContentService.hpp source/application/StarUserGeneratedContentService_pc_steam.hpp source/application/StarUserGeneratedContentService_pc_steam.cpp
git commit -m "Add Steam Workshop query, subscribe and status APIs

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Preview cache and preview widget

**Files:**
- Create: `source/frontend/StarWorkshopPreview.hpp`
- Create: `source/frontend/StarWorkshopPreview.cpp`
- Modify: `source/frontend/CMakeLists.txt`

**Interfaces:**
- Consumes: `isAllowedWorkshopPreviewUrl` (Task 2), `decodeImage` / `fitImage` (Task 3), `HttpClient::getAsync(String const&)` returning `WorkerPoolPromise<HttpResponse>` (existing, `source/core/StarHttpClient.hpp`).
- Produces:
  - `class WorkshopPreviewCache { ImageConstPtr get(String const& itemId, String const& url); void update(); };`
  - `class WorkshopPreviewWidget : public Widget { void setImage(ImageConstPtr image); };`

- [ ] **Step 1: Create the header**

Create `source/frontend/StarWorkshopPreview.hpp`:

```cpp
#pragma once

#include "StarWidget.hpp"
#include "StarRenderer.hpp"
#include "StarHttpClient.hpp"
#include "StarImage.hpp"

namespace Star {

STAR_CLASS(WorkshopPreviewCache);
STAR_CLASS(WorkshopPreviewWidget);

// Fetches, decodes and caches Workshop preview images in memory.
class WorkshopPreviewCache {
public:
  static size_t const MaxEntries = 64;
  static unsigned const MaxImageSide = 512;

  // Returns the preview if it is ready.  The first call for an item starts
  // fetching it, provided the URL is on an allowed host.
  ImageConstPtr get(String const& itemId, String const& url);

  // Decodes any finished fetches.  Call once per frame.
  void update();

private:
  struct Entry {
    Maybe<WorkerPoolPromise<HttpResponse>> fetch;
    ImageConstPtr image;
    uint64_t lastUsed = 0;
  };

  void evictOldest();

  HashMap<String, Entry> m_entries;
  uint64_t m_useCounter = 0;
};

// Draws an in-memory image centered and aspect-fitted in the widget bounds,
// or a placeholder icon when there is no image.
class WorkshopPreviewWidget : public Widget {
public:
  WorkshopPreviewWidget();

  void setImage(ImageConstPtr image);

protected:
  void renderImpl() override;

private:
  ImageConstPtr m_image;
  ImageConstPtr m_textureImage;
  TexturePtr m_texture;
};

}
```

- [ ] **Step 2: Create the source**

Create `source/frontend/StarWorkshopPreview.cpp`:

```cpp
#include "StarWorkshopPreview.hpp"
#include "StarWorkshopLogic.hpp"
#include "StarImageDecode.hpp"
#include "StarGuiContext.hpp"
#include "StarLogging.hpp"

namespace Star {

static char const* const WorkshopPreviewPlaceholder = "/interface/modsmenu/modicon.png";

ImageConstPtr WorkshopPreviewCache::get(String const& itemId, String const& url) {
  if (!m_entries.contains(itemId)) {
    if (m_entries.size() >= MaxEntries)
      evictOldest();

    Entry entry;
    if (isAllowedWorkshopPreviewUrl(url))
      entry.fetch = HttpClient::getAsync(url);
    else
      Logger::debug("Not fetching Workshop preview for {} from disallowed url '{}'", itemId, url);
    m_entries[itemId] = std::move(entry);
  }

  auto& entry = m_entries[itemId];
  entry.lastUsed = ++m_useCounter;
  return entry.image;
}

void WorkshopPreviewCache::update() {
  for (auto& pair : m_entries) {
    auto& entry = pair.second;
    if (!entry.fetch || !entry.fetch->done())
      continue;

    try {
      auto const& response = entry.fetch->get();
      if (!response.error.empty() || response.statusCode != 200) {
        Logger::debug("Workshop preview fetch for {} failed: {} {}", pair.first, response.statusCode, response.error);
      } else if (auto image = decodeImage(ByteArray(response.body.utf8Ptr(), response.body.utf8Size()))) {
        entry.image = make_shared<Image>(fitImage(*image, MaxImageSide));
      } else {
        Logger::debug("Could not decode Workshop preview for {}", pair.first);
      }
    } catch (std::exception const& e) {
      Logger::debug("Workshop preview fetch for {} threw: {}", pair.first, outputException(e, false));
    }

    entry.fetch = {};
  }
}

void WorkshopPreviewCache::evictOldest() {
  Maybe<String> oldestId;
  uint64_t oldestUse = 0;
  for (auto const& pair : m_entries) {
    if (!oldestId || pair.second.lastUsed < oldestUse) {
      oldestId = pair.first;
      oldestUse = pair.second.lastUsed;
    }
  }
  if (oldestId)
    m_entries.remove(*oldestId);
}

WorkshopPreviewWidget::WorkshopPreviewWidget() {}

void WorkshopPreviewWidget::setImage(ImageConstPtr image) {
  m_image = std::move(image);
}

void WorkshopPreviewWidget::renderImpl() {
  auto guiContext = context();
  Vec2F position = Vec2F(screenPosition());
  Vec2F box = Vec2F(size());

  if (!m_image) {
    Vec2F placeholderSize = Vec2F(guiContext->textureSize(WorkshopPreviewPlaceholder));
    guiContext->drawInterfaceQuad(WorkshopPreviewPlaceholder, position + (box - placeholderSize) / 2);
    return;
  }

  if (m_textureImage != m_image) {
    m_texture = guiContext->renderer()->createTexture(*m_image, TextureAddressing::Clamp, TextureFiltering::Linear);
    m_textureImage = m_image;
  }

  Vec2F imageSize = Vec2F(m_image->size());
  float scale = std::min(box[0] / imageSize[0], box[1] / imageSize[1]);
  Vec2F drawSize = imageSize * scale;
  RectF screenRect = RectF::withSize(position + (box - drawSize) / 2, drawSize).scaled(guiContext->interfaceScale());

  guiContext->renderer()->immediatePrimitives().emplace_back(std::in_place_type_t<RenderQuad>(), m_texture, screenRect, Vec4B::filled(255), 0.0f);
}

}
```

- [ ] **Step 3: Add to the frontend library**

In `source/frontend/CMakeLists.txt`, replace

```cmake
    StarWireInterface.hpp
  )
```

with

```cmake
    StarWireInterface.hpp
    StarWorkshopPreview.hpp
  )
```

and replace

```cmake
    StarWireInterface.cpp
  )
```

with

```cmake
    StarWireInterface.cpp
    StarWorkshopPreview.cpp
  )
```

- [ ] **Step 4: Build**

```bash
cd source && cmake --preset linux-release && cmake --build --preset linux-release --target starbound
```

Expected: exit code 0.

- [ ] **Step 5: Commit**

```bash
git add source/frontend/StarWorkshopPreview.hpp source/frontend/StarWorkshopPreview.cpp source/frontend/CMakeLists.txt
git commit -m "Add Workshop preview image cache and widget

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: Dependency dialog

**Files:**
- Create: `assets/opensb/interface/workshopmenu/dependencies.config`
- Create: `source/frontend/StarWorkshopDependencyDialog.hpp`
- Create: `source/frontend/StarWorkshopDependencyDialog.cpp`
- Modify: `source/frontend/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `enum class WorkshopDependencyChoice { SubscribeAll, JustThis, Cancel };`
  - `class WorkshopDependencyDialog : public Pane { typedef std::function<void(WorkshopDependencyChoice)> Callback; void displayRequirements(StringList const& requiredTitles, StringList const& unavailableTitles, Callback callback); void displayResolveFailure(Callback callback); };` Call a `display*` method, then show it with `PaneManager::displayPane(PaneLayer::ModalWindow, dialog)`.

- [ ] **Step 1: Create the layout**

Create `assets/opensb/interface/workshopmenu/dependencies.config`:

```json
{
  "paneLayout" : {
    "panefeature" : {
      "type" : "panefeature"
    },
    "background" : {
      "type" : "background",
      "fileHeader" : "/interface/confirmation/header.png",
      "fileBody" : "/interface/confirmation/body.png",
      "fileFooter" : "/interface/confirmation/footer.png"
    },
    "windowtitle" : {
      "type" : "title",
      "title" : "Required Mods",
      "subtitle" : "^#b9b5b2;This mod depends on other Workshop items",
      "size" : 12
    },
    "message" : {
      "type" : "label",
      "position" : [125, 118],
      "hAnchor" : "mid",
      "vAnchor" : "top",
      "wrapWidth" : 230,
      "fontSize" : 8,
      "value" : "",
      "mouseTransparent" : true
    },
    "subscribeAll" : {
      "type" : "button",
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "pressed" : "/interface/buttonactive.png",
      "caption" : "All",
      "fontSize" : 7,
      "position" : [12, 8]
    },
    "justThis" : {
      "type" : "button",
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "pressed" : "/interface/buttonactive.png",
      "caption" : "Just this",
      "fontSize" : 7,
      "position" : [98, 8]
    },
    "cancel" : {
      "type" : "button",
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "pressed" : "/interface/buttonactive.png",
      "caption" : "Cancel",
      "fontSize" : 7,
      "position" : [184, 8]
    }
  }
}
```

- [ ] **Step 2: Create the header**

Create `source/frontend/StarWorkshopDependencyDialog.hpp`:

```cpp
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
```

- [ ] **Step 3: Create the source**

Create `source/frontend/StarWorkshopDependencyDialog.cpp`:

```cpp
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
```

When "All" is hidden because there are no required dependencies (only unavailable ones), "Just this" still subscribes the root mod.

- [ ] **Step 4: Add to the frontend library**

In `source/frontend/CMakeLists.txt`, add `    StarWorkshopDependencyDialog.hpp` on the line before `    StarWorkshopPreview.hpp`, and `    StarWorkshopDependencyDialog.cpp` on the line before `    StarWorkshopPreview.cpp`.

- [ ] **Step 5: Validate the JSON and build**

```bash
python3 -m json.tool assets/opensb/interface/workshopmenu/dependencies.config > /dev/null && echo JSON_OK
cd source && cmake --preset linux-release && cmake --build --preset linux-release --target starbound
```

Expected: `JSON_OK`, then build exit code 0.

- [ ] **Step 6: Commit**

```bash
git add assets/opensb/interface/workshopmenu/dependencies.config source/frontend/StarWorkshopDependencyDialog.hpp source/frontend/StarWorkshopDependencyDialog.cpp source/frontend/CMakeLists.txt
git commit -m "Add Workshop dependency confirmation dialog

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: Workshop browser pane

**Files:**
- Create: `assets/opensb/interface/workshopmenu/workshopmenu.config`
- Create: `source/frontend/StarWorkshopMenu.hpp`
- Create: `source/frontend/StarWorkshopMenu.cpp`
- Modify: `source/frontend/CMakeLists.txt`

**Interfaces:**
- Consumes: service methods (Task 4), `WorkshopDependencyResolver`, `WorkshopApplyState` (Task 2), `WorkshopPreviewCache`, `WorkshopPreviewWidget` (Task 5), `WorkshopDependencyDialog` (Task 6).
- Produces: `class WorkshopMenu : public Pane { WorkshopMenu(PaneManager* manager, UserGeneratedContentServicePtr service, std::function<void()> requestApply); void update(float dt) override; };` The menu calls `requestApply()` when Apply is clicked and allowed.

- [ ] **Step 1: Create the layout**

Create `assets/opensb/interface/workshopmenu/workshopmenu.config`:

```json
{
  "paneLayout" : {
    "background" : {
      "type" : "background",
      "fileHeader" : "/interface/modsmenu/header.png",
      "fileBody" : "/interface/modsmenu/body.png",
      "fileFooter" : "/interface/modsmenu/footer.png"
    },

    "windowtitle" : {
      "type" : "title",
      "title" : "Workshop",
      "subtitle" : "^#b9b5b2;Browse Steam Workshop mods",
      "position" : [0, 254],
      "icon" : {
        "type" : "image",
        "file" : "/interface/modsmenu/iconmodmanager.png",
        "position" : [0, 0],
        "zlevel" : -1
      }
    },

    "listBackground" : {
      "type" : "image",
      "file" : "/interface/modsmenu/scrollbg.png",
      "position" : [2, 37],
      "zlevel" : -5
    },

    "informationBackground" : {
      "type" : "image",
      "file" : "/interface/modsmenu/informationbg.png",
      "position" : [175, 37],
      "zlevel" : -5
    },

    "searchBox" : {
      "type" : "textbox",
      "position" : [8, 207],
      "hint" : "Search Workshop",
      "maxWidth" : 100,
      "fontSize" : 8,
      "border" : true,
      "callback" : "null",
      "enterKey" : "search"
    },

    "searchButton" : {
      "type" : "button",
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "pressed" : "/interface/buttonactive.png",
      "caption" : "Search",
      "fontSize" : 7,
      "callback" : "search",
      "position" : [114, 204]
    },

    "itemsArea" : {
      "type" : "scrollArea",
      "rect" : [3, 45, 172, 200],
      "children" : {
        "list" : {
          "type" : "list",
          "callback" : "items",
          "position" : [1, -1],
          "schema" : {
            "selectedBG" : "/interface/modsmenu/modselected.png",
            "unselectedBG" : "/interface/modsmenu/modbackground.png",
            "spacing" : [0, 1],
            "memberSize" : [162, 18],
            "listTemplate" : {
              "background" : {
                "type" : "image",
                "file" : "/interface/modsmenu/modbackground.png",
                "position" : [0, 0],
                "zlevel" : -1
              },
              "name" : {
                "type" : "label",
                "position" : [4, 9],
                "hAnchor" : "left",
                "vAnchor" : "mid",
                "fontSize" : 8,
                "value" : ""
              },
              "state" : {
                "type" : "label",
                "position" : [156, 9],
                "hAnchor" : "right",
                "vAnchor" : "mid",
                "fontSize" : 7,
                "color" : "#b9b5b2",
                "value" : ""
              }
            }
          }
        }
      }
    },

    "status" : {
      "type" : "label",
      "position" : [87, 130],
      "hAnchor" : "mid",
      "vAnchor" : "mid",
      "wrapWidth" : 150,
      "fontSize" : 8,
      "value" : ""
    },

    "statusRetry" : {
      "type" : "button",
      "visible" : false,
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "pressed" : "/interface/buttonactive.png",
      "caption" : "Retry",
      "fontSize" : 7,
      "position" : [60, 105]
    },

    "sortPopular" : {
      "type" : "button",
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "baseImageChecked" : "/interface/buttonactive.png",
      "hoverImageChecked" : "/interface/buttonactivehover.png",
      "checkable" : true,
      "checked" : true,
      "caption" : "Popular",
      "fontSize" : 7,
      "position" : [4, 20]
    },

    "sortRecent" : {
      "type" : "button",
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "baseImageChecked" : "/interface/buttonactive.png",
      "hoverImageChecked" : "/interface/buttonactivehover.png",
      "checkable" : true,
      "checked" : false,
      "caption" : "Recent",
      "fontSize" : 7,
      "position" : [60, 20]
    },

    "sortSubscribed" : {
      "type" : "button",
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "baseImageChecked" : "/interface/buttonactive.png",
      "hoverImageChecked" : "/interface/buttonactivehover.png",
      "checkable" : true,
      "checked" : false,
      "caption" : "Subscribed",
      "fontSize" : 7,
      "position" : [116, 20]
    },

    "prevPage" : {
      "type" : "button",
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "pressed" : "/interface/buttonactive.png",
      "caption" : "Prev",
      "fontSize" : 7,
      "position" : [4, 4]
    },

    "pageLabel" : {
      "type" : "label",
      "position" : [87, 11],
      "hAnchor" : "mid",
      "vAnchor" : "mid",
      "fontSize" : 8,
      "value" : ""
    },

    "nextPage" : {
      "type" : "button",
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "pressed" : "/interface/buttonactive.png",
      "caption" : "Next",
      "fontSize" : 7,
      "position" : [116, 4]
    },

    "title" : {
      "type" : "label",
      "position" : [262, 216],
      "hAnchor" : "mid",
      "vAnchor" : "top",
      "wrapWidth" : 160,
      "fontSize" : 9,
      "value" : ""
    },

    "authorLabel" : {
      "type" : "label",
      "value" : "AUTHOR",
      "position" : [266, 200],
      "hAnchor" : "left",
      "vAnchor" : "top",
      "color" : "#b9b5b2",
      "fontSize" : 7
    },

    "author" : {
      "type" : "label",
      "value" : "",
      "position" : [266, 191],
      "hAnchor" : "left",
      "vAnchor" : "top",
      "wrapWidth" : 80,
      "fontSize" : 8
    },

    "subscribersLabel" : {
      "type" : "label",
      "value" : "SUBSCRIBERS",
      "position" : [266, 176],
      "hAnchor" : "left",
      "vAnchor" : "top",
      "color" : "#b9b5b2",
      "fontSize" : 7
    },

    "subscribers" : {
      "type" : "label",
      "value" : "",
      "position" : [266, 167],
      "hAnchor" : "left",
      "vAnchor" : "top",
      "fontSize" : 8
    },

    "stateLabel" : {
      "type" : "label",
      "value" : "STATUS",
      "position" : [266, 152],
      "hAnchor" : "left",
      "vAnchor" : "top",
      "color" : "#b9b5b2",
      "fontSize" : 7
    },

    "itemState" : {
      "type" : "label",
      "value" : "",
      "position" : [266, 143],
      "hAnchor" : "left",
      "vAnchor" : "top",
      "fontSize" : 8
    },

    "descriptionArea" : {
      "type" : "scrollArea",
      "rect" : [178, 46, 348, 122],
      "children" : {
        "description" : {
          "type" : "label",
          "value" : "",
          "hAnchor" : "left",
          "vAnchor" : "top",
          "fontSize" : 8,
          "wrapWidth" : 160
        }
      }
    },

    "subscribe" : {
      "type" : "button",
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "pressed" : "/interface/buttonactive.png",
      "caption" : "Subscribe",
      "fontSize" : 7,
      "position" : [180, 20]
    },

    "retryDownload" : {
      "type" : "button",
      "visible" : false,
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "pressed" : "/interface/buttonactive.png",
      "caption" : "Retry",
      "fontSize" : 7,
      "position" : [236, 20]
    },

    "openSteam" : {
      "type" : "button",
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "pressed" : "/interface/buttonactive.png",
      "caption" : "Steam",
      "fontSize" : 7,
      "position" : [292, 20]
    },

    "applyLabel" : {
      "type" : "label",
      "value" : "",
      "position" : [288, 11],
      "hAnchor" : "right",
      "vAnchor" : "mid",
      "color" : "#b9b5b2",
      "fontSize" : 7
    },

    "apply" : {
      "type" : "button",
      "visible" : false,
      "base" : "/interface/button.png",
      "hover" : "/interface/buttonhover.png",
      "pressed" : "/interface/buttonactive.png",
      "caption" : "Apply",
      "fontSize" : 7,
      "position" : [292, 4]
    }
  }
}
```

- [ ] **Step 2: Create the header**

Create `source/frontend/StarWorkshopMenu.hpp`:

```cpp
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
  ButtonWidgetPtr m_sortSubscribed;
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
```

- [ ] **Step 3: Create the source**

Create `source/frontend/StarWorkshopMenu.cpp`:

```cpp
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

namespace Star {

static uint32_t const WorkshopPageSize = 50;
static size_t const WorkshopRowTitleLimit = 26;

WorkshopMenu::WorkshopMenu(PaneManager* manager, UserGeneratedContentServicePtr service, std::function<void()> requestApply)
  : m_manager(manager), m_service(std::move(service)), m_requestApply(std::move(requestApply)),
    m_sort(WorkshopSort::Popular), m_page(1), m_pageCount(1), m_hasQueried(false) {
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
  reader.registerCallback("sortSubscribed", [this](Widget*) { setSort(WorkshopSort::MostSubscribed); });
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
  m_sortSubscribed = fetchChild<ButtonWidget>("sortSubscribed");
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
  m_preview->setPosition(Vec2I(178, 126));
  m_preview->setSize(Vec2I(82, 82));
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
  requestPage(1);
}

void WorkshopMenu::requestPage(uint32_t page) {
  if (m_query)
    m_service->takeQueryResult(*m_query);

  m_hasQueried = true;
  m_page = page;
  m_query = m_service->queryItems(m_searchText, m_sort, page);

  m_items.clear();
  m_list->clear();
  m_prevPage->setEnabled(false);
  m_nextPage->setEnabled(false);
  setStatus("Loading...");
}

void WorkshopMenu::setSort(WorkshopSort sort) {
  m_sort = sort;
  m_sortPopular->setChecked(sort == WorkshopSort::Popular);
  m_sortRecent->setChecked(sort == WorkshopSort::Recent);
  m_sortSubscribed->setChecked(sort == WorkshopSort::MostSubscribed);
  requestPage(1);
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
  m_pageCount = std::max<uint32_t>(1, (result->totalResults + WorkshopPageSize - 1) / WorkshopPageSize);
  setStatus(m_items.empty() ? "No mods found" : "");
  populateList();
}

void WorkshopMenu::populateList() {
  m_list->clear();
  for (auto const& item : m_items) {
    auto row = m_list->addItem();
    String title = item.title;
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
    m_title->setText(item->title);
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
```

- [ ] **Step 4: Add to the frontend library**

In `source/frontend/CMakeLists.txt`, add `    StarWorkshopMenu.hpp` on the line before `    StarWorkshopPreview.hpp`, and `    StarWorkshopMenu.cpp` on the line before `    StarWorkshopPreview.cpp`.

- [ ] **Step 5: Validate the JSON and build**

```bash
python3 -m json.tool assets/opensb/interface/workshopmenu/workshopmenu.config > /dev/null && echo JSON_OK
cd source && cmake --preset linux-release && cmake --build --preset linux-release --target starbound
```

Expected: `JSON_OK`, then build exit code 0.

- [ ] **Step 6: Commit**

```bash
git add assets/opensb/interface/workshopmenu/workshopmenu.config source/frontend/StarWorkshopMenu.hpp source/frontend/StarWorkshopMenu.cpp source/frontend/CMakeLists.txt
git commit -m "Add Workshop browser pane

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 8: Title screen button, Workshop state and Apply

**Files:**
- Modify: `assets/opensb/interface/windowconfig/title.config.patch.lua`
- Modify: `source/frontend/StarTitleScreen.hpp`
- Modify: `source/frontend/StarTitleScreen.cpp`
- Modify: `source/client/StarClientApplication.hpp`
- Modify: `source/client/StarClientApplication.cpp`

**Interfaces:**
- Consumes: `WorkshopMenu(PaneManager*, UserGeneratedContentServicePtr, std::function<void()>)` (Task 7).
- Produces: `TitleState::Workshop`; `bool TitleScreen::takeModReloadRequest();` used by `ClientApplication::updateTitle`.

- [ ] **Step 1: Add the main menu button**

In `assets/opensb/interface/windowconfig/title.config.patch.lua`, insert after the first `for … end` loop (after line 6, before `data.skyBackdropDarken = …`):

```lua
  local modsButton
  for _, v in pairs(data.mainMenuButtons) do
    if v.key == "mods" then
      modsButton = v
    end
  end
  if modsButton then
    table.insert(data.mainMenuButtons, {
      key = "workshop",
      button = "/interface/modsmenu/workshopbutton.png",
      hover = "/interface/modsmenu/workshopbuttonhover.png",
      offset = jarray{modsButton.offset[1] - 90, modsButton.offset[2]},
      rightAnchored = true
    })
  end
```

- [ ] **Step 2: Update the TitleScreen header**

In `source/frontend/StarTitleScreen.hpp`:

1. After `STAR_CLASS(ModsMenu);` add `STAR_CLASS(WorkshopMenu);`.
2. In `enum class TitleState`, after `  Mods,` add `  Workshop,`.
3. After `  void stopMusic();` add:

```cpp
  // True once, after the Workshop menu asked for mods to be reloaded.
  bool takeModReloadRequest();
```

4. After `  void initModsMenu();` add `  void initWorkshopMenu();`.
5. After `  PanePtr m_backgroundMenu;` add:

```cpp
  WorkshopMenuPtr m_workshopMenu;
  bool m_modReloadRequested = false;
```

- [ ] **Step 3: Update the TitleScreen source**

In `source/frontend/StarTitleScreen.cpp`:

1. After `#include "StarModsMenu.hpp"` add `#include "StarWorkshopMenu.hpp"`.
2. In the constructor, after `  initModsMenu();` add `  initWorkshopMenu();`.
3. In `initMainMenu()`, after `  buttonCallbacks["mods"] = [=](Widget*) { switchState(TitleState::Mods); };` add:

```cpp
  buttonCallbacks["workshop"] = [=](Widget*) { switchState(TitleState::Workshop); };
  bool hasWorkshop = (bool)m_guiContext->applicationController()->userGeneratedContentService();
```

and at the start of the `for (auto buttonConfig : config.getArray("mainMenuButtons")) {` loop body, right after `    String key = buttonConfig.getString("key");`, add:

```cpp
    if (key == "workshop" && !hasWorkshop)
      continue;
```

4. After the whole `TitleScreen::initModsMenu()` function add:

```cpp
void TitleScreen::initWorkshopMenu() {
  auto service = m_guiContext->applicationController()->userGeneratedContentService();
  if (!service)
    return;

  m_workshopMenu = make_shared<WorkshopMenu>(&m_paneManager, service, [this]() { m_modReloadRequested = true; });
  m_workshopMenu->setAnchor(PaneAnchor::Center);
  m_workshopMenu->lockPosition();

  m_paneManager.registerPane("workshopMenu", PaneLayer::Hud, m_workshopMenu, [this](PanePtr const&) {
      back();
    });
}

bool TitleScreen::takeModReloadRequest() {
  bool requested = m_modReloadRequested;
  m_modReloadRequested = false;
  return requested;
}
```

5. In `switchState`, replace

```cpp
    } if (titleState == TitleState::Mods) {
      m_paneManager.displayRegisteredPane("modsMenu");
    } else if (titleState == TitleState::SinglePlayerSelectCharacter) {
```

with

```cpp
    } if (titleState == TitleState::Mods) {
      m_paneManager.displayRegisteredPane("modsMenu");
    } else if (titleState == TitleState::Workshop) {
      if (m_workshopMenu)
        m_paneManager.displayRegisteredPane("workshopMenu");
    } else if (titleState == TitleState::SinglePlayerSelectCharacter) {
```

6. In `back()`, replace

```cpp
  else if (m_titleState == TitleState::Mods)
    switchState(TitleState::Main);
```

with

```cpp
  else if (m_titleState == TitleState::Mods)
    switchState(TitleState::Main);
  else if (m_titleState == TitleState::Workshop)
    switchState(TitleState::Main);
```

- [ ] **Step 4: Turn the request into a mods reload in ClientApplication**

In `source/client/StarClientApplication.hpp`, after `  bool m_loggedUGCCheck;` add:

```cpp
  // Set when the Workshop menu applies changes, so updateMods reloads even if
  // no Workshop directories remain.
  bool m_forceModReload = false;
```

In `source/client/StarClientApplication.cpp`:

1. In `updateTitle`, replace

```cpp
  m_titleScreen->update(dt);
  m_mainMixer->update(dt);
```

with

```cpp
  m_titleScreen->update(dt);
  if (m_titleScreen->takeModReloadRequest()) {
    Logger::info("Reloading mods after Workshop changes");
    m_titleScreen->stopMusic();
    m_forceModReload = true;
    m_loggedUGCCheck = false;
    changeState(MainAppState::Mods);
    return;
  }
  m_mainMixer->update(dt);
```

2. In the `getStateString` switch inside `updateTitle`, after

```cpp
        case TitleState::Mods:
          return "In Mods";
```

add

```cpp
        case TitleState::Workshop:
          return "In Workshop";
```

3. In `updateMods`, replace

```cpp
        if (modDirectories.empty()) {
          changeState(MainAppState::Splash);
        } else {
```

with

```cpp
        bool forceReload = m_forceModReload;
        m_forceModReload = false;
        if (modDirectories.empty() && !forceReload) {
          changeState(MainAppState::Splash);
        } else {
```

- [ ] **Step 5: Build and run all tests**

```bash
cd source && cmake --build --preset linux-release && ctest --preset linux-release
```

Expected: build exit code 0; `100% tests passed`.

- [ ] **Step 6: Commit**

```bash
git add assets/opensb/interface/windowconfig/title.config.patch.lua source/frontend/StarTitleScreen.hpp source/frontend/StarTitleScreen.cpp source/client/StarClientApplication.hpp source/client/StarClientApplication.cpp
git commit -m "Add Workshop button to title screen and apply via mod reload

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 9: Install locally and run the manual checklist

The user already has an OpenStarbound client in `~/Desktop/starboundclient` (with `packed.pak`, `opensb.pak` and Steam files). This task installs the new build there, keeping backups.

**Files:** none in the repo

- [ ] **Step 1: Back up the existing client binary and OpenStarbound assets**

```bash
cp ~/Desktop/starboundclient/linux/starbound ~/Desktop/starboundclient/linux/starbound.pre-workshop
cp ~/Desktop/starboundclient/assets/opensb.pak ~/Desktop/starboundclient/assets/opensb.pak.pre-workshop
```

- [ ] **Step 2: Install the new binary and repack the OpenStarbound assets**

```bash
cp dist/starbound ~/Desktop/starboundclient/linux/starbound
dist/asset_packer assets/opensb ~/Desktop/starboundclient/assets/opensb.pak
```

Expected: both commands exit 0.

- [ ] **Step 3: Launch with Steam running**

```bash
cd ~/Desktop/starboundclient/linux && ./run-client.sh
```

Watch `~/Desktop/starboundclient/storage/starbound.log` for errors mentioning `workshop`.

- [ ] **Step 4: Work through the checklist and record results**

1. Workshop button visible next to the Mods button with Steam running; absent when launched with Steam closed.
2. Search returns matching results; each sort changes results; Prev/Next paging works.
3. Details show a preview image (including a JPG preview), author name, subscriber count.
4. Subscribe to a mod with no dependencies → badge shows % then Subscribed → Apply → loading screen → mod listed in the Mods menu.
5. Subscribe to a mod with dependencies → dialog lists them → All → all download.
6. "Just this" subscribes only the mod; "Cancel" subscribes nothing.
7. Unsubscribe → Apply → mod gone from the Mods menu; its dependencies remain.
8. Disconnect the network → search shows "Couldn't reach Workshop" + Retry; reconnect → Retry loads results.
9. Apply is disabled and shows "Waiting for N downloads" while a download is in progress.
10. Enter a world after Apply → the game runs with the new mods.

For any failure, capture the relevant `starbound.log` lines and fix under superpowers:systematic-debugging before continuing.

- [ ] **Step 5: Restore the backups if needed**

```bash
cp ~/Desktop/starboundclient/linux/starbound.pre-workshop ~/Desktop/starboundclient/linux/starbound
cp ~/Desktop/starboundclient/assets/opensb.pak.pre-workshop ~/Desktop/starboundclient/assets/opensb.pak
```

Only run this if the user wants their original client back.
