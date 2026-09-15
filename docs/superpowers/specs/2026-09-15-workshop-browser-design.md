# In-Game Steam Workshop Browser — Design

Date: 2026-09-15
Status: Approved design, pending spec review

## Goal

Let players browse, search, subscribe to, and unsubscribe from Steam Workshop
mods directly from the OpenStarbound title screen, with required dependencies
subscribed automatically, and apply the changes without restarting the game.

## Scope

In scope:

- Steam Workshop only, via the Steam UGC API (no third-party mirrors, no other
  mod sites, no SteamCMD).
- Search by text, sort (Popular / Recent / Most Subscribed), paged results.
- Details panel: title, author, description, preview image, subscriber count,
  Subscribe / Unsubscribe, download progress, "Open in Steam" link.
- Dependency auto-subscribe with a confirmation popup.
- "Apply changes" button that live-reloads assets from the title screen.

Out of scope (can be added later without redesign):

- Tag filters, collections, ratings, comments.
- Any Lua access to Workshop functionality. This is deliberate: exposing
  subscribe/unsubscribe to Lua would let any installed mod act on the player's
  Steam account. All Workshop actions happen only from native C++ UI in
  response to player clicks.
- Non-Steam builds. They do not get a Workshop service; the button is disabled.

## Existing code this builds on

- `source/platform/StarUserGeneratedContentService.hpp` — platform-neutral UGC
  interface (currently: `subscribedContentIds`, `contentDownloadDirectory`,
  `triggerContentDownload`).
- `source/application/StarUserGeneratedContentService_pc_steam.{hpp,cpp}` —
  Steam implementation.
- `source/application/StarPlatformServices_pc.cpp:221` — `SteamAPI_RunCallbacks()`
  is already pumped, so `CCallResult`/`STEAM_CALLBACK` async results work.
- `source/client/StarClientApplication.cpp` `loadMods()` / `updateMods()` —
  collects installed UGC directories and calls `Root::loadMods(dirs)`, which
  triggers `Root::reload()`.
- `source/frontend/StarTitleScreen.{hpp,cpp}` — `TitleState` enum,
  `mainMenuButtons` config, `initModsMenu()` pane registration pattern.
- `source/frontend/StarModsMenu.{hpp,cpp}` — pane built with `GuiReader` from a
  JSON layout; the pattern the new menu follows.
- `cpr` (vcpkg) — HTTP client already linked; used for preview image fetches.
- `core/StarImage` — PNG decoding only.

## Architecture

### 1. Workshop service API (platform + Steam implementation)

Extend `UserGeneratedContentService` with platform-neutral types and methods.
All network operations are asynchronous: a call starts a request and returns a
request handle; the caller polls for the result each frame. No method blocks.

Types (in `StarUserGeneratedContentService.hpp`):

```cpp
enum class WorkshopSort { Popular, Recent, MostSubscribed };

struct WorkshopItem {
  String id;
  String title;
  String author;          // display name if available, else Steam ID string
  String description;
  String previewUrl;
  uint64_t subscriberCount;
  StringList dependencyIds;
  bool available;         // false if hidden/removed
};

struct WorkshopPage {
  List<WorkshopItem> items;
  uint32_t page;
  uint32_t totalResults;
};

enum class WorkshopItemState { NotSubscribed, Downloading, Installed, NeedsUpdate, DownloadFailed };

struct WorkshopItemStatus {
  WorkshopItemState state;
  float downloadProgress;  // 0..1, meaningful only while Downloading
};

enum class WorkshopRequestStatus { Pending, Succeeded, Failed };
```

New virtual methods:

```cpp
using WorkshopRequestId = uint64_t;

virtual WorkshopRequestId queryItems(String const& searchText, WorkshopSort sort, uint32_t page) = 0;
virtual WorkshopRequestId queryItemDetails(StringList const& ids) = 0;
virtual WorkshopRequestStatus requestStatus(WorkshopRequestId request) const = 0;
virtual Maybe<WorkshopPage> takeQueryResult(WorkshopRequestId request) = 0;

virtual WorkshopRequestId subscribe(String const& id) = 0;
virtual WorkshopRequestId unsubscribe(String const& id) = 0;

virtual WorkshopItemStatus itemStatus(String const& id) const = 0;
```

`queryItemDetails` is what dependency resolution uses to fetch the items a mod
requires (and their own requirements).

Steam implementation mapping:

| Method | Steam API |
|---|---|
| `queryItems` | `CreateQueryAllUGCRequest(EUGCQuery, k_EUGCMatchingUGCType_Items, appId, appId, page)`; sort → `k_EUGCQuery_RankedByTrend` / `RankedByPublicationDate` / `RankedByTotalUniqueSubscriptions`; non-empty search text → `k_EUGCQuery_RankedByTextSearch` + `SetSearchText`; `SetReturnLongDescription(true)`; `SetReturnChildren(true)`; `SendQueryUGCRequest` → `CCallResult<SteamUGCQueryCompleted_t>` |
| result reading | `GetQueryUGCResult`, `GetQueryUGCPreviewURL`, `GetQueryUGCStatistic(k_EItemStatistic_NumSubscriptions)`, `GetQueryUGCChildren`, then `ReleaseQueryUGCRequest` |
| `queryItemDetails` | `CreateQueryUGCDetailsRequest(ids, n)` + `SetReturnChildren(true)`; requested IDs absent from the results are returned with `available = false` |
| `subscribe` / `unsubscribe` | `SubscribeItem` / `UnsubscribeItem` → `CCallResult<RemoteStorageSubscribePublishedFileResult_t>` / `RemoteStorageUnsubscribePublishedFileResult_t` |
| `itemStatus` | `GetItemState` flags + `GetItemDownloadInfo` for progress; `DownloadItemResult_t` with a non-OK result marks `DownloadFailed` |

Author names: `GetQueryUGCResult` gives the owner's Steam ID. The display name is
resolved with `SteamFriends()->GetFriendPersonaName` after
`RequestUserInformation(id, true)`; until it resolves, the ID string is shown.

### 2. Workshop logic without Steam (`source/game/StarWorkshopLogic.{hpp,cpp}`)

Pure logic with no Steam or UI dependency, so it can be unit tested against a
fake service:

- `WorkshopDependencyResolver` — given a root item ID and a function to fetch
  item details, walks dependencies breadth-first. Each ID is visited once, so
  cycles terminate. Output: `toSubscribe` (not yet subscribed, available),
  `unavailable` (hidden or removed), `alreadySubscribed`. The walk is
  incremental: `step()` issues detail requests for the current frontier and
  returns `Pending` until every level is resolved.
- `WorkshopApplyState` — tracks whether subscriptions changed since the last
  apply, and whether any subscribed item is still `Downloading`. Exposes
  `canApply()`, `pendingDownloadCount()`, `hasChanges()`.

### 3. Workshop browser pane (`source/frontend/StarWorkshopMenu.{hpp,cpp}`)

A `Pane` subclass built with `GuiReader` from
`assets/opensb/interface/workshopmenu/workshopmenu.config`, which contains the
layout and the placeholder preview image.

Widgets:

- Search text box plus a Search button (Enter also searches).
- Sort selector: Popular / Recent / Most Subscribed.
- Result list (`ListWidget`), one row per item: title, author, state badge
  (Subscribed / Downloading n% / Failed).
- Prev / Next page buttons with a "Page n of m" label.
- Details panel: preview image, title, author, subscriber count, description,
  Subscribe/Unsubscribe button, Retry button (shown on failure), "Open in
  Steam" button (uses `desktopService()->openUrl` with
  `https://steamcommunity.com/sharedfiles/filedetails/?id=<id>`).
- Status line for loading and error messages, with a Retry button.
- "Apply changes" button, visible when `WorkshopApplyState::hasChanges()`.

`update(dt)` polls request status, item statuses for visible rows, preview
fetches, and the active dependency resolver.

### 4. Preview images

- Fetched asynchronously with `cpr::GetAsync` from the Steam CDN preview URL.
- Decoded with `stb_image` (single header vendored into `source/extern/`), which
  supports JPG, PNG and GIF (first frame). Decoded images are converted to
  `Star::Image`.
- Registered into a `MemoryAssetSource` under
  `/workshop/previews/<id>.png` so `ImageWidget::setImage` can use them.
- Fetched only for the selected item and visible rows. Cached in memory for the
  session, capped at 64 entries with least-recently-used eviction.
- Images wider or taller than 512px are downscaled before caching.
- Only URLs whose host ends in `steamuserimages-a.akamaihd.net` or
  `images.steamusercontent.com` are fetched. Anything else gets the
  placeholder. Items do not control arbitrary fetch targets.

### 5. Title screen wiring

- Add `TitleState::Workshop`.
- Add a `"workshop"` entry to the `buttonCallbacks` map in `TitleScreen`.
- Add the button in `assets/opensb/interface/windowconfig/title.config.patch.lua`:
  after the existing offset loop, find the vanilla `mods` entry in
  `data.mainMenuButtons` and append a copy with `key = "workshop"`, images
  `/interface/title/workshop.png` and `/interface/title/workshophover.png`, and
  an offset directly beside the Mods button (same row, shifted by the Mods
  button's image width). If no `mods` entry exists (for example, a mod replaced
  the menu), the patch skips adding the button.
- Add `initWorkshopMenu()` following `initModsMenu()`, registering pane
  `"workshopMenu"`, displayed from `switchState`.
- If `appController()->userGeneratedContentService()` is null, the button is
  disabled and its tooltip reads "Requires Steam".

### 6. Apply changes

1. Enabled only when `canApply()`: there are changes and no pending downloads.
   While downloads are running, the label reads "Waiting for N downloads…".
2. On click: close the Workshop pane and show the existing loading cinematic.
3. Collect installed directories via `subscribedContentIds()` +
   `contentDownloadDirectory()`. This logic is extracted from
   `ClientApplication::loadMods()` into a shared helper so startup and Apply
   use identical code.
4. Call `Root::singleton().loadMods(dirs)`, which triggers `Root::reload()`.
5. Re-fetch `m_root->configuration()` after reload, as `updateMods()` does.
6. Rebuild the `TitleScreen` so no pane holds widgets or images from the old
   assets. Return to `TitleState::Main`.

Apply is reachable only from the title screen and never while in a world.

The pane asks `ClientApplication` to reload through a callback given to
`TitleScreen` at construction, e.g. `std::function<void()> requestModReload`.
Frontend code never calls `Root::loadMods` directly.

## Dependency auto-subscribe flow

1. The player clicks Subscribe on item X.
2. Start a `WorkshopDependencyResolver` for X. The Subscribe button shows
   "Checking requirements…".
3. When it resolves:
   - `toSubscribe` empty and `unavailable` empty → subscribe to X directly.
   - Otherwise → show a confirmation popup:
     "This mod also needs: A, B, C." followed by, if any are unavailable,
     "Unavailable (hidden or removed): D — this mod may not work."
     Buttons: **Subscribe all** / **Just this mod** / **Cancel**.
4. **Subscribe all** subscribes X and everything in `toSubscribe`.
   **Just this mod** subscribes to X only. **Cancel** does nothing.
5. Unsubscribing never removes dependencies.
6. If resolution fails (a network error while fetching details), the popup
   reads "Couldn't check requirements" with **Subscribe anyway** / **Cancel**.

## Error handling

| Situation | Behavior |
|---|---|
| No UGC service (Steam not running, or non-Steam build) | Workshop button disabled, tooltip "Requires Steam". |
| Query fails or times out (30s) | List area shows "Couldn't reach Workshop" + Retry. |
| Preview fetch/decode fails or disallowed host | Placeholder image. Logged at debug level. No popup. |
| Subscribe/unsubscribe call fails | Status line shows "Subscribe failed" + Retry; item state unchanged. |
| Download fails | Row badge "Download failed" + Retry (calls `DownloadItem` again). Apply skips items that are not installed. |
| Apply pressed while downloading | Not possible; button disabled with "Waiting for N downloads…". |
| Reload throws on a broken mod | Same path as a startup mod failure: `ClientApplication::setError`, which shows the error screen and returns to the title. The Workshop menu remains usable to unsubscribe. |
| Leaving the menu mid-download | Steam continues in the background; apply state is kept on `TitleScreen` and survives reopening the pane. |
| Game closed before Apply | No special handling; subscriptions load at next launch as today. |

## Testing

Automated (gtest, added to the game test target in `source/test/CMakeLists.txt`,
file `workshop_logic_test.cpp`), using a fake details fetcher:

- Resolver: no dependencies; one level; multiple levels; diamond (A→B, A→C,
  B→D, C→D visits D once); cycle (A→B→A terminates); already-subscribed items
  excluded from `toSubscribe`; unavailable items reported; a fetch failure
  yields a failed status.
- Apply state: no changes → cannot apply; change + downloading → cannot apply,
  correct pending count; change + all installed → can apply; state resets
  after apply.
- Preview host allowlist: accepted and rejected URLs.

Manual checklist (Steam-enabled build, real account):

1. Workshop button enabled with Steam running; disabled with Steam closed.
2. Search returns matching results; each sort order changes results; paging works.
3. Details panel shows the preview image (including a JPG preview).
4. Subscribe to a mod with no dependencies → downloads → Apply → mod appears in
   the Mods menu.
5. Subscribe to a mod that has dependencies → popup lists them → Subscribe all
   → all download.
6. "Just this mod" and "Cancel" behave as described.
7. Unsubscribe → Apply → mod gone from the Mods menu; its dependencies remain.
8. Disconnect network → search shows the error + Retry; reconnect → Retry works.
9. Apply is disabled while a download is in progress.
10. Enter a world after Apply → the game runs with the new mods.

## Files touched

New:

- `source/game/StarWorkshopLogic.hpp`, `source/game/StarWorkshopLogic.cpp`
- `source/frontend/StarWorkshopMenu.hpp`, `source/frontend/StarWorkshopMenu.cpp`
- `source/extern/stb_image.h`
- `source/test/workshop_logic_test.cpp`
- `assets/opensb/interface/workshopmenu/workshopmenu.config` (+ placeholder image)
- `assets/opensb/interface/title/workshop.png`, `assets/opensb/interface/title/workshophover.png`

Modified:

- `assets/opensb/interface/windowconfig/title.config.patch.lua`

- `source/platform/StarUserGeneratedContentService.hpp`
- `source/application/StarUserGeneratedContentService_pc_steam.{hpp,cpp}`
- `source/frontend/StarTitleScreen.{hpp,cpp}`
- `source/client/StarClientApplication.{hpp,cpp}` (shared UGC directory helper, reload callback)
- `source/game/CMakeLists.txt`, `source/frontend/CMakeLists.txt`, `source/test/CMakeLists.txt`
