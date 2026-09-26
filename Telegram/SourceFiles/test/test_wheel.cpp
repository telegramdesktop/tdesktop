/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_wheel.h"

#include "base/unique_qptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/abstract_button.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/scroll_area.h"
#include "window/window_controller.h"

#include "styles/style_widgets.h"

namespace Test {
namespace {

constexpr auto kFixtureWidthFactor = 4;
constexpr auto kViewportHeightFactor = 3;
constexpr auto kContentHeightFactor = 12;

const auto kDownStep = QPoint(0, -120);

struct Fixture {
	base::unique_qptr<Ui::RpWidget> container;
	base::unique_qptr<Ui::RpWidget> stray;
	Ui::ScrollArea *scroll = nullptr;
	Ui::AbstractButton *button = nullptr;
	Ui::AbstractButton *strayButton = nullptr;
};

[[nodiscard]] QString DescribeScroll(
		const QString &label,
		int before,
		int after,
		int max,
		const WheelDelivery &delivery) {
	return u"%1: scrollTop %2 -> %3 max=%4 %5"_q
		.arg(label)
		.arg(before)
		.arg(after)
		.arg(max)
		.arg(WheelDeliveryDetails(delivery));
}

[[nodiscard]] bool BuildFixture(Fixture &fixture) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	const auto row = st::defaultActiveButton.height;
	const auto width = row * kFixtureWidthFactor;
	const auto viewportH = row * kViewportHeightFactor;
	const auto contentH = row * kContentHeightFactor;
	const auto skip = row / 2;

	fixture.container = base::make_unique_q<Ui::RpWidget>(
		window->widget().get());
	const auto container = fixture.container.get();
	container->setAttribute(Qt::WA_NoMousePropagation);
	container->setGeometry(
		0,
		0,
		width + skip * 2,
		viewportH + skip * 2);
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		container,
		st::defaultScrollArea);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scroll->setGeometry(skip, skip, width, viewportH);
	auto inner = object_ptr<Ui::RpWidget>(scroll);
	inner->resize(width, contentH);
	const auto button = Ui::CreateChild<Ui::AbstractButton>(inner.data());
	button->setGeometry(0, 0, width, row);
	button->show();
	scroll->setOwnedWidget(std::move(inner));
	scroll->show();
	container->show();
	Ui::SendPendingMoveResizeEvents(scroll);
	if (const auto content = scroll->widget()) {
		Ui::SendPendingMoveResizeEvents(content);
	}
	fixture.scroll = scroll;
	fixture.button = button;

	fixture.stray = base::make_unique_q<Ui::RpWidget>(nullptr);
	const auto stray = fixture.stray.get();
	stray->resize(width, row);
	fixture.strayButton = Ui::CreateChild<Ui::AbstractButton>(stray);
	fixture.strayButton->setGeometry(0, 0, width, row);
	return true;
}

} // namespace

void AppendWheelSelfTest(not_null<Runner*> runner) {
	struct State {
		Fixture fixture;
		int max = 0;
		int subjectBefore = 0;
		int subjectAfter = 0;
		int controlBefore = 0;
		int controlAfter = 0;
		int inertBefore = 0;
		int inertAfter = 0;
		WheelDelivery subject;
		WheelDelivery control;
		WheelDelivery inert;
		WheelDelivery refused;
		bool built = false;
	};
	const auto state = new State();

	runner->add({
		.name = u"wheel self-test: a covering button reaches the viewport"_q,
		.run = [=] {
			state->built = BuildFixture(state->fixture);
			Check(
				state->built,
				u"fixture gate: the self-test fixture was built"_q,
				state->built
					? QString()
					: u"Core::App().activePrimaryWindow() is null"_q);
			if (!state->built) {
				return;
			}
			const auto scroll = state->fixture.scroll;
			const auto button = state->fixture.button;
			const auto viewport = scroll->viewport();
			state->max = scroll->scrollTopMax();
			Check(
				(viewport != nullptr) && (state->max > 0),
				u"fixture gate: the scroll area has a viewport and a "
				"nonzero range"_q,
				u"viewport=%1 scrollTopMax=%2"_q
					.arg(viewport
						? WidgetDescription(viewport)
						: u"null"_q)
					.arg(state->max));
			state->subjectBefore = scroll->scrollTop();
			state->subject = Wheel(
				button,
				kDownStep,
				button->rect().center());
			state->subjectAfter = scroll->scrollTop();
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto text = DescribeScroll(
				u"subject"_q,
				state->subjectBefore,
				state->subjectAfter,
				state->max,
				state->subject);
			Note(text);
			const auto viewport = state->fixture.scroll->viewport();
			const auto viewportId = viewport
				? WidgetDescription(viewport)
				: u"null"_q;
			Check(
				state->subject.delivered
					&& state->subject.refusal.isEmpty()
					&& (state->subjectAfter != state->subjectBefore)
					&& (state->subject.receiver == viewportId),
				u"a wheel aimed at a covering AbstractButton moves the "
				"scroll area and names the viewport as the receiver"_q,
				text);
		},
	});

	runner->add({
		.name = u"wheel self-test: the viewport control still moves"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto scroll = state->fixture.scroll;
			const auto viewport = scroll->viewport();
			scroll->scrollToY(0);
			Ui::SendPendingMoveResizeEvents(scroll);
			state->controlBefore = scroll->scrollTop();
			state->control = Wheel(
				viewport,
				kDownStep,
				viewport->rect().center());
			state->controlAfter = scroll->scrollTop();
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto text = DescribeScroll(
				u"control"_q,
				state->controlBefore,
				state->controlAfter,
				state->max,
				state->control);
			Note(text);
			Check(
				state->control.delivered
					&& state->control.refusal.isEmpty()
					&& (state->controlAfter != state->controlBefore),
				u"the same wheel delivered at the viewport still moves "
				"the scroll area"_q,
				text);
		},
	});

	runner->add({
		.name = u"wheel self-test: a QAbstractScrollArea itself is not "
			"certified delivered"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto scroll = state->fixture.scroll;
			scroll->scrollToY(0);
			Ui::SendPendingMoveResizeEvents(scroll);
			state->inertBefore = scroll->scrollTop();
			state->inert = Wheel(
				scroll,
				kDownStep,
				scroll->rect().center());
			state->inertAfter = scroll->scrollTop();
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto text = DescribeScroll(
				u"inert"_q,
				state->inertBefore,
				state->inertAfter,
				state->max,
				state->inert);
			Note(text);
			const auto areaId = WidgetDescription(state->fixture.scroll);
			Check(
				!state->inert.delivered
					&& !state->inert.refusal.isEmpty()
					&& (state->inert.inert == areaId)
					&& (state->inertAfter == state->inertBefore)
					&& state->inert.refusal.contains(
						u"returned the event still accepted without "
						"handling it"_q),
				u"a wheel sent to the QAbstractScrollArea itself is not "
				"delivered, names the accepted-but-inert trap, and does "
				"not scroll"_q,
				text);
		},
	});

	runner->add({
		.name = u"wheel self-test: no scrolling ancestor refuses by name"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto button = state->fixture.strayButton;
			state->refused = Wheel(
				button,
				kDownStep,
				button->rect().center());
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto text = WheelDeliveryDetails(state->refused);
			Note(text);
			Check(
				!state->refused.delivered
					&& !state->refused.refusal.isEmpty()
					&& text.contains(u"no widget consumed the wheel"_q)
					&& text.contains(u"window=1"_q),
				u"a wheel aimed into a widget tree with no scrolling "
				"ancestor reports a named reason"_q,
				text);
		},
	});

	runner->add({
		.name = u"wheel self-test: teardown"_q,
		.run = [=] {
			state->fixture.container = nullptr;
			state->fixture.stray = nullptr;
			state->fixture.scroll = nullptr;
			state->fixture.button = nullptr;
			state->fixture.strayButton = nullptr;
			Note(u"wheel self-test: fixture released, alive=%1 stray=%2"_q
				.arg(state->fixture.container ? 1 : 0)
				.arg(state->fixture.stray ? 1 : 0));
		},
	});
}

} // namespace Test

#endif // _DEBUG
