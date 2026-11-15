/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_widget_conformance_pictures.h"

#include "test/test_capture.h"
#include "test/test_chat_show.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widget_conformance_state.h"
#include "test/test_widget_fixture.h"
#include "test/test_widget_ops.h"
#include "ui/style/style_core_scale.h"
#include "ui/widgets/elastic_scroll.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtCore/QPoint>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtGui/QImage>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace Test::Conformance {
namespace {

// The rows of each band, in CSS px: one band fits the chat list's 500
// logical px viewport at interface scale 100, where the tallest corrected
// pictures do not, and two hold a card of up to 600 CSS px.
constexpr auto kBandCss = 300;
constexpr auto kBands = 2;

// HistoryInner's floating date after a scroll (history_inner_widget.cpp):
// hidden by a timer kScrollDateHideTimeout after an upward scroll, or
// kScrollDateHideOnDayCrossingTimeout after a downward one that moved the
// top message into another day, then faded out over
// st::historyDateFadeDuration (ui/chat/chat.style). One window covers
// both; the margin covers the coarse timer's slack and the fade's last
// frame.
constexpr auto kScrollDateHideTimeout = crl::time(1000);
constexpr auto kScrollDateHideOnDayCrossingTimeout = crl::time(3000);
constexpr auto kScrollDateFadeDuration = crl::time(200);
constexpr auto kScrollDateMargin = crl::time(150);
constexpr auto kScrollDateHideAndFade = std::max(
		kScrollDateHideTimeout,
		kScrollDateHideOnDayCrossingTimeout)
	+ kScrollDateFadeDuration
	+ kScrollDateMargin;
// The date wait's own bound, from its run: room for the scroll to settle
// again before the capture is refused.
constexpr auto kScrollDateBound = crl::time(8000);

// The requested pictures of the selected runs, and what each incomplete
// one misses.
struct PictureCount {
	QStringList missing;
	int requested = 0;
	int complete = 0;
};

[[nodiscard]] QJsonArray Strings(const QStringList &list) {
	auto result = QJsonArray();
	for (const auto &entry : list) {
		result.push_back(entry);
	}
	return result;
}

[[nodiscard]] QString CoreText(quintptr core) {
	return core
		? (u"0x"_q + QString::number(quint64(core), 16))
		: u"none"_q;
}

[[nodiscard]] QJsonValue SizeArray(QSize size) {
	return size.isEmpty()
		? QJsonValue(QJsonValue::Null)
		: QJsonValue(QJsonArray{ size.width(), size.height() });
}

// The CSS size of a card |card| logical px big, rounded down: whole only
// when LogicalFromCss() of it gives |card|'s size back.
[[nodiscard]] QSize CardCss(QRect card) {
	const auto scale = style::Scale();
	return QSize(card.width() * 100 / scale, card.height() * 100 / scale);
}

// Band |index| of a card |card| CSS px big: its rows from index * kBandCss,
// at most kBandCss of them, at the card's whole width; empty when the card
// ends above them.
[[nodiscard]] QRect BandRect(QSize card, int index) {
	const auto top = index * kBandCss;
	return (top < card.height())
		? QRect(0, top, card.width(), std::min(kBandCss, card.height() - top))
		: QRect();
}

// Band |index| of a card |css| CSS px big on the card |card| of a view, in
// the view's coordinates; empty when either is.
[[nodiscard]] QRect BandLocal(QRect card, QSize css, int index) {
	const auto band = BandRect(css, index);
	return (card.isEmpty() || band.isEmpty())
		? QRect()
		: LogicalFromCss(band).translated(card.topLeft());
}

// Whether band |index| is needed: the first always, a later one by the
// first capture's card size, unknown without a first capture.
[[nodiscard]] std::optional<bool> BandNeeded(
		const PictureReading &picture,
		int index) {
	if (!index) {
		return true;
	} else if (picture.card.isEmpty()) {
		return std::nullopt;
	}
	return !BandRect(picture.card, index).isEmpty();
}

[[nodiscard]] QString UnplacedText(int index) {
	return u"band %1 was not captured, so band %2 has no measured card to "
		u"be placed on"_q.arg(NumberText(index - 1), NumberText(index));
}

[[nodiscard]] QString NotNeededText(
		const PictureReading &picture,
		int index) {
	return u"not needed: the card is %1 CSS px tall and ends above band %2's "
		u"first row, %3"_q.arg(
			NumberText(picture.card.height()),
			NumberText(index),
			NumberText(index * kBandCss));
}

// Why band |index|, accepted on a card |css| CSS px big of core |core| at
// host |generation|, cannot join the bands before it: another core, host
// generation or card size than the first capture's, or rows that do not
// continue the band before it. Empty for the first band and one that
// joins.
[[nodiscard]] QString JoinRefusal(
		const PictureReading &picture,
		int index,
		QSize css,
		quintptr core,
		int generation) {
	if (!index) {
		return QString();
	}
	const auto &previous = picture.bands[index - 1];
	const auto rows = BandRect(picture.card, index);
	if ((core != picture.core) || (generation != picture.generation)) {
		return u"band %1 is of core %2 at host generation %3, the first "
			u"capture of core %4 at generation %5: bands of different hosts "
			u"are never joined"_q.arg(
				NumberText(index),
				CoreText(core),
				NumberText(generation),
				CoreText(picture.core),
				NumberText(picture.generation));
	} else if (css != picture.card) {
		return u"band %1's card is %2x%3 CSS px, the first capture's "
			u"%4x%5"_q.arg(
				NumberText(index),
				NumberText(css.width()),
				NumberText(css.height()),
				NumberText(picture.card.width()),
				NumberText(picture.card.height()));
	} else if (previous.file.isEmpty()
		|| rows.isEmpty()
		|| (previous.css.y() + previous.css.height() != rows.y())) {
		return u"band %1's rows %2 do not continue band %3's captured rows "
			u"%4"_q.arg(
				NumberText(index),
				RectText(rows),
				NumberText(index - 1),
				previous.file.isEmpty()
					? u"(none)"_q
					: RectText(previous.css));
	}
	return QString();
}

// What keeps |picture| from covering its card: every band it needs
// without a file, a captured band whose rows are not the ones the first
// capture's card size gives it, and rows below the last band. Empty for
// a complete picture and for one not requested.
[[nodiscard]] QStringList PictureGaps(const PictureReading &picture) {
	auto result = QStringList();
	if (!picture.requested) {
		return result;
	}
	for (auto i = 0; i != int(picture.bands.size()); ++i) {
		const auto &band = picture.bands[i];
		const auto needed = BandNeeded(picture, i);
		const auto rows = BandRect(picture.card, i);
		if (needed && !*needed) {
			continue;
		} else if (band.file.isEmpty()) {
			result.push_back(u"band %1 %2%3"_q.arg(
				NumberText(i),
				band.refusal,
				band.detail.isEmpty()
					? QString()
					: (u": "_q + band.detail)));
		} else if (band.css != rows) {
			result.push_back(u"band %1 holds the rows %2, not %3 of the %4x%5 "
				u"CSS px card"_q.arg(
					NumberText(i),
					RectText(band.css),
					RectText(rows),
					NumberText(picture.card.width()),
					NumberText(picture.card.height())));
		}
	}
	const auto covered = int(picture.bands.size()) * kBandCss;
	if (picture.card.height() > covered) {
		result.push_back(u"the card's rows %1 to %2 are below the last "
			u"band"_q.arg(
				NumberText(covered),
				NumberText(picture.card.height())));
	}
	return result;
}

[[nodiscard]] QJsonObject BandObject(
		const PictureReading &picture,
		int index) {
	const auto &band = picture.bands[index];
	const auto needed = BandNeeded(picture, index);
	if (needed && !*needed) {
		return QJsonObject{
			{ u"band"_q, index },
			{ u"needed"_q, false },
		};
	}
	return QJsonObject{
		{ u"band"_q, index },
		{
			u"needed"_q,
			needed ? QJsonValue(true) : QJsonValue(QJsonValue::Null),
		},
		{ u"css"_q, RectArray(band.css) },
		{ u"list"_q, RectArray(band.list) },
		{ u"card"_q, RectArray(band.card.card) },
		{ u"core"_q, CoreText(band.card.core) },
		{ u"generation"_q, band.generation },
		{
			u"image"_q,
			QJsonArray{ band.image.width(), band.image.height() },
		},
		{ u"file"_q, band.file },
		{ u"refusal"_q, band.refusal },
		{ u"detail"_q, band.detail },
		{ u"show"_q, ShowObject(band.show.get()) },
		{ u"dateWait"_q, DateWaitObject(band.date.get()) },
	};
}

[[nodiscard]] QJsonObject PictureObject(const PictureReading &picture) {
	auto bands = QJsonArray();
	for (auto i = 0; i != int(picture.bands.size()); ++i) {
		bands.push_back(BandObject(picture, i));
	}
	const auto missing = PictureGaps(picture);
	return QJsonObject{
		{ u"bandCss"_q, kBandCss },
		{ u"card"_q, SizeArray(picture.card) },
		{ u"core"_q, CoreText(picture.core) },
		{ u"generation"_q, picture.generation },
		{ u"stages"_q, picture.stages },
		{ u"devicePixelRatio"_q, picture.ratio },
		{ u"interfaceScale"_q, picture.scale },
		{ u"complete"_q, missing.isEmpty() },
		{ u"missing"_q, Strings(missing) },
		{ u"bands"_q, bands },
	};
}

// ---------------------------------------------------------------------
// The floating-date wait before a capture.

// The readings so far, for the bound's refusal and the stage's timeout.
[[nodiscard]] QString DateWaitText(const DateWaitReading &reading) {
	return u"the show's scrollTop=%1, last scrollTop=%2 held %3 ms of the "
		u"%4 ms needed, %5 changes, %6 ms waited"_q.arg(
			NumberText(reading.shownTop),
			NumberText(reading.top),
			NumberText(reading.stillMs),
			NumberText(kScrollDateHideAndFade),
			NumberText(reading.changes),
			NumberText(reading.waitedMs));
}

// From the stage's run at |now|: the show's accepted scroll is the first
// reading, still since |now|, as every earlier scroll is older. A show
// that showed nothing has nothing to wait out.
void StartDateWait(
		DateWaitReading &reading,
		const ChatListShow *shown,
		crl::time now) {
	reading = DateWaitReading{ .started = now, .stillSince = now };
	if (!shown || !shown->shown()) {
		reading.detail = u"the show showed nothing, so there was no capture "
			u"to wait for"_q;
		reading.finished = true;
		return;
	}
	reading.shownTop = shown->scrollTop;
	reading.top = shown->scrollTop;
}

// The owning Ui::ElasticScroll of the show's list, read only.
[[nodiscard]] std::optional<int> ShownScrollTop(const ChatListShow &shown) {
	const auto scroll = FixtureListScroll(shown.list.data());
	return scroll ? std::make_optional(scroll->scrollTop()) : std::nullopt;
}

// One reading |top| at |now|, none without the list or its scroll: true
// once decided, accepted when the show's scroll top held for
// kScrollDateHideAndFade, refused when the list is gone or at the bound.
[[nodiscard]] bool ObserveDateWait(
		DateWaitReading &reading,
		std::optional<int> top,
		crl::time now) {
	reading.waitedMs = now - reading.started;
	if (!top) {
		reading.detail = u"the show's list or its owning scroll is gone, "
			u"after %1 ms"_q.arg(NumberText(reading.waitedMs));
		reading.finished = true;
		return true;
	} else if (*top != reading.top) {
		reading.top = *top;
		reading.stillSince = now;
		++reading.changes;
	}
	reading.stillMs = now - reading.stillSince;
	if ((reading.top == reading.shownTop)
		&& (reading.stillMs >= kScrollDateHideAndFade)) {
		reading.detail = QString();
		reading.hidden = true;
		reading.finished = true;
		return true;
	} else if (reading.waitedMs >= kScrollDateBound) {
		reading.detail = u"the list's scroll did not hold the show's scroll "
			u"top for %1 ms within %2 ms, so HistoryInner's floating date "
			u"may still cover the viewport's top: %3"_q.arg(
				NumberText(kScrollDateHideAndFade),
				NumberText(kScrollDateBound),
				DateWaitText(reading));
		reading.finished = true;
		return true;
	}
	return false;
}

[[nodiscard]] PictureCount CountPictures(const ConformanceState &state) {
	auto result = PictureCount();
	for (const auto &run : state.runs) {
		if (!run.picture.requested) {
			continue;
		}
		++result.requested;
		const auto gaps = PictureGaps(run.picture);
		if (gaps.isEmpty()) {
			++result.complete;
		} else {
			result.missing.push_back(run.key + u": "_q + gaps.join(u"; "_q));
		}
	}
	return result;
}

// Why band |index|'s show does not run: the run's skip, why no capture of
// it can be taken at all, or, for a later band, no first capture to place
// it on or a card that ends above it.
[[nodiscard]] QString BandSkip(
		const ConformanceState &state,
		const ConformanceRun &run,
		int index) {
	const auto skip = RunSkip(state, run);
	if (!skip.isEmpty()) {
		return skip;
	}
	const auto block = SnapshotBlock(run);
	const auto needed = BandNeeded(run.picture, index);
	return !block.name.isEmpty()
		? (block.name + u": "_q + block.detail)
		: !needed
		? UnplacedText(index)
		: !*needed
		? NotNeededText(run.picture, index)
		: QString();
}

void RefuseBand(
		PictureBand &band,
		const QString &name,
		const QString &detail) {
	band.refusal = name;
	band.detail = detail;
}

// In the stage after band |index|'s show and its date wait: nothing for a
// band the card ends above; else the view, core, card and geometry the
// show accepted, the band joining the earlier ones, the accepted date
// wait, one accepted capture of the shown rect from the list's own paint,
// its 2x checked on the image, then its file. The first capture keeps the
// card's CSS size, its core and host generation and the stage rects the
// widget published.
void TakeBand(
		const ConformanceState &state,
		ConformanceRun &run,
		int index) {
	auto &picture = run.picture;
	auto &band = picture.bands[index];
	picture.ratio = style::DevicePixelRatio();
	picture.scale = style::Scale();
	const auto block = SnapshotBlock(run);
	const auto needed = BandNeeded(picture, index);
	if (!block.name.isEmpty()) {
		RefuseBand(band, block.name, block.detail);
		return;
	} else if (!needed) {
		RefuseBand(band, u"not-captured"_q, UnplacedText(index));
		return;
	} else if (!*needed) {
		RefuseBand(band, QString(), QString());
		return;
	}
	const auto shown = band.show.get();
	if (!shown || !shown->shown()) {
		RefuseBand(
			band,
			u"show-refused"_q,
			(shown && shown->finished)
				? (ChatListShowRefusalName(shown->refusal)
					+ u" - "_q
					+ shown->reason)
				: u"the band's show did not run"_q);
		return;
	}
	// The band's show hovers nothing on purpose; only a press needs one.
	auto unhovered = *shown;
	unhovered.hovered = true;
	const auto changed = RevalidateWidgetCard(
		*state.fixture,
		run.message->id,
		u"picture band "_q + NumberText(index),
		unhovered,
		band.card);
	const auto &card = band.card.card;
	const auto css = CardCss(card);
	const auto size = index ? picture.card : css;
	const auto local = BandLocal(card, size, index);
	band.css = BandRect(size, index);
	band.generation = run.message->tap
		? ReadWidgetOpsLatest(*run.message->tap).generation
		: 0;
	const auto joined = JoinRefusal(
		picture,
		index,
		css,
		band.card.core,
		band.generation);
	const auto refusal = !changed.isEmpty()
		? changed
		: (LogicalFromCss(QRect(QPoint(), css)).size() != card.size())
		? u"the card's %1x%2 logical px are no whole number of CSS px at "
			u"interface scale %3%"_q.arg(
				NumberText(card.width()),
				NumberText(card.height()),
				NumberText(style::Scale()))
		: !joined.isEmpty()
		? joined
		: (shown->local != local)
		? u"the show showed %1 of the view, not band %2's %3 on the accepted "
			u"card %4"_q.arg(
				RectText(shown->local),
				NumberText(index),
				RectText(local),
				RectText(card))
		: QString();
	if (!refusal.isEmpty()) {
		RefuseBand(band, u"capture-refused"_q, refusal);
		return;
	}
	const auto date = DateWaitRefusal(band.date.get());
	if (!date.name.isEmpty()) {
		RefuseBand(band, date.name, date.detail);
		return;
	}
	const auto list = WidgetFixtureList(*state.fixture);
	auto capture = PreparedWidgetCapture();
	if (!capture.prepare(list, list, shown->rect)) {
		RefuseBand(band, u"capture-refused"_q, capture.pendingReason());
		return;
	}
	const auto &image = capture.image();
	band.list = shown->rect;
	band.image = image.size();
	const auto expected = band.css.size() * kCropScale;
	if (image.size() != expected) {
		RefuseBand(
			band,
			u"not-2x"_q,
			u"the accepted %1x%2 image of the %3x%4 CSS px band is not "
				u"%5x%6"_q.arg(
					NumberText(image.width()),
					NumberText(image.height()),
					NumberText(band.css.width()),
					NumberText(band.css.height()),
					NumberText(expected.width()),
					NumberText(expected.height())));
		return;
	}
	const auto file = u"pictures/%1.%2.png"_q.arg(
		FileBase(run),
		NumberText(index));
	const auto error = SavePng(state, image, file);
	if (!error.isEmpty()) {
		RefuseBand(band, u"write-failed"_q, error);
		return;
	}
	band.file = file;
	RefuseBand(band, QString(), QString());
	if (!index) {
		picture.card = css;
		picture.core = band.card.core;
		picture.generation = band.generation;
		RefreshResult(run);
		const auto stages = run.result.value.value(u"stages"_q);
		picture.stages = stages.isUndefined()
			? QJsonValue(QJsonValue::Null)
			: stages;
	}
}

void RecordBand(
		const ConformanceState &state,
		ConformanceRun &run,
		int index) {
	TakeBand(state, run, index);
	const auto &picture = run.picture;
	const auto &band = picture.bands[index];
	const auto name = u"picture band "_q + NumberText(index);
	run.progress = !band.file.isEmpty()
		? (name + u" taken"_q)
		: band.refusal.isEmpty()
		? (name + u" not needed"_q)
		: (name + u" refused: "_q + band.refusal);
	auto row = BandObject(picture, index);
	row.insert(u"run"_q, run.key);
	row.insert(u"pictureCard"_q, SizeArray(picture.card));
	row.insert(u"pictureCore"_q, CoreText(picture.core));
	row.insert(u"pictureGeneration"_q, picture.generation);
	Row(u"CONFORMANCE_PICTURE"_q, row);
}

// ---------------------------------------------------------------------
// Self-checks.

[[nodiscard]] WidgetOpsSelfCheckRow CheckRow(
		bool passed,
		const QString &name,
		const QString &details) {
	return {
		.name = u"picture self-check: "_q + name,
		.details = details,
		.passed = passed,
	};
}

// A requested picture of a |card| CSS px big card of core 1 at host
// generation 1 whose first |captured| bands are saved, at their rows.
[[nodiscard]] PictureReading CapturedControl(QSize card, int captured) {
	auto result = PictureReading{
		.card = card,
		.core = 1,
		.generation = 1,
		.requested = true,
	};
	result.bands.resize(kBands);
	for (auto i = 0; i != captured; ++i) {
		auto &band = result.bands[i];
		band.css = BandRect(card, i);
		band.file = u"pictures/visual_x.light.%1.png"_q.arg(NumberText(i));
		band.refusal = QString();
	}
	return result;
}

// Two visual entries and an auto one, with a run of the first visual and
// of the auto one, |pictures| asked for.
void FillSelectionControl(
		ConformanceState &state,
		const QStringList &pictures) {
	state.args.pictures = pictures;
	for (const auto &[id, set] : {
			std::pair{ u"visual/a"_q, u"visual"_q },
			std::pair{ u"visual/b"_q, u"visual"_q },
			std::pair{ u"auto/c"_q, u"auto"_q } }) {
		state.tests.push_back(ManifestTest{ .id = id, .set = set });
	}
	for (const auto &[id, set] : {
			std::pair{ u"visual/a"_q, u"visual"_q },
			std::pair{ u"auto/c"_q, u"auto"_q } }) {
		state.runs.push_back(ConformanceRun{
			.id = id,
			.set = set,
			.theme = u"light"_q,
			.key = id + u"|light|390"_q,
		});
	}
}

// A requested visual entry with a run gets a picture of two bands; an
// auto id and an id of no entry are unknown, each once; a visual entry
// without a run gets a note and nothing else; no pictures asks for none.
[[nodiscard]] WidgetOpsSelfCheckRow CheckPictureSelection() {
	auto requested = ConformanceState();
	FillSelectionControl(requested, {
		u"visual/a"_q,
		u"auto/c"_q,
		u"visual/b"_q,
		u"visual/zz"_q,
		u"visual/zz"_q,
		u"visual/b"_q,
	});
	SelectPictures(requested);
	const auto notes = PictureNotes(requested);
	auto none = ConformanceState();
	FillSelectionControl(none, {});
	SelectPictures(none);
	const auto &marked = requested.runs[0].picture;
	const auto passed = (requested.unknownPictures
			== QStringList{ u"auto/c"_q, u"visual/zz"_q })
		&& marked.requested
		&& (marked.bands.size() == kBands)
		&& !requested.runs[1].picture.requested
		&& (notes.size() == 2)
		&& notes[0].startsWith(
			u"pictures: runs with a picture in this launch: 1, of "
			u"[visual/a]."_q)
		&& notes[1].startsWith(u"visual/b: a picture was asked for"_q)
		&& PictureNotes(none).isEmpty()
		&& none.unknownPictures.isEmpty()
		&& !none.runs[0].picture.requested
		&& none.runs[0].picture.bands.empty();
	return CheckRow(
		passed,
		u"a requested visual entry's run gets a picture of two bands; an "
			u"auto id and one of no entry are unknown, each once; a visual "
			u"entry this launch does not run gets a note only; no pictures "
			u"schedules none"_q,
		CompactJson(QJsonObject{
			{ u"unknown"_q, Strings(requested.unknownPictures) },
			{ u"marked"_q, marked.requested },
			{ u"bands"_q, int(marked.bands.size()) },
			{ u"notes"_q, Strings(notes) },
		}));
}

// The bands of a 544 CSS px card, and the gaps of pictures of 336, 280 and
// 650 CSS px cards, one with band 1 refused, one with band 1 shifted, one
// with nothing captured and one not requested.
[[nodiscard]] WidgetOpsSelfCheckRow CheckCoverage() {
	const auto full = CapturedControl(QSize(390, 336), 2);
	const auto low = CapturedControl(QSize(390, 280), 1);
	auto refused = CapturedControl(QSize(390, 336), 1);
	RefuseBand(refused.bands[1], u"show-refused"_q, u"cannot-fit - x"_q);
	auto shifted = full;
	shifted.bands[1].css = QRect(0, 296, 390, 40);
	const auto tall = CapturedControl(QSize(390, 650), 2);
	auto nothing = PictureReading{ .requested = true };
	nothing.bands.resize(kBands);
	const auto gaps = [](const PictureReading &picture) {
		return PictureGaps(picture);
	};
	const auto first = [&](const PictureReading &picture) {
		const auto list = gaps(picture);
		return (list.size() == 1) ? list.front() : QString();
	};
	const auto geometry = (BandRect(QSize(390, 544), 0)
			== QRect(0, 0, 390, 300))
		&& (BandRect(QSize(390, 544), 1) == QRect(0, 300, 390, 244))
		&& BandRect(QSize(390, 300), 1).isEmpty();
	const auto passed = geometry
		&& gaps(full).isEmpty()
		&& (BandObject(full, 1).value(u"needed"_q) == QJsonValue(true))
		&& (PictureObject(full).value(u"complete"_q) == QJsonValue(true))
		&& gaps(low).isEmpty()
		&& (BandObject(low, 1)
			== QJsonObject{ { u"band"_q, 1 }, { u"needed"_q, false } })
		&& first(refused).startsWith(u"band 1 show-refused: cannot-fit"_q)
		&& (PictureObject(refused).value(u"complete"_q) == QJsonValue(false))
		&& first(shifted).startsWith(u"band 1 holds the rows"_q)
		&& first(tall).contains(u"rows 600 to 650"_q)
		&& (gaps(nothing).size() == 2)
		&& BandObject(nothing, 1).value(u"needed"_q).isNull()
		&& gaps(PictureReading()).isEmpty();
	return CheckRow(
		passed,
		u"bands are the card's rows from 0 and 300, at most 300 each; a "
			u"picture is complete only with every band its card needs at "
			u"its rows, a band the card ends above not needed, and a "
			u"refused, shifted or uncaptured band or rows below the last "
			u"band named"_q,
		CompactJson(QJsonObject{
			{ u"geometry"_q, geometry },
			{ u"refused"_q, Strings(gaps(refused)) },
			{ u"shifted"_q, Strings(gaps(shifted)) },
			{ u"tall"_q, Strings(gaps(tall)) },
			{ u"nothing"_q, Strings(gaps(nothing)) },
		}));
}

// Band 1 joins band 0 only from its core at its host generation at its
// card size, after band 0 was saved; the first band always opens one.
[[nodiscard]] WidgetOpsSelfCheckRow CheckJoin() {
	const auto first = CapturedControl(QSize(390, 336), 1);
	const auto card = first.card;
	auto unsaved = first;
	unsaved.bands[0].file = QString();
	const auto same = JoinRefusal(first, 1, card, 1, 1);
	const auto core = JoinRefusal(first, 1, card, 2, 1);
	const auto generation = JoinRefusal(first, 1, card, 1, 2);
	const auto size = JoinRefusal(first, 1, QSize(390, 340), 1, 1);
	const auto gap = JoinRefusal(unsaved, 1, card, 1, 1);
	const auto opening = JoinRefusal(PictureReading(), 0, QSize(), 0, 0);
	const auto passed = same.isEmpty()
		&& core.contains(u"never joined"_q)
		&& generation.contains(u"never joined"_q)
		&& size.contains(u"390x340"_q)
		&& gap.contains(u"do not continue"_q)
		&& opening.isEmpty();
	return CheckRow(
		passed,
		u"band 1 joins band 0 only from its core and host generation at its "
			u"card size once band 0 was saved; another core, generation or "
			u"size, or no band 0, is refused by name"_q,
		CompactJson(QJsonObject{
			{ u"same"_q, same },
			{ u"core"_q, core },
			{ u"generation"_q, generation },
			{ u"size"_q, size },
			{ u"gap"_q, gap },
		}));
}

// The date wait from a show at scroll top 40 at time 1000: held there for
// the whole window accepted and not a ms sooner; a scroll away and back
// restarting the window; held at another top refused at the bound, not a
// ms sooner; a lost scroll and a show that showed nothing decided at once;
// no wait refused by name.
[[nodiscard]] WidgetOpsSelfCheckRow CheckDateWait() {
	constexpr auto kStart = crl::time(1000);
	constexpr auto kTop = 40;
	const auto started = [&] {
		auto result = DateWaitReading();
		auto shown = ChatListShow{ .scrollTop = kTop, .finished = true };
		StartDateWait(result, &shown, kStart);
		return result;
	};
	const auto window = kStart + kScrollDateHideAndFade;
	auto held = started();
	const auto heldEarly = ObserveDateWait(held, kTop, window - 1);
	const auto heldLate = ObserveDateWait(held, kTop, window);
	auto moved = started();
	const auto away = ObserveDateWait(moved, kTop + 22, kStart + 100);
	const auto back = ObserveDateWait(moved, kTop, kStart + 200);
	const auto movedEarly = ObserveDateWait(moved, kTop, window);
	const auto movedLate = ObserveDateWait(moved, kTop, window + 200);
	auto elsewhere = started();
	const auto other = ObserveDateWait(elsewhere, kTop + 22, kStart + 100)
		|| ObserveDateWait(elsewhere, kTop + 22, window + 100)
		|| ObserveDateWait(elsewhere, kTop + 22, kStart + kScrollDateBound - 1);
	const auto bound = ObserveDateWait(
		elsewhere,
		kTop + 22,
		kStart + kScrollDateBound);
	auto lost = started();
	const auto gone = ObserveDateWait(lost, std::nullopt, kStart + 50);
	auto nothing = DateWaitReading();
	StartDateWait(nothing, nullptr, kStart);
	const auto refused = DateWaitRefusal(&elsewhere);
	const auto none = DateWaitRefusal(nullptr);
	const auto pending = started();
	const auto open = DateWaitRefusal(&pending);
	const auto passed = !heldEarly
		&& heldLate
		&& held.hidden
		&& !held.changes
		&& DateWaitRefusal(&held).name.isEmpty()
		&& (DateWaitObject(&held).value(u"hidden"_q) == QJsonValue(true))
		&& !away
		&& !back
		&& !movedEarly
		&& movedLate
		&& moved.hidden
		&& (moved.changes == 2)
		&& !other
		&& bound
		&& !elsewhere.hidden
		&& (refused.name == u"date-not-hidden"_q)
		&& refused.detail.contains(u"scrollTop=40, last scrollTop=62"_q)
		&& gone
		&& !lost.hidden
		&& (DateWaitRefusal(&lost).name == u"date-not-hidden"_q)
		&& nothing.finished
		&& !nothing.hidden
		&& (none.name == u"date-not-hidden"_q)
		&& (open.name == u"date-not-hidden"_q)
		&& (DateWaitObject(nullptr)
			== QJsonObject{ { u"finished"_q, false } });
	return CheckRow(
		passed,
		u"the date wait accepts only the show's scroll top held for the "
			u"longer of the two hide timeouts, the fade and the margin "
			u"(3350 ms), restarts on a change, "
			u"refuses date-not-hidden at the bound or without its scroll, "
			u"and a capture without an accepted wait is refused by name"_q,
		CompactJson(QJsonObject{
			{ u"held"_q, DateWaitObject(&held) },
			{ u"moved"_q, DateWaitObject(&moved) },
			{ u"elsewhere"_q, DateWaitObject(&elsewhere) },
			{ u"lost"_q, lost.detail },
			{ u"nothing"_q, nothing.detail },
			{ u"open"_q, open.detail },
		}));
}

} // namespace

std::shared_ptr<const DateWaitReading> AppendDateWait(
		not_null<Runner*> runner,
		const QString &name,
		Fn<QString()> skipReason,
		std::shared_ptr<const ChatListShow> shown) {
	const auto reading = std::make_shared<DateWaitReading>();
	runner->add({
		.name = name,
		.skipReason = std::move(skipReason),
		.run = [=] {
			StartDateWait(*reading, shown.get(), crl::now());
		},
		.until = [=] {
			return reading->finished
				|| ObserveDateWait(
					*reading,
					ShownScrollTop(*shown),
					crl::now());
		},
		.timeout = kScrollDateBound + kDefaultStageTimeout,
		.timeoutDetails = [=] {
			return DateWaitText(*reading);
		},
	});
	return reading;
}

ConformanceRefusal DateWaitRefusal(const DateWaitReading *reading) {
	if (reading && reading->hidden) {
		return {};
	}
	return {
		.name = u"date-not-hidden"_q,
		.detail = !reading
			? u"no wait for the floating date was appended"_q
			: !reading->finished
			? u"the wait for the floating date did not run to its end"_q
			: reading->detail,
	};
}

QJsonObject DateWaitObject(const DateWaitReading *reading) {
	if (!reading || !reading->finished) {
		return QJsonObject{ { u"finished"_q, false } };
	}
	return QJsonObject{
		{ u"finished"_q, true },
		{ u"hidden"_q, reading->hidden },
		{ u"shownTop"_q, reading->shownTop },
		{ u"top"_q, reading->top },
		{ u"changes"_q, reading->changes },
		{ u"stillMs"_q, qint64(reading->stillMs) },
		{ u"neededMs"_q, qint64(kScrollDateHideAndFade) },
		{ u"waitedMs"_q, qint64(reading->waitedMs) },
		{ u"detail"_q, reading->detail },
	};
}

void SelectPictures(ConformanceState &state) {
	if (!state.packageError.isEmpty()) {
		return;
	}
	const auto &pictures = state.args.pictures;
	for (const auto &id : pictures) {
		const auto visual = std::any_of(
			begin(state.tests),
			end(state.tests),
			[&](const ManifestTest &test) {
				return (test.id == id) && (test.set == u"visual"_q);
			});
		if (!visual && !state.unknownPictures.contains(id)) {
			state.unknownPictures.push_back(id);
		}
	}
	for (auto &run : state.runs) {
		if ((run.set == u"visual"_q) && pictures.contains(run.id)) {
			run.picture.requested = true;
			run.picture.bands.resize(kBands);
		}
	}
}

void AppendPictureBands(
		not_null<Runner*> runner,
		const std::shared_ptr<ConformanceState> &state,
		int index) {
	if (!state->runs[index].picture.requested) {
		return;
	}
	const auto key = state->runs[index].key;
	const auto prefix = Prefixed(key + u": "_q);
	const auto entry = [=]() -> ConformanceRun& {
		return state->runs[index];
	};
	for (auto i = 0; i != int(entry().picture.bands.size()); ++i) {
		const auto name = u"picture band "_q + NumberText(i);
		entry().picture.bands[i].show = AppendShowInChatList(runner, {
			.name = u"widget conformance "_q + key + u" "_q + name,
			.resolve = [=] {
				return ChatListShowTarget{
					WidgetFixtureList(*state->fixture),
					entry().message->id,
				};
			},
			.rect = [=](ChatListShowView view) {
				const auto card = WidgetCardLocal(view);
				return BandLocal(
					card,
					i ? entry().picture.card : CardCss(card),
					i);
			},
			.ready = [=](ChatListShowView view) {
				const auto card = ReadWidgetCard(view);
				if (!WidgetCardReady(card)) {
					return false;
				}
				entry().picture.bands[i].card = card;
				return true;
			},
			.readyDetails = [](ChatListShowView view) {
				return WidgetCardText(ReadWidgetCard(view));
			},
			.skipReason = [=] {
				return BandSkip(*state, entry(), i);
			},
			.timeout = kShowTimeout,
		});
		entry().picture.bands[i].date = AppendDateWait(
			runner,
			prefix + u"wait out the floating date before "_q + name,
			[=] {
				return BandSkip(*state, entry(), i);
			},
			entry().picture.bands[i].show);
		runner->add({
			.name = prefix + u"capture "_q + name + u" at 2x"_q,
			.skipReason = [=] {
				return RunSkip(*state, entry());
			},
			.run = [=] {
				RecordBand(*state, entry(), i);
			},
		});
	}
}

void InsertPictureExtra(QJsonObject &entry, const ConformanceRun &run) {
	if (run.picture.requested) {
		entry.insert(u"picture"_q, PictureObject(run.picture));
	}
}

QStringList PictureNotes(const ConformanceState &state) {
	auto result = QStringList();
	if (state.args.pictures.isEmpty()) {
		return result;
	}
	auto runs = 0;
	auto pictured = QStringList();
	for (const auto &run : state.runs) {
		if (run.picture.requested) {
			++runs;
			if (!pictured.contains(run.id)) {
				pictured.push_back(run.id);
			}
		}
	}
	result.push_back(u"pictures: runs with a picture in this launch: %1, of "
		u"[%2]. Each gets its whole card captured after its load wait, "
		u"before its log and removal: band 0 is the card's CSS rows from 0 "
		u"and band 1 those from %3, each at most %3 tall at the card's whole "
		u"width, so two bands hold a card of up to %4 CSS px, which the "
		u"list's viewport does not; a band the card ends above is not "
		u"needed. Each band is shown in the chat list, ready, with no hover, "
		u"and captured from the list's own paint at exactly %5 image px per "
		u"CSS px as pictures/<id>.<theme>.<band>.png. Band 1 is taken only "
		u"from band 0's core and host generation at its card size, "
		u"continuing its rows: two moments of one host's card, never of two "
		u"hosts or sizes. The runner judges no picture and report.mjs reads "
		u"none; each run's picture extra keeps the bands and the stage "
		u"rects the widget published at band 0."_q.arg(
			NumberText(runs),
			pictured.join(u","_q),
			NumberText(kBandCss),
			NumberText(kBands * kBandCss),
			NumberText(kCropScale)));
	auto noted = QStringList();
	for (const auto &id : state.args.pictures) {
		const auto visual = std::any_of(
			begin(state.tests),
			end(state.tests),
			[&](const ManifestTest &test) {
				return (test.id == id) && (test.set == u"visual"_q);
			});
		const auto ran = std::any_of(
			begin(state.runs),
			end(state.runs),
			[&](const ConformanceRun &run) { return (run.id == id); });
		if (visual && !ran && !noted.contains(id)) {
			noted.push_back(id);
			result.push_back(u"%1: a picture was asked for, but this launch "
				u"runs it in no theme, so none is taken here."_q.arg(id));
		}
	}
	return result;
}

QJsonObject PictureTotals(const ConformanceState &state) {
	const auto count = CountPictures(state);
	return QJsonObject{
		{ u"requested"_q, count.requested },
		{ u"complete"_q, count.complete },
		{ u"missing"_q, Strings(count.missing) },
	};
}

void CheckPictures(const ConformanceState &state) {
	if (state.args.pictures.isEmpty()) {
		return;
	}
	const auto count = CountPictures(state);
	Check(
		count.missing.isEmpty(),
		Prefixed(u"every requested picture of a selected run has every band "
			u"its card needs, covering the card from one host at one size"_q),
		u"pictures=%1 complete=%2 missing=[%3]"_q.arg(
			NumberText(count.requested),
			NumberText(count.complete),
			count.missing.join(u" | "_q)));
}

std::vector<WidgetOpsSelfCheckRow> RunPictureCheck() {
	return {
		CheckPictureSelection(),
		CheckCoverage(),
		CheckJoin(),
		CheckDateWait(),
	};
}

} // namespace Test::Conformance

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
