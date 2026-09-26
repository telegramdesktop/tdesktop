/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_types.h"

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Test {

// Records |documentId| as one the scenario itself created, so MakeStubEmoji
// may answer for it. Idempotent: a repeated id is not a second entry, and the
// first registration of an id logs one Note quoting the id and the running
// count, which is the run's evidence that the registration reached this
// module. There is deliberately no way to unregister: the caller owns every
// stub already handed out, so dropping an id could not unmake one, and would
// only leave a seam painting a fixture the registry claims is gone.
void RegisterStubEmojiDocument(DocumentId documentId);

// The fixture, or null. Null unless Active() and unless |documentId| was
// registered above, so a real gift's pattern document can never be
// substituted. The caller owns the result. Never logs: a production seam calls
// this on every paint.
//
// The returned emoji answers ready() and readyInDefaultState() true on the
// first call, with no Data::Session, no document, no download and no
// event-loop turn behind it. unload() is a no-op and entityData() is empty.
//
// paint() FILLS THE WHOLE LARGE-EMOJI BOX - a plain square, no corner radius -
// at context.position in context.textColor, with no pen, no brush and no other
// painter-state change, so the caller's current opacity and transform apply to
// it exactly as they do to a real emoji. The box side is
// Ui::Emoji::GetSizeLarge() / style::DevicePixelRatio(), i.e. LOGICAL pixels
// (GetSizeLarge() itself is device pixels), which is also what width()
// returns. Ui::PaintBgPoints (ui/top_background_gradient.cpp) sets a per-point
// opacity and scale on its painter before its file-local PrepareImage helper
// calls paint(), and both still apply here unchanged.
// context.size, context.scaled and context.now are ignored: the box is always
// the large one and there is no animation to seek.
//
// The filled square is WORST-CASE ink on purpose, not representative ink: it
// carries ink to the box's left and right extremes over the whole vertical
// extent, which no real glyph does. A containment or legibility verdict taken
// under this fixture is therefore conservative - a pass holds a fortiori for
// any real glyph, while a failure may be pessimistic and must be re-checked
// against a real glyph before it is reported as a product bug.
[[nodiscard]] std::unique_ptr<Ui::Text::CustomEmoji> MakeStubEmoji(
	DocumentId documentId);

// How many times MakeStubEmoji answered with a stub. This is the only evidence
// a run has that a production seam really took the fixture, because the seam
// consumes the returned pointer and the scenario never sees it again.
[[nodiscard]] int StubEmojiHandedOutCount();

} // namespace Test
