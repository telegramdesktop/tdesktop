# macOS native-notification evidence

[Harness guide](../README.md) · [Helper index](helpers/README.md)

Read this before a run whose evidence posts a native macOS notification
through the legacy `NSUserNotification` backend
(`Platform::Notifications::Manager`, NS) or the `UserNotifications` backend
(`Platform::Notifications::UNManager`, UN). Citations name one of three task
records under the AI repository's `tasks/<task id>/`: the **ids task**
`2026/10/28/keep-peer-and-message-ids-out-of-macos-notification-identifiers`,
the **clears task**
`2026/10/29/remove-macos-notifications-of-deleted-saved-messages-and-read-monoforum-sublists`
or the **mark task**
`2026/10/29/keep-macos-notifications-working-after-modern-notifications-are-turned-off`.
A Run, Test or section cited without a file is in that task's `work/test.md`.

## Choosing a bundle id

After a launch that constructed `UNManager`, usernoted records the bundle id
as a modern client and refuses every later NS post from it; an id with no UN
launch is accepted as a legacy client (ids task Runs 3 and 5; mark task Run 2
Tests 1 and 2). So one id serves one backend. The table reads the `macbook`
host on 2026-10-29 under macOS 27.0.1 (mark task `work/result.md` Summary and
`task.md` Facts to recheck; the clears task records no version). Recheck it
before relying on it.

| Bundle id | Recorded state | UN authorization | Use for |
| --- | --- | --- | --- |
| `com.tdesktop.TelegramDebug`, every checkout's Debug build | Modern since the ids task's first UN launch, NS refused since (ids task Run 3); mark never read | 2 in every recorded reading, across rebuilds (ids task Runs 2, 4, 8; clears task Run 3) | UN only |
| `com.tdesktop.TelegramDebugNsEvidence` | Made by the ids task, only `CFBundleIdentifier` changed; delivered there (Runs 5-8) and from new clones in the clears task (Backends, Runs 2, 3; readback only); no UN launch recorded; mark never read | Not measured | NS |
| `com.tdesktop.TelegramDebugLegacyEvidence` ("TG Legacy Evidence") | Legacy, no mark; delivered in B0-B2, B2 while other ids were marked; permission request unanswered (mark task Run 1 Test 2; Run 2 Test 2, Campaign side effects) | Not measured | NS |
| `com.tdesktop.TelegramDebugProvisionalEvidence` ("TG Provisional Evidence") | Modern since its first launch C1; mark `1`, restored after the C3 control, where NS was refused (mark task Run 2 Tests 1, 3, Campaign side effects) | 3 after the provisional step, then 2 in C1 and C2, both delivered (Run 2 Tests 1, 8) | UN |
| `com.tdesktop.TelegramDebugKeepUnEvidence` ("TG KeepUN Evidence") | NS at A0, modern since A1, mark `1` (mark task Run 1 Test 1); its NS refusal is inferred, not measured: A3 never ran (Run 2) | 1 (denied) in A1 and A1r; provisional request refused (Run 1 Test 1; Run 2 Tests 1, 8) | Neither |

NS leg: a new clone of `...NsEvidence`, else of `...LegacyEvidence`, with the
option off in its sandbox and its mark absent
([backend choice](#how-the-app-picks-the-backend)): a launch that selects
`UNManager` converts the id. UN leg: the shared Debug build with the option
on in its own sandbox, else a new clone of `...ProvisionalEvidence`; read
authorization in process (`getNotificationSettingsWithCompletionHandler:`)
and treat anything but 2 (authorized) or 3 (provisional) as a fixture gate,
not a product FAIL (mark task Run 1 Test 1, Evidence recovery 1). Make no UN
call, not even a settings or delivered-list query, in a launch of a
legacy-only id: whether that converts it is [not measured](#not-measured)
(mark task `work/decisions.md` E4).

**Reuse before minting.** A minted id stays permanently in System Settings >
Notifications and in usernoted's client records, with a defaults domain
`~/Library/Preferences/<id>.plist` and possibly an unanswered permission
request. Deleting the clone removes none of them, and nothing in the
workflow undoes a conversion (mark task `work/result.md` Environment side
effects (owner), `work/decisions.md` Alternatives considered 8; ids task
Campaign side effects, Performer assessment (Attempt 1)). Mint only when no
id above fits, as `com.tdesktop.TelegramDebug<Purpose>Evidence` with
`CFBundleName` "TG <Purpose> Evidence" (mark task, preamble), and report it
as a side effect. Never restart usernoted, edit `com.apple.ncprefs` or
usernoted's database, or change the defaults of an id the task does not
control (mark task Run 1 Evidence recovery 1).

**The permission trap.** A fresh id's first NS launch queues a system "allow
notifications" request: at its first legacy connection, seconds before the
post, usernoted logs `Sending request for permission` and presents an
`askpermissions` record; NS posts still deliver (mark task Run 1 Test 1,
Performer assessment (Run 1), `evidence/usernoted-excerpts.txt` A0 and B0;
ids task Run 5). While it stayed unanswered, a later UN launch of the id read
status 1, the product's request failed with
`App Error: Notifications authorization error: Notifications are not allowed for this application`,
and the provisional step below was refused (mark task Run 1 Test 1 A1, Run 2
Test 1 A1r and Test 8). Nobody could answer on the locked console; what an
answer changes is not measured. Never rely on an unanswered prompt, wait for
the owner or drive the UI on a locked console (Performer assessment (Run 1)).
**Never put an NS leg before a UN leg on one id**; keep each id on one backend.

## How the app picks the backend

`Platform::Notifications::Create()` in
`platform/mac/notifications_manager_mac.mm` installs `UNManager` when
`UseUNManager()` in `platform/mac/notifications_manager_mac_un.mm` is true,
else `Manager`. `UseUNManager()` needs the `mac-modern-notifications` option
on or the bundle's `TelegramUserNotificationsUsed` defaults key (the mark)
set, and macOS 10.14+ with a bundle identifier. `UNManager::Private`'s
constructor sets the mark before its first `UNUserNotificationCenter` call;
the app never clears it (mark task `work/decisions.md` D1; Run 1 Test 3). The
option is per working directory (`base::options::init` in
`core/launcher.cpp`), so per sandbox; the mark is per OS user and bundle id,
shared by every working directory and not inherited by another id (D1; Run 2
Tests 2, 3). `defaults read <id> TelegramUserNotificationsUsed` prints `1`
when set and "Could not find key" or "Domain ... not found" when absent (Run
1 Test 1; Run 2 Tests 1, 3).

With the option off, deleting the mark while no copy of the id runs
(`defaults delete <id> TelegramUserNotificationsUsed`) selects `Manager`
again, but usernoted still refuses it: C3 was refused, and the campaign
restored the mark with
`defaults write <id> TelegramUserNotificationsUsed -bool YES` (Run 2 Test 1,
Campaign side effects). Do it only as a refusal control on an evidence id the
task controls, never on the shared Debug id.

With the Xcode generator the Debug bundle id is `com.tdesktop.TelegramDebug`
(`bundle_identifier` in `Telegram/CMakeLists.txt`): one usernoted client for
every checkout's Debug build on this Mac, whose NS posts usernoted already
refuses (ids task Campaign side effects, Run 3). Its next UN launch of
mark-aware source writes the mark; from then on mark-aware Debug launches
select `UNManager` whatever the option says, while older source ignores the
mark (mark task `work/result.md` Environment side effects (owner);
`work/decisions.md` D3). Either way it serves UN legs only.

## Making the clone

From the source root, with `TASK_DIR` set to the task folder, clone the
task's own Debug build. A rebuild needs a fresh clone (clears task
`work/context.md` Test environment, Runs 2 and 3; mark task Run 1 Clones,
Run 2 Test 7) at a new path: run the whole block again, which makes a new
directory under `$TASK_DIR/.local/` each time. `cp -R` onto an existing
clone nests the new bundle inside it, and the old executable still launches.

```sh
id='com.tdesktop.TelegramDebugNsEvidence'
mkdir -p "${TASK_DIR:?}/.local" &&
  clone="$(mktemp -d "$TASK_DIR/.local/clone-ns-XXXXXX")/Telegram.app" &&
  test ! -e "$clone" &&
  cp -c -R out/Debug/Telegram.app "$clone" &&
  /usr/libexec/PlistBuddy -c "Set :CFBundleIdentifier $id" \
    "$clone/Contents/Info.plist" &&
  codesign --force --sign - \
    --preserve-metadata=entitlements,flags,runtime "$clone" &&
  codesign --verify "$clone" &&
  echo "clone=$clone"
```

The chain stops at the first failed step; only a run that ends by printing
`clone=<path>` made a usable clone (`codesign --verify` exited 0). Launch
that path as `$clone` below. `codesign -dvv "$clone"` shows
`Identifier=<id>`. For an id with a display name in the table, also
`Set :CFBundleName` to it, as the mark task did.

Each leg gets its own ignored `.local/` sandbox whose
`test_TelegramForcePortable` is a `cp -R` copy of the golden; a UN leg adds
`"mac-modern-notifications": true` to the copy's
`tdata/experimental_options.json`, keeping other keys, never in the golden
(clears task Run 2 Sandboxes; ids task Run 2). Launch the clone's executable,
one clone process at a time, adding the task's `--env`, `--deadline` and
`--quiet` flags (clears task Run 2 Launch; mark task Run 1 Launch command):

```sh
python3 .agents/skills/process-inbox/scripts/workspace.py test-run \
  --exe "$clone/Contents/MacOS/Telegram" --run-dir "$run" \
  --portable-root "$sandbox"
```

[Launch behavior](launch.md) and the
[test loop](../../../../.agents/shared/test-loop.md) (Test account (portable
data) — hard rules) govern launch, portable data and cleanup. Deleting the
clone leaves the id registered.

## Provisional authorization for a never-used id

Only a campaign that mints an id for a UN leg while nobody is at the console
needs this. The mark task's `work/test-overlay.patch` keeps it as disposable
overlay code: `TestKeepUnProbe::ProvisionalSeam()`, with `SeamAllowlisted()`,
`SeamLogSettings()`, `SeamRequestProvisional()` and the helpers and includes
they use, in the `_DEBUG` block appended to
`Telegram/SourceFiles/platform/mac/specific_mac.mm`; a forward declaration
and one call right after `ApplyAppNapHold()` in
`Test::ApplyStartupOverrides()` (`Telegram/SourceFiles/test/test_agent.cpp`).
Adapt those parts into the task's own overlay, not the permanent harness.
`Core::Application::run()` calls the overrides before `startDomain()`, and
the seam read the dummy manager (`manager_at_seam=other(type=0)`), so it runs
before the product's manager exists (mark task Run 1 Evidence recovery 1,
Run 2 Test 8).

It reads the settings, requests `UNAuthorizationOptionProvisional` and reads
them again, waiting at most 5 s per read and 10 s for the request
(`kSeamSettingsWait`, `kSeamRequestWait`). Without `-testagent`
(`Test::Active()`) it returns silently; otherwise it logs one
`PROVISIONAL_SEAM ... stage=gate ... run=<0|1> gate=[<first failed gate>]`
row and calls UN only when all of these hold:

- the working directory has the disposable copy's `testing` marker;
- `TDESKTOP_TEST_PROVISIONAL=1`, passed with `test-run --env`;
- the running bundle id is on an allowlist of the campaign's own UN ids,
  never the shared Debug id (the original lists `...KeepUnEvidence` and
  `...ProvisionalEvidence`; put the new id there);
- `Platform::Notifications::UseUNManager()` is true, so the product contacts
  UN in that launch anyway.

Keep every gate (mark task `work/test-overlay.patch`, `work/decisions.md` D5,
Run 2 Tests 2 and 8). The new id's first launch ever must be this UN launch,
with the option pre-seeded in its sandbox copy. In C1, the first launch of
`...ProvisionalEvidence`, the status went from 0 to 3, read 2 after the
product's own request, and the post was delivered; C2, with no seam and the
option off, read 2 and delivered (Run 2 Tests 1, 8). It does not rescue a
denied id (A1r). The seam makes the first UN contact, so such a launch cannot
measure the product's own (Run 2 Test 3), and C1's product request still
queued an `askpermissions` record (Test 8).

## Capturing usernoted

Run `log stream --predicate 'process == "usernoted"' --debug --style syslog`
from before the launch until after the client exits (mark task Run 1
Capture; ids task Run 2). An absence claim needs a live stream with no gap
over the whole launch (mark task `work/decisions.md` E5). `log show` is only
a cross-check: about 25 minutes later the store had dropped debug lines such
as `Added modern connection` but kept `Delivering` and refusal lines (mark
task Run 2 Test 2). Recorded lines (mark task
`evidence/usernoted-excerpts.txt` and Performer assessment (Run 1); ids task
`evidence/usernoted-legacy-modern.txt`):

- delivery: `Delivering <NotificationRecord app:"<id>" ident:"<ident>" req:"<request id>" ...`,
  after `Request from <LegacyConnection identifier: <id> ...> to add request`
  (NS) or `Added request <NotificationRecord app:"<id>" ...` (UN);
- refusal: `Legacy client <id> connecting to modern client. You can't mix modern clients with legacy clients.`,
  `Ignoring connection <LegacyConnection identifier: <id> ...>`, then
  `Denying message 7 from connection <LegacyConnection ...>` per call (C3
  also logged one `Denying message 3`);
- conversion to a modern client: `Added modern connection for <id> <pid> ()`;
  a legacy client logs `Added legacy connection for <id> pid: <pid>`;
- queued permission request: `Sending request for permission`, then
  `Added request <NotificationRecord app:"com.apple.notificationcenter.askpermissions" ... req:"<id>"`
  and `Presenting` it;
- provisional grant:
  `Authorization set for <id> to allow: YES critical: NO provisionally: YES`.

`Setting modern authorization [badge, sound, alert] for <id>` alone is no
conversion: a new legacy client logs it too (the excerpts' header). A
delivery is the exact request identifier read back in process (NS
`deliveredNotifications`; UN
`getDeliveredNotificationsWithCompletionHandler:`, in a UN launch only) plus
its `Delivering` line inside the post window (mark task Run 2 Test 1). Keep
raw captures in the task's ignored `.local/`; tracked evidence quotes lines
cut after `req:` (the excerpts' header; ids task `work/test.md` preamble).

## Not measured

Do not mint an id to measure any of these.

- Persistence of usernoted's modern record across a reboot or a usernoted
  restart, and macOS versions other than 27.0.1 (mark task `task.md` Facts
  to recheck).
- Whether a read-only `UNUserNotificationCenter` call converts an id (same;
  `work/decisions.md` Alternatives considered 3).
- Whether a new clone of an authorized evidence id keeps its authorization:
  the shared Debug id stayed at 2 across binaries rebuilt in place (ids task
  Runs 2, 4, 8; clears task Run 3), and `...KeepUnEvidence`'s new clone read
  1, but that id was denied already (mark task Run 2 Tests 1, 7).
- A never-used id's first UN launch without the provisional step (mark task
  `task.md` Facts to recheck: it "may meet the system permission prompt").
- What answering a pending permission request does, and whether such
  requests stay in Notification Center (mark task `work/result.md`
  Environment side effects (owner); Run 2 Campaign side effects).
- The current mark of the shared Debug id and of `...NsEvidence`: no record
  reads it, and the mark task launched neither (same). Read it before
  relying on it.
