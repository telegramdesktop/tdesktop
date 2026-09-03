/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_runner.h"

#include <QtCore/QPointer>
#include <QtGui/QImage>
#include <QtWidgets/QWidget>

#include <vector>

namespace Test {

// The one capture for a toast, and the only one whose saved frame cannot
// hold the product surface the toast was drawn over.
//
// Test::CaptureViaWindow grabs the toast's window cropped to the toast's
// rect mapped into it. That is the right answer to blankness and the wrong
// answer to hygiene. st::toastBg is #2c3033e5 (ui/colors.palette:448) -
// alpha 0xE5, about 90% opaque - so roughly a tenth of every pixel the
// product painted underneath composes into the saved frame, legibly enough
// to read text through it. The rect's corners are worse than that:
// Widget::paintBackground is _roundRect.paint(p, rect())
// (ui/toast/toast_widget.cpp:525-528), which fills the straight bands and
// draws four corner images (ui/round_rect.cpp:65-125) whose mask is filled
// with Qt::transparent and carries only a rounded rect
// (ui/image/image_prepare.cpp:75-98), so outside the arc the toast paints
// nothing at all and whatever is behind it shows there unblended.
//
// Test::GrabWidget and Test::GrabRect call Ui::GrabWidgetToImage(widget,
// rect, st::windowBg->c) (test_capture.cpp:236-247), which fills the result
// with the harness theme base and then renders only that target's own
// subtree (ui/ui_utility.cpp:133-151). A grab rooted at the toast is
// therefore hygienic by construction: every pixel the translucent toast
// leaves uncovered measures st::windowBg and never the product. That is
// the whole mechanism this module rests on, and it is why a root that is
// not the live toast is refused here rather than reframed.
//
// Ui::Toast::internal::Widget::_shownLevel is private (toast_widget.h:40),
// and while it is below 1 paintEvent draws the whole frame into a
// transparent proxy at that opacity and returns (toast_widget.cpp:585-600)
// - which is exactly where a toast-rooted grab reads blank. So settledness
// here is measured and not read: the settled background blend is derived
// from the live palette and one grab is sampled against it.
//
// Ui::Toast::internal::Manager exposes only instance() and addToast() and
// keeps its _toastByWidget map private (ui/toast/toast_manager.h:27-29,
// :42), so no API enumerates live toasts and the honest resolution is the
// harness's own visible-widget walk over the top level widgets.
//
// This is its own module rather than part of test_capture.h because it
// appends Runner stages and test_runner.cpp already includes
// test_capture.h: the reverse include would invert the harness's layering
// and make its most-included module runner-aware, which is the rule
// test_menu.h:57-63 states. It is not part of test_via_window.* either,
// because that module is the self-test of CaptureViaWindow and is this
// module's own unchanged-scenario control.

// |toast| is non-null exactly when |refusal| is empty at the moment the
// reading is taken: a caller cannot take the pointer without being handed
// the reason there is none. It is a QPointer<QWidget>, so a retained
// reading can only go from resolved to unresolved and never hands back a
// pointer into freed memory, while |frame|, |identity| and |refusal| are
// values that stay printable after the toast is gone.
// Test::WindowMappedCapture (test_capture.h:199-208) and
// Test::PaintingLayerRootResult (:142-149) carry the same type and the
// same contract.
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
// than one - an ambiguous answer is refused rather than guessed.
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
// test_capture.h:185-187 promises for the window-mapped family. A refused
// request answers a null image and never a reframed one, so a poll around
// a refused target ends in a named stage timeout rather than in a grab of
// something else. ToastSubtreeReady is the settled-show term: it is true
// once the sampled frame matches st::toastBg's own settled blend over the
// harness base, which no mid-fade frame does.
[[nodiscard]] QImage GrabToastSubtree(
	QWidget *widget,
	QRect requested = QRect());
[[nodiscard]] bool ToastSubtreeReady(QWidget *widget);
[[nodiscard]] QString ToastSubtreeDetails(QWidget *widget);

// Loud on a structural refusal, because no amount of waiting repairs one:
// no widget, a root that is not the live toast, an invisible or empty
// target, or a rect outside the toast's geometry. A Note and never a FAIL
// on an unsettled or blank frame, because the decisive oracle for a toast
// is textual and this capture corroborates it.
bool CaptureToastSubtree(
	not_null<QWidget*> widget,
	const QString &name,
	QRect requested = QRect());

// The joined accessibilityName() of the toast's Ui::FlatLabels - the read
// the harness README already calls decisive - and its comparison, which
// goes through the shipped Test::CheckTextReads (test_text_reads.h:31-34)
// so that the space-class normalization and the both-verdicts printing are
// composed rather than re-derived. A widget that is not a live toast reads
// back an empty string.
[[nodiscard]] QString ReadToastText(QWidget *widget);
void CheckToastReads(
	QWidget *widget,
	const QString &expected,
	const QString &what);

// This module measuring itself, in four stages, over a synthetic
// sentinel-bearing surface it paints and a real Ui::Toast of its own.
//
// The surface is two horizontal bands of two high-contrast tones chosen so
// that nothing a toast-rooted frame can hold comes near either of them:
// such a frame carries st::windowBg, blends of st::toastBg over it and
// white st::toastFg label ink, all of which lie on or beside the segment
// between those two colours, while each sentinel tone is at least 72
// channel units from every point of it. The two tones also keep a
// window-mapped crop of the same rect clear of the blank threshold without
// resting on the toast's own paint, which is the reason
// test_via_window.cpp:65-68 gives for using two. No account fixture
// secret, wallet phrase, password or hint is read, painted or named
// anywhere: the only text the self-test paints is its own literal.
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
// shows the same comparison declining a different phrase. Stage 3 refuses
// a null, an ancestor that paints the product and a rect larger than the
// toast, each by name and quoting both rects. Stage 4 is teardown.
//
// It needs no session, no chats list, no network and no account fixture.
// The only thing it asks of the process is a primary window to parent the
// fixture to, and a missing one is reported as a named fixture gate
// instead of crashing. It appends its own teardown last, and it emits no
// deliberate failure: every refusal it demonstrates is observed through
// the pure ReadToastSubtree, ToastSubtreeReady and ToastSubtreeDetails
// readings, which log nothing, and asserted as a passing Check whose
// details carry the refusal verbatim. test_text_reads.h:70-78 records that
// module as the harness's one self-test that emits deliberate failures,
// and this one does not become a second.
void AppendToastSubtreeCaptureSelfTest(not_null<Runner*> runner);

} // namespace Test
