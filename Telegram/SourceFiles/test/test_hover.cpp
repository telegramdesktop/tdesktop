/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_hover.h"

#include "base/unique_qptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_ink.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_style.h"
#include "test/test_widgets.h"
#include "ui/widgets/buttons.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "window/window_controller.h"

#include <QtGui/QPainter>

#include "styles/palette.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

constexpr auto kStyleStableFor = crl::time(200);
constexpr auto kStyleDeadline = crl::time(5000);

// Test::PillBand insets the derived fill region by box.height() / 2 on the
// left and on the right, and keeps the inset band only while it stays at
// least 8 device pixels wide. A button narrower than its own height plus 8
// would therefore be measured through its un-inset box, rounded caps and
// the surface behind them included. Four times the style height clears that
// at every interface scale and leaves a band three heights wide, which
// comfortably contains the centred label the ink scan reads.
constexpr auto kFixtureWidthFactor = 4;

struct Fixture {
	base::unique_qptr<Ui::RpWidget> container;
	Ui::RoundButton *subject = nullptr;
	Ui::RoundButton *control = nullptr;
};

struct FillReading {
	bool over = false;
	bool down = false;
	bool normalOk = false;
	bool overOk = false;
	int normalRows = 0;
	int overRows = 0;
	int normalFillRows = 0;
	int overFillRows = 0;
	int inkPixels = 0;
	QString resolved;
	QString normalReason;
	QString overReason;
	QString inkReport;
};

// The literal DeriveBand answers with when no row of the box it was given
// reads as the fill it was given. Kept here so the self-test asserts the
// refusal text a scenario actually sees, and so loosening it in test_ink.cpp
// fails this module instead of passing quietly.
[[nodiscard]] QString NoFillRowsReason() {
	return u"no row of the recovered box has the pill fill "
		u"as its own background"_q;
}

[[nodiscard]] QString ResolvedFill(bool normalOk, bool overOk) {
	if (normalOk && overOk) {
		return u"ambiguous"_q;
	} else if (normalOk) {
		return u"textBg"_q;
	} else if (overOk) {
		return u"textBgOver"_q;
	}
	return u"none"_q;
}

[[nodiscard]] FillReading ReadBands(
		const DerivedBand &normal,
		const DerivedBand &over) {
	auto result = FillReading();
	result.normalOk = normal.ok;
	result.overOk = over.ok;
	result.normalRows = int(normal.rows.size());
	result.overRows = int(over.rows.size());
	result.normalFillRows = normal.fillRows;
	result.overFillRows = over.fillRows;
	result.normalReason = normal.reason;
	result.overReason = over.reason;
	result.resolved = ResolvedFill(normal.ok, over.ok);
	return result;
}

[[nodiscard]] FillReading ReadFill(
		not_null<Ui::RoundButton*> button,
		const QImage &image) {
	// A grab holds logical-size x devicePixelRatio() device pixels, so the
	// button's logical geometry is multiplied by that ratio exactly once,
	// here at the sampling boundary, and every band below indexes device
	// space. Doing it anywhere else, or twice, samples the wrong rectangle
	// at every interface scale other than 100%.
	const auto ratio = image.devicePixelRatio();
	const auto logical = button->geometry();
	const auto box = QRectF(
		logical.x() * ratio,
		logical.y() * ratio,
		logical.width() * ratio,
		logical.height() * ratio).toRect();
	// One ink candidate, textFg, and never the two fills: kSameTolerance
	// governs ink separation while kBackgroundSame governs the fill, and a
	// real button style keeps its two fills far closer than kSameTolerance
	// on purpose. With a lone candidate Separable() is trivially true and
	// CollisionDump() answers "none", so the ink side carries no collision
	// the fill side would then have to explain away.
	const auto &st = button->st();
	const auto measured = MeasurePaintedInk(
		image,
		box,
		st.textBg->c,
		{ { u"textFg"_q, st.textFg->c } });
	auto result = ReadBands(
		measured.derived,
		DeriveBand(image, box, st.textBgOver->c));
	result.over = button->isOver();
	result.down = button->isDown();
	result.inkPixels = measured.scan.inkPixels;
	result.inkReport = measured.report;
	return result;
}

[[nodiscard]] QString DescribeBand(
		const QString &label,
		bool ok,
		int rows,
		int fillRows,
		const QString &reason) {
	return u"%1[ok=%2 rows=%3 fillRows=%4 reason=%5]"_q
		.arg(label)
		.arg(ok ? 1 : 0)
		.arg(rows)
		.arg(fillRows)
		.arg(reason);
}

[[nodiscard]] QString Describe(
		const QString &label,
		const FillReading &reading) {
	return u"%1: resolved=%2 isOver=%3 isDown=%4 %5 %6 inkPx=%7 ink=%8"_q
		.arg(label, reading.resolved)
		.arg(reading.over ? 1 : 0)
		.arg(reading.down ? 1 : 0)
		.arg(
			DescribeBand(
				u"normal"_q,
				reading.normalOk,
				reading.normalRows,
				reading.normalFillRows,
				reading.normalReason),
			DescribeBand(
				u"over"_q,
				reading.overOk,
				reading.overRows,
				reading.overFillRows,
				reading.overReason))
		.arg(reading.inkPixels)
		.arg(reading.inkReport.isEmpty() ? u"none"_q : reading.inkReport);
}

[[nodiscard]] QString DescribeImage(const QImage &image) {
	return u"size=%1x%2 devicePixelRatio=%3"_q
		.arg(image.width())
		.arg(image.height())
		.arg(image.devicePixelRatio());
}

void CheckNormalFill(
		const FillReading &reading,
		const QString &what,
		const QString &details) {
	Check(
		(reading.resolved == u"textBg"_q)
			&& reading.normalOk
			&& (reading.normalRows > 0)
			&& !reading.overOk
			&& (reading.inkPixels >= kMinInkPixels),
		what,
		details);
}

[[nodiscard]] bool BuildFixture(Fixture &fixture) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	const auto height = st::defaultActiveButton.height;
	const auto width = height * kFixtureWidthFactor;
	const auto skip = height / 2;
	fixture.container = base::make_unique_q<Ui::RpWidget>(
		window->widget().get());
	const auto container = fixture.container.get();
	container->setGeometry(0, 0, width + skip * 2, height * 2 + skip * 3);
	container->paintOn([=](QPainter &p) {
		p.fillRect(container->rect(), st::windowBgOver);
	});
	// The container is deliberately never shown. Ui::GrabWidgetToImage
	// renders a not-visible source explicitly and marks its opaque children
	// dirty afterwards, so a hidden fixture still paints into the grab -
	// while a hidden fixture can never be entered by the real cursor, which
	// would otherwise set Over on the never-touched control and destroy the
	// only thing that control is there to prove. Its opaque paint handler is
	// load-bearing for the same reason: the grab's uncovered pixels have to
	// read as the fixture's own background rather than as the harness base.
	// The two buttons are shown, because Qt skips an explicitly hidden child
	// when it renders a widget's children.
	const auto add = [&](int top) {
		const auto button = Ui::CreateChild<Ui::RoundButton>(
			container,
			rpl::single(u"Measure"_q),
			st::defaultActiveButton);
		button->setFullWidth(width);
		button->setGeometry(skip, top, width, height);
		button->show();
		return button;
	};
	fixture.subject = add(skip);
	fixture.control = add(skip * 2 + height);
	return true;
}

} // namespace

void AppendClickHoverSelfTest(not_null<Runner*> runner) {
	struct State {
		Fixture fixture;
		QString control;
		QString afterClick;
		bool built = false;
	};
	// Leaked on purpose, the way the harness's other self-tests leak theirs:
	// the stages outlive this call. The teardown stage releases the fixture,
	// after which the State holds nothing but QStrings and PODs - no rpl
	// subscription to anything the session owns.
	const auto state = new State();
	const auto probe = [] {
		const auto &st = st::defaultActiveButton;
		return std::vector<StyleSample>{
			{ .label = u"textBg"_q, .color = st.textBg->c },
			{ .label = u"textBgOver"_q, .color = st.textBgOver->c },
			{ .label = u"textFg"_q, .color = st.textFg->c },
		};
	};

	runner->add({
		.name = u"click hover self-test: positive"_q,
		.run = [=] {
			state->built = BuildFixture(state->fixture);
			Check(
				state->built,
				u"fixture gate: the self-test fixture was built"_q,
				state->built
					? QString()
					: u"Core::App().activePrimaryWindow() is null"_q);
			const auto &st = st::defaultActiveButton;
			const auto delta = ChannelDelta(st.textBg->c, st.textBgOver->c);
			Check(
				delta > kBackgroundSame,
				u"fixture gate: the live style's normal and over fills "
				"are separable"_q,
				u"textBg=%1 textBgOver=%2 delta=%3 kBackgroundSame=%4"_q
					.arg(st.textBg->c.name(), st.textBgOver->c.name())
					.arg(delta)
					.arg(kBackgroundSame));
		},
		.until = [=] {
			return StyleSettled(
				u"click-hover-self-test"_q,
				probe,
				kStyleStableFor,
				kStyleDeadline);
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			// One grab, measured and saved: the evidence PNG is the frame
			// the readings came out of, never a second one taken later.
			const auto image = GrabWidget(state->fixture.container.get());
			Check(
				!LooksBlank(image),
				u"fixture gate: the fixture grab is not blank"_q,
				DescribeImage(image));
			SaveImage(image, u"click_hover_before"_q);
			const auto subject = ReadFill(state->fixture.subject, image);
			const auto control = ReadFill(state->fixture.control, image);
			const auto subjectText = Describe(u"subject"_q, subject);
			state->control = Describe(u"control"_q, control);
			Note(subjectText);
			Note(state->control);
			Check(
				!subject.over && !subject.down,
				u"before any input the subject button is neither over "
				"nor down"_q,
				subjectText);
			CheckNormalFill(
				subject,
				u"before any input the subject button's painted fill "
				"resolves as the style's normal textBg"_q,
				subjectText);
			Check(
				!control.over && !control.down,
				u"before any input the control button is neither over "
				"nor down"_q,
				state->control);
			CheckNormalFill(
				control,
				u"before any input the never-touched control's painted "
				"fill resolves as the style's normal textBg"_q,
				state->control);
		},
	});

	runner->add({
		.name = u"click hover self-test: negative"_q,
		.run = [=] {
			// The public Ui::AbstractButton::setSynteticOver, not a
			// Test::Click: this stage provokes the bad reading directly, so
			// that it is reproduced on both legs and the recovery stage's
			// success is shown to be discriminating rather than universal.
			// It starts no ripple - RippleButton::onStateChanged returns
			// before the ripple branch while down == wasDown.
			if (const auto subject = state->fixture.subject) {
				subject->setSynteticOver(true);
			}
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto subject = state->fixture.subject;
			const auto image = GrabWidget(state->fixture.container.get());
			const auto reading = ReadFill(subject, image);
			const auto text = Describe(u"subject hovered"_q, reading);
			Note(text);
			Check(
				(reading.resolved == u"textBgOver"_q)
					&& reading.over
					&& !reading.normalOk
					&& (reading.normalRows == 0)
					&& (reading.normalFillRows == 0),
				u"a hovered button paints the style's textBgOver, and "
				"DeriveBand under textBg refuses it with no rows"_q,
				text);
			Check(
				reading.normalReason == NoFillRowsReason(),
				u"the refused band says why it could not be derived "
				"instead of reporting an absence"_q,
				u"reason=%1"_q.arg(reading.normalReason));
			subject->setSynteticOver(false);
			Check(
				!subject->isOver() && !subject->isDown(),
				u"the deliberate hover is cleared again before the repair "
				"itself is exercised"_q,
				u"isOver=%1 isDown=%2"_q
					.arg(subject->isOver() ? 1 : 0)
					.arg(subject->isDown() ? 1 : 0));
		},
	});

	runner->add({
		.name = u"click hover self-test: recovery"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto subject = state->fixture.subject;
			const auto container = state->fixture.container.get();
			// finishAnimating() is why the reading is deterministic, not
			// why it passes. Every press starts a RippleButton ripple:
			// RoundButton::prepareRippleStartPosition derives its origin
			// from the real QCursor::pos() with no containment guard, the
			// style's ripple colour is opaque, and the animation runs for
			// showDuration plus hideDuration - so a grab taken inside that
			// window feeds ripple pixels to the rows DeriveBand takes the
			// modal of, on a schedule that differs per run and per machine.
			// RippleButton::finishAnimating() only resets the ripple; it
			// touches no state flag, so it can neither clear Over nor hide
			// the defect this stage exists to catch, and it is applied
			// identically on both legs.
			Click(subject);
			subject->finishAnimating();
			const auto clicked = ReadFill(subject, GrabWidget(container));
			state->afterClick = Describe(u"subject"_q, clicked);
			Note(state->afterClick);
			Check(
				!clicked.over && !clicked.down,
				u"a completed synthetic click leaves its target neither "
				"over nor down"_q,
				state->afterClick);
			CheckNormalFill(
				clicked,
				u"the clicked button's painted fill resolves as the "
				"style's normal textBg, not as an absence"_q,
				state->afterClick);
			const auto inner = subject->rect();
			Drag(
				subject,
				QPoint(inner.width() / 4, inner.height() / 2),
				QPoint((inner.width() * 3) / 4, inner.height() / 2));
			subject->finishAnimating();
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto image = GrabWidget(state->fixture.container.get());
			Check(
				!LooksBlank(image),
				u"fixture gate: the post-input grab is not blank"_q,
				DescribeImage(image));
			SaveImage(image, u"click_hover_after"_q);
			const auto dragged = ReadFill(state->fixture.subject, image);
			const auto control = ReadFill(state->fixture.control, image);
			const auto draggedText = Describe(u"subject"_q, dragged);
			const auto controlText = Describe(u"control"_q, control);
			Note(draggedText);
			Note(controlText);
			Check(
				!dragged.over && !dragged.down,
				u"a completed synthetic drag whose endpoints are both "
				"inside the widget leaves it neither over nor down"_q,
				draggedText);
			CheckNormalFill(
				dragged,
				u"the dragged button's painted fill resolves as the "
				"style's normal textBg, not as an absence"_q,
				draggedText);
			Check(
				draggedText == state->afterClick,
				u"Test::Drag leaves exactly what Test::Click leaves: the "
				"two readings are identical"_q,
				u"afterClick=[%1] afterDrag=[%2]"_q
					.arg(state->afterClick, draggedText));
			Check(
				controlText == state->control,
				u"the never-touched control reads exactly as it did "
				"before any input, so the change is about the clicked "
				"widget and not about the measurement"_q,
				u"before=[%1] after=[%2]"_q
					.arg(state->control, controlText));
		},
	});

	runner->add({
		.name = u"click hover self-test: refusal text"_q,
		.run = [] {
			// The negative of this stage is an implementation that made the
			// recovery stage pass by loosening kBackgroundSame or by having
			// DeriveBand certify a band it could not derive. On an image
			// that is nothing but the over fill, the refusal and the
			// certification must both still read exactly as they did.
			const auto &st = st::defaultActiveButton;
			const auto box = QRect(
				0,
				0,
				st.height * kFixtureWidthFactor,
				st.height);
			auto image = QImage(
				box.size(),
				QImage::Format_ARGB32_Premultiplied);
			image.fill(st.textBgOver->c);
			const auto refused = DeriveBand(image, box, st.textBg->c);
			const auto certified = DeriveBand(image, box, st.textBgOver->c);
			const auto reading = ReadBands(refused, certified);
			const auto text = Describe(u"refusal text"_q, reading);
			Note(text);
			Check(
				!refused.ok
					&& refused.rows.empty()
					&& (refused.reason == NoFillRowsReason()),
				u"DeriveBand still refuses a band it cannot derive, with "
				"no rows and the reason that names why"_q,
				text);
			Check(
				certified.ok && !certified.rows.empty(),
				u"DeriveBand still certifies the band it can derive, so "
				"the refusal discriminates instead of refusing every "
				"reading"_q,
				text);
			Check(
				text.contains(u"normal[ok="_q)
					&& text.contains(u"over[ok="_q)
					&& text.contains(u"reason="_q),
				u"the reading names both bands it judged and the reason "
				"each one carries"_q,
				text);
		},
	});

	runner->add({
		.name = u"click hover self-test: teardown"_q,
		.run = [=] {
			// Last on purpose. A timed-out stage or the watchdog skips
			// every stage after it, so anything still held here would
			// outlive the run: the fixture is parented to the primary
			// window and would keep two Ui::RoundButtons alive inside it
			// for the rest of the process. Releasing the unique_qptr
			// destroys the container and with it both buttons, which is
			// why the raw back-pointers are dropped in the same breath.
			state->fixture.container = nullptr;
			state->fixture.subject = nullptr;
			state->fixture.control = nullptr;
			Note(u"click hover self-test: fixture released, alive=%1"_q
				.arg(state->fixture.container ? 1 : 0));
		},
	});
}

} // namespace Test

#endif // _DEBUG
