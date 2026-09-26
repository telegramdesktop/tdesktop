/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_menu.h"

#include "base/unique_qptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "ui/effects/animation_value.h"
#include "ui/widgets/popup_menu.h"
#include "ui/rp_widget.h"
#include "window/window_controller.h"

namespace Test {
namespace {

// Three is the smallest count that reads as a real menu in the saved PNG
// and still lets the capture assert the menu it accepted is this fixture's
// own rather than whatever else the process had open.
constexpr auto kSelfTestActions = 3;

struct Fixture {
	base::unique_qptr<Ui::PopupMenu> menu;
	base::unique_qptr<Ui::PopupMenu> empty;
	base::unique_qptr<Ui::RpWidget> stray;
	QPoint at;
};

[[nodiscard]] base::unique_qptr<Ui::PopupMenu> MakeMenu(
		not_null<QWidget*> parent,
		int actions) {
	auto result = base::make_unique_q<Ui::PopupMenu>(parent.get());
	// The fixture owns the lifetime, not the popup. _deleteOnHide defaults
	// to true (ui/widgets/popup_menu.h:260) and hideEvent() then
	// deleteLater()s the menu (popup_menu.cpp:623-631) - and this self-test
	// hides its menu on purpose between the two capture legs, so leaving
	// that on would race the base::unique_qptr holding it.
	result->deleteOnHide(false);
	for (auto i = 0; i != actions; ++i) {
		result->addAction(u"Harness %1"_q.arg(i + 1), [] {});
	}
	return result;
}

[[nodiscard]] bool BuildFixture(Fixture &fixture) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	const auto parent = window->widget().get();
	fixture.menu = MakeMenu(parent, kSelfTestActions);
	// Never opened, and never opened by accident either: popup() on a menu
	// with no actions takes its else branch and hides and deleteLater()s it
	// (popup_menu.cpp:945-958). It exists only so the refusal stage can read
	// an empty one and show that carrying no actions is refused on its own.
	fixture.empty = MakeMenu(parent, 0);
	fixture.stray = base::make_unique_q<Ui::RpWidget>(parent);
	fixture.at = parent->mapToGlobal(parent->rect().center());
	return true;
}

} // namespace

PopupMenuReading ReadPopupMenu(QWidget *widget) {
	auto result = PopupMenuReading();
	if (!widget) {
		result.identity = u"no widget"_q;
		return result;
	}
	result.identity = WidgetDescription(widget);
	const auto menu = dynamic_cast<Ui::PopupMenu*>(widget);
	if (!menu) {
		return result;
	}
	result.isMenu = true;
	// isVisible(), not !isHidden(): the contract here really is "this widget
	// and every ancestor are shown", because a popup whose ancestors are
	// hidden holds no pixels for a capture to accept.
	result.visible = menu->isVisible();
	result.transparent = menu->useTransparency();
	result.showingContent = menu->menu()->isVisible();
	result.width = menu->width();
	result.height = menu->height();
	// Read on the popup itself, which forwards to the inner menu
	// (ui/widgets/popup_menu.h:87, popup_menu.cpp:333-335).
	result.actions = int(menu->actions().size());
	return result;
}

bool PopupMenuReady(QWidget *widget) {
	const auto reading = ReadPopupMenu(widget);
	return reading.isMenu
		&& reading.visible
		&& (reading.width > 0)
		&& (reading.height > 0)
		&& (reading.actions > 0);
}

QString PopupMenuDetails(QWidget *widget) {
	const auto reading = ReadPopupMenu(widget);
	if (!widget) {
		return u"no widget resolved"_q;
	} else if (!reading.isMenu) {
		return u"the resolved widget is not a Ui::PopupMenu: %1"_q
			.arg(reading.identity);
	}
	return u"menu %1 visible=%2 size=%3x%4 actions=%5 showingContent=%6 "
		"transparent=%7 - showingContent is reported and never required: "
		"PopupMenu::startShowAnimation() calls hideChildren() and only the "
		"final paintEvent's Ui::PostponeCall calls showChildren() again, so "
		"a readiness over menu()->isVisible() can wait until the popup dies "
		"of a focus-out"_q
		.arg(reading.identity)
		.arg(reading.visible ? 1 : 0)
		.arg(reading.width)
		.arg(reading.height)
		.arg(reading.actions)
		.arg(reading.showingContent ? 1 : 0)
		.arg(reading.transparent ? 1 : 0);
}

void CapturePopupMenu(
		not_null<Runner*> runner,
		const QString &name,
		Fn<QWidget*()> resolve,
		Fn<void()> open,
		Fn<void(QWidget*, const QImage &)> inspect,
		crl::time timeout) {
	if (open) {
		runner->add({
			.name = u"open popup menu: %1"_q.arg(name),
			.run = [=] {
				// "Opens or accepts an already-open menu" is exactly this
				// and no more: the opener runs only when the resolver does
				// not already answer a ready menu, so a caller may append
				// this helper after a stage that already opened one.
				const auto already = PopupMenuReady(resolve());
				if (!already) {
					open();
				}
				Note(u"open popup menu: %1 alreadyOpen=%2 opened=%3 - %4"_q
					.arg(name)
					.arg(already ? 1 : 0)
					.arg(already ? 0 : 1)
					.arg(PopupMenuDetails(resolve())));
			},
		});
	}
	// The shared prepared capture, never a grab-check-save of its own: its
	// blank-frame refusal is what decides when a show animation has left a
	// frame worth saving, and it refuses without logging, so a menu that is
	// still opening costs ticks rather than failures.
	runner->captureAndInspect(
		name,
		resolve,
		[](QWidget *widget) { return PopupMenuReady(widget); },
		inspect,
		timeout,
		[](QWidget *widget) { return PopupMenuDetails(widget); });
}

void AppendPopupMenuCaptureSelfTest(not_null<Runner*> runner) {
	struct State {
		Fixture fixture;
		PopupMenuReading openTurn;
		QString openTurnDetails;
		QString oneShotReason;
		bool openTurnReady = false;
		bool oneShotAccepted = false;
		bool built = false;
	};
	// Leaked on purpose, the way the harness's other self-tests leak theirs:
	// the stages outlive this call. The teardown stage releases the fixture,
	// after which the State holds nothing but QStrings and PODs - no rpl
	// subscription to anything the session owns.
	const auto state = new State();
	const auto resolve = [=]() -> QWidget* {
		return state->fixture.menu.get();
	};
	const auto open = [=] {
		if (const auto menu = state->fixture.menu.get()) {
			menu->popup(state->fixture.at);
		}
	};
	const auto inspect = [=](QWidget *widget, const QImage &image) {
		const auto ratio = image.devicePixelRatio();
		Check(
			(image.width() >= int(widget->width() * ratio))
				&& (image.height() >= int(widget->height() * ratio)),
			u"the accepted frame covers the whole popup"_q,
			u"image=%1x%2 popup=%3x%4 devicePixelRatio=%5"_q
				.arg(image.width())
				.arg(image.height())
				.arg(widget->width())
				.arg(widget->height())
				.arg(ratio));
		Check(
			!LooksBlank(image),
			u"the accepted frame is not a show-animation frame with "
			"nothing painted on it"_q,
			u"image=%1x%2"_q.arg(image.width()).arg(image.height()));
		Check(
			ReadPopupMenu(widget).actions == kSelfTestActions,
			u"the captured popup is the self-test's own menu, carrying the "
			"actions the fixture put in it"_q,
			PopupMenuDetails(widget));
	};

	runner->add({
		.name = u"popup menu self-test: open"_q,
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
			const auto menu = state->fixture.menu.get();
			menu->popup(state->fixture.at);
			// Everything below is snapshotted here, in the turn that called
			// popup(), because that is the turn both failing shapes live in
			// and Runner runs .then a tick later: st::defaultPopupMenu's
			// showDuration is 200ms, four ticks, so a reading taken in .then
			// would answer for a settled menu and would prove nothing.
			state->openTurn = ReadPopupMenu(menu);
			state->openTurnReady = PopupMenuReady(menu);
			state->openTurnDetails = PopupMenuDetails(menu);
			auto probe = PreparedWidgetCapture();
			state->oneShotAccepted = probe.prepare(menu);
			state->oneShotReason = probe.pendingReason();
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			Check(
				state->openTurn.transparent,
				u"fixture gate: the platform supports translucent popups, "
				"so the show animation really does hide the menu's "
				"children"_q,
				state->openTurnDetails);
			Check(
				!anim::Disabled(),
				u"fixture gate: animations are enabled, so the show "
				"animation really has frames"_q,
				u"anim::Disabled()=%1"_q.arg(anim::Disabled() ? 1 : 0));
			Check(
				state->openTurnReady,
				u"the content-identity readiness accepts the popup in the "
				"turn that opened it"_q,
				state->openTurnDetails);
			Check(
				!state->openTurn.showingContent,
				u"a readiness over the inner Ui::Menu's visibility does not "
				"accept the popup in that same turn, so the two predicates "
				"disagree and the failing shape is reached, not merely "
				"described"_q,
				state->openTurnDetails);
			Check(
				!state->oneShotAccepted,
				u"a one-shot prepared grab in the opening turn refuses the "
				"frame instead of saving it"_q,
				state->oneShotReason.isEmpty()
					? state->openTurnDetails
					: state->oneShotReason);
		},
	});

	CapturePopupMenu(runner, u"popup_menu_open"_q, resolve, {}, inspect);

	runner->add({
		.name = u"popup menu self-test: close"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			// hideMenu(true) reaches hideFast() -> hideFinished() -> hide()
			// with no opacity animation to wait out (popup_menu.cpp:683-701,
			// 764-779), so the next leg starts from a menu that is provably
			// closed and really exercises CapturePopupMenu's opener branch
			// instead of accepting one that never closed.
			if (const auto menu = state->fixture.menu.get()) {
				menu->hideMenu(true);
			}
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			Check(
				!PopupMenuReady(resolve()),
				u"the fixture menu is closed before the opening leg"_q,
				PopupMenuDetails(resolve()));
		},
	});

	CapturePopupMenu(
		runner,
		u"popup_menu_reopened"_q,
		resolve,
		open,
		inspect);

	runner->add({
		.name = u"popup menu self-test: refusal text"_q,
		.run = [=] {
			if (!state->built) {
				return;
			}
			const auto missing = PopupMenuDetails(nullptr);
			Check(
				!PopupMenuReady(nullptr)
					&& missing.contains(u"no widget"_q),
				u"a null target is refused and named, never taken for a "
				"menu that is merely not ready yet"_q,
				missing);
			const auto stray = state->fixture.stray.get();
			const auto strayDetails = PopupMenuDetails(stray);
			const auto strayIdentity = stray
				? WidgetDescription(stray)
				: QString();
			Check(
				stray
					&& !PopupMenuReady(stray)
					&& strayDetails.contains(u"is not a Ui::PopupMenu"_q)
					&& !strayIdentity.isEmpty()
					&& strayDetails.contains(strayIdentity),
				u"a widget that is not a Ui::PopupMenu is refused, and the "
				"refusal names what was resolved instead"_q,
				strayDetails);
			const auto empty = state->fixture.empty.get();
			Check(
				empty
					&& !PopupMenuReady(empty)
					&& (ReadPopupMenu(empty).actions == 0),
				u"a Ui::PopupMenu carrying no actions is refused, which is "
				"also why the helper never calls popup() itself: popup() on "
				"an empty menu hides and deleteLater()s it"_q,
				PopupMenuDetails(empty));
			const auto details = PopupMenuDetails(resolve());
			Check(
				details.contains(u"showingContent="_q)
					&& details.contains(
						u"showingContent is reported and never required"_q)
					&& details.contains(u"hideChildren()"_q),
				u"every menu reading prints showingContent together with "
				"the mechanism that makes it a report and never a gate"_q,
				details);
			const auto root = PaintingLayerRoot(resolve());
			Check(
				!root.resolved()
					&& (root.widget == nullptr)
					&& !root.refusal.isEmpty(),
				u"the painting-layer-root resolver refuses a Ui::PopupMenu, "
				"because a popup is its own window and, with "
				"Qt::WA_NoSystemBackground, its own render root"_q,
				root.refusal);
		},
	});

	runner->add({
		.name = u"popup menu self-test: teardown"_q,
		.run = [=] {
			// Last on purpose. A timed-out stage or the watchdog skips every
			// stage after it, so anything still held here would outlive the
			// run: a Qt::Popup window would stay over the primary window for
			// the rest of the process, taking the input a later scenario
			// aims at the window underneath it, and the stray widget would
			// stay parented to that window. deleteOnHide(false) is why
			// releasing the unique_qptrs - and not hideEvent()'s
			// deleteLater() - is what destroys the two menus.
			if (const auto menu = state->fixture.menu.get()) {
				menu->hideMenu(true);
			}
			state->fixture.menu = nullptr;
			state->fixture.empty = nullptr;
			state->fixture.stray = nullptr;
			Note(u"popup menu self-test: fixture released, menu=%1 empty=%2 "
				"stray=%3"_q
				.arg(state->fixture.menu ? 1 : 0)
				.arg(state->fixture.empty ? 1 : 0)
				.arg(state->fixture.stray ? 1 : 0));
		},
	});
}

} // namespace Test

#endif // _DEBUG
