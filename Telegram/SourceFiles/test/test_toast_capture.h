/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_ink.h"
#include "test/test_runner.h"

#include <crl/crl_time.h>

#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>

#include <vector>

namespace Test {

// The toast captures, and the only ones whose saved frames cannot hold
// the product surface the toast was drawn over.
//
// Test::CaptureViaWindow grabs the toast's window cropped to the toast's
// rect mapped into it. That is the right answer to blankness and the wrong
// answer to hygiene. st::toastBg is #2c3033e5 (ui/colors.palette) - alpha
// 0xE5, about 90% opaque - so roughly a tenth of every pixel the product
// painted underneath composes into the saved frame, legibly enough to read
// text through it. The rect's corners are worse than that:
// Widget::paintBackground is _roundRect.paint(p, rect())
// (ui/toast/toast_widget.cpp), which fills the straight bands and draws
// four corner images (Ui::DrawRoundedRect in ui/round_rect.cpp) whose mask
// is filled with Qt::transparent and carries only a rounded rect
// (PrepareCornersMask in ui/image/image_prepare.cpp), so outside the arc
// the toast paints nothing at all and whatever is behind it shows there
// unblended.
//
// Test::GrabWidget and Test::GrabRect call Ui::GrabWidgetToImage(widget,
// rect, st::windowBg->c) (test_capture.cpp), which fills the result with
// the harness theme base and then renders only that target's own subtree
// (ui/ui_utility.cpp). A grab rooted at the toast is therefore hygienic by
// construction: every pixel the translucent toast leaves uncovered measures
// st::windowBg and never the product. That is the whole mechanism this
// module rests on, and it is why a root that is not the live toast is
// refused here rather than reframed.
//
// Ui::Toast::internal::Widget::_shownLevel is private (toast_widget.h), and
// while it is below 1 paintEvent draws the whole frame into a transparent
// proxy at that opacity and returns (toast_widget.cpp) - which is exactly
// where a toast-rooted grab reads blank. So settledness here is measured
// and not read: the settled background blend is derived from the live
// palette and one grab is sampled against it.
//
// Ui::Toast::internal::Manager exposes only instance() and addToast() and
// keeps its _toastByWidget map private (ui/toast/toast_manager.h), so no
// API enumerates live toasts and the honest resolution is the harness's own
// visible-widget walk over the top level widgets.
//
// This is its own module rather than part of test_capture.h because it
// appends Runner stages and test_runner.cpp already includes
// test_capture.h: the reverse include would invert the harness's layering
// and make its most-included module runner-aware, which is the rule
// the layering note above PopupMenuReading in test_menu.h states. It is
// not part of test_via_window.* either, because that module is the
// self-test of CaptureViaWindow and is this module's own
// unchanged-scenario control.

// |toast| is non-null exactly when |refusal| is empty at the moment the
// reading is taken: a caller cannot take the pointer without being handed
// the reason there is none. It is a QPointer<QWidget>, so a retained
// reading can only go from resolved to unresolved and never hands back a
// pointer into freed memory, while |frame|, |identity| and |refusal| are
// values that stay printable after the toast is gone.
// Test::WindowMappedCapture (test_capture.h) and
// Test::PaintingLayerRootResult carry the same type and the same contract.
struct ToastSubtreeReading {
	QPointer<QWidget> toast;
	QRect frame;
	QString refusal;
	QString identity;

	[[nodiscard]] bool resolved() const {
		return toast != nullptr;
	}
};

// The visible-widget walk that answers which toasts are live now. It runs
// over every top level widget rather than one root, because a toast is
// parented wherever Ui::Toast::Show was called. FindLiveToast() answers
// that single toast, and nullptr when the walk finds zero of them or more
// than one - an ambiguous answer is refused rather than guessed. "Live"
// means visible in the walk, and that includes a toast that
// Instance::hideAnimated() is fading: it stays visible until
// Instance::hide() runs at the end of the fade (durationFadeOut, 1000 ms
// for st::defaultToast). Read shown, hiding and gone apart through
// ReadToastState below.
[[nodiscard]] std::vector<QWidget*> FindLiveToasts();
[[nodiscard]] QWidget *FindLiveToast();

// ReadToastSubtree takes no grab. A root that is not a live
// Ui::Toast::internal::Widget is refused by name, and so is a child inside
// a toast, on purpose: the frame is rooted at the toast or nowhere,
// because everything above it paints the product and everything below it
// is not the toast. A non-empty |requested| that the toast's own rect does
// not contain is refused quoting both rects, never silently reframed.
[[nodiscard]] ToastSubtreeReading ReadToastSubtree(
	QWidget *widget,
	QRect requested = QRect());

// GrabToastSubtree, ToastSubtreeReady and ToastSubtreeDetails take exactly
// one grab each, so a poll costs one grab per tick - the budget
// test_capture.h's comment on WindowMappedCapture promises for the
// window-mapped family. A refused request answers a null image and never a
// reframed one, so a poll around a refused target ends in a named stage
// timeout rather than in a grab of something else. ToastSubtreeReady is the
// settled-show term: it is true once the sampled frame matches
// st::toastBg's own settled blend over the harness base, which no mid-fade
// frame does. It is a background term only and says nothing about the
// toast's painted children (see ReadToastContent).
[[nodiscard]] QImage GrabToastSubtree(
	QWidget *widget,
	QRect requested = QRect());
[[nodiscard]] bool ToastSubtreeReady(QWidget *widget);
[[nodiscard]] QString ToastSubtreeDetails(QWidget *widget);

// Loud on a structural refusal, because no amount of waiting repairs one:
// no widget, a root that is not the live toast, an invisible or empty
// target, or a rect outside the toast's geometry. A Note and never a FAIL
// on an unsettled or blank frame, because the decisive oracle for a toast
// is textual and this capture corroborates it. It qualifies background
// settledness only, so it also saves a settled frame whose text, icon and
// link were never painted; a toast saved as visual proof of its painted
// content goes through ReadToastContent and CaptureToastContent below.
bool CaptureToastSubtree(
	not_null<QWidget*> widget,
	const QString &name,
	QRect requested = QRect());

// The joined accessibilityName() of the toast's Ui::FlatLabels - the read
// the harness README already calls decisive - and its comparison, which
// goes through the shipped Test::CheckTextReads (test_text_reads.h) so that
// the space-class normalization and the both-verdicts printing are composed
// rather than re-derived. A widget that is not a live toast reads back an
// empty string.
[[nodiscard]] QString ReadToastText(QWidget *widget);
void CheckToastReads(
	QWidget *widget,
	const QString &expected,
	const QString &what);

// A toast's phase, measured from outside. The shown level and
// Instance::_hiding are private (toast_widget.h, toast.h), so the phase is
// read from the FindLiveToasts() walk and from the settled-background
// measurement ToastSubtreeReady takes, over a chain of readings:
// - toast-gone: the widget is destroyed or no longer in the walk. Final: a
//   reread of a gone reading stays gone without a grab.
// - toast-shown: in the walk, and its toast-rooted grab measures
//   st::toastBg's settled blend - exactly ToastSubtreeReady's term, so it
//   says nothing about the painted children.
// - toast-hiding: in the walk and measured below the settled blend after an
//   earlier reading of the same chain measured it shown; a toast drops
//   below its full level again only through hideAnimated().
// - toast-unsettled: in the walk, below the settled blend, and never
//   measured shown in this chain - a fade-in, or a fade-out whose chain
//   started mid-way, which one frame cannot tell apart.
// - toast-unresolved: no widget, or one that is not a
//   Ui::Toast::internal::Widget, so there is nothing to read; |refusal|
//   names the case. A null widget - for example a FindLiveToast() that
//   refused an ambiguity - is unresolved and never gone. Final too.
// Start a chain with ReadToastState(widget) while the toast is still shown
// and continue it with RereadToastState(previous) on every poll; a reading
// of a live toast takes one grab. A chain first read mid-fade answers
// toast-unsettled, never toast-hiding.
//
// Limits. Hiding is read from the first grab measurably below the settled
// blend: in the turn hideAnimated() is called the fade has run no frame,
// and for the first part of the linear fade the frame stays inside the
// settled tolerance - about 6% of the fade-out on a light palette, about
// 20% on a dark one - so the reading still answers toast-shown there. A
// toast shown with Config::attach slides at full opacity and reads shown
// until gone. A toast whose window is hidden leaves the walk and reads
// gone, which is the walk's own limit.
//
// |settledPermille| is the sampled share of the last grab, -1 when the
// reading took none; |identity| is the handed widget's WidgetDescription,
// refreshed by every live reading, and |text| the ReadToastText of the
// first reading; |readings| counts the readings of the chain. Reference:
// docs/helpers/test_toast_capture.md.
enum class ToastPhase {
	Unresolved,
	Unsettled,
	Shown,
	Hiding,
	Gone,
};
[[nodiscard]] QString ToastPhaseName(ToastPhase phase);

struct ToastStateReading {
	QPointer<QWidget> toast;
	ToastPhase phase = ToastPhase::Unresolved;
	bool live = false;
	bool seenShown = false;
	int settledPermille = -1;
	int readings = 0;
	QString identity;
	QString text;
	QString refusal;
};
[[nodiscard]] ToastStateReading ReadToastState(QWidget *widget);
[[nodiscard]] ToastStateReading RereadToastState(
	const ToastStateReading &previous);
[[nodiscard]] QString ToastStateDetails(const ToastStateReading &reading);

inline constexpr auto kToastGoneMargin = crl::time(500);

// The bounded wait for gone, a pure reading of the chain given the
// caller's own elapsed time: it rereads |previous| once. |waited| 0 never
// refuses. Once |waited| >= |bound| a toast that is not gone answers
// "toast-not-gone: <identity> "<text>" still <phase> after <waited> ms
// (bound <bound> ms); live toasts: [...]", and |live| lists every toast the
// walk still holds as "<identity> "<text>"". An unresolved chain answers
// "toast-unresolved: <refusal>" at once. A gone toast never refuses, so
// gone() past the bound is still gone and the caller compares |waited|
// with |bound|. Poll it from |until| returning done(), so the stage ends
// on the answer and never in a stage timeout, carry |state| into the next
// poll, and assert gone() and the refusal in |then|. After hideAnimated(),
// bound the wait by durationFadeOut * anim::SlowMultiplier() +
// kToastGoneMargin.
struct ToastGoneWait {
	ToastStateReading state;
	crl::time waited = 0;
	crl::time bound = 0;
	QStringList live;
	QString refusal;

	[[nodiscard]] bool gone() const {
		return state.phase == ToastPhase::Gone;
	}
	[[nodiscard]] bool done() const {
		return gone() || !refusal.isEmpty();
	}
};
[[nodiscard]] ToastGoneWait ReadToastGoneWait(
	const ToastStateReading &previous,
	crl::time waited,
	crl::time bound);
[[nodiscard]] QString ToastGoneWaitDetails(const ToastGoneWait &wait);

// The toast-state readings measuring themselves on real Ui::Toasts of
// their own, parented to the primary window: a fresh toast reads unsettled
// and then shown once its show settles; right after hideAnimated() it reads
// hiding while the walk still counts it, then gone within the fade-out plus
// kToastGoneMargin; a default-duration toast left alone reads shown until
// its duration ends, then hiding, then gone; a toast never hidden ends the
// bounded wait in a named toast-not-gone refusal that names it; null and
// non-toast widgets read toast-unresolved. The first stage records the
// fixture gate - a primary window, and no other live toast the walk would
// count - and the stages after it are composed in Runner::addGated on that
// refusal; the three fade stages are N/A by name under anim::Disabled().
// Teardown is a Runner::addRecovery stage that hides every toast it
// showed. It needs no session, chats list, network or account fixture and
// emits no deliberate failure: every refusal is observed through the pure
// readings and asserted as a passing Check.
void AppendToastStateSelfTest(not_null<Runner*> runner);

// Painted-content readiness: the contract for a toast frame saved as
// visual proof of its text or link. ToastSubtreeReady's settled background
// does not establish painted children. While the shown level is below 1,
// Widget::paintEvent draws a proxy and calls disableChildrenPaintOnce(),
// which sets Qt::WA_UpdatesDisabled on the toast's direct child widgets -
// the Content wrapper holding the body Ui::FlatLabel and the icon - until
// a Ui::PostponeCall restore runs (toggleChildrenPaint() and
// scheduleChildrenPaintRestore() in toast_widget.cpp), and QWidget::render,
// which every toast-rooted grab goes through, skips a child whose updates
// are disabled (QWidgetPrivate::paintSiblingsRecursive). A full-level grab
// taken between the two holds the settled rounded background and no text,
// icon or link, while ReadToastText - model state - still matches the
// phrase. That signature is what this contract refuses.
//
// ReadToastContent resolves the root through ReadToastSubtree and takes
// exactly one grab, on which it measures the settled term and the content
// terms together. Content: every Ui::FlatLabel of the toast with a
// non-empty accessibilityName() holds at least kMinInkPixels pixels at
// least kInkDelta from the settled blend inside its rect, and each pen in
// |pens| has at least kMinInkPixels pixels within kSameTolerance inside the
// label rects (test_ink.h). The first failing term names the state:
// - content-refused: the root was refused; |reason| is ReadToastSubtree's
//   refusal, no grab is taken and |image| is null.
// - content-unsettled: the sampled background is not st::toastBg's settled
//   blend yet.
// - content-no-labels: no labelled Ui::FlatLabel, as in a custom-content
//   toast, which is unsupported and never qualifies.
// - content-not-painted: a label rect holds less content ink than the
//   floor.
// - content-pen-missing: a pen has fewer pixels than the floor.
// - content-qualified: every term holds; |reason| is "none".
// content-unread is a frame no reading filled. The icon is not qualified.
// |childrenPaintDisabled| counts the toast's direct child widgets with
// updatesEnabled() false just before the grab, -1 when none was taken: a
// diagnostic of the cause, never a term. |labels| are device-pixel rects
// in |image|, |labelInk| their content-ink counts and |penPixels| the
// counts of |pens|, all empty when nothing was measured.
//
// The wait is a pure reading of the caller's elapsed time: |waited| 0
// never expires; once |waited| >= |bound| a frame that is not qualified is
// |expired|, keeps its failing |state| and reads "content-wait-expired
// after <waited> ms (bound <bound> ms): <state> - <reason>". Poll it from
// |until| returning decided(), keep the frame, and assert qualified() in
// |then|, so a toast that never paints ends in that named diagnostic and
// never in a stage timeout. Draining postponed calls
// (Test::SettlePostponedCalls) runs lib_ui's restore.
//
// CaptureToastContent saves exactly |accepted.image| - the pixels the
// reading measured - and takes no second grab, so readiness and saving
// qualify the same image. It logs the toast's GEOMETRY row while the toast
// is alive and the SCREENSHOT row; a frame that is not qualified(), or one
// that cannot be written, is a loud Fail and nothing is saved. Reference:
// docs/helpers/test_toast_capture.md.
enum class ToastContentState {
	Unread,
	Refused,
	Unsettled,
	NoLabels,
	NotPainted,
	PenMissing,
	Qualified,
};
[[nodiscard]] QString ToastContentStateName(ToastContentState state);

inline constexpr auto kToastContentBound = crl::time(2000);

struct ToastContentFrame {
	ToastSubtreeReading reading;
	QImage image;
	int settledPermille = 0;
	bool settled = false;
	int childrenPaintDisabled = -1;
	std::vector<QRect> labels;
	std::vector<int> labelInk;
	std::vector<InkCandidate> pens;
	std::vector<int> penPixels;
	crl::time waited = 0;
	crl::time bound = 0;
	bool expired = false;
	ToastContentState state = ToastContentState::Unread;
	QString reason;

	[[nodiscard]] bool qualified() const {
		return state == ToastContentState::Qualified;
	}
	[[nodiscard]] bool decided() const {
		return qualified() || expired;
	}
};
[[nodiscard]] ToastContentFrame ReadToastContent(
	QWidget *widget,
	std::vector<InkCandidate> pens = {},
	crl::time waited = 0,
	crl::time bound = kToastContentBound);

// "toast content: target=<identity|<none>> frame=<rect> state=<name>
// settled=<n|none>/1000 childrenPaintDisabled=<n|none> labels=<n>
// labelInk=[...] floor=<kMinInkPixels> pens=[<name>=<n|none> ...]
// waited=<ms> bound=<ms> expired=<0|1> - <reason>".
[[nodiscard]] QString ToastContentDetails(const ToastContentFrame &frame);
bool CaptureToastContent(
	const ToastContentFrame &accepted,
	const QString &name);

// The painted-content contract measuring itself on real toasts shown
// through one Ui::Show of the primary window (Window::Controller::uiShow()),
// so every repeat is Ui::Show::showToast's own: it hides the previous toast
// animated and shows the next while the previous one still fades. Over a
// synthetic sentinel surface moved beneath each toast, a plain toast and
// three repeated rich toasts - bold words, a link and an action - are
// accepted with their text and link pens painted, checked again in each
// saved PNG reloaded pixel for pixel. The missing-paint signature is
// reproduced on the third rich toast through lib_ui's own lifecycle
// (setShownLevel(0.5), one toast-rooted grab, setShownLevel(1.)) - settled,
// read-back matching, no paint - and refused, while CaptureToastSubtree,
// the settled-only control, saves it; children kept without paint end the
// bounded wait in content-wait-expired, and null and ancestor roots refuse
// with no grab. The first stage records the fixture gate - a primary
// window, no other live toast, sentinel tones clear of the palette - and
// the stages after it are composed in Runner::addGated on that refusal;
// teardown is a Runner::addRecovery stage that hides every toast it
// showed. It calls no toast-state or glyph reading, needs no session,
// chats list, network or account fixture, and emits no deliberate failure.
void AppendToastContentCaptureSelfTest(not_null<Runner*> runner);

// This module measuring itself, in seven stages, over a synthetic
// sentinel-bearing surface it paints and a real Ui::Toast of its own.
//
// The surface is two horizontal bands of two high-contrast tones chosen so
// that nothing a toast-rooted frame can hold comes near either of them on
// any palette, and not merely on the one the tones were picked against.
// Such a frame carries st::windowBg, blends of st::toastBg over it and
// st::toastFg label ink, every one of which is a grey or within a few
// channel units of one, as is every antialiased mixture of them; and
// Test::ChannelDelta is the maximum absolute per-channel difference, so
// the nearest grey to a colour sits exactly half that colour's own channel
// span away. Both tones span nearly the whole range, which puts every grey
// at least 95 channel units from either and every near-grey at least about
// 92 - far outside the counter's 40-unit tolerance, whether the theme is
// light or dark. Stage 1 measures that instead of trusting it: a named
// fixture gate reports the separation from st::windowBg, from the settled
// toast blend and from st::toastFg, and FAILs if the smallest of them
// drops below twice the tolerance, so a palette that ever breaks the
// argument says so in the log rather than flipping the counter's verdict
// on a correct frame. The two tones also keep a window-mapped crop of the
// same rect clear of the blank threshold without resting on the toast's
// own paint - their lightness is 160 against 96 - which is the reason
// test_via_window.cpp's comment on PaintTwoTone gives for using two. No
// account fixture secret, wallet phrase, password or hint is read, painted
// or named anywhere: the only text the self-test paints is its own literal.
//
// Stage 1 reads the fade-in window in the one turn it exists, where the
// shown level is still 0, and shows the settled-show term answering false
// there and the capture declining with a Note that moves no failure count.
// Stage 2 waits on that same term as a real poll and then decides the
// module's claim twice: once by counting sentinel pixels in the two saved
// PNGs of the same toast in the same turn, and once - strictly stronger
// and independent of the corners - by hiding the surface beneath and
// showing that no pixel of the subtree frame moved while many pixels of
// the window-mapped frame did. It also reads the toast's phrase back and
// shows the same comparison declining a different phrase. Stage 3 shows a
// second toast beside the fixture and takes it down inside the same turn,
// where the walk answers two and FindLiveToast() refuses the ambiguity
// with nullptr; the fixture is shown single again there and a tick later.
// Stages 4 and 5 show a non-infinite toast with kDefaultDuration, wait on
// ToastSubtreeReady inside its lifetime, then wait until the product's
// own hide path has taken it out of the walk - quoting the live count,
// the texts and the elapsed time - so the fixture is single again before
// Stage 6 reads. Stage 6 refuses a null, an ancestor that paints the
// product and a rect larger than the toast, each by name and quoting both
// rects. Stage 7 is teardown, and it is where the other refusal is read:
// the control walk answers the fixture toast alone, this module's own
// Instance::hide() takes it down, and the same walk then answers an empty
// list that FindLiveToast() refuses with nullptr.
//
// It needs no session, no chats list, no network and no account fixture.
// The only thing it asks of the process is a primary window to parent the
// fixture to, and a missing one is reported as a named fixture gate
// instead of crashing. It appends its own teardown last, and it emits no
// deliberate failure: every refusal it demonstrates is observed through
// the pure FindLiveToasts, FindLiveToast, ReadToastSubtree,
// ToastSubtreeReady and ToastSubtreeDetails readings, which log nothing,
// and asserted as a passing Check whose details carry the refusal
// verbatim. test_text_reads.h's comment on AppendTextReadsSelfTest records
// that module as the harness's one self-test that emits deliberate
// failures, and this one does not become a second.
void AppendToastSubtreeCaptureSelfTest(not_null<Runner*> runner);

} // namespace Test
