/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "test/test_widget_ops.h"

#include <QtCore/QJsonObject>
#include <QtCore/QStringList>

#include <memory>
#include <vector>

// The widget conformance runner's requested pictures: for a visual run of
// an entry the caller asked a picture of, its whole card as the chat list
// paints it, at 2x, in two bands of at most 300 CSS px from the card's top
// (the list's viewport is shorter than the tallest such cards), each band
// shown and captured as the stage snapshot is, joined only to bands of the
// same host at the same card size. The runner judges no picture: the
// bands are the caller's evidence. It also holds the floating-date wait
// that the stage snapshot and every band take between their show and
// their capture. Internal to test_widget_conformance.cpp; reference:
// docs/helpers/test_widget_conformance.md.

namespace Test {
class Runner;
struct ChatListShow;
struct ConformanceRefusal;
} // namespace Test

namespace Test::Conformance {

struct ConformanceRun;
struct ConformanceState;
struct DateWaitReading;

// Right after a list-paint capture's show, the stage |name|, skipped by
// |skipReason| (the show's own): HistoryInner shows its floating date over
// the viewport's top when a scroll moves the list up, or down into another
// day, and hides it with a fade a timeout later (1 s or 3 s), so the
// capture's frame is accepted only once the owning Ui::ElasticScroll of
// the show's list read the show's accepted scrollTop unchanged for the
// longer timeout, the fade and a margin (3.35 s), sampled on every runner
// tick from the stage's run (any earlier scroll is older). It reads only:
// no input, hover or scroll. A list or scroll gone, or the bound (8 s)
// first, ends it unaccepted; a show that showed nothing ends it at once.
// Returns its reading for the capture stage.
[[nodiscard]] std::shared_ptr<const DateWaitReading> AppendDateWait(
	not_null<Runner*> runner,
	const QString &name,
	Fn<QString()> skipReason,
	std::shared_ptr<const ChatListShow> shown);

// The capture's refusal before its frame, checked after the revalidation:
// none once |reading|'s wait was accepted, else `date-not-hidden` with
// why (no wait, a wait that did not end, a lost scroll or the bound).
[[nodiscard]] ConformanceRefusal DateWaitRefusal(
	const DateWaitReading *reading);

// The `dateWait` field of a snapshot or band: {"finished": false} until
// the wait ended, then whether it was accepted and its readings.
[[nodiscard]] QJsonObject DateWaitObject(const DateWaitReading *reading);

// After SelectRuns(): every run of a visual entry named in args.pictures
// gets a requested picture with its two bands, and every named id that is
// no visual entry of the manifest goes into |state.unknownPictures|, once.
// A named visual entry this launch runs in no theme gets nothing here, a
// note in PictureNotes(). Does nothing when the package was refused.
void SelectPictures(ConformanceState &state);

// Before the run's log and removal, for a run with a requested picture:
// per band, the show of its CSS rows on the current card (no hover,
// WidgetCardReady() accepted), AppendDateWait() and the capture stage,
// which revalidates the accepted card, refuses an unaccepted date wait,
// captures the shown rect of the list with PreparedWidgetCapture, requires
// exactly 2x the band's CSS size and saves
// pictures/<FileBase(run)>.<band>.png. Band 0 is the first capture: its
// card's CSS size, core, host generation and published stages are kept,
// and band 1 is placed on that size, taken only from the same core and
// generation at the same size, or refused. A band the card ends above is
// not needed: its show is skipped and it is no missing capture. Each
// capture stage Notes a CONFORMANCE_PICTURE row. Appends nothing for any
// other run.
void AppendPictureBands(
	not_null<Runner*> runner,
	const std::shared_ptr<ConformanceState> &state,
	int index);

// The run's `picture` extra into its run file |entry|, completed or not,
// for a run with a requested picture; nothing for any other run.
void InsertPictureExtra(QJsonObject &entry, const ConformanceRun &run);

// The run file's notes on the pictures, none when none was asked for: how
// the bands are taken and that they are never judged, and each named
// visual entry this launch runs in no theme.
[[nodiscard]] QStringList PictureNotes(const ConformanceState &state);

// The totals row's `pictures`: the selected runs with a requested picture,
// the complete ones, and what every other one misses.
[[nodiscard]] QJsonObject PictureTotals(const ConformanceState &state);

// The totals stage only, when any picture was asked for: one Check that
// every selected run with a requested picture has every band its card
// needs, together covering the card's rows from the same host.
void CheckPictures(const ConformanceState &state);

// Pure controls on literal states: SelectPictures() and PictureNotes() of
// requested visual, auto, unknown, unselected and no ids; the coverage of
// pictures of several card heights, with a band missing, shifted or not
// needed and nothing captured; the joining of a later band of another
// core, host generation, card size or after a missing band; and the date
// wait's decision over literal scroll readings and times. Row names carry
// no prefix.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> RunPictureCheck();

} // namespace Test::Conformance
