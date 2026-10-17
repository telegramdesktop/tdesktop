/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace Test {

class Runner;

// AppendTextDropSelfTest is Test::DropText (test_widgets.h) measuring
// itself. The fixture is a real Ui::InputField inside a parentless,
// never-shown container that accepts drops and counts every drag event it
// receives. The container is the only drop-accepting ancestor, so no
// product drop area is in Qt's climb; a never-created window makes
// InputField::dropEventInner's raise and activate no-ops, so the run never
// brings the app forward; and it needs no session, network, draft, wallet
// or fixture secret.
//
// Delivery: with the field's own mime hook taking the insertion the hook
// receives exactly the dropped text and the field stays empty; with the
// hook leaving it the field holds exactly that text. Refusals, each from a
// control that creates its condition: a viewport with acceptDrops off and
// a disabled field (viewport-refuses-drops); a read-only editor beneath
// the drop-accepting container (enter-not-accepted), preceded by a raw
// premise that sends the same enter and drop without the helper and shows
// them landing in the container, so the helper's zero there is not
// vacuous; and an editor turned read-only by a viewport filter on the drop
// itself (drop-not-accepted). Every leg also reads the container's
// counters, QDragManager's current target (compared by pointer only, never
// dereferenced) and the clipboard oracle.
//
// Clipboard oracle: the self-test never reads or writes the clipboard. It
// counts QClipboard::changed emissions within each synchronous DropText
// call. On Cocoa (and xcb) every in-process write, a clear included, emits
// changed synchronously inside the write (qcocoaclipboard.mm setMimeData
// -> emitChanged), so a clipboard write inside the call cannot escape the
// count, and asynchronous emissions (an app activation re-syncing an
// external change) cannot land inside a block that runs no event loop. On
// Windows changed is emitted only from WM_CLIPBOARDUPDATE, asynchronously,
// so there the count is NOT a detector. A clipboard read has no runtime
// detector: the hook receiving exactly the self-test's own text, which no
// clipboard in the run was given, carries that half, with the static grep.
// Reading the clipboard as an oracle is rejected: it is the very read that
// came back empty, the owner's clipboard may hold secrets, and a
// programmatic pasteboard read on recent macOS may prompt.
//
// It emits no deliberate failure and no N/A.
void AppendTextDropSelfTest(not_null<Runner*> runner);

} // namespace Test
