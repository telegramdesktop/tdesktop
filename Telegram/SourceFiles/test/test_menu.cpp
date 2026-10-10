/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_menu.h"

#include "base/flat_map.h"
#include "base/invoke_queued.h"
#include "base/unique_qptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_console_lock.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/effects/animation_value.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/menu/menu_common.h"
#include "ui/widgets/popup_menu.h"
#include "ui/rp_widget.h"
#include "window/window_controller.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QAction>

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
	// to true (ui/widgets/popup_menu.h) and hideEvent() then deleteLater()s
	// the menu (popup_menu.cpp) - and this self-test hides its menu on
	// purpose between the two capture legs, so leaving that on would race
	// the base::unique_qptr holding it.
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
	// (popup_menu.cpp). It exists only so the refusal stage can read an
	// empty one and show that carrying no actions is refused on its own.
	fixture.empty = MakeMenu(parent, 0);
	fixture.stray = base::make_unique_q<Ui::RpWidget>(parent);
	fixture.at = parent->mapToGlobal(parent->rect().center());
	return true;
}

// One Ui::PopupMenu alive before a context-menu event. The QPointer makes a
// menu destroyed since read as null, so it matches nothing - a new menu
// allocated at a dead one's address is never taken for the old one.
struct MenuSnapshotEntry {
	QPointer<Ui::PopupMenu> menu;
	bool visible = false;
};

// Every Ui::PopupMenu in the application, wherever it is parented. Every
// popup is its own window, but the walk does not lean on window flags:
// QApplication::allWidgets() holds every widget, and the dynamic_cast is
// the harness's usual stand-in for findChildren on a type without
// Q_OBJECT.
[[nodiscard]] std::vector<Ui::PopupMenu*> LivePopupMenus() {
	auto result = std::vector<Ui::PopupMenu*>();
	for (const auto widget : QApplication::allWidgets()) {
		if (const auto menu = dynamic_cast<Ui::PopupMenu*>(widget)) {
			result.push_back(menu);
		}
	}
	return result;
}

[[nodiscard]] std::vector<MenuSnapshotEntry> SnapshotPopupMenus() {
	auto result = std::vector<MenuSnapshotEntry>();
	for (const auto menu : LivePopupMenus()) {
		result.push_back({ .menu = menu, .visible = menu->isVisible() });
	}
	return result;
}

// PopupMenu::ensureSubmenu and PopupMenu::addAction(text, submenu) both
// parent a submenu to its menu, keeping its window flags.
[[nodiscard]] bool IsSubmenu(not_null<Ui::PopupMenu*> menu) {
	return dynamic_cast<Ui::PopupMenu*>(menu->parentWidget()) != nullptr;
}

[[nodiscard]] QString EventReasonName(QContextMenuEvent::Reason reason) {
	switch (reason) {
	case QContextMenuEvent::Mouse: return u"mouse"_q;
	case QContextMenuEvent::Keyboard: return u"keyboard"_q;
	case QContextMenuEvent::Other: return u"other"_q;
	}
	return u"unknown"_q;
}

[[nodiscard]] QString AppStateName() {
	switch (QGuiApplication::applicationState()) {
	case Qt::ApplicationSuspended: return u"suspended"_q;
	case Qt::ApplicationHidden: return u"hidden"_q;
	case Qt::ApplicationInactive: return u"inactive"_q;
	case Qt::ApplicationActive: return u"active"_q;
	}
	return u"unknown"_q;
}

[[nodiscard]] QString FocusWindowName(QWidget *menu, QWidget *receiver) {
	const auto focus = QGuiApplication::focusWindow();
	if (!focus) {
		return u"none"_q;
	} else if (menu && (focus == menu->windowHandle())) {
		return u"menu"_q;
	} else if (receiver && (focus == receiver->window()->windowHandle())) {
		return u"receiver-window"_q;
	}
	return u"other"_q;
}

// Called inside the settle action, right after sendEvent returned and
// before any postponed call runs, so it reads the menu in the turn - and
// the very call - that built it.
void TakeFreshMenu(
		ContextMenuReading &result,
		const std::vector<MenuSnapshotEntry> &before) {
	const auto live = LivePopupMenus();
	result.menusAfter = int(live.size());
	auto fresh = std::vector<Ui::PopupMenu*>();
	for (const auto menu : live) {
		const auto known = ranges::find_if(before, [&](
				const MenuSnapshotEntry &entry) {
			return (entry.menu.data() == menu);
		});
		if (known != end(before)) {
			// A menu the handler kept from an earlier event and showed
			// again is no new menu, but it is counted, so a no-menu
			// reading says that it happened.
			if (!known->visible && menu->isVisible()) {
				++result.reshown;
			}
			continue;
		} else if (IsSubmenu(menu)) {
			continue;
		}
		fresh.push_back(menu);
	}
	result.fresh = int(fresh.size());
	for (const auto menu : fresh) {
		result.freshMenus.push_back(WidgetDescription(menu));
	}
	if (fresh.empty()) {
		result.refusal = ContextMenuRefusal::NoMenu;
		result.reason = u"no Ui::PopupMenu that was not alive before the "
			"event exists after it (menusBefore=%1 menusAfter=%2 "
			"reshown=%3)"_q
			.arg(result.menusBefore)
			.arg(result.menusAfter)
			.arg(result.reshown);
		return;
	} else if (fresh.size() > 1) {
		result.refusal = ContextMenuRefusal::SeveralMenus;
		result.reason = u"%1 Ui::PopupMenus that were not alive before the "
			"event exist after it, so none is taken for the menu: [%2]"_q
			.arg(result.fresh)
			.arg(result.freshMenus.join(u"; "_q));
		return;
	}
	const auto menu = fresh.front();
	const auto parent = menu->parentWidget();
	result.menu = menu;
	result.identity = WidgetDescription(menu);
	result.parent = parent;
	result.parentIdentity = parent ? WidgetDescription(parent) : u"none"_q;
	result.parentIsReceiver = (parent != nullptr)
		&& (parent == result.receiverWidget.data());
	const auto &actions = menu->actions();
	result.actions = int(actions.size());
	for (const auto action : actions) {
		if (action->isSeparator()) {
			++result.separators;
		} else {
			// An entry whose QAction carries no text stays in place as an
			// empty string, so positions in |texts| match the menu's.
			result.texts.push_back(action->text());
		}
	}
}

const auto kContextFirst = u"Harness context First"_q;
const auto kContextSecond = u"Harness context Second"_q;
const auto kContextDisabled = u"Harness context Disabled"_q;
const auto kContextAbsent = u"Harness context Absent"_q;
const auto kContextTwin = u"Harness context Twin"_q;
const auto kContextSubmenu = u"Harness context Submenu"_q;
const auto kContextInner = u"Harness context Inner"_q;
const auto kContextPremise = u"Harness context Premise"_q;

enum class HostMode {
	Build,
	BuildElsewhere,
	Discard,
	Two,
	Duplicate,
};

// A receiving widget whose context-menu handler builds its menus the way
// the products do (HistoryInner::showContextMenu and friends): a
// base::unique_qptr<Ui::PopupMenu> member replaced on every event, built
// synchronously inside it. Its callbacks report through |ran| and never
// capture the host, so a queued callback outliving it touches nothing it
// owned.
class ContextMenuHost final : public Ui::RpWidget {
public:
	ContextMenuHost(QWidget *parent, Fn<void(QString)> ran);

	void setMode(HostMode mode, QWidget *elsewhere = nullptr);
	void dropMenus();
	[[nodiscard]] int handled() const;
	[[nodiscard]] QContextMenuEvent::Reason lastReason() const;

protected:
	void contextMenuEvent(QContextMenuEvent *e) override;

private:
	void fill(not_null<Ui::PopupMenu*> menu);

	Fn<void(QString)> _ran;
	QPointer<QWidget> _elsewhere;
	base::unique_qptr<Ui::PopupMenu> _menu;
	base::unique_qptr<Ui::PopupMenu> _extra;
	HostMode _mode = HostMode::Build;
	int _handled = 0;
	QContextMenuEvent::Reason _lastReason = QContextMenuEvent::Other;

};

struct ContextMenuSelfTest {
	base::unique_qptr<ContextMenuHost> host;
	base::unique_qptr<ContextMenuHost> remote;
	base::unique_qptr<ContextMenuHost> refusals;
	base::unique_qptr<Ui::RpWidget> elsewhere;
	base::unique_qptr<Ui::RpWidget> premiseOwner;
	base::unique_qptr<QObject> marker;
	QPointer<QAction> premise;
	base::flat_map<QString, int> runs;
	ContextMenuReading positive;
	ContextMenuReading keyboard;
	ContextMenuReading remoteReading;
	ContextMenuReading none;
	ContextMenuReading several;
	ContextMenuReading gone;
	ContextMenuReading duplicate;
	ContextMenuReading delivery;
	MenuTrigger twinTrigger;
	MenuTrigger submenuTrigger;
	MenuTrigger firstTrigger;
	MenuTrigger absentTrigger;
	MenuTrigger disabledTrigger;
	MenuTrigger goneTrigger;
	QString gate;
	QPoint at;
	int receiverRooted = -1;
	int handledBeforeNone = 0;
	int turns = 0;
	int turnsBefore = 0;
	int turnsAtReturn = 0;
	int firstAtReturn = 0;
	int premiseAtReturn = 0;
	int premiseAfterHelper = 0;
	bool built = false;
};

ContextMenuHost::ContextMenuHost(QWidget *parent, Fn<void(QString)> ran)
: RpWidget(parent)
, _ran(std::move(ran)) {
}

void ContextMenuHost::setMode(HostMode mode, QWidget *elsewhere) {
	_mode = mode;
	_elsewhere = elsewhere;
}

void ContextMenuHost::dropMenus() {
	for (const auto menu : { _menu.get(), _extra.get() }) {
		if (menu) {
			menu->hideMenu(true);
		}
	}
	_menu = nullptr;
	_extra = nullptr;
}

int ContextMenuHost::handled() const {
	return _handled;
}

QContextMenuEvent::Reason ContextMenuHost::lastReason() const {
	return _lastReason;
}

void ContextMenuHost::contextMenuEvent(QContextMenuEvent *e) {
	++_handled;
	_lastReason = e->reason();
	// Never a parentless popup: the Qt 6 Wayland branch of
	// PopupMenu::prepareGeometryFor dereferences parentWidget().
	const auto elsewhere = _elsewhere
		? _elsewhere.data()
		: static_cast<QWidget*>(this);
	switch (_mode) {
	case HostMode::Build:
	case HostMode::BuildElsewhere:
		_menu = base::make_unique_q<Ui::PopupMenu>(
			(_mode == HostMode::Build) ? this : elsewhere);
		fill(_menu.get());
		_menu->popup(e->globalPos());
		break;
	case HostMode::Discard:
		// The products' own empty-menu shape (HistoryInner and
		// ListWidget): build, find nothing to add, destroy - so no menu
		// built here survives the handler.
		_menu = base::make_unique_q<Ui::PopupMenu>(this);
		if (_menu->empty()) {
			_menu = nullptr;
		}
		break;
	case HostMode::Two:
		_menu = base::make_unique_q<Ui::PopupMenu>(this);
		fill(_menu.get());
		_extra = base::make_unique_q<Ui::PopupMenu>(elsewhere);
		fill(_extra.get());
		break;
	case HostMode::Duplicate: {
		const auto ran = _ran;
		_menu = base::make_unique_q<Ui::PopupMenu>(this);
		_menu->addAction(kContextTwin, [=] { ran(kContextTwin); });
		_menu->addAction(kContextTwin, [=] { ran(kContextTwin); });
		// The submenu entry built the product way: MenuCallback's
		// .fillSubmenu sets a dummy child QMenu on the entry and creates
		// the submenu through ensureSubmenu, parented to and owned by
		// _menu. Never PopupMenu::addAction(text,
		// std::make_unique<Ui::PopupMenu>(nullptr)): lib_ui's
		// Menu::addAction(text, std::unique_ptr<QMenu>) leaks a
		// parentless QMenu on that route.
		const auto add = Ui::Menu::CreateAddActionCallback(_menu.get());
		add({
			.text = kContextSubmenu,
			.handler = [=] { ran(kContextSubmenu); },
			.fillSubmenu = [=](not_null<Ui::PopupMenu*> submenu) {
				submenu->addAction(kContextInner, [=] {
					ran(kContextInner);
				});
			},
		});
	} break;
	}
	// Accepted in every mode, so the event never propagates to the primary
	// window and to a product handler there.
	e->accept();
}

void ContextMenuHost::fill(not_null<Ui::PopupMenu*> menu) {
	const auto ran = _ran;
	menu->addAction(kContextFirst, [=] { ran(kContextFirst); });
	// Not the last entry, so popup()'s clearLastSeparator keeps it and the
	// reading counts it.
	menu->addSeparator();
	menu->addAction(kContextSecond, [=] { ran(kContextSecond); });
	menu->addAction(kContextDisabled, [=] {
		ran(kContextDisabled);
	})->setEnabled(false);
}

[[nodiscard]] bool BuildContextMenuFixture(
		not_null<ContextMenuSelfTest*> state) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return false;
	}
	const auto parent = window->widget().get();
	const auto raw = state.get();
	const auto ran = [raw](QString text) {
		++raw->runs[text];
	};
	// Never shown: a sent event needs no visibility, each popup is its own
	// window whatever its parent's state, and hidden receivers paint
	// nothing over the primary window a later scenario aims at.
	state->host = base::make_unique_q<ContextMenuHost>(parent, ran);
	state->remote = base::make_unique_q<ContextMenuHost>(parent, ran);
	state->refusals = base::make_unique_q<ContextMenuHost>(parent, ran);
	state->elsewhere = base::make_unique_q<Ui::RpWidget>(parent);
	state->premiseOwner = base::make_unique_q<Ui::RpWidget>(parent);
	state->marker = base::make_unique_q<QObject>();
	state->at = state->host->rect().center();
	return true;
}

// Idempotent: the teardown stage and Runner::onFinish both call it. The
// menus go first, because a host's second menu may be parented to
// |elsewhere|; releasing |premiseOwner| destroys the premise action.
void ReleaseContextMenuFixture(not_null<ContextMenuSelfTest*> state) {
	const auto hosts = {
		state->host.get(),
		state->remote.get(),
		state->refusals.get(),
	};
	for (const auto host : hosts) {
		if (host) {
			host->dropMenus();
		}
	}
	state->host = nullptr;
	state->remote = nullptr;
	state->refusals = nullptr;
	state->elsewhere = nullptr;
	state->premiseOwner = nullptr;
	state->marker = nullptr;
}

// Empty unless ConsoleLockGate answers: only a locked console gates.
[[nodiscard]] QString PopupMenuLockGate(const ConsoleLockReading &console) {
	const auto gate = ConsoleLockGate(console);
	if (gate.isEmpty()) {
		return QString();
	}
	return gate
		+ u"; the application is deactivated on a locked console and Qt "
		"closes a shown Ui::PopupMenu before the next turn, so this stage, "
		"which needs the self-test's menu to stay open across turns, cannot "
		"run - a scheduling condition: rerun on an unlocked console"_q;
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
	// (PopupMenu::actions in ui/widgets/popup_menu.h and popup_menu.cpp).
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
		crl::time timeout,
		Fn<QString()> skipReason) {
	if (open) {
		runner->add({
			.name = u"open popup menu: %1"_q.arg(name),
			.skipReason = skipReason,
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
		[](QWidget *widget) { return PopupMenuDetails(widget); },
		std::move(skipReason));
}

void AppendPopupMenuCaptureSelfTest(not_null<Runner*> runner) {
	struct State {
		Fixture fixture;
		PopupMenuReading openTurn;
		ConsoleLockReading lock;
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
	// Read when each gated stage begins, and kept once locked: a menu
	// popped or held while the console was locked is already closed.
	const auto lockGate = [=] {
		if (!state->lock.locked()) {
			state->lock = ReadConsoleLock();
		}
		return PopupMenuLockGate(state->lock);
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
			// Read in the turn that decides whether Qt closes the menu.
			state->lock = ReadConsoleLock();
			Note(ConsoleLockText(state->lock));
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

	CapturePopupMenu(
		runner,
		u"popup_menu_open"_q,
		resolve,
		{},
		inspect,
		kDefaultStageTimeout,
		lockGate);

	runner->add({
		.name = u"popup menu self-test: close"_q,
		.skipReason = lockGate,
		.run = [=] {
			if (!state->built) {
				return;
			}
			// hideMenu(true) reaches hideFast() -> hideFinished() -> hide()
			// with no opacity animation to wait out (popup_menu.cpp), so the
			// next leg starts from a menu that is provably closed and really
			// exercises CapturePopupMenu's opener branch instead of
			// accepting one that never closed.
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
		inspect,
		kDefaultStageTimeout,
		lockGate);

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

QString ContextMenuRefusalName(ContextMenuRefusal refusal) {
	switch (refusal) {
	case ContextMenuRefusal::None: return u"none"_q;
	case ContextMenuRefusal::ReceiverGone: return u"receiver-gone"_q;
	case ContextMenuRefusal::NoMenu: return u"no-menu"_q;
	case ContextMenuRefusal::SeveralMenus: return u"several-menus"_q;
	}
	Unexpected("Refusal in Test::ContextMenuRefusalName.");
}

ContextMenuLifecycle ReadContextMenuLifecycle(
		const ContextMenuReading &reading) {
	auto result = ContextMenuLifecycle();
	const auto menu = reading.menu.data();
	const auto active = QApplication::activeWindow();
	const auto popup = QApplication::activePopupWidget();
	result.appState = AppStateName();
	result.activeWindow = active ? WidgetDescription(active) : u"none"_q;
	result.focusWindow = FocusWindowName(
		menu,
		reading.receiverWidget.data());
	result.activePopup = !popup
		? u"none"_q
		: (popup == menu)
		? u"self"_q
		: u"other"_q;
	if (!menu) {
		return result;
	}
	result.alive = true;
	// isVisible() as ReadPopupMenu reads it: a popup is its own window, so
	// this is the menu's own shown state whatever its parent's is.
	result.visible = menu->isVisible();
	result.hidden = menu->isHidden();
	result.geometry = menu->geometry();
	result.actions = ReadPopupMenu(menu).actions;
	result.ready = PopupMenuReady(menu);
	return result;
}

QString ContextMenuLifecycleText(const ContextMenuLifecycle &lifecycle) {
	return u"alive=%1 visible=%2 hidden=%3 activePopup=%4 geometry=%5 "
		"actions=%6 ready=%7 appState=%8 activeWindow=%9 focusWindow=%10"_q
		.arg(lifecycle.alive ? 1 : 0)
		.arg(lifecycle.visible ? 1 : 0)
		.arg(lifecycle.hidden ? 1 : 0)
		.arg(lifecycle.activePopup)
		.arg(RectText(lifecycle.geometry))
		.arg(lifecycle.actions)
		.arg(lifecycle.ready ? 1 : 0)
		.arg(lifecycle.appState)
		.arg(lifecycle.activeWindow)
		.arg(lifecycle.focusWindow);
}

ContextMenuReading OpenContextMenu(
		QWidget *receiver,
		QPoint point,
		QContextMenuEvent::Reason reason) {
	auto result = ContextMenuReading();
	result.eventReason = reason;
	result.local = point;
	if (!receiver) {
		result.refusal = ContextMenuRefusal::ReceiverGone;
		result.reason = u"no receiving widget was handed to the reader - a "
			"destroyed QPointer reads as null - so nothing was sent"_q;
		result.atBuild = ReadContextMenuLifecycle(result);
		result.afterSettle = result.atBuild;
		return result;
	}
	result.receiverWidget = receiver;
	result.receiver = WidgetDescription(receiver);
	result.receiverEnabled = receiver->isEnabled();
	result.receiverVisible = receiver->isVisible();
	result.global = receiver->mapToGlobal(point);
	const auto before = SnapshotPopupMenus();
	result.menusBefore = int(before.size());
	auto event = QContextMenuEvent(reason, point, result.global);
	Settle([&] {
		QApplication::sendEvent(receiver, &event);
		result.delivered = true;
		result.accepted = event.isAccepted();
		result.receiverAlive = (result.receiverWidget.data() != nullptr);
		TakeFreshMenu(result, before);
		if (!result.receiverAlive) {
			result.refusal = ContextMenuRefusal::ReceiverGone;
			result.reason = u"the receiving widget was destroyed while it "
				"handled the event, so nothing it built is read as its "
				"menu"_q;
			result.menu.clear();
			result.parent.clear();
		}
		// In the call that built the menu, before any postponed call
		// runs: the reading Run 2 decided every leg by.
		result.atBuild = ReadContextMenuLifecycle(result);
	});
	result.afterSettle = ReadContextMenuLifecycle(result);
	return result;
}

QString ContextMenuDetails(const ContextMenuReading &reading) {
	const auto orNone = [](const QString &value) {
		return value.isEmpty() ? u"none"_q : value;
	};
	// One substitution per field, so a product's text that carries a %N
	// marker is never re-substituted by a later arg().
	const auto fields = QStringList{
		u"built=%1"_q.arg(reading.built() ? 1 : 0),
		u"refusal=%1"_q.arg(ContextMenuRefusalName(reading.refusal)),
		u"reason=%1"_q.arg(EventReasonName(reading.eventReason)),
		u"receiver=%1"_q.arg(orNone(reading.receiver)),
		u"enabled=%1"_q.arg(reading.receiverEnabled ? 1 : 0),
		u"visible=%1"_q.arg(reading.receiverVisible ? 1 : 0),
		u"local=%1,%2"_q.arg(reading.local.x()).arg(reading.local.y()),
		u"global=%1,%2"_q.arg(reading.global.x()).arg(reading.global.y()),
		u"delivered=%1"_q.arg(reading.delivered ? 1 : 0),
		u"accepted=%1"_q.arg(reading.accepted ? 1 : 0),
		u"receiverAlive=%1"_q.arg(reading.receiverAlive ? 1 : 0),
		u"menusBefore=%1"_q.arg(reading.menusBefore),
		u"menusAfter=%1"_q.arg(reading.menusAfter),
		u"fresh=%1"_q.arg(reading.fresh),
		u"reshown=%1"_q.arg(reading.reshown),
		u"freshMenus=[%1]"_q.arg(reading.freshMenus.join(u"; "_q)),
		u"menu=%1"_q.arg(orNone(reading.identity)),
		u"parent=%1"_q.arg(orNone(reading.parentIdentity)),
		u"parentIsReceiver=%1"_q.arg(reading.parentIsReceiver ? 1 : 0),
		u"texts=[%1]"_q.arg(reading.texts.join(u" | "_q)),
		u"actions=%1"_q.arg(reading.actions),
		u"separators=%1"_q.arg(reading.separators),
		u"atBuild={%1}"_q.arg(ContextMenuLifecycleText(reading.atBuild)),
		u"afterSettle={%1}"_q.arg(
			ContextMenuLifecycleText(reading.afterSettle)),
	};
	const auto line = u"context menu: "_q + fields.join(u" "_q);
	return reading.reason.isEmpty()
		? line
		: (line + u" - "_q + reading.reason);
}

QString MenuTriggerRefusalName(MenuTriggerRefusal refusal) {
	switch (refusal) {
	case MenuTriggerRefusal::None: return u"none"_q;
	case MenuTriggerRefusal::MenuGone: return u"menu-gone"_q;
	case MenuTriggerRefusal::NoSuchAction: return u"no-such-action"_q;
	case MenuTriggerRefusal::SeveralActions: return u"several-actions"_q;
	case MenuTriggerRefusal::ActionDisabled: return u"action-disabled"_q;
	case MenuTriggerRefusal::SubmenuEntry: return u"submenu-entry"_q;
	}
	Unexpected("Refusal in Test::MenuTriggerRefusalName.");
}

MenuTrigger TriggerContextMenuAction(
		const ContextMenuReading &reading,
		const QString &text) {
	auto result = MenuTrigger();
	result.text = text;
	const auto menu = dynamic_cast<Ui::PopupMenu*>(reading.menu.data());
	if (!menu) {
		result.refusal = MenuTriggerRefusal::MenuGone;
		result.reason = u"the reading holds no live Ui::PopupMenu - its "
			"refusal is %1, or the menu was destroyed since - so nothing "
			"was triggered"_q.arg(ContextMenuRefusalName(reading.refusal));
		result.after = ReadContextMenuLifecycle(reading);
		return result;
	}
	auto texts = QStringList();
	auto matches = std::vector<QAction*>();
	for (const auto action : menu->actions()) {
		if (action->isSeparator()) {
			continue;
		}
		texts.push_back(action->text());
		if (action->text() == text) {
			matches.push_back(action);
		}
	}
	result.matches = int(matches.size());
	if (matches.empty()) {
		result.refusal = MenuTriggerRefusal::NoSuchAction;
		result.reason = u"no entry of the menu reads exactly this text, so "
			"nothing was triggered; the menu holds [%1]"_q
			.arg(texts.join(u" | "_q));
	} else if (matches.size() > 1) {
		result.refusal = MenuTriggerRefusal::SeveralActions;
		result.reason = u"%1 entries read exactly this text, so the one a "
			"user would pick is ambiguous and none was triggered"_q
			.arg(result.matches);
	} else if (!matches.front()->isEnabled()) {
		result.refusal = MenuTriggerRefusal::ActionDisabled;
		result.reason = u"the entry is disabled: a user cannot trigger it, "
			"and Qt 5's QAction::activate would still emit triggered for "
			"it while Qt 6's ignores it, so it is refused on both"_q;
	} else if (matches.front()->menu()) {
		result.refusal = MenuTriggerRefusal::SubmenuEntry;
		result.reason = u"the entry carries a QMenu, so a user's click opens "
			"its submenu instead of running its callback, and nothing was "
			"triggered"_q;
	} else {
		const auto action = QPointer<QAction>(matches.front());
		Settle([&] {
			action->trigger();
			// Exactly the calls posted to this action - the queued
			// CreateAction callback - and no other receiver's.
			if (action) {
				QCoreApplication::sendPostedEvents(
					action.data(),
					QEvent::MetaCall);
			}
		});
		result.triggered = true;
		result.actionAlive = !action.isNull();
	}
	result.after = ReadContextMenuLifecycle(reading);
	return result;
}

QString MenuTriggerDetails(const MenuTrigger &trigger) {
	const auto fields = QStringList{
		u"text=\"%1\""_q.arg(trigger.text),
		u"triggered=%1"_q.arg(trigger.triggered ? 1 : 0),
		u"matches=%1"_q.arg(trigger.matches),
		u"actionAlive=%1"_q.arg(trigger.actionAlive ? 1 : 0),
		u"refusal=%1"_q.arg(MenuTriggerRefusalName(trigger.refusal)),
		u"after={%1}"_q.arg(ContextMenuLifecycleText(trigger.after)),
	};
	const auto line = u"menu trigger: "_q + fields.join(u" "_q);
	return trigger.reason.isEmpty()
		? line
		: (line + u" - "_q + trigger.reason);
}

void AppendContextMenuReaderSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<ContextMenuSelfTest>();
	// The fixture callbacks and the queued premise and marker capture the
	// state by a plain pointer: it outlives all of them, because the
	// fixture is released by the teardown stage and by onFinish while the
	// shared_ptr itself lives on in the static runner's stages.
	const auto raw = state.get();
	runner->onFinish([=] { ReleaseContextMenuFixture(state.get()); });
	const auto gate = [=] {
		return state->gate;
	};
	const auto expected = QStringList{
		kContextFirst,
		kContextSecond,
		kContextDisabled,
	};
	// Reads without inserting, so a count read in a Check never creates
	// the entry it reads.
	const auto runCount = [=](const QString &text) {
		const auto i = state->runs.find(text);
		return (i != state->runs.end()) ? i->second : 0;
	};

	runner->add({
		.name = u"context menu reader self-test: a menu built in the "
			"receiving widget"_q,
		.run = [=] {
			state->built = BuildContextMenuFixture(state.get());
			Check(
				state->built,
				u"fixture gate: a primary window to parent the self-test "
				"widgets to"_q,
				state->built
					? QString()
					: u"Core::App().activePrimaryWindow() is null"_q);
			if (!state->built) {
				state->gate = u"fixture gate: "
					"Core::App().activePrimaryWindow() is null, so the "
					"self-test widgets have no window to live in"_q;
				return;
			}
			state->positive = OpenContextMenu(state->host.get(), state->at);
		},
		.then = [=] {
			if (!state->built) {
				return;
			}
			const auto &positive = state->positive;
			const auto details = ContextMenuDetails(positive);
			const auto handled = state->host->handled();
			Check(
				positive.built()
					&& (positive.fresh == 1)
					&& positive.delivered,
				u"positive: exactly one new Ui::PopupMenu was built in the "
				"turn the event was delivered"_q,
				details);
			Check(
				(positive.texts == expected)
					&& (positive.actions == 4)
					&& (positive.separators == 1),
				u"positive: the reading holds exactly the texts the handler "
				"added, in order, without the separator"_q,
				details);
			Check(
				!positive.parent.isNull()
					&& (positive.parent.data() == state->host.get())
					&& positive.parentIsReceiver
					&& (handled == 1)
					&& positive.accepted,
				u"positive: the menu is the receiving widget's own child and "
				"the handler ran once"_q,
				details + u" handled=%1"_q.arg(handled));
			Check(
				positive.atBuild.alive
					&& positive.atBuild.visible
					&& positive.atBuild.ready
					&& (positive.atBuild.activePopup == u"self"_q)
					&& positive.afterSettle.alive,
				u"positive: at build the popup is shown, ready and the "
				"application's active popup, and it is still alive after "
				"the postponed calls settled"_q,
				details);
			Note(u"positive: one turn later the same menu reads {%1} - an "
				"observation, never a check"_q.arg(ContextMenuLifecycleText(
					ReadContextMenuLifecycle(positive))));
		},
	});

	runner->add({
		.name = u"context menu reader self-test: the reason passes through "
			"and an earlier reading outlives its menu"_q,
		.skipReason = gate,
		.run = [=] {
			// The host is still in Build mode, and its handler replaces the
			// menu it keeps - which destroys the positive reading's menu.
			state->keyboard = OpenContextMenu(
				state->host.get(),
				state->at,
				QContextMenuEvent::Keyboard);
		},
		.then = [=] {
			const auto &keyboard = state->keyboard;
			const auto handlerReason = state->host->lastReason();
			Check(
				keyboard.built()
					&& (keyboard.fresh == 1)
					&& (keyboard.texts == expected)
					&& (keyboard.eventReason == QContextMenuEvent::Keyboard)
					&& (handlerReason == QContextMenuEvent::Keyboard),
				u"keyboard: a Keyboard-reason event reaches the handler as "
				"Keyboard and builds the same menu"_q,
				ContextMenuDetails(keyboard)
					+ u" handlerReason=%1"_q.arg(
						EventReasonName(handlerReason)));
			const auto &positive = state->positive;
			const auto details = ContextMenuDetails(positive);
			const auto now = ReadContextMenuLifecycle(positive);
			Check(
				positive.menu.isNull()
					&& (positive.texts == expected)
					&& !positive.identity.isEmpty()
					&& details.contains(positive.identity)
					&& !now.alive,
				u"a reading stays readable after its menu is destroyed: the "
				"pointer is null and the texts and identity are values"_q,
				details + u" now={%1}"_q.arg(ContextMenuLifecycleText(now)));
		},
	});

	runner->add({
		.name = u"context menu reader self-test: a menu the handler parents "
			"to another widget"_q,
		.skipReason = gate,
		.run = [=] {
			state->remote->setMode(
				HostMode::BuildElsewhere,
				state->elsewhere.get());
			state->remoteReading = OpenContextMenu(
				state->remote.get(),
				state->at);
			// Run 2's receiver-rooted search, taken in the same turn, as
			// the control: it misses a menu parented elsewhere.
			state->receiverRooted = int(
				FindAll<Ui::PopupMenu>(state->remote.get()).size());
		},
		.then = [=] {
			const auto &reading = state->remoteReading;
			const auto details = ContextMenuDetails(reading);
			Check(
				reading.built()
					&& (reading.fresh == 1)
					&& (reading.texts == expected),
				u"elsewhere: the reader takes the one new menu although the "
				"handler parented it to another widget, with the same "
				"texts"_q,
				details);
			Check(
				!reading.parent.isNull()
					&& (reading.parent.data() == state->elsewhere.get())
					&& !reading.parentIsReceiver
					&& (state->receiverRooted == 0),
				u"elsewhere: the menu's parent is the other widget, and a "
				"search rooted at the receiving widget finds no menu - the "
				"application-wide snapshot is what found it"_q,
				details
					+ u" receiverRooted=%1"_q.arg(state->receiverRooted));
			state->remote->dropMenus();
		},
	});

	runner->add({
		.name = u"context menu reader self-test: a widget whose handler "
			"builds no menu"_q,
		.skipReason = gate,
		.run = [=] {
			state->handledBeforeNone = state->host->handled();
			state->host->setMode(HostMode::Discard);
			state->none = OpenContextMenu(state->host.get(), state->at);
		},
		.then = [=] {
			const auto &none = state->none;
			const auto handled = state->host->handled();
			Check(
				(none.refusal == ContextMenuRefusal::NoMenu)
					&& (ContextMenuRefusalName(none.refusal) == u"no-menu"_q)
					&& none.menu.isNull()
					&& (none.fresh == 0)
					&& none.delivered
					&& (handled == state->handledBeforeNone + 1),
				u"no menu: the event reached a handler that built and "
				"discarded an empty menu, and the reader refuses by name as "
				"no-menu with no menu"_q,
				ContextMenuDetails(none)
					+ u" handled=%1->%2"_q
						.arg(state->handledBeforeNone)
						.arg(handled));
			const auto &positive = state->positive;
			const auto &keyboard = state->keyboard;
			const auto receiver = none.receiverWidget.data();
			Check(
				positive.built()
					&& keyboard.built()
					&& (receiver != nullptr)
					&& (positive.receiverWidget.data() == receiver)
					&& (keyboard.receiverWidget.data() == receiver),
				u"no menu: the refusal stands beside positive controls the "
				"same helper built in the same widget, the rule a product "
				"no-menu reading needs"_q,
				u"positive: built=%1 receiver=%2 texts=[%3]; keyboard: "
				"built=%4 receiver=%5 texts=[%6]; none: receiver=%7"_q
					.arg(positive.built() ? 1 : 0)
					.arg(positive.receiver)
					.arg(positive.texts.join(u" | "_q))
					.arg(keyboard.built() ? 1 : 0)
					.arg(keyboard.receiver)
					.arg(keyboard.texts.join(u" | "_q))
					.arg(none.receiver));
		},
	});

	runner->add({
		.name = u"context menu reader self-test: refusals"_q,
		.skipReason = gate,
		.run = [=] {
			const auto refusals = state->refusals.get();
			refusals->setMode(HostMode::Two, state->elsewhere.get());
			state->several = OpenContextMenu(refusals, state->at);
			refusals->dropMenus();
			state->gone = OpenContextMenu(nullptr, state->at);
			refusals->setMode(HostMode::Duplicate);
			state->duplicate = OpenContextMenu(refusals, state->at);
			state->twinTrigger = TriggerContextMenuAction(
				state->duplicate,
				kContextTwin);
			state->submenuTrigger = TriggerContextMenuAction(
				state->duplicate,
				kContextSubmenu);
			refusals->dropMenus();
		},
		// The trigger refusals are judged here, a turn later, so a queued
		// callback a silent trigger had posted would already have run.
		.then = [=] {
			const auto &several = state->several;
			Check(
				(several.refusal == ContextMenuRefusal::SeveralMenus)
					&& (ContextMenuRefusalName(several.refusal)
						== u"several-menus"_q)
					&& (several.fresh == 2)
					&& (several.freshMenus.size() == 2)
					&& several.menu.isNull(),
				u"several menus: a handler that builds two new menus is "
				"refused by name, both are named and neither is taken"_q,
				ContextMenuDetails(several));
			const auto &gone = state->gone;
			Check(
				(gone.refusal == ContextMenuRefusal::ReceiverGone)
					&& (ContextMenuRefusalName(gone.refusal)
						== u"receiver-gone"_q)
					&& !gone.delivered
					&& gone.menu.isNull(),
				u"receiver gone: a null receiving widget is refused by name "
				"and nothing is sent"_q,
				ContextMenuDetails(gone));
			const auto &duplicate = state->duplicate;
			const auto duplicateTexts = QStringList{
				kContextTwin,
				kContextTwin,
				kContextSubmenu,
			};
			Check(
				duplicate.built()
					&& (duplicate.fresh == 1)
					&& (duplicate.texts == duplicateTexts),
				u"a submenu the handler creates is not counted as a second "
				"new menu"_q,
				ContextMenuDetails(duplicate));
			const auto &twin = state->twinTrigger;
			const auto twinRuns = runCount(kContextTwin);
			Check(
				(twin.refusal == MenuTriggerRefusal::SeveralActions)
					&& (MenuTriggerRefusalName(twin.refusal)
						== u"several-actions"_q)
					&& (twin.matches == 2)
					&& !twin.triggered
					&& (twinRuns == 0),
				u"trigger: two entries reading the same text are refused by "
				"name as ambiguous, and neither callback ran by a later "
				"turn"_q,
				MenuTriggerDetails(twin) + u" twinRuns=%1"_q.arg(twinRuns));
			const auto &submenu = state->submenuTrigger;
			const auto submenuRuns = runCount(kContextSubmenu);
			const auto innerRuns = runCount(kContextInner);
			Check(
				(submenu.refusal == MenuTriggerRefusal::SubmenuEntry)
					&& (MenuTriggerRefusalName(submenu.refusal)
						== u"submenu-entry"_q)
					&& !submenu.triggered
					&& (submenuRuns == 0)
					&& (innerRuns == 0),
				u"trigger: an entry that opens a submenu is refused by name, "
				"and neither its own handler nor the submenu's entry ran by "
				"a later turn"_q,
				MenuTriggerDetails(submenu)
					+ u" submenuRuns=%1 innerRuns=%2"_q
						.arg(submenuRuns)
						.arg(innerRuns));
		},
	});

	runner->add({
		.name = u"context menu reader self-test: triggering an entry "
			"delivers its CreateAction callback before the call returns"_q,
		.skipReason = gate,
		.run = [=] {
			state->host->setMode(HostMode::Build);
			state->delivery = OpenContextMenu(state->host.get(), state->at);
			// The premise, owned by a plain widget rather than the popup,
			// so a popup the console closes cannot drop its queued call: a
			// raw trigger() of a CreateAction action leaves its callback
			// queued.
			state->premise = Ui::Menu::CreateAction(
				state->premiseOwner.get(),
				kContextPremise,
				[raw] { ++raw->runs[kContextPremise]; }).get();
			state->premise->trigger();
			state->premiseAtReturn = runCount(kContextPremise);
			// A queued call to another receiver, posted just before the
			// trigger: it runs only when the event loop turns.
			state->turnsBefore = state->turns;
			InvokeQueued(state->marker.get(), [raw] { ++raw->turns; });
			state->firstTrigger = TriggerContextMenuAction(
				state->delivery,
				kContextFirst);
			state->firstAtReturn = runCount(kContextFirst);
			state->turnsAtReturn = state->turns;
			state->premiseAfterHelper = runCount(kContextPremise);
			state->absentTrigger = TriggerContextMenuAction(
				state->delivery,
				kContextAbsent);
			state->disabledTrigger = TriggerContextMenuAction(
				state->delivery,
				kContextDisabled);
		},
		.then = [=] {
			const auto &first = state->firstTrigger;
			const auto firstRuns = runCount(kContextFirst);
			const auto counters = u"firstAtReturn=%1 firstRunsNow=%2 "
				"turnsBefore=%3 turnsAtReturn=%4 turnsNow=%5"_q
				.arg(state->firstAtReturn)
				.arg(firstRuns)
				.arg(state->turnsBefore)
				.arg(state->turnsAtReturn)
				.arg(state->turns);
			Check(
				state->delivery.built()
					&& first.triggered
					&& (first.refusal == MenuTriggerRefusal::None)
					&& (first.matches == 1)
					&& (state->firstAtReturn == 1)
					&& (state->turnsAtReturn == state->turnsBefore),
				u"trigger: the CreateAction callback's sentinel is set when "
				"TriggerContextMenuAction returns, before any event-loop "
				"turn - a queued marker posted just before it had not run "
				"yet"_q,
				MenuTriggerDetails(first) + u" "_q + counters);
			Check(
				(state->turns > state->turnsBefore) && (firstRuns == 1),
				u"trigger: the marker ran on a later turn, so its silence at "
				"the return was not vacuous, and the callback ran exactly "
				"once"_q,
				counters);
			const auto premiseRuns = runCount(kContextPremise);
			Check(
				(state->premiseAtReturn == 0)
					&& (state->premiseAfterHelper == 0)
					&& (premiseRuns == 1),
				u"premise: a raw QAction::trigger() of a "
				"Ui::Menu::CreateAction action leaves its callback queued "
				"until a later turn, and the helper delivered no other "
				"receiver's queued call"_q,
				u"premiseAtReturn=%1 premiseAfterHelper=%2 "
				"premiseRunsNow=%3"_q
					.arg(state->premiseAtReturn)
					.arg(state->premiseAfterHelper)
					.arg(premiseRuns));
			const auto &absent = state->absentTrigger;
			Check(
				(absent.refusal == MenuTriggerRefusal::NoSuchAction)
					&& (MenuTriggerRefusalName(absent.refusal)
						== u"no-such-action"_q)
					&& !absent.triggered
					&& absent.reason.contains(kContextFirst),
				u"trigger: an absent text is refused by name, listing the "
				"texts the menu holds"_q,
				MenuTriggerDetails(absent));
			const auto &disabled = state->disabledTrigger;
			const auto disabledRuns = runCount(kContextDisabled);
			const auto secondRuns = runCount(kContextSecond);
			Check(
				(disabled.refusal == MenuTriggerRefusal::ActionDisabled)
					&& (MenuTriggerRefusalName(disabled.refusal)
						== u"action-disabled"_q)
					&& !disabled.triggered
					&& (disabledRuns == 0)
					&& (secondRuns == 0),
				u"trigger: a disabled entry is refused by name rather than "
				"triggered, and no other entry's callback ran by a later "
				"turn"_q,
				MenuTriggerDetails(disabled)
					+ u" disabledRuns=%1 secondRuns=%2"_q
						.arg(disabledRuns)
						.arg(secondRuns));
			state->host->dropMenus();
			state->goneTrigger = TriggerContextMenuAction(
				state->delivery,
				kContextFirst);
			const auto &gone = state->goneTrigger;
			const auto firstRunsAfter = runCount(kContextFirst);
			Check(
				(gone.refusal == MenuTriggerRefusal::MenuGone)
					&& (MenuTriggerRefusalName(gone.refusal)
						== u"menu-gone"_q)
					&& !gone.triggered
					&& (firstRunsAfter == 1),
				u"trigger: a reading whose menu was destroyed is refused by "
				"name as menu-gone, and First's callback count is "
				"unchanged"_q,
				MenuTriggerDetails(gone)
					+ u" firstRuns=%1"_q.arg(firstRunsAfter));
		},
	});

	runner->add({
		.name = u"context menu reader self-test: teardown"_q,
		.run = [=] {
			// Last on purpose, and with no gate, so it also runs after the
			// fixture gate fired; onFinish repeats the release on every path
			// that skips this stage.
			ReleaseContextMenuFixture(state.get());
			Note(u"context menu reader self-test: fixture released, host=%1 "
				"remote=%2 refusals=%3 elsewhere=%4 premiseOwner=%5 "
				"marker=%6"_q
				.arg(state->host ? 1 : 0)
				.arg(state->remote ? 1 : 0)
				.arg(state->refusals ? 1 : 0)
				.arg(state->elsewhere ? 1 : 0)
				.arg(state->premiseOwner ? 1 : 0)
				.arg(state->marker ? 1 : 0));
		},
	});
}

} // namespace Test

#endif // _DEBUG
