# In-Game Steam Workshop Browser — Design

Date: 2026-09-15
Status: Approved

## Goal

Let players browse, search, subscribe to, and unsubscribe from Steam Workshop
mods directly from the OpenStarbound title screen, with required dependencies
subscribed automatically, and apply the changes without restarting the game.

## Scope

In scope:

- Steam Workshop only, via the Steam UGC API (no third-party mirrors, no other
  mod sites, no SteamCMD).
- Search by text, browse by Popular or Recent, and a Mine tab listing the
  player's own subscriptions. Results are paged 50 at a time.
- Details panel: title, author, description, preview image, subscriber count,
  Subscribe / Unsubscribe, download progress, "Open in Steam" link.
- Dependency auto-subscribe with a confirmation popup.
- "Apply changes" button that reloads mods from the title screen.

Out of scope (can be added later without redesign):

- Tag filters, collections, ratings, comments.
- Any Lua access to Workshop functionality. This is deliberate: exposing
  subscribe/unsubscribe to Lua would let any installed mod act on the player's
  Steam account. All Workshop actions happen only from native C++ UI in
  response to player clicks.
- Non-Steam builds. They have no Workshop service, so the button is not shown.

## Existing code this builds on

- `source/platform/StarUserGeneratedContentService.hpp` — platform-neutral UGC
  interface (currently: `subscribedContentIds`, `contentDownloadDirectory`,
  `triggerContentDownload`).
- `source/application/StarUserGeneratedContentService_pc_steam.{hpp,cpp}` —
  Steam implementation, created only when Steam is available.
- `source/application/StarPlatformServices_pc.cpp:221` — `SteamAPI_RunCallbacks()`
  is already pumped, so `CCallResult` / `STEAM_CALLBACK` async results work.
- `source/client/StarClientApplication.cpp` — `MainAppState::Mods` runs
  `updateMods()`, which collects installed UGC directories and calls
  `Root::loadMods(dirs)` (which reloads Root), then goes Splash → Title.
  Changing from `Title` to any lower state destroys the `TitleScreen`, and
  reaching `Title` again recreates it.
- `source/frontend/StarTitleScreen.{hpp,cpp}` — `TitleState` enum,
  `mainMenuButtons` loop, `initModsMenu()` pane registration pattern.
- `source/frontend/StarModsMenu.{hpp,cpp}` and
  `source/frontend/StarHttpTrustDialog.{hpp,cpp}` — patterns for panes and
  modal dialogs built with `GuiReader` from JSON layouts.
- `source/core/StarHttpClient.hpp` — `HttpClient::getAsync` (cpr on a worker
  pool), used for preview image fetches.
- `core/StarImage` — PNG decoding only; rows are stored bottom-up.
- Vanilla assets already contain `/interface/modsmenu/*` art (including
  `workshopbutton.png` / `workshopbuttonhover.png`, 86×18) and
  `/interface/button*.png` (54×14), which the new UI reuses.

## Architecture

### 1. Workshop service API (platform + Steam implementation)

Extend `UserGeneratedContentService` (and give it a virtual destructor) with
platform-neutral types and methods. All network operations are asynchronous:
a call starts a request and returns a request id; the caller polls each frame.
No method blocks.

Types (in `StarUserGeneratedContentService.hpp`):

```cpp
enum class WorkshopSort { Popular, Recent, MostSubscribed };

struct WorkshopItem {
  String id;
  String title;
  String authorId;        // Steam ID; resolve with personaName()
  String description;
  String previewUrl;
  uint64_t subscriberCount = 0;
  StringList dependencyIds;
  bool available = true;  // false if hidden/removed/banned
};

struct WorkshopPage {
  List<WorkshopItem> items;
  uint32_t page = 1;
  uint32_t totalResults = 0;
};

enum class WorkshopItemState { NotSubscribed, Downloading, Installed, NeedsUpdate, DownloadFailed };

struct WorkshopItemStatus {
  WorkshopItemState state = WorkshopItemState::NotSubscribed;
  float downloadProgress = 0.0f;  // 0..1, meaningful only while Downloading
};

enum class WorkshopRequestStatus { Pending, Succeeded, Failed };

typedef uint64_t WorkshopRequestId;
```

New virtual methods:

```cpp
virtual WorkshopRequestId queryItems(String const& searchText, WorkshopSort sort, uint32_t page) = 0;
virtual WorkshopRequestId queryItemDetails(StringList const& ids) = 0;
virtual WorkshopRequestStatus requestStatus(WorkshopRequestId request) const = 0;
virtual Maybe<WorkshopPage> takeQueryResult(WorkshopRequestId request) = 0;

virtual WorkshopRequestId subscribe(String const& id) = 0;
virtual WorkshopRequestId unsubscribe(String const& id) = 0;
virtual void releaseRequest(WorkshopRequestId request) = 0;

virtual WorkshopItemStatus itemStatus(String const& id) const = 0;
virtual bool retryDownload(String const& id) = 0;
virtual Maybe<String> personaName(String const& steamId) = 0;
```

Semantics:

- `requestStatus` reports `Failed` for unknown ids and for any request still
  pending 30 seconds after it started.
- `takeQueryResult` removes a query request (cancelling it if pending) and
  returns the page only if it succeeded.
- `releaseRequest` removes a subscribe/unsubscribe request (cancelling it if
  pending).
- `retryDownload` clears a recorded download failure and calls `DownloadItem`.
- `personaName` returns the owner's display name, or nothing while Steam is
  still fetching it (callers show the ID meanwhile).

Steam implementation mapping:

| Method | Steam API |
|---|---|
| `queryItems` | `CreateQueryAllUGCRequest(type, k_EUGCMatchingUGCType_Items, appId, appId, page)` with `appId = SteamUtils()->GetAppID()`; sort → `k_EUGCQuery_RankedByTrend` / `RankedByPublicationDate` / `RankedByTotalUniqueSubscriptions`; non-empty search → `k_EUGCQuery_RankedByTextSearch` + `SetSearchText`; `SetReturnLongDescription(true)`; `SetReturnChildren(true)`; `SendQueryUGCRequest` → `CCallResult<…, SteamUGCQueryCompleted_t>` |
| result reading | `GetQueryUGCResult`, `GetQueryUGCPreviewURL`, `GetQueryUGCStatistic(k_EItemStatistic_NumSubscriptions)`, `GetQueryUGCChildren`; `ReleaseQueryUGCRequest` when the request is removed |
| `queryItemDetails` | `CreateQueryUGCDetailsRequest(ids, n)` + the same result reading; requested ids absent from results are appended with `available = false` |
| `subscribe` / `unsubscribe` | `SubscribeItem` / `UnsubscribeItem` → `CCallResult` on `RemoteStorageSubscribePublishedFileResult_t` / `RemoteStorageUnsubscribePublishedFileResult_t` |
| `itemStatus` | `GetItemState` flags + `GetItemDownloadInfo`; a `DownloadItemResult_t` with a non-OK result marks the item `DownloadFailed` |
| `personaName` | `SteamFriends()->RequestUserInformation(id, true)` then `GetFriendPersonaName`, cached per ID |

### 2. Workshop logic without Steam (`source/game/StarWorkshopLogic.{hpp,cpp}`)

Pure logic depending only on core and the platform types, unit tested without
Steam or assets:

- `isAllowedWorkshopPreviewUrl(url)` — true only for `https://` URLs whose host
  is exactly `steamuserimages-a.akamaihd.net` or `images.steamusercontent.com`
  (case-insensitive), with no userinfo (`@`) or port.
- `WorkshopDependencyResolver` — a state machine that does no I/O. Constructed
  with the root item id and the set of already-subscribed ids. The caller
  loops: `nextBatch()` returns ids whose details are needed, the caller fetches
  them with `queryItemDetails`, then passes the items to `supplyBatch()` (or
  calls `fail()`). It walks dependencies breadth-first and visits each id once,
  so cycles and diamonds terminate. Outputs, in discovery order:
  `toSubscribe()` (available, not subscribed, not the root),
  `unavailable()` (missing or `available == false`), `alreadySubscribed()`,
  and `titleFor(id)` for display.
- `WorkshopApplyState` — tracks ids whose subscription changed since the menu
  opened and which of those are still downloading. Exposes `markChanged`,
  `setDownloading`, `hasChanges`, `pendingDownloadCount`, `canApply`,
  `changedIds`, `reset`.

### 3. Image decoding (`source/core/StarImageDecode.{hpp,cpp}`)

- `Maybe<Image> decodeImage(ByteArray const& bytes)` — decodes PNG, JPEG, GIF
  (first frame) and BMP with `stb_image` into RGBA32 with rows bottom-up,
  matching `Image::readPng`. Returns nothing on failure.
- `Image fitImage(Image const& image, unsigned maxSide)` — nearest-neighbour
  downscale preserving aspect ratio so neither side exceeds `maxSide`; images
  already small enough are returned unchanged.
- `stb` comes from vcpkg (added to `source/vcpkg.json`), found with
  `find_path(STB_INCLUDE_DIRS "stb_image.h" REQUIRED)`.

### 4. Preview images (`source/frontend/StarWorkshopPreview.{hpp,cpp}`)

- `WorkshopPreviewCache` — `get(itemId, url)` returns the decoded image if
  ready, starting an `HttpClient::getAsync` fetch the first time an item is
  requested (only if `isAllowedWorkshopPreviewUrl`). `update()` polls finished
  fetches, decodes with `decodeImage`, and shrinks with `fitImage(…, 512)`.
  Holds at most 64 entries, evicting the least recently requested. Fetches are
  only made for the selected item.
- `WorkshopPreviewWidget` — a `Widget` that uploads its image with
  `Renderer::createTexture` and draws it centered and aspect-fitted in its
  bounds as a `RenderQuad`. With no image it draws
  `/interface/modsmenu/modicon.png` as a placeholder. Preview images never
  enter the asset system.

### 5. Workshop menu (`source/frontend/StarWorkshopMenu.{hpp,cpp}`)

A `Pane` built with `GuiReader` from
`assets/opensb/interface/workshopmenu/workshopmenu.config`, reusing vanilla
`/interface/modsmenu/` background art (359×245).

Widgets:

- Search text box (Enter searches) and a Search button.
- Mode buttons: Popular / Recent / Mine (checkable, one checked). Mine pages
  through `subscribedContentIds()` resolved with `queryItemDetails()`, and
  searching leaves it. The service still offers the MostSubscribed sort, which
  the UI no longer uses because "Subscribed" read as "my subscriptions".
- Result list: one row per item with a truncated title and a state badge
  (Subscribed / n% / Update / Failed).
- Prev / Next buttons and a "Page n of m" label (50 items per page).
- Details: preview widget, title, author, subscriber count, status,
  description (scrollable), Subscribe/Unsubscribe button, Retry button (shown
  when the download failed), Steam button (opens
  `https://steamcommunity.com/sharedfiles/filedetails/?id=<id>` via
  `desktopService()->openUrl`, or copies it to the clipboard without one).
- Status line in the list area for loading and error messages, with a Retry
  button that repeats the failed action.
- Apply button plus a label, visible once there are changes.

`update(dt)` starts the first query, polls the active query, the dependency
resolver and pending subscribe/unsubscribe requests, updates row badges and
the details panel, and refreshes the Apply button.

### 6. Dependency dialog (`source/frontend/StarWorkshopDependencyDialog.{hpp,cpp}`)

A modal `Pane` built from
`assets/opensb/interface/workshopmenu/dependencies.config`, reusing vanilla
`/interface/confirmation/` art. Two modes:

- `displayRequirements(requiredTitles, unavailableTitles, callback)` — message
  "This mod also needs: A, B, C" and, if any, "Unavailable (hidden or
  removed): D. This mod may not work." Buttons: **All** / **Just this** /
  **Cancel**. Long lists show the first 6 titles and "and N more".
- `displayResolveFailure(callback)` — message "Couldn't check requirements
  for this mod." Buttons: **Anyway** / **Cancel**.

Dismissing the dialog any other way counts as Cancel.

### 7. Title screen wiring

- Add `TitleState::Workshop` and a `"workshop"` button callback.
- `assets/opensb/interface/windowconfig/title.config.patch.lua` appends a
  `workshop` entry to `mainMenuButtons` using
  `/interface/modsmenu/workshopbutton.png` / `workshopbuttonhover.png`,
  right-anchored on the same row as the vanilla `mods` button and 90px further
  left. If no `mods` entry exists, no button is added.
- If `applicationController()->userGeneratedContentService()` is null, the
  `workshop` button is skipped and the menu is not registered.
- `initWorkshopMenu()` registers pane `"workshopMenu"` following
  `initModsMenu()`. `back()` from Workshop returns to Main.

### 8. Apply changes

1. The Apply button is enabled only when `canApply()`: there are changes and
   none of the changed items are still downloading. While downloads run, the
   label reads "Waiting for N downloads".
2. On click, the menu calls a callback given by `TitleScreen`, which sets a
   flag. `TitleScreen::takeModReloadRequest()` returns and clears it.
3. `ClientApplication::updateTitle` checks the flag right after
   `m_titleScreen->update(dt)`. If set, it stops title music, sets
   `m_forceModReload = true`, and calls `changeState(MainAppState::Mods)`,
   then returns.
4. Leaving Title destroys the `TitleScreen`. `updateMods` downloads any new
   subscriptions, collects installed directories and calls
   `Root::loadMods(dirs)`. With `m_forceModReload` set, it reloads even when
   the directory list is empty (the player unsubscribed from everything). The
   flag is then cleared.
5. The normal Splash → Title path fully loads Root and creates a fresh
   `TitleScreen`, so no pane holds widgets or images from the old assets.

Apply is reachable only from the title screen and never while in a world.
Frontend code never calls `Root::loadMods` directly.

## Dependency auto-subscribe flow

1. The player clicks Subscribe on item X. The button shows "Checking...".
2. The menu creates a `WorkshopDependencyResolver` for X and drives it with
   `queryItemDetails` until it finishes or fails.
3. When it finishes:
   - `toSubscribe` and `unavailable` both empty → subscribe to X.
   - Otherwise → show the dependency dialog. **All** subscribes X and every
     `toSubscribe` id; **Just this** subscribes X; **Cancel** does nothing.
4. If it fails → show the failure dialog. **Anyway** subscribes X; **Cancel**
   does nothing.
5. Unsubscribing never removes dependencies.

## Error handling

| Situation | Behavior |
|---|---|
| No UGC service (Steam not running, or non-Steam build) | Workshop button not shown. |
| Query fails or times out (30s) | Status "Couldn't reach Workshop" + Retry (repeats the same page). |
| Preview fetch/decode fails or disallowed host | Placeholder image; logged at debug level. |
| Subscribe/unsubscribe fails | Status "Subscribe failed" / "Unsubscribe failed" + Retry. |
| Download fails | Row badge "Failed"; details Retry button calls `retryDownload`. Failed items don't block Apply. |
| Apply pressed while downloading | Not possible; button disabled with "Waiting for N downloads". |
| Reload throws on a broken mod | Same path as a startup mod failure. The Workshop menu is still reachable afterwards to unsubscribe. |
| Leaving the menu mid-download | Steam continues; the menu pane and its apply state persist while the title screen exists. |
| Game closed before Apply | Subscriptions load at next launch, as today. |

## Testing

Automated:

- `source/test/workshop_logic_test.cpp` in a new `workshop_tests` executable
  (links core + `game/StarWorkshopLogic.cpp` only, labelled `NoAssets` so the
  `linux-release` test preset runs it):
  - Resolver: no dependencies; one level; multiple levels; diamond (D fetched
    once); cycle terminates; already-subscribed excluded from `toSubscribe`;
    missing and `available == false` reported unavailable; `fail()`;
    `nextBatch()` empty while awaiting.
  - Apply state: no changes; change + downloading; download finishes; ignores
    unchanged ids; reset.
  - Preview URL allowlist: accepted and rejected URLs.
- `source/test/image_decode_test.cpp` in `core_tests`: decodes a hand-built
  2×2 BMP with correct orientation and colors; rejects garbage bytes;
  `fitImage` shrinks large images and keeps small ones.

Manual checklist (Steam-enabled build installed into a local OpenStarbound
client, real Steam account):

1. Workshop button visible with Steam running; absent when launched without Steam.
2. Search returns matching results; each sort changes results; paging works.
3. Details show a preview image (including a JPG preview), author name, subscribers.
4. Subscribe to a mod with no dependencies → downloads → Apply → mod appears in the Mods menu.
5. Subscribe to a mod with dependencies → dialog lists them → All → all download.
6. "Just this" and "Cancel" behave as described.
7. Unsubscribe → Apply → mod gone from the Mods menu; its dependencies remain.
8. Disconnect network → search shows the error + Retry; reconnect → Retry works.
9. Apply is disabled while a download is in progress.
10. Enter a world after Apply → the game runs with the new mods.

## Files touched

New:

- `source/game/StarWorkshopLogic.hpp`, `source/game/StarWorkshopLogic.cpp`
- `source/core/StarImageDecode.hpp`, `source/core/StarImageDecode.cpp`
- `source/frontend/StarWorkshopPreview.hpp`, `source/frontend/StarWorkshopPreview.cpp`
- `source/frontend/StarWorkshopDependencyDialog.hpp`, `source/frontend/StarWorkshopDependencyDialog.cpp`
- `source/frontend/StarWorkshopMenu.hpp`, `source/frontend/StarWorkshopMenu.cpp`
- `source/test/workshop_logic_test.cpp`, `source/test/image_decode_test.cpp`
- `assets/opensb/interface/workshopmenu/workshopmenu.config`
- `assets/opensb/interface/workshopmenu/dependencies.config`

Modified:

- `source/vcpkg.json`, `source/CMakeLists.txt`
- `source/core/CMakeLists.txt`, `source/game/CMakeLists.txt`, `source/frontend/CMakeLists.txt`, `source/test/CMakeLists.txt`
- `source/platform/StarUserGeneratedContentService.hpp`
- `source/application/StarUserGeneratedContentService_pc_steam.{hpp,cpp}`
- `source/frontend/StarTitleScreen.{hpp,cpp}`
- `source/client/StarClientApplication.{hpp,cpp}`
- `assets/opensb/interface/windowconfig/title.config.patch.lua`
