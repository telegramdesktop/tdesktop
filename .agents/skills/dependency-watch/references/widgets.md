# Widget dependencies and QuickJS

Discover these dependencies from the exact snapshot on every run. The mappings
below are navigation clues, not permanent pins or proof that every bundled
component is shipped.

## Follow the consumer graph

- `Telegram/cmake/lib_litehtml.cmake` selects the copied litehtml and Gumbo
  sources in `Telegram/ThirdParty/twidget`, from
  https://github.com/TelegramMessenger/twidget. Track original
  https://github.com/litehtml/litehtml and the parser's documented upstream
  separately. Read the fork's notices and import history; a copied tree may
  incorporate fixes absent from its original upstream.
- `Telegram/cmake/lib_quickjs.cmake` selects QuickJS and its C shim from
  `Telegram/ThirdParty/widget-sandbox`, from
  https://github.com/TelegramMessenger/widget-sandbox. Read that pin's
  `.gitmodules` and nested Git tree. The initial mapping is
  `packages/ios/Sources/CQuickJS/quickjs` ->
  https://github.com/TelegramMessenger/quickjs ->
  https://github.com/quickjs-ng/quickjs. The `ios` directory name does not make
  the native engine iOS-only: check the actual desktop CMake target.
- Follow `lib_widget_sandbox.cmake`, `lib_widget_renderer.cmake`, resource
  declarations and bundle-generation scripts. Inventory runtime JavaScript
  packages baked into sandbox/validator bundles, their lockfile resolutions,
  the bytecode compiler and build tools. Distinguish these from unused demo
  applications, platform assets and alternate renderer copies in the same repo.

The root helper records only root gitlinks. Resolve child gitlinks through APIs
or isolated caches under `storage_root`, never by updating live submodules.
Persist the complete parent-pin -> child-pin -> compiled-source mapping. A
change to the child fork's master does not update its pinned parent or tdesktop.

## Review QuickJS beyond releases

Compare our pinned fork, the fork's current primary branch, the latest upstream release
and upstream development head. Establish the shared ancestor, list all local
changes and review the complete unimported upstream delta. Read the current
upstream security policy:
https://github.com/quickjs-ng/quickjs/blob/master/SECURITY.md

As of the initial review, QuickJS-ng accepts ordinary public bug reports rather
than issuing CVEs. Refresh that policy and paginate issues/PRs, including older
unresolved reports. Look beyond security labels: allocator failures, GC and
reference ownership, parser/compiler bounds, reentrant coercions, Promise/module
lifetime, stack limits and missing interrupt polls can affect hostile widgets.
Recheck merged fixes against the exact release tag; a newer release can still
omit a fix merged immediately after it. Bellard QuickJS reports are leads, not
automatic matches to QuickJS-ng; establish code equivalence and ancestry.

For each candidate distinguish merged fixes, unmerged proposals, reported bugs
without a reviewed fix, and equivalent local backports. Read fixing code and
prerequisites, not just titles or a reporter's claim. Keep upstream reproduction
claims separate from local source confirmation and from an actual reproduction
with Telegram's budgets. Persist unresolved memory-safety candidates in the
pressing block even when the proposed replacement is held for review.

Trace widget payload -> HTML/script selection -> native evaluation and jobs,
including inline modules and teardown. Check which intrinsics and C APIs are
available. A JavaScript policy sandbox, memory limit or interrupt handler is not
proof that a native engine memory error is contained; allocation failure and
interruption themselves can trigger bugs. Conversely, inspect shim bounds
checks before assigning an engine C API bug to the application.

Review `JS_ReadObject` separately: determine whether bytecode is packaged and
trusted or supplied by a widget. Do not label hostile-bytecode reports as an
exposed widget path without evidence. Do not treat a source hash or bytecode
format number as an engine identity or an authentication mechanism.

## Choose an import or backport route

Assess a full upstream import against selective security backports. Preserve
fork allocator/GC/compiler changes and local uncatchable-exception semantics;
inspect semantic overlaps even when patches merge cleanly. Dry merge checks
belong only in isolated caches. Conflict-free application is not validation.

For a full import, inspect public API changes and all native callers, bytecode
opcodes/version, generated builtin headers, bundle compiler and resource
generation. Existing precompiled sandbox/validator bundles require explicit
compatibility evidence or regeneration using the new engine. Source fallback
does not prove stale bytecode is safe to deserialize. Record this work as an
adoption prerequisite; this monitor does not rebuild it.

For each fix record the exact upstream commit or reviewed PR head, prerequisites,
local equivalents, platform exposure, destination, validation needs, decision
and review date/condition. Prefer reviewed upstream fixes for urgent gaps;
do not import an entire unmerged PR stack just because its final patch is small.
Routine release adoption follows the seven-day observation policy; confirmed
applicable security fixes can be expedited after assessment.

The implementation route, only when separately authorized, is the owned fork's
published primary branch, then the widget-sandbox nested pin and necessary
resources, then tdesktop's parent gitlink. Initially QuickJS uses `master` while
widget-sandbox and twidget use `main`; resolve these branches afresh. Verify
each published commit is reachable from its primary branch before updating its
consumer. Apply the same synchronization and no-topic-branch discipline as the
repository's dependency backport rules, preserving unrelated work. Do not invent direct
prepare/Docker/Snap revision edits if they consume the same recursive submodule
graph already.

Required validation for an implementation should match the change: upstream
regressions and allocator-failure tests, ASan/UBSan, interrupt and teardown
paths, ordinary exceptions versus terminal budget failures, bundle loading and
widget behavior, and desktop Debug builds on affected platforms. Name all
unperformed checks; an advisory source review does not establish runtime safety.
