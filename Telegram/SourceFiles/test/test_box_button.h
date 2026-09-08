/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtWidgets/QWidget>

namespace Test {

class Runner;

// Ui::BoxContent::addButton builds its Ui::RoundButton parented to the box
// (ui/layers/box_content.cpp:141-153) and hands it to the delegate, and
// Ui::BoxLayerWidget::addButton then re-parents it onto the shell -
// raw->setParent(this); raw->show(); (ui/layers/box_layer_widget.cpp:327-331).
// A footer button is therefore a direct child of the Ui::BoxLayerWidget and
// not a descendant of the published content widget at all, so a
// content-rooted FindAll<Ui::RoundButton>(box) can only ever answer zero, a
// click aimed that way never fires, and the stage times out with the target
// plainly on screen. Run 1 of
// 2026/08/30/replace-wallet-with-new-or-imported paid for that at six alert
// sites.
//
// The search is rooted at Test::PaintingLayerRoot(box).widget and kept to
// that root's direct children: the box is itself a child of that root, so an
// unrestricted walk also reaches every button inside the content and could
// click one that merely carries the wanted label. accessibilityName
// (ui/widgets/buttons.h:152-154, virtual on Ui::RpWidget at
// ui/rp_widget.h:421) is matched case-insensitively and carries the
// untransformed label, so a setTextTransform(ToUpper) display cannot change
// the match. Ui::BoxContent::triggerButton(int)
// (ui/layers/box_content.h:131-133) is not this: it is index-based, so a
// caller would have to know the footer order, and it calls clicked()
// directly, bypassing the press/release/leave route Test::Click guarantees.
// ClickBoxButton stays single-shot; the self-correcting repeat for a
// still-settling layer stays in the scenario, per the README flake row that
// already covers it.
//
// |match| is non-null exactly when |refusal| is empty at the moment the
// reading is taken: a caller cannot take the pointer without being handed
// the reason there is none. |present|, |disabled| and |hidden| are the
// answer beside that refusal: a shell button that carries the wanted label
// and is merely disabled or hidden is present, and is not a match.
//
// The helper and its self-test share one module for the reason
// test_menu.h:56-62 states: a facility that appends Runner stages cannot
// live in test_capture.h, because test_runner.cpp already includes it.
struct BoxShellButtons {
	QPointer<QWidget> root;
	QPointer<QWidget> match;
	int shellButtons = 0;
	int shellRoundButtons = 0;
	int contentRoundButtons = 0;
	QStringList labels;
	QString refusal;
	QString identity;
	bool present = false;
	bool disabled = false;
	bool hidden = false;

	[[nodiscard]] bool matched() const {
		return match != nullptr;
	}
};

// One reading, taken once, so a pass and a refusal print the same fields.
// Its counts, labels, refusal, identity, present, disabled and hidden are
// values and stay valid for the whole run; |root| and |match| are
// QPointer<QWidget>, so they stay valid too - as answers rather than as
// pointers. The unusable-button answer is those three bools, never a
// pointer a caller could dereference after the shell is gone. A footer
// callback that closes its box deletes the shell and every button on it
// synchronously (box_layer_widget.h:85-87 through layer_widget.cpp:944-977),
// and the reading survives that: matched() answers false from that moment
// on, on the very object the caller is still holding, with its counts,
// labels, refusal, identity, present, disabled and hidden intact and
// printable. So a reading may be kept for as long as the caller wants it -
// across turns and across stages - and it reports its own subject's death
// instead of pretending to still have one. What a caller still owes is the
// other half: format WidgetDescription(|match|) and WidgetDescription(|root|)
// while the reading is matched, because those take not_null<QWidget*>, whose
// Expects is a crash and not a refusal, and there is nothing left to format
// afterwards - print the recorded text, not the pointer. PopupMenuReading
// (test_menu.h:63-72), whose wording this follows, carries no widget
// pointer at all.
[[nodiscard]] BoxShellButtons ReadBoxButtons(
	QWidget *box,
	const QString &label);

[[nodiscard]] bool BoxButtonReady(QWidget *box, const QString &label);

// Re-read, like BoxButtonReady: true when a shell button carries |label|
// and is visible-but-disabled (BoxButtonDisabled) or not visible
// (BoxButtonHidden). Neither is a match, and neither is a pointer.
[[nodiscard]] bool BoxButtonDisabled(QWidget *box, const QString &label);
[[nodiscard]] bool BoxButtonHidden(QWidget *box, const QString &label);

// The same reading as text, for a stage's timeoutDetails and for a Check's
// details. Re-reads internally, the way PopupMenuDetails does.
[[nodiscard]] QString BoxButtonDetails(QWidget *box, const QString &label);

// Clicks the shell footer button carrying |label|, through the real
// press/release/leave route. Anything else - a null box, a box with no
// painting layer root, a label no shell button carries, a match that is
// hidden or disabled - is a logged FAIL naming what was seen, never a
// silent no-op.
bool ClickBoxButton(QWidget *box, const QString &label);

// The self-test builds one real Ui::GenericBox with five addButton footer
// buttons and a decoy Ui::RoundButton inside its content. Three footers are
// the original click/refusal/close subjects; the fourth is shown and
// disabled ("Harness Busy") and the fifth is hidden ("Harness Hidden"). The
// decoy is the control: without it the content-rooted zero would read as
// "this box has no buttons at all" rather than "the footer row is not in
// the content". The busy/hidden pair is the control for the unusable
// answer: without it matched()==false would still read as "the button is
// not there".
//
// The first show uses anim::type::normal so LayerStackWidget hides the
// shell for the show animation. In that same .run the self-test reads
// every footer as (hidden), including Busy, and the title label as not
// visible; after until sees box->isVisible() it re-reads Submit as ready
// and Busy as disabled on that same box. The hidden-during-animation
// refusal is observed through ReadBoxButtons, which logs nothing, never
// through a second announced ClickBoxButton Fail.
//
// box_button_shell is saved through CaptureBoxLayer so the frame contains
// the footer row; a CaptureInLayerRoot before-leg on the same fixture
// shows the cropped frame is strictly shorter and cannot hold that band.
//
// It needs no session, no chats list, no network and no account fixture. The
// only thing it asks of the process is a primary window to show a layer in,
// and a missing one is a named fixture gate instead of a crash. It appends
// its own teardown last. Unknown/null/decoy/stray refusals are still
// observed through the pure ReadBoxButtons / BoxButtonDetails readings,
// which log nothing, and asserted as a passing Check. ClickBoxButton on the
// disabled footer is the one announced deliberate Fail - a Note names the
// next FAIL row as that negative control, the way AppendTextReadsSelfTest
// announces its two, so a run carrying this self-test ends with that
// failure counted in SCENARIO_RESULT by design. The retained busy reading
// is asserted again after closeBox() destroys the shell. Its before-leg,
// the stage timeout this repair removes, is produced by rooting the search
// at the box again and re-running the identical scenario, never by a stage
// that fails on purpose.
void AppendBoxButtonClickSelfTest(not_null<Runner*> runner);

} // namespace Test
