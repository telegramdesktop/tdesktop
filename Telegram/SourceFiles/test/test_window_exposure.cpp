/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_window_exposure.h"

#include "base/event_filter.h"
#include "base/unique_qptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "ui/widgets/separate_panel.h"
#include "ui/rp_widget.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtGui/QScreen>

#include <array>

#ifdef Q_OS_MAC
#include <CoreGraphics/CoreGraphics.h>
#endif // Q_OS_MAC

#include "styles/palette.h"
#include "styles/style_layers.h"

namespace Test {
namespace {

// Only on macOS does a covered window turn unexposed in a way a client can
// repair by stacking; see the off-macOS paragraph in the header.
#ifdef Q_OS_MAC
constexpr auto kStacking = true;
#else // Q_OS_MAC
constexpr auto kStacking = false;
#endif // Q_OS_MAC

[[nodiscard]] bool StayOnTop(QWindow *window) {
	return window && (window->flags() & Qt::WindowStaysOnTopHint);
}

[[nodiscard]] QString ApplicationStateName(Qt::ApplicationState state) {
	switch (state) {
	case Qt::ApplicationSuspended: return u"suspended"_q;
	case Qt::ApplicationHidden: return u"hidden"_q;
	case Qt::ApplicationInactive: return u"inactive"_q;
	case Qt::ApplicationActive: return u"active"_q;
	}
	Unexpected("State in Test::ApplicationStateName.");
}

// The focus window may be any window of the process, not only a widget's,
// so it is named by its own QWindow: a widget's QWindow carries the name Qt
// derives from the widget's (<objectName>Window, or <Class>ClassWindow,
// QWidgetWindow::updateObjectName), and any other window without a name is
// named by its class.
[[nodiscard]] QString WindowIdentity(QWindow *window) {
	if (!window) {
		return u"none"_q;
	}
	const auto name = window->objectName();
	return u"%1 %2"_q.arg(
		(name.isEmpty()
			? QString::fromLatin1(window->metaObject()->className())
			: name),
		RectText(window->geometry()));
}

[[nodiscard]] WindowExposure ReadGlobal() {
	auto result = WindowExposure();
	result.applicationState = QGuiApplication::applicationState();
	result.focusWindow = QGuiApplication::focusWindow();
	result.focusIdentity = WindowIdentity(result.focusWindow.data());
	return result;
}

} // namespace

WindowExposure ReadWindowExposure(QWidget *widget) {
	auto result = ReadGlobal();
	if (!widget) {
		result.refusal = u"null-widget: no widget was given"_q;
		return result;
	}
	const auto top = widget->window();
	const auto handle = top->windowHandle();
	result.identity = WidgetDescription(top);
	if (handle) {
		result.identity += u" frame="_q + RectText(handle->frameGeometry());
		result.window = handle;
		result.exposed = handle->isExposed();
		result.active = handle->isActive();
		result.stayOnTop = StayOnTop(handle);
	}
	if (top->isMinimized()
		|| (handle && (handle->windowStates() & Qt::WindowMinimized))) {
		result.refusal = u"minimized: stacking cannot expose a minimized "
			u"window"_q;
	} else if (!handle) {
		result.refusal = u"hidden: the widget's own window carries no "
			u"QWindow handle - it was never shown"_q;
	} else if (top->isHidden()) {
		result.refusal = u"hidden: isHidden()"_q;
	} else if (!handle->isVisible()) {
		result.refusal = u"hidden: the QWindow is not visible, for example "
			u"Qt::WA_DontShowOnScreen"_q;
	}
	return result;
}

WindowExposure KeepWindowExposed(QWidget *widget) {
	auto result = ReadWindowExposure(widget);
	if (!result.refusal.isEmpty() || !kStacking || result.stayOnTop) {
		return result;
	}
	result.window->setFlag(Qt::WindowStaysOnTopHint, true);
	result.applied = StayOnTop(result.window.data());
	return result;
}

void RestoreWindowExposure(const WindowExposure &reading) {
	// An applied reading had the hint clear before, and QWindow::setFlags
	// returns early on an unchanged value (qwindow.cpp), so a
	// repeated undo is a no-op.
	if (reading.applied && reading.window) {
		reading.window->setFlag(Qt::WindowStaysOnTopHint, false);
	}
}

QString WindowExposureText(const WindowExposure &reading) {
	const auto bit = [](bool value) {
		return value ? u"1"_q : u"0"_q;
	};
	const auto line = u"window exposure: target=%1 exposed=%2 active=%3 "
		u"stayOnTop=%4 applied=%5 stacking=%6 appState=%7 "
		u"focusWindow=%8"_q.arg(
			(reading.identity.isEmpty() ? u"none"_q : reading.identity),
			bit(reading.exposed),
			bit(reading.active),
			bit(reading.stayOnTop),
			bit(reading.applied),
			(kStacking ? u"macos"_q : u"none"_q),
			ApplicationStateName(reading.applicationState),
			reading.focusIdentity);
	return reading.refusal.isEmpty()
		? line
		: (line + u" - %1"_q.arg(reading.refusal));
}

namespace {

// The self-test's bounds. A freshly shown window is exposed and paints its
// first frame well within kWarmupBound, the window server reports a covered
// window occluded - and a lifted one visible again - well within
// kOcclusionBound and kExposureBound, and an exposed window paints a
// requested update() well within kExposureBound too. kSettleBound is how
// long the target has been shown before the cover goes up, because a
// freshly shown window gets one more layer display about half a second
// after it is shown and Qt counts any layer display as an expose
// (QCocoaWindow::handleExposeEvent), so a target covered sooner reads
// isExposed() true while the window server still reports it occluded.
// kHeldWindow is how long the covered target, which nothing asks to
// repaint, has to stay unexposed and unpainted for the premise to hold -
// the symptom observed, never provoked. Every bound is well inside
// kDefaultStageTimeout, so each wait returns on its deadline and the
// stage's then decides with values instead of an opaque stage timeout.
constexpr auto kWarmupBound = crl::time(3000);
constexpr auto kSettleBound = crl::time(2000);
constexpr auto kOcclusionBound = crl::time(3000);
constexpr auto kHeldWindow = crl::time(500);
constexpr auto kExposureBound = crl::time(3000);

// Every harness window's objectName() starts with it. Qt names a widget's
// QWindow <objectName>Window and follows renames (the QWidgetWindow
// constructor and QWidgetWindow::updateObjectName), so the teardown counts
// exactly the harness's own top-level QWindows by it and nothing the
// product owns.
const auto kNamePrefix = u"keep_exposed_"_q;

enum class LegKind {
	Window,
	Panel,
};

struct Leg {
	LegKind kind = LegKind::Window;
	QString name;
	base::unique_qptr<Ui::RpWidget> target;
	base::unique_qptr<Ui::RpWidget> cover;
	Qt::WindowFlags widgetFlags;
	WindowExposure baseline;
	WindowExposure control;
	WindowExposure reading;
	crl::time shownAt = 0;
	crl::time started = 0;
	int paints = 0;
	int paintMark = 0;
	bool built = false;
	bool exposedBeforeCover = false;
	bool occluded = false;
	bool held = false;
	bool exposedAfterHelper = false;
};

struct State {
	std::array<Leg, 2> legs;
};

[[nodiscard]] QString FlagsText(Qt::WindowFlags flags) {
	return u"0x"_q + QString::number(uint(flags), 16);
}

[[nodiscard]] QString StageName(const Leg &leg, const QString &what) {
	return u"keep-exposed self-test: %1: %2"_q.arg(leg.name, what);
}

[[nodiscard]] crl::time Elapsed(const Leg &leg) {
	return crl::now() - leg.started;
}

// The one row a leg's waits and decisions print: the live reading of its
// target, the paints it has received, the mark the premise and the effect
// compare against, and the time since the current stage's own start.
[[nodiscard]] QString LegText(const Leg &leg) {
	return u"%1 paints=%2 paintMark=%3 elapsed=%4"_q.arg(
		WindowExposureText(ReadWindowExposure(leg.target.get())),
		QString::number(leg.paints),
		QString::number(leg.paintMark),
		QString::number(Elapsed(leg)));
}

// The paint receipt the premise and the effect read: QEvent::Paint
// delivered to the target itself, never to one of its children. The filter
// is a child of the target and dies with it, so a released leg leaves
// nothing counting.
void CountPaints(not_null<Leg*> leg) {
	base::install_event_filter(leg->target.get(), [=](not_null<QEvent*> e) {
		if (e->type() == QEvent::Paint) {
			++leg->paints;
		}
		return base::EventFilterResult::Continue;
	});
}

[[nodiscard]] bool SameActivation(
		const WindowExposure &a,
		const WindowExposure &b) {
	return (a.active == b.active)
		&& (a.focusWindow.data() == b.focusWindow.data())
		&& (a.applicationState == b.applicationState);
}

[[nodiscard]] int HarnessWindowCount() {
	auto result = 0;
	for (const auto window : QGuiApplication::topLevelWindows()) {
		if (window->objectName().startsWith(kNamePrefix)) {
			++result;
		}
	}
	return result;
}

// A parentless Ui::RpWidget is a Qt::Window: a titled QNSWindow at the
// normal level, like the main window. Not a Ui::RpWindow: its mac helper
// creates the native window in its constructor, before
// Qt::WA_ShowWithoutActivating could be copied to the QWindow's
// _q_showWithoutActivating property (only inside QWidgetPrivate::create).
// Null without a primary screen, which leaves the leg unbuilt.
[[nodiscard]] base::unique_qptr<Ui::RpWidget> MakeOrdinaryWindow() {
	const auto screen = QGuiApplication::primaryScreen();
	if (!screen) {
		return nullptr;
	}
	auto result = base::make_unique_q<Ui::RpWidget>(nullptr);
	const auto raw = result.get();
	raw->setObjectName(kNamePrefix + u"target"_q);
	raw->setWindowTitle(u"Harness exposure window"_q);
	raw->setAttribute(Qt::WA_ShowWithoutActivating);
	raw->setAttribute(Qt::WA_QuitOnClose, false);
	raw->setAttribute(Qt::WA_OpaquePaintEvent);
	raw->paintOn([=](QPainter &p) {
		p.fillRect(raw->rect(), st::windowBgOver);
	});
	auto geometry = QRect(
		QPoint(),
		QSize(st::boxWidth, st::separatePanelTitleHeight * 3));
	geometry.moveCenter(screen->availableGeometry().center());
	raw->setGeometry(geometry);
	return result;
}

// A default Ui::SeparatePanel needs no session, account or network, and its
// initLayout sets Qt::WindowStaysOnTopHint among its widget flags. The hint
// is cleared first, while the panel has no native window yet, so the
// re-parent inside QWidget::setWindowFlag hides nothing - the product shape
// of wallet_panel.cpp and test_panel.cpp. The constructor creates no native
// window either (Platform::FullScreenEvents reads internalWinId() only), so
// Qt::WA_ShowWithoutActivating still reaches the QWindow at creation.
// Never showAndActivate(), showInner() or setHideOnDeactivate(false): each
// reaches showAndActivate() (separate_panel.cpp),
// which calls raise() and activateWindow(). No showBox() or showLayer()
// either - the fixture needs no content.
[[nodiscard]] base::unique_qptr<Ui::RpWidget> MakePanel() {
	auto result = base::make_unique_q<Ui::SeparatePanel>();
	const auto raw = result.get();
	raw->setWindowFlag(Qt::WindowStaysOnTopHint, false);
	raw->setObjectName(kNamePrefix + u"panel"_q);
	raw->setAttribute(Qt::WA_ShowWithoutActivating);
	raw->setAttribute(Qt::WA_QuitOnClose, false);
	raw->setTitle(rpl::single(u"Harness exposure panel"_q));
	raw->setInnerSize(QSize(
		st::separatePanelTitleHeight * 6,
		st::separatePanelTitleHeight * 4));
	return result;
}

// A plain show() of a Qt::Window or a Qt::Dialog raises nothing
// (QWidgetPrivate::show_helper), and with _q_showWithoutActivating set
// QCocoaWindow::setVisible takes the orderFront: branch, not
// makeKeyAndOrderFront: (QCocoaWindow::setVisible and
// QCocoaWindow::shouldRefuseKeyWindowAndFirstResponder), so neither
// the application nor the key window changes. Qt::WA_QuitOnClose is
// cleared on every harness top level because ~QWidget still runs
// close_helper(CloseNoEvent) for a created, visible one, and that path can
// reach QGuiApplicationPrivate::maybeQuit().
void BuildTarget(not_null<Leg*> leg) {
	leg->target = (leg->kind == LegKind::Panel)
		? MakePanel()
		: MakeOrdinaryWindow();
	if (!leg->target) {
		return;
	}
	CountPaints(leg);
	leg->target->show();
	leg->widgetFlags = leg->target->windowFlags();
}

// The occlusion control: an opaque titled top level over the target's
// whole frame, ordered after it by the same application. Titled because
// on macOS 27 the window server never reported the target occluded under a
// frameless Qt top level (style mask 0xe), not even with its NSWindow and
// layer forced opaque, while a titled one (0xf) occluded it within 1.5 s.
// Its flags are set before it is created, and Qt::WindowDoesNotAcceptFocus
// keeps it from ever becoming the key window. Never Qt::Tool - a tool
// window is raised on show, and raise() activates - and never the
// stay-on-top hint.
void BuildCover(not_null<Leg*> leg) {
	const auto handle = leg->target->windowHandle();
	auto cover = base::make_unique_q<Ui::RpWidget>(nullptr);
	const auto raw = cover.get();
	raw->setWindowFlags(Qt::WindowFlags(Qt::Window)
		| Qt::WindowDoesNotAcceptFocus);
	raw->setObjectName(kNamePrefix + u"cover"_q);
	raw->setWindowTitle(u"Harness exposure cover"_q);
	raw->setAttribute(Qt::WA_ShowWithoutActivating);
	raw->setAttribute(Qt::WA_QuitOnClose, false);
	raw->setAttribute(Qt::WA_OpaquePaintEvent);
	raw->paintOn([=](QPainter &p) {
		p.fillRect(raw->rect(), st::windowBg);
	});
	const auto margin = st::separatePanelTitleHeight;
	raw->setGeometry(handle->frameGeometry().marginsAdded(
		QMargins(margin, margin, margin, margin)));
	leg->cover = std::move(cover);
	raw->show();
}

// base::unique_qptr deletes synchronously, and ~QWidget deletes the
// widget's QWidgetWindow with it (QWidgetPrivate::deleteTLSysExtra), so a
// released window is gone from QGuiApplication::topLevelWindows() in the
// same statement. Nothing is restored first: a deleted window cannot float.
// Idempotent - releasing a null unique_qptr is a no-op - so the teardown
// stage and Runner::onFinish can both run it.
void ReleaseLeg(not_null<Leg*> leg) {
	leg->cover = nullptr;
	leg->target = nullptr;
}

// Core::Application::screenIsLocked() starts false and changes only on the
// lock and unlock notifications that arrive after launch
// (the screenIsLocked: and screenIsUnlocked: observers in
// main_window_mac.mm, and Application::setScreenIsLocked), so a
// console locked before the launch reads unlocked for the whole run, and a
// window shown behind the lock screen keeps a stale isExposed(). The
// session's own CGSSessionScreenIsLocked - the bit workspace.py test-run
// reports as screen_locked - knows it from the start; a missing dictionary
// or key reads unlocked. A read-only query: it orders, activates and
// focuses nothing.
[[nodiscard]] bool ConsoleLocked() {
	if (Core::App().screenIsLocked()) {
		return true;
	}
#ifdef Q_OS_MAC
	const auto session = CGSessionCopyCurrentDictionary();
	if (!session) {
		return false;
	}
	const auto guard = gsl::finally([=] {
		CFRelease(session);
	});
	const auto value = CFDictionaryGetValue(
		session,
		CFSTR("CGSSessionScreenIsLocked"));
	return value && CFEqual(value, kCFBooleanTrue);
#else // Q_OS_MAC
	return false;
#endif // Q_OS_MAC
}

[[nodiscard]] QString FixtureSkip(const Leg &leg, bool needBuilt) {
	if (!kStacking) {
		return u"off macOS the helper is a documented no-op: Windows and "
			u"X11 never turn a covered window unexposed, and Wayland gives "
			u"clients no stacking control"_q;
	} else if (ConsoleLocked()) {
		return u"the console is locked, so occlusion and exposure cannot "
			u"be decided"_q;
	} else if (needBuilt && !leg.built) {
		return u"the fixture gate failed"_q;
	}
	return QString();
}

[[nodiscard]] QString DecidingSkip(const Leg &leg) {
	const auto fixture = FixtureSkip(leg, true);
	if (!fixture.isEmpty()) {
		return fixture;
	} else if (!leg.occluded) {
		return u"no occlusion control: "_q + WindowExposureText(leg.control);
	}
	return QString();
}

[[nodiscard]] QString HelperSkip(const Leg &leg) {
	const auto deciding = DecidingSkip(leg);
	if (!deciding.isEmpty()) {
		return deciding;
	} else if (!leg.held) {
		return u"the premise did not hold"_q;
	}
	return QString();
}

[[nodiscard]] QString PaintSkip(const Leg &leg) {
	const auto helper = HelperSkip(leg);
	if (!helper.isEmpty()) {
		return helper;
	} else if (!leg.exposedAfterHelper) {
		return u"the helper did not expose the target"_q;
	}
	return QString();
}

[[nodiscard]] QString UndoSkip(const Leg &leg) {
	const auto deciding = DecidingSkip(leg);
	if (!deciding.isEmpty()) {
		return deciding;
	} else if (!leg.reading.applied) {
		return u"the helper applied nothing"_q;
	}
	return QString();
}

[[nodiscard]] QString TeardownSkip(const Leg &leg) {
	return (leg.target || leg.cover)
		? QString()
		: u"the leg built no window"_q;
}

void DecideFixture(not_null<Leg*> leg) {
	const auto target = leg->target.get();
	const auto handle = target ? target->windowHandle() : nullptr;
	leg->built = handle && handle->isVisible() && !StayOnTop(handle);
	// The control's before-state: only a target the window server has
	// displayed and painted can be turned unexposed by the cover. A target
	// it never displayed - on a Space that is not visible, or with its first
	// display deferred - reads unexposed with or without the cover.
	leg->exposedBeforeCover = handle
		&& handle->isExposed()
		&& (leg->paints > 0);
	Check(
		leg->built,
		u"fixture gate: %1: the harness target is shown as a top-level "
		u"window at the normal level"_q.arg(leg->name),
		u"%1 widgetFlags=%2 primaryScreen=%3 exposedBeforeCover=%4"_q.arg(
			LegText(*leg),
			FlagsText(leg->widgetFlags),
			(QGuiApplication::primaryScreen() ? u"1"_q : u"0"_q),
			(leg->exposedBeforeCover ? u"1"_q : u"0"_q)));
}

// The cover goes up only here, once the target has been shown for
// kSettleBound. The before-state is read again right before it: another
// application may have covered the target during the settle, and then an
// unexposed reading under the cover would not be the cover's.
void CoverSettledTarget(not_null<Leg*> leg) {
	leg->exposedBeforeCover = leg->exposedBeforeCover
		&& ReadWindowExposure(leg->target.get()).exposed;
	if (leg->built) {
		BuildCover(leg);
	}
}

// The control is a reading, never a pass on its own: it passes only on an
// exposed-then-unexposed transition - the fixture saw the target exposed
// and painted, it still read exposed right before the cover, and under the
// harness cover it reads unexposed. Any other outcome makes the leg a named
// N/A, and every later deciding stage reads |occluded| in its skipReason.
void DecideControl(not_null<Leg*> leg) {
	leg->control = ReadWindowExposure(leg->target.get());
	const auto &control = leg->control;
	const auto cover = leg->cover ? leg->cover->windowHandle() : nullptr;
	const auto details = u"%1 cover=%2 elapsed=%3 bound=%4 "
		u"screenLocked=%5 exposedBeforeCover=%6 targetAge=%7"_q.arg(
			WindowExposureText(control),
			(cover ? RectText(cover->frameGeometry()) : u"none"_q),
			QString::number(Elapsed(*leg)),
			QString::number(kOcclusionBound),
			(ConsoleLocked() ? u"1"_q : u"0"_q),
			(leg->exposedBeforeCover ? u"1"_q : u"0"_q),
			QString::number(crl::now() - leg->shownAt));
	Check(
		(leg->baseline.focusWindow.data() == control.focusWindow.data())
			&& (leg->baseline.applicationState == control.applicationState)
			&& !control.active,
		u"%1: showing the harness target and its cover changed neither "
		u"the focus window nor the application state, and the target is "
		u"not active"_q.arg(leg->name),
		u"before the target: appState=%1 focusWindow=%2; under the cover: "
		u"%3"_q.arg(
			ApplicationStateName(leg->baseline.applicationState),
			leg->baseline.focusIdentity,
			WindowExposureText(control)));
	if (control.refusal.isEmpty()
		&& !control.exposed
		&& leg->exposedBeforeCover) {
		leg->occluded = true;
		Check(
			!control.exposed,
			u"%1: control: under the harness cover the target reads "
			u"isExposed() false within %2 ms, before the helper"_q.arg(
				leg->name,
				QString::number(kOcclusionBound)),
			details);
		// The paints the target had when it was found occluded, which the
		// premise compares against. Nothing asks the covered target to
		// repaint until DecideHelper has read it exposed, so a paint or an
		// exposure before the helper is not one the self-test provoked.
		leg->paintMark = leg->paints;
		return;
	}
	const auto why = !control.refusal.isEmpty()
		? u"the target was refused, so its occlusion cannot be read"_q
		: !leg->exposedBeforeCover
		? (u"the target was not exposed and painted right before the "
			u"cover, so an unexposed reading cannot be attributed to the "
			u"cover"_q)
		: (u"the target still read isExposed() true under the harness "
			u"cover after %1 ms - Qt received no occlusion change"_q.arg(
				kOcclusionBound));
	Skipped(
		StageName(*leg, u"occlusion control"_q),
		u"%1 - %2"_q.arg(why, details));
}

void DecidePremise(not_null<Leg*> leg) {
	const auto now = ReadWindowExposure(leg->target.get());
	leg->held = (leg->paints == leg->paintMark)
		&& now.refusal.isEmpty()
		&& !now.exposed;
	Check(
		leg->held,
		u"%1: premise: the covered target, which nothing asks to repaint, "
		u"stays unexposed and gets no paint for %2 ms - the symptom the "
		u"helper repairs"_q.arg(leg->name, QString::number(kHeldWindow)),
		LegText(*leg));
}

void DecideHelper(not_null<Leg*> leg) {
	const auto target = leg->target.get();
	const auto handle = target ? target->windowHandle() : nullptr;
	const auto settled = ReadWindowExposure(target);
	const auto &reading = leg->reading;
	const auto shown = target && !target->isHidden();
	const auto flags = target ? target->windowFlags() : Qt::WindowFlags();
	Check(
		reading.refusal.isEmpty()
			&& !reading.exposed
			&& !reading.stayOnTop
			&& reading.applied
			&& StayOnTop(handle)
			&& shown
			&& (flags == leg->widgetFlags),
		u"%1: the helper read the covered target unexposed, set "
		u"Qt::WindowStaysOnTopHint on its QWindow only, and left the "
		u"widget shown with its own flags unchanged"_q.arg(leg->name),
		u"%1 windowFlags=%2 widgetFlags=%3 widgetFlagsAtShow=%4 "
		u"hidden=%5"_q.arg(
			WindowExposureText(reading),
			(handle ? FlagsText(handle->flags()) : u"none"_q),
			FlagsText(flags),
			FlagsText(leg->widgetFlags),
			(shown ? u"0"_q : u"1"_q)));
	Check(
		settled.exposed,
		u"%1: within %2 ms of the helper the target reads isExposed() "
		u"true, with no repaint requested"_q.arg(
			leg->name,
			QString::number(kExposureBound)),
		u"paints=%1 paintMark=%2 elapsed=%3 bound=%4 %5"_q.arg(
			QString::number(leg->paints),
			QString::number(leg->paintMark),
			QString::number(Elapsed(*leg)),
			QString::number(kExposureBound),
			WindowExposureText(settled)));
	Check(
		SameActivation(reading, settled),
		u"%1: the target's isActive(), the focus window and the "
		u"application state read the same before the helper and once the "
		u"target is exposed"_q.arg(leg->name),
		u"before: %1; after: %2"_q.arg(
			WindowExposureText(reading),
			WindowExposureText(settled)));
	// The paint is asked for only now, after the target read exposed, so
	// the exposure check above observes the helper alone, with nothing the
	// self-test requested.
	leg->exposedAfterHelper = target && settled.exposed;
	if (leg->exposedAfterHelper) {
		leg->paintMark = leg->paints;
		target->update();
	}
}

void DecidePaint(not_null<Leg*> leg) {
	Check(
		leg->paints > leg->paintMark,
		u"%1: once exposed, the target receives at least one paint within "
		u"%2 ms of its update()"_q.arg(
			leg->name,
			QString::number(kExposureBound)),
		u"%1 bound=%2"_q.arg(LegText(*leg), QString::number(kExposureBound)));
}

// Everything in one turn: a second call on a window that already carries
// the hint, the undo of that second reading, then the undo of the first.
void DecideUndo(not_null<Leg*> leg) {
	const auto target = leg->target.get();
	const auto handle = target ? target->windowHandle() : nullptr;
	const auto second = KeepWindowExposed(target);
	RestoreWindowExposure(second);
	const auto keptAfterSecondUndo = StayOnTop(handle);
	RestoreWindowExposure(leg->reading);
	const auto after = ReadWindowExposure(target);
	const auto shown = target && !target->isHidden();
	const auto flags = target ? target->windowFlags() : Qt::WindowFlags();
	Check(
		second.stayOnTop && !second.applied && keptAfterSecondUndo,
		u"%1: a call on a window that already has the hint applies "
		u"nothing, and undoing that reading leaves the hint set"_q.arg(
			leg->name),
		u"second: %1; stayOnTopAfterItsUndo=%2"_q.arg(
			WindowExposureText(second),
			(keptAfterSecondUndo ? u"1"_q : u"0"_q)));
	Check(
		!after.stayOnTop && shown && (flags == leg->widgetFlags),
		u"%1: undoing the helper's reading clears the hint it set, with "
		u"the widget still shown and its own flags unchanged"_q.arg(
			leg->name),
		u"%1 windowFlags=%2 widgetFlags=%3 widgetFlagsAtShow=%4 "
		u"hidden=%5"_q.arg(
			WindowExposureText(after),
			(handle ? FlagsText(handle->flags()) : u"none"_q),
			FlagsText(flags),
			FlagsText(leg->widgetFlags),
			(shown ? u"0"_q : u"1"_q)));
	Check(
		SameActivation(leg->reading, after),
		u"%1: the undo activates nothing either"_q.arg(leg->name),
		u"before the helper: %1; after the undo: %2"_q.arg(
			WindowExposureText(leg->reading),
			WindowExposureText(after)));
}

void DecideTeardown(not_null<Leg*> leg) {
	const auto before = HarnessWindowCount();
	ReleaseLeg(leg);
	const auto after = HarnessWindowCount();
	// |before| - 2 for a built leg, the target and its cover - is what
	// proves the count can see harness windows at all.
	Check(
		(after == 0) && (before > 0 || !leg->built),
		u"%1: the self-test destroyed the target and the cover it created, "
		u"so no harness window is left over the console"_q.arg(leg->name),
		u"before=%1 after=%2"_q.arg(before).arg(after));
}

// Runs on every platform and on a locked console: it shows nothing. The
// fixtures are locals released at the end of the same turn, and they are
// built here rather than in a run, since a then cannot see a run's locals.
void AppendRefusalStage(not_null<Runner*> runner) {
	runner->add({
		.name = u"keep-exposed self-test: refusals"_q,
		.then = [] {
			const auto failuresBefore = FailureCount();
			const auto none = KeepWindowExposed(nullptr);

			// Never shown, so isHidden(), whether or not Qt has already
			// made its QWidgetWindow.
			auto unshown = base::make_unique_q<Ui::RpWidget>(nullptr);
			unshown->setObjectName(kNamePrefix + u"unshown"_q);
			unshown->setAttribute(Qt::WA_QuitOnClose, false);
			const auto hidden = KeepWindowExposed(unshown.get());

			// QWidget::setWindowState creates the native window, and
			// QCocoaWindow::setWindowState applies nothing to a window that
			// is not visible (qcocoawindow.mm), so it stays ordered
			// out while Qt reads it minimized.
			auto iconic = base::make_unique_q<Ui::RpWidget>(nullptr);
			iconic->setObjectName(kNamePrefix + u"minimized"_q);
			iconic->setAttribute(Qt::WA_ShowWithoutActivating);
			iconic->setAttribute(Qt::WA_QuitOnClose, false);
			iconic->setWindowState(Qt::WindowMinimized);
			const auto minimized = KeepWindowExposed(iconic.get());
			const auto minimizedHint = StayOnTop(iconic->windowHandle());

			const auto failuresAfter = FailureCount();
			unshown = nullptr;
			iconic = nullptr;

			Check(
				none.refusal.startsWith(u"null-widget:"_q) && !none.applied,
				u"refusals: a null widget is refused as null-widget, with "
				u"nothing applied"_q,
				WindowExposureText(none));
			Check(
				hidden.refusal.startsWith(u"hidden:"_q) && !hidden.applied,
				u"refusals: a top-level widget that was never shown is "
				u"refused as hidden, with nothing applied"_q,
				WindowExposureText(hidden));
			Check(
				minimized.refusal.startsWith(u"minimized:"_q)
					&& !minimized.applied
					&& !minimizedHint,
				u"refusals: a minimized top-level widget is refused as "
				u"minimized, with nothing applied and no hint on its "
				u"QWindow"_q,
				u"%1 stayOnTopAfter=%2"_q.arg(
					WindowExposureText(minimized),
					(minimizedHint ? u"1"_q : u"0"_q)));
			Check(
				failuresAfter == failuresBefore,
				u"refusals: a refusal is a returned value, not a logged "
				u"failure"_q,
				u"failuresBefore=%1 failuresAfter=%2"_q
					.arg(failuresBefore)
					.arg(failuresAfter));
		},
	});
}

void AppendLegStages(not_null<Runner*> runner, not_null<Leg*> leg) {
	runner->add({
		.name = StageName(*leg, u"fixture"_q),
		.skipReason = [=] { return FixtureSkip(*leg, false); },
		.run = [=] {
			// Read before any harness window of this leg exists: its
			// process-wide fields are the focus window and the application
			// state the no-activation check compares against.
			leg->baseline = ReadWindowExposure(nullptr);
			BuildTarget(leg);
			leg->shownAt = crl::now();
			leg->started = crl::now();
		},
		.until = [=] {
			const auto target = leg->target.get();
			const auto handle = target ? target->windowHandle() : nullptr;
			return !target
				|| (Elapsed(*leg) >= kWarmupBound)
				|| (handle
					&& handle->isVisible()
					&& handle->isExposed()
					&& (leg->paints > 0));
		},
		.then = [=] { DecideFixture(leg); },
		.timeoutDetails = [=] { return LegText(*leg); },
	});

	runner->add({
		.name = StageName(*leg, u"settle before the cover"_q),
		.skipReason = [=] { return FixtureSkip(*leg, true); },
		.until = [=] {
			return (crl::now() - leg->shownAt) >= kSettleBound;
		},
		.then = [=] { CoverSettledTarget(leg); },
		.timeoutDetails = [=] { return LegText(*leg); },
	});

	runner->add({
		.name = StageName(*leg, u"occlusion control"_q),
		.skipReason = [=] { return FixtureSkip(*leg, true); },
		.run = [=] { leg->started = crl::now(); },
		.until = [=] {
			return !ReadWindowExposure(leg->target.get()).exposed
				|| (Elapsed(*leg) >= kOcclusionBound);
		},
		.then = [=] { DecideControl(leg); },
		.timeoutDetails = [=] { return LegText(*leg); },
	});

	runner->add({
		.name = StageName(
			*leg,
			u"premise: a covered target that nothing asks to repaint stays "
			u"unexposed and gets no paint"_q),
		.skipReason = [=] { return DecidingSkip(*leg); },
		.run = [=] { leg->started = crl::now(); },
		.until = [=] {
			return (leg->paints > leg->paintMark)
				|| ReadWindowExposure(leg->target.get()).exposed
				|| (Elapsed(*leg) >= kHeldWindow);
		},
		.then = [=] { DecidePremise(leg); },
		.timeoutDetails = [=] { return LegText(*leg); },
	});

	runner->add({
		.name = StageName(
			*leg,
			u"the helper exposes the covered target without activating "
			u"anything"_q),
		.skipReason = [=] { return HelperSkip(*leg); },
		.run = [=] {
			// The helper alone: nothing asks the target to repaint before it
			// reads exposed, so the exposure this stage waits for is the
			// helper's. |paintMark| stays the control's mark until then.
			leg->reading = KeepWindowExposed(leg->target.get());
			leg->started = crl::now();
		},
		.until = [=] {
			return ReadWindowExposure(leg->target.get()).exposed
				|| (Elapsed(*leg) >= kExposureBound);
		},
		.then = [=] { DecideHelper(leg); },
		.timeoutDetails = [=] { return LegText(*leg); },
	});

	runner->add({
		.name = StageName(
			*leg,
			u"the exposed target paints a requested update"_q),
		.skipReason = [=] { return PaintSkip(*leg); },
		.run = [=] { leg->started = crl::now(); },
		.until = [=] {
			return (leg->paints > leg->paintMark)
				|| (Elapsed(*leg) >= kExposureBound);
		},
		.then = [=] { DecidePaint(leg); },
		.timeoutDetails = [=] { return LegText(*leg); },
	});

	runner->add({
		.name = StageName(*leg, u"undo restores the previous flag state"_q),
		.skipReason = [=] { return UndoSkip(*leg); },
		.then = [=] { DecideUndo(leg); },
	});

	runner->add({
		.name = StageName(*leg, u"teardown"_q),
		.skipReason = [=] { return TeardownSkip(*leg); },
		.then = [=] { DecideTeardown(leg); },
	});
}

} // namespace

void AppendKeepWindowExposedSelfTest(not_null<Runner*> runner) {
	// Leaked on purpose, the way this directory's other self-tests leak
	// theirs: the stages outlive this call. After the release the State
	// holds only readings, PODs and dead QPointers - it subscribes to
	// nothing and owns no window.
	const auto state = new State();
	state->legs[0].kind = LegKind::Window;
	state->legs[0].name = u"ordinary window"_q;
	state->legs[1].kind = LegKind::Panel;
	state->legs[1].name = u"separate panel"_q;

	// The release point, registered at append time rather than left to the
	// per-leg teardown stages: a stage that times out, the scenario
	// watchdog and skip-to-end all skip every stage after them (README.md,
	// "Scenario teardown before quit"), and a parentless harness window
	// left alive on such a run would float over the owner's console until
	// the process ends. finish() also runs this after a teardown stage
	// already ran, which ReleaseLeg tolerates.
	runner->onFinish([=] {
		for (auto &leg : state->legs) {
			ReleaseLeg(&leg);
		}
	});

	AppendRefusalStage(runner);
	for (auto &leg : state->legs) {
		AppendLegStages(runner, &leg);
	}
}

} // namespace Test

#endif // _DEBUG
