/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#ifdef _DEBUG

#include "test/test_console_shield.h"

#include "base/event_filter.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "test/test_agent.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <QtCore/QThread>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>
#include <QtGui/QPainter>
#include <QtGui/QScreen>
#include <QtGui/QShortcut>
#include <QtGui/QShortcutEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QTextEdit>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
#include <private/qhighdpiscaling_p.h>
#include <qpa/qwindowsysteminterface.h>
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

#include "styles/palette.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

// HarnessScopeLevel() while no HarnessInputScope is open.
constexpr auto kNoHarnessScope = -1;

// The level stages run at: Core::Sandbox launches the application from a
// queued call inside its exec().
constexpr auto kOutermostLoopLevel = 1;

// Pointer, wheel and touch veto rows are written at n=1 and every this
// many; every veto is still counted.
constexpr auto kHighRateRowEvery = 100;

// The one application event filter. It owns its re-front timer, so
// destroying it removes the filter and stops the timer together.
class ConsoleShieldFilter final : public QObject {
public:
	ConsoleShieldFilter();
	~ConsoleShieldFilter();

	// Moves the filter to the front of the application's filters, only at
	// the outermost loop level: a nested loop may have been entered from
	// inside Qt's iteration over that list, which an install reorders.
	bool refront();

protected:
	bool eventFilter(QObject *receiver, QEvent *event) override;

private:
	base::Timer _refront;

};

struct ShieldState {
	ConsoleShieldReading reading;
	std::unique_ptr<ConsoleShieldFilter> filter;
	bool finishRegistered = false;
};

[[nodiscard]] ShieldState &Shield() {
	static auto result = ShieldState();
	return result;
}

[[nodiscard]] int &HarnessScopeLevel() {
	static auto result = kNoHarnessScope;
	return result;
}

[[nodiscard]] int CurrentLoopLevel() {
	return QThread::currentThread()->loopLevel();
}

[[nodiscard]] bool HarnessInputInScope() {
	const auto level = HarnessScopeLevel();
	return (level != kNoHarnessScope) && (level == CurrentLoopLevel());
}

[[nodiscard]] QString ConsoleInputClassName(ConsoleInputClass value) {
	switch (value) {
	case ConsoleInputClass::Key: return u"key"_q;
	case ConsoleInputClass::ShortcutOverride: return u"shortcut-override"_q;
	case ConsoleInputClass::Shortcut: return u"shortcut"_q;
	case ConsoleInputClass::InputMethod: return u"input-method"_q;
	case ConsoleInputClass::MouseButton: return u"mouse-button"_q;
	case ConsoleInputClass::Pointer: return u"pointer"_q;
	case ConsoleInputClass::Wheel: return u"wheel"_q;
	case ConsoleInputClass::Touch: return u"touch"_q;
	case ConsoleInputClass::ContextMenu: return u"context-menu"_q;
	case ConsoleInputClass::Drop: return u"drop"_q;
	case ConsoleInputClass::Close: return u"close"_q;
	}
	Unexpected("Class in Test::ConsoleInputClassName.");
}

// Only a widget's Close is classified: the window-level Close every close
// starts with is spontaneous for a product close too.
[[nodiscard]] std::optional<ConsoleInputClass> ClassOf(
		not_null<QObject*> receiver,
		not_null<QEvent*> event) {
	switch (event->type()) {
	case QEvent::KeyPress:
	case QEvent::KeyRelease:
		return ConsoleInputClass::Key;
	case QEvent::ShortcutOverride:
		return ConsoleInputClass::ShortcutOverride;
	case QEvent::Shortcut:
		return ConsoleInputClass::Shortcut;
	case QEvent::InputMethod:
		return ConsoleInputClass::InputMethod;
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease:
	case QEvent::MouseButtonDblClick:
	case QEvent::NonClientAreaMouseButtonPress:
	case QEvent::NonClientAreaMouseButtonRelease:
	case QEvent::NonClientAreaMouseButtonDblClick:
		return ConsoleInputClass::MouseButton;
	case QEvent::MouseMove:
	case QEvent::NonClientAreaMouseMove:
	case QEvent::Enter:
	case QEvent::Leave:
	case QEvent::TabletPress:
	case QEvent::TabletMove:
	case QEvent::TabletRelease:
	case QEvent::TabletEnterProximity:
	case QEvent::TabletLeaveProximity:
		return ConsoleInputClass::Pointer;
	case QEvent::Wheel:
		return ConsoleInputClass::Wheel;
	case QEvent::TouchBegin:
	case QEvent::TouchUpdate:
	case QEvent::TouchEnd:
	case QEvent::TouchCancel:
	case QEvent::NativeGesture:
		return ConsoleInputClass::Touch;
	case QEvent::ContextMenu:
		return ConsoleInputClass::ContextMenu;
	case QEvent::DragEnter:
	case QEvent::DragMove:
	case QEvent::DragLeave:
	case QEvent::Drop:
		return ConsoleInputClass::Drop;
	case QEvent::Close:
		return receiver->isWidgetType()
			? std::make_optional(ConsoleInputClass::Close)
			: std::nullopt;
	default:
		return std::nullopt;
	}
}

// The classes whose platform route is a sent event: QShortcutMap sends
// every QShortcutEvent, macOS QNSView sends IME text and
// QGuiApplicationPrivate::processDrag / processDrop send platform drags.
[[nodiscard]] bool VetoedWhenSent(ConsoleInputClass inputClass) {
	switch (inputClass) {
	case ConsoleInputClass::Shortcut:
	case ConsoleInputClass::InputMethod:
	case ConsoleInputClass::Drop:
		return true;
	default:
		return false;
	}
}

[[nodiscard]] bool HighRate(ConsoleInputClass inputClass) {
	switch (inputClass) {
	case ConsoleInputClass::Pointer:
	case ConsoleInputClass::Wheel:
	case ConsoleInputClass::Touch:
		return true;
	default:
		return false;
	}
}

// Counts the veto and writes its row. It reads only the event's class,
// the receiver's type and, for a shortcut, the shortcut's key sequence.
void RecordVeto(
		ConsoleInputClass inputClass,
		not_null<QObject*> receiver,
		not_null<QEvent*> event) {
	auto &reading = Shield().reading;
	const auto n = ++reading.counts[int(inputClass)];
	++reading.vetoes;
	if (HighRate(inputClass)
		&& (n != 1)
		&& ((n % kHighRateRowEvery) != 0)) {
		return;
	}
	const auto &instance = *receiver;
	auto row = u"CONSOLE_SHIELD: vetoed class=%1 n=%2 total=%3 "
		u"receiver=%4 label=%5"_q.arg(
			ConsoleInputClassName(inputClass),
			QString::number(n),
			QString::number(reading.vetoes),
			QString::fromUtf8(typeid(instance).name()),
			reading.label);
	if (inputClass == ConsoleInputClass::Shortcut) {
		const auto shortcut = static_cast<QShortcutEvent*>(event.get());
		row += u" sequence="_q
			+ shortcut->key().toString(QKeySequence::PortableText);
	}
	Note(row);
}

ConsoleShieldFilter::ConsoleShieldFilter()
: _refront([=] { refront(); }) {
	QCoreApplication::instance()->installEventFilter(this);
	_refront.callEach(kConsoleShieldRefront);
}

ConsoleShieldFilter::~ConsoleShieldFilter() {
	_refront.cancel();
	if (const auto instance = QCoreApplication::instance()) {
		instance->removeEventFilter(this);
	}
}

bool ConsoleShieldFilter::refront() {
	if (CurrentLoopLevel() > kOutermostLoopLevel) {
		return false;
	}
	QCoreApplication::instance()->installEventFilter(this);
	return true;
}

bool ConsoleShieldFilter::eventFilter(QObject *receiver, QEvent *event) {
	const auto inputClass = ClassOf(receiver, event);
	if (!inputClass
		|| HarnessInputInScope()
		|| (!event->spontaneous() && !VetoedWhenSent(*inputClass))) {
		return false;
	}
	if (*inputClass == ConsoleInputClass::Close) {
		event->ignore();
	}
	RecordVeto(*inputClass, receiver, event);
	return true;
}

} // namespace

void ArmConsoleShield(not_null<Runner*> runner, const QString &label) {
	if (!Active()) {
		return;
	}
	auto &shield = Shield();
	if (shield.filter) {
		const auto moved = shield.filter->refront();
		Note(u"CONSOLE_SHIELD: already armed label=%1 requested=%2 "
			u"moved=%3 vetoes=%4"_q.arg(
				shield.reading.label,
				label,
				moved ? u"1"_q : u"0"_q,
				QString::number(shield.reading.vetoes)));
		return;
	}
	shield.reading = ConsoleShieldReading{
		.label = label,
		.armedAt = crl::now(),
		.armed = true,
	};
	shield.filter = std::make_unique<ConsoleShieldFilter>();
	Note(u"CONSOLE_SHIELD: armed label=%1 refrontMs=%2"_q.arg(
		label,
		QString::number(kConsoleShieldRefront)));
	if (!shield.finishRegistered) {
		shield.finishRegistered = true;
		runner->onFinish([] {
			Shield().finishRegistered = false;
			ReleaseConsoleShield(u"finish"_q);
		});
	}
}

bool ReleaseConsoleShield(const QString &by) {
	auto &shield = Shield();
	if (!shield.filter) {
		Note(u"CONSOLE_SHIELD: release skipped by=%1 - not armed"_q.arg(by));
		return false;
	}
	shield.filter = nullptr;
	shield.reading.armed = false;
	Note(u"CONSOLE_SHIELD: released by=%1 armedMs=%2 %3"_q.arg(
		by,
		QString::number(crl::now() - shield.reading.armedAt),
		ConsoleShieldSummary(shield.reading)));
	return true;
}

ConsoleShieldReading ReadConsoleShield() {
	return Shield().reading;
}

QString ConsoleShieldSummary(const ConsoleShieldReading &reading) {
	auto counts = QStringList();
	for (auto i = 0; i != kConsoleInputClassCount; ++i) {
		counts.push_back(u"%1=%2"_q.arg(
			ConsoleInputClassName(ConsoleInputClass(i)),
			QString::number(reading.counts[i])));
	}
	return u"label=%1 armed=%2 vetoes=%3 counts=[%4]"_q.arg(
		reading.label,
		reading.armed ? u"1"_q : u"0"_q,
		QString::number(reading.vetoes),
		counts.join(QChar(',')));
}

HarnessInputScope::HarnessInputScope()
: _previous(std::exchange(HarnessScopeLevel(), CurrentLoopLevel())) {
}

HarnessInputScope::~HarnessInputScope() {
	HarnessScopeLevel() = _previous;
}

namespace {

// The self-test's measured arm on A and its finish-only arm on D. One
// token each, so every CONSOLE_SHIELD row stays space-separated.
const auto kArmLabel = u"console-shield-self-test"_q;
const auto kFinishLabel = u"console-shield-self-test-finish"_q;

// Fixture text the harness legs type and commit into A's field.
const auto kHarnessTyped = u"ab"_q;
const auto kHarnessCommitted = u"cd"_q;

// The foreign keys' synthetic text: a Cyrillic letter (U+0436) no harness
// leg types, so a field holding it was reached by a foreign key. The
// self-test never prints it, and no CONSOLE_SHIELD row may contain it.
constexpr auto kForeignChar = char16_t(0x0436);

// The fixture's own shortcut, Ctrl+Alt+Shift+F12, which no product
// shortcut binds. A window's QShortcut (Qt::WindowShortcut) matches only
// while that window is Qt's active window.
constexpr auto kShortcutKey = Qt::Key_F12;
constexpr auto kShortcutModifiers = Qt::KeyboardModifiers(
	Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier);

// Several re-front intervals: how long a newer application filter has to
// fall behind the shield before the measured foreign keys. A requested
// yield, never a deadline the shield promises.
constexpr auto kRefrontYield = kConsoleShieldRefront * 5;

// Foreign input goes through QWindowSystemInterface, which, like Qt-level
// activation, needs the private Qt API.
#ifdef DESKTOP_APP_DISABLE_PRIVATE_QT_API
constexpr auto kPlatformInput = false;
#else // DESKTOP_APP_DISABLE_PRIVATE_QT_API
constexpr auto kPlatformInput = true;
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

// The re-front control's newer application filter. Constructed after the
// arm, it is installed in front of the shield; it passes every event and
// counts the key events that reach it while counting.
class NewerFilter final : public QObject {
public:
	NewerFilter();
	~NewerFilter();

	void setCounting(bool counting);
	[[nodiscard]] int keys() const;

protected:
	bool eventFilter(QObject *receiver, QEvent *event) override;

private:
	int _keys = 0;
	bool _counting = false;

};

// What one object filter saw, by type; |spontaneous| counts the
// spontaneous events among those types.
struct EventCounts {
	int keyPress = 0;
	int keyRelease = 0;
	int inputMethod = 0;
	int mousePress = 0;
	int mouseRelease = 0;
	int mouseDouble = 0;
	int close = 0;
	int spontaneous = 0;
};

// One purpose-built top-level window. |windowEvents| records its QWindow,
// the first hop of platform input; |widgetEvents| the window widget, which
// gets its own Close; |fieldEvents| the field's raw editor, the focus
// child keys reach; |buttonEvents| the button. D has no button and no
// shortcut.
struct Subject {
	QString name;
	base::unique_qptr<Ui::RpWidget> window;
	QPointer<Ui::InputField> field;
	QPointer<Ui::RoundButton> button;
	EventCounts windowEvents;
	EventCounts widgetEvents;
	EventCounts fieldEvents;
	EventCounts buttonEvents;
	int clicks = 0;
	int activations = 0;
};

// |text| is compared, never printed: it holds the synthetic letter.
struct SubjectReading {
	EventCounts window;
	EventCounts widget;
	EventCounts field;
	EventCounts button;
	QString text;
	int clicks = 0;
	int activations = 0;
	bool alive = false;
	bool hidden = false;
	bool platformWindow = false;
};

struct SubjectDelta {
	EventCounts window;
	EventCounts widget;
	EventCounts field;
	EventCounts button;
	int clicks = 0;
	int activations = 0;
	int foreign = 0;
};

// One measured delivery: the shield and the subject read right before and
// right after it, in the same synchronous block.
struct LegReading {
	ConsoleShieldReading shieldBefore;
	ConsoleShieldReading shieldAfter;
	SubjectReading before;
	SubjectReading after;
	bool returned = false;
	bool measured = false;
};

// The converted mouse pair: logical window-local and global points of the
// target's centre and the native ones QWindowSystemInterface was given.
struct PlatformClickReading {
	QPointF local;
	QPointF global;
	QPointF nativeLocal;
	QPointF nativeGlobal;
	bool pressAccepted = false;
	bool releaseAccepted = false;
	bool sent = false;
};

struct SavedFocus {
	QPointer<QWindow> window;
	QPointer<QWidget> widget;
};

// The four required foreign classes on one subject, each measured on its
// own, the shortcut inside a Qt-level activation that is then restored.
// |newerKeys| is what the newer filter saw of the key leg, when present.
struct ForeignLegs {
	LegReading keys;
	LegReading mouse;
	LegReading shortcut;
	LegReading close;
	WindowActivation activation;
	QString points;
	QString focus;
	int newerKeys = 0;
	bool newerPresent = false;
	bool subjectActive = false;
	bool focusRestored = false;
};

// Armed A: the public harness helpers.
struct HarnessLegs {
	ConsoleShieldReading armed;
	LegReading click;
	LegReading typed;
	LegReading backspace;
	LegReading committed;
};

// Armed A: the shortcut a harness PressKey produces, the same sent key
// pair outside Settle, and a QWindowSystemInterface key pair inside it.
struct HarnessPlatformLegs {
	LegReading shortcut;
	LegReading sentOutside;
	LegReading keys;
	WindowActivation activation;
	QString focus;
	bool subjectActive = false;
	bool focusRestored = false;
};

// A: armed subject; B: unarmed control; C: post-release control; D: the
// finish-only arm. The stages and the finish callbacks share it.
struct State {
	Subject a;
	Subject b;
	Subject c;
	Subject d;
	std::unique_ptr<NewerFilter> newer;
	LegReading newerFront;
	ForeignLegs control;
	HarnessLegs harness;
	HarnessPlatformLegs harnessPlatform;
	ForeignLegs vetoed;
	LegReading productClose;
	ConsoleShieldReading releaseBefore;
	ConsoleShieldReading releaseAfter;
	ForeignLegs replay;
	ConsoleShieldReading secondBefore;
	ConsoleShieldReading secondAfter;
	QString gate = u"fixture gate: the fixture stage has not run"_q;
	QString buildRefusal;
	QString finishRefusal;
	QString placement;
	QString finishPlacement;
	crl::time newerInstalledAt = 0;
	crl::time newerWaited = 0;
	int newerFrontKeys = 0;
	bool firstReleased = false;
	bool secondReleased = false;
	bool finishCheckRegistered = false;
};

NewerFilter::NewerFilter() {
	QCoreApplication::instance()->installEventFilter(this);
}

NewerFilter::~NewerFilter() {
	if (const auto instance = QCoreApplication::instance()) {
		instance->removeEventFilter(this);
	}
}

void NewerFilter::setCounting(bool counting) {
	_counting = counting;
}

int NewerFilter::keys() const {
	return _keys;
}

bool NewerFilter::eventFilter(QObject *receiver, QEvent *event) {
	const auto type = event->type();
	if (_counting
		&& ((type == QEvent::KeyPress) || (type == QEvent::KeyRelease))) {
		++_keys;
	}
	return false;
}

void RecordEvent(not_null<EventCounts*> counts, not_null<QEvent*> event) {
	switch (event->type()) {
	case QEvent::KeyPress: ++counts->keyPress; break;
	case QEvent::KeyRelease: ++counts->keyRelease; break;
	case QEvent::InputMethod: ++counts->inputMethod; break;
	case QEvent::MouseButtonPress: ++counts->mousePress; break;
	case QEvent::MouseButtonRelease: ++counts->mouseRelease; break;
	case QEvent::MouseButtonDblClick: ++counts->mouseDouble; break;
	case QEvent::Close: ++counts->close; break;
	default: return;
	}
	if (event->spontaneous()) {
		++counts->spontaneous;
	}
}

// An object filter: it runs after every application filter, so it sees
// only what the shield let through.
void InstallRecorder(
		not_null<QObject*> object,
		not_null<EventCounts*> counts) {
	base::install_event_filter(object, [=](not_null<QEvent*> event) {
		RecordEvent(counts, event);
		return base::EventFilterResult::Continue;
	});
}

[[nodiscard]] EventCounts CountsDelta(
		const EventCounts &after,
		const EventCounts &before) {
	return {
		.keyPress = after.keyPress - before.keyPress,
		.keyRelease = after.keyRelease - before.keyRelease,
		.inputMethod = after.inputMethod - before.inputMethod,
		.mousePress = after.mousePress - before.mousePress,
		.mouseRelease = after.mouseRelease - before.mouseRelease,
		.mouseDouble = after.mouseDouble - before.mouseDouble,
		.close = after.close - before.close,
		.spontaneous = after.spontaneous - before.spontaneous,
	};
}

[[nodiscard]] QString CountsText(const EventCounts &counts) {
	return u"[keys=%1/%2 im=%3 mouse=%4/%5 double=%6 close=%7 spont=%8]"_q
		.arg(counts.keyPress)
		.arg(counts.keyRelease)
		.arg(counts.inputMethod)
		.arg(counts.mousePress)
		.arg(counts.mouseRelease)
		.arg(counts.mouseDouble)
		.arg(counts.close)
		.arg(counts.spontaneous);
}

[[nodiscard]] int ForeignChars(const QString &text) {
	return int(text.count(QChar(kForeignChar)));
}

[[nodiscard]] QString PointText(QPoint point) {
	return u"%1,%2"_q.arg(point.x()).arg(point.y());
}

[[nodiscard]] QString PointText(QPointF point) {
	return u"%1,%2"_q.arg(point.x()).arg(point.y());
}

[[nodiscard]] QKeySequence ShortcutSequence() {
	return QKeySequence(QKeyCombination(kShortcutModifiers, kShortcutKey));
}

[[nodiscard]] SubjectReading ReadSubject(const Subject &subject) {
	const auto window = subject.window.get();
	const auto handle = window ? window->windowHandle() : nullptr;
	return {
		.window = subject.windowEvents,
		.widget = subject.widgetEvents,
		.field = subject.fieldEvents,
		.button = subject.buttonEvents,
		.text = subject.field ? subject.field->getLastText() : QString(),
		.clicks = subject.clicks,
		.activations = subject.activations,
		.alive = (window != nullptr),
		.hidden = !window || window->isHidden(),
		.platformWindow = handle && handle->handle(),
	};
}

[[nodiscard]] SubjectDelta Difference(const LegReading &leg) {
	const auto &after = leg.after;
	const auto &before = leg.before;
	return {
		.window = CountsDelta(after.window, before.window),
		.widget = CountsDelta(after.widget, before.widget),
		.field = CountsDelta(after.field, before.field),
		.button = CountsDelta(after.button, before.button),
		.clicks = after.clicks - before.clicks,
		.activations = after.activations - before.activations,
		.foreign = ForeignChars(after.text) - ForeignChars(before.text),
	};
}

template <typename Action>
[[nodiscard]] LegReading MeasureLeg(const Subject &subject, Action &&action) {
	auto result = LegReading();
	result.shieldBefore = ReadConsoleShield();
	result.before = ReadSubject(subject);
	result.returned = action();
	result.shieldAfter = ReadConsoleShield();
	result.after = ReadSubject(subject);
	result.measured = true;
	return result;
}

[[nodiscard]] int Vetoed(
		const LegReading &leg,
		ConsoleInputClass inputClass) {
	return leg.shieldAfter.count(inputClass)
		- leg.shieldBefore.count(inputClass);
}

[[nodiscard]] int Vetoes(const LegReading &leg) {
	return leg.shieldAfter.vetoes - leg.shieldBefore.vetoes;
}

// The same arm held the shield on both sides of the leg, so a zero delta
// is a reading of an armed shield, not of a released one.
[[nodiscard]] bool ArmedThrough(const LegReading &leg, const QString &label) {
	return leg.measured
		&& leg.shieldBefore.armed
		&& leg.shieldAfter.armed
		&& (leg.shieldBefore.label == label)
		&& (leg.shieldBefore.armedAt == leg.shieldAfter.armedAt);
}

[[nodiscard]] bool Unarmed(const LegReading &leg) {
	return leg.measured && !leg.shieldBefore.armed && !leg.shieldAfter.armed;
}

[[nodiscard]] QString VetoDeltaText(const LegReading &leg) {
	auto parts = QStringList();
	for (auto i = 0; i != kConsoleInputClassCount; ++i) {
		const auto inputClass = ConsoleInputClass(i);
		if (const auto delta = Vetoed(leg, inputClass)) {
			parts.push_back(u"%1=%2"_q.arg(
				ConsoleInputClassName(inputClass),
				QString::number(delta)));
		}
	}
	return u"armed=%1->%2 vetoes+=%3 [%4]"_q.arg(
		leg.shieldBefore.armed ? u"1"_q : u"0"_q,
		leg.shieldAfter.armed ? u"1"_q : u"0"_q,
		QString::number(Vetoes(leg)),
		parts.join(QChar(',')));
}

// Every number a leg's verdict is made against, and never the field's
// text: only its length and how many synthetic letters it gained.
[[nodiscard]] QString LegText(const LegReading &leg) {
	if (!leg.measured) {
		return u"not measured"_q;
	}
	const auto delta = Difference(leg);
	const auto &before = leg.before;
	const auto &after = leg.after;
	return QStringList{
		u"returned=%1"_q.arg(leg.returned ? 1 : 0),
		VetoDeltaText(leg),
		u"window+=%1"_q.arg(CountsText(delta.window)),
		u"widget+=%1"_q.arg(CountsText(delta.widget)),
		u"field+=%1"_q.arg(CountsText(delta.field)),
		u"button+=%1"_q.arg(CountsText(delta.button)),
		u"clicks+=%1 activations+=%2 foreignChars+=%3"_q
			.arg(delta.clicks)
			.arg(delta.activations)
			.arg(delta.foreign),
		u"textLength=%1->%2 hidden=%3->%4 platformWindow=%5->%6"_q
			.arg(before.text.size())
			.arg(after.text.size())
			.arg(before.hidden ? 1 : 0)
			.arg(after.hidden ? 1 : 0)
			.arg(before.platformWindow ? 1 : 0)
			.arg(after.platformWindow ? 1 : 0),
	}.join(QChar(' '));
}

[[nodiscard]] bool SameReading(
		const ConsoleShieldReading &a,
		const ConsoleShieldReading &b) {
	return (a.label == b.label)
		&& (a.armedAt == b.armedAt)
		&& (a.vetoes == b.vetoes)
		&& (a.counts == b.counts)
		&& (a.armed == b.armed);
}

[[nodiscard]] QString ReadingText(const ConsoleShieldReading &reading) {
	return u"%1 armedAt=%2"_q.arg(
		ConsoleShieldSummary(reading),
		QString::number(reading.armedAt));
}

[[nodiscard]] QString PlatformInputSkip() {
	return kPlatformInput
		? QString()
		: (u"unsupported: built with DESKTOP_APP_DISABLE_PRIVATE_QT_API, so "
			u"there is no QWindowSystemInterface delivery to send foreign "
			u"input through and no Qt-level activation for a shortcut"_q);
}

// The QWindowSystemInterface key pair console typing arrives as, carrying
// the synthetic letter; outside Test::Settle it is foreign input. Without
// the private Qt API, or without a window, it sends nothing.
bool PlatformKeys(QWindow *window) {
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	if (!window) {
		return false;
	}
	using Delivery = QWindowSystemInterface::SynchronousDelivery;
	const auto text = QString(QChar(kForeignChar));
	const auto guard = QPointer<QWindow>(window);
	const auto pressed = QWindowSystemInterface::handleKeyEvent<Delivery>(
		window,
		QEvent::KeyPress,
		0,
		Qt::NoModifier,
		text);
	if (!guard) {
		return false;
	}
	const auto released = QWindowSystemInterface::handleKeyEvent<Delivery>(
		window,
		QEvent::KeyRelease,
		0,
		Qt::NoModifier,
		text);
	return pressed && released;
#else // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return false;
#endif // DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

// The converted mouse pair: a left press and release at the target's
// centre, its logical window-local and global points converted with
// QHighDpi::toNativeLocalPosition and toNativeGlobalPosition into the
// native coordinates QWindowSystemInterface::handleMouseEvent takes.
PlatformClickReading PlatformClick(QWidget *target) {
	auto result = PlatformClickReading();
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	const auto top = target ? target->window() : nullptr;
	const auto window = top ? top->windowHandle() : nullptr;
	if (!window) {
		return result;
	}
	using Delivery = QWindowSystemInterface::SynchronousDelivery;
	const auto center = QPointF(target->rect().center());
	result.local = target->mapTo(top, center);
	result.global = target->mapToGlobal(center);
	result.nativeLocal = QHighDpi::toNativeLocalPosition(
		result.local,
		window);
	result.nativeGlobal = QHighDpi::toNativeGlobalPosition(
		result.global,
		window);
	const auto guard = QPointer<QWindow>(window);
	result.sent = true;
	result.pressAccepted
		= QWindowSystemInterface::handleMouseEvent<Delivery>(
			window,
			result.nativeLocal,
			result.nativeGlobal,
			Qt::LeftButton,
			Qt::LeftButton,
			QEvent::MouseButtonPress);
	if (guard) {
		result.releaseAccepted
			= QWindowSystemInterface::handleMouseEvent<Delivery>(
				window,
				result.nativeLocal,
				result.nativeGlobal,
				Qt::NoButton,
				Qt::LeftButton,
				QEvent::MouseButtonRelease);
	}
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return result;
}

[[nodiscard]] QString PlatformClickText(const PlatformClickReading &click) {
	return u"sent=%1 local=%2 global=%3 nativeLocal=%4 nativeGlobal=%5 "
		u"pressAccepted=%6 releaseAccepted=%7"_q.arg(
			click.sent ? u"1"_q : u"0"_q,
			PointText(click.local),
			PointText(click.global),
			PointText(click.nativeLocal),
			PointText(click.nativeGlobal),
			click.pressAccepted ? u"1"_q : u"0"_q,
			click.releaseAccepted ? u"1"_q : u"0"_q);
}

// QWindowSystemInterface::handleShortcutEvent, the route a console
// shortcut takes: a spontaneous ShortcutOverride first, then the shortcut
// map, which sends the QShortcutEvent.
bool PlatformShortcut(QWindow *window) {
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return window
		&& QWindowSystemInterface::handleShortcutEvent(
			window,
			ulong(crl::now()),
			kShortcutKey,
			kShortcutModifiers,
			0,
			0,
			0);
#else // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return false;
#endif // DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

// The platform's close request - a title-bar button, the window manager -
// as QCocoaWindow::windowShouldClose delivers it. True when accepted.
bool PlatformClose(QWindow *window) {
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	using Delivery = QWindowSystemInterface::SynchronousDelivery;
	return window
		&& QWindowSystemInterface::handleCloseEvent<Delivery>(window);
#else // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	return false;
#endif // DESKTOP_APP_DISABLE_PRIVATE_QT_API
}

[[nodiscard]] SavedFocus SaveFocus() {
	return {
		.window = QGuiApplication::focusWindow(),
		.widget = QApplication::focusWidget(),
	};
}

// Gives Qt the saved focus window back - none when it had none or it is
// gone - through the QPA seam ForceWindowActive uses, then the saved focus
// widget its focus. Activates no platform window. True when the focus
// window is the saved one and, when a widget was saved, so is the focus
// widget.
bool RestoreFocus(const SavedFocus &saved) {
#ifndef DESKTOP_APP_DISABLE_PRIVATE_QT_API
	QWindowSystemInterface::handleFocusWindowChanged(
		saved.window.data(),
		Qt::ActiveWindowFocusReason);
	QWindowSystemInterface::flushWindowSystemEvents();
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API
	const auto widget = saved.widget.data();
	if (widget && (QApplication::focusWidget() != widget)) {
		widget->setFocus(Qt::OtherFocusReason);
	}
	return (QGuiApplication::focusWindow() == saved.window.data())
		&& (!widget || (QApplication::focusWidget() == widget));
}

[[nodiscard]] QString ObjectText(QObject *object) {
	if (!object) {
		return u"none"_q;
	}
	const auto name = object->objectName();
	return name.isEmpty()
		? QString::fromLatin1(object->metaObject()->className())
		: name;
}

[[nodiscard]] QString FocusText(const SavedFocus &saved) {
	return u"savedFocusWindow=%1 savedFocusWidget=%2 focusWindow=%3 "
		u"focusWidget=%4"_q.arg(
			ObjectText(saved.window.data()),
			ObjectText(saved.widget.data()),
			ObjectText(QGuiApplication::focusWindow()),
			ObjectText(QApplication::focusWidget()));
}

// A parentless Ui::RpWidget is a Qt::Window. Shown without activating
// (Qt::WA_ShowWithoutActivating set before its native window exists, and
// never raise()) and with Qt::WA_QuitOnClose cleared, because closing it is
// a measured leg - MakeOrdinaryWindow in test_window_exposure.cpp. Sizes
// come from the live style: st::boxWidth and the default field and button.
void BuildSubject(
		not_null<Subject*> subject,
		const QString &name,
		bool withControls) {
	subject->name = name;
	subject->window = base::make_unique_q<Ui::RpWidget>(nullptr);
	const auto window = subject->window.get();
	window->setObjectName(u"console_shield_self_test_"_q + name);
	window->setWindowTitle(u"Harness console shield "_q + name);
	window->setAttribute(Qt::WA_ShowWithoutActivating);
	window->setAttribute(Qt::WA_QuitOnClose, false);
	window->setAttribute(Qt::WA_OpaquePaintEvent);
	window->paintOn([=](QPainter &p) {
		p.fillRect(window->rect(), st::windowBg);
	});
	const auto skip = st::defaultActiveButton.height / 2;
	const auto field = Ui::CreateChild<Ui::InputField>(
		window,
		st::defaultInputField,
		rpl::single(u"Console shield "_q + name));
	field->resize(st::boxWidth, field->height());
	field->move(skip, skip);
	subject->field = field;
	InstallRecorder(field->rawTextEdit(), &subject->fieldEvents);
	auto height = skip * 2 + field->height();
	if (withControls) {
		const auto button = Ui::CreateChild<Ui::RoundButton>(
			window,
			rpl::single(u"Shield"_q),
			st::defaultActiveButton);
		button->move(skip, height);
		height += button->height() + skip;
		button->setClickedCallback([=] {
			++subject->clicks;
		});
		subject->button = button;
		InstallRecorder(button, &subject->buttonEvents);
		const auto shortcut = new QShortcut(ShortcutSequence(), window);
		QObject::connect(shortcut, &QShortcut::activated, shortcut, [=] {
			++subject->activations;
		});
	}
	InstallRecorder(window, &subject->widgetEvents);
	window->resize(st::boxWidth + skip * 2, height);
}

// The corner of |available| where a |size| box lies farthest from
// |cursor|, so the owner's real pointer is unlikely to hover the windows.
[[nodiscard]] QPoint FarCorner(
		const QRect &available,
		QSize size,
		QPoint cursor) {
	const auto right = available.x() + available.width() - size.width();
	const auto bottom = available.y() + available.height() - size.height();
	const auto corners = std::array{
		available.topLeft(),
		QPoint(right, available.y()),
		QPoint(available.x(), bottom),
		QPoint(right, bottom),
	};
	auto result = corners.front();
	auto distance = -1.;
	for (const auto &corner : corners) {
		const auto box = QRect(corner, size);
		const auto dx = std::max({
			box.left() - cursor.x(),
			0,
			cursor.x() - box.right() });
		const auto dy = std::max({
			box.top() - cursor.y(),
			0,
			cursor.y() - box.bottom() });
		const auto now = std::hypot(double(dx), double(dy));
		if (now > distance) {
			distance = now;
			result = corner;
		}
	}
	return result;
}

// Stacks the windows in one column, each below room for its title bar, in
// the corner of |available| farthest from |cursor|.
[[nodiscard]] QString PlaceColumn(
		const std::vector<not_null<Subject*>> &subjects,
		const QRect &available,
		QPoint cursor) {
	const auto titleRoom = st::defaultActiveButton.height;
	auto column = QSize();
	for (const auto subject : subjects) {
		const auto size = subject->window->size();
		column = QSize(
			std::max(column.width(), size.width()),
			column.height() + titleRoom + size.height());
	}
	auto position = FarCorner(available, column, cursor);
	const auto bounds = QRect(position, column);
	for (const auto subject : subjects) {
		const auto size = subject->window->size();
		position.ry() += titleRoom;
		subject->window->setGeometry(QRect(position, size));
		position.ry() += size.height();
	}
	return u"screen=%1 column=%2 fits=%3"_q.arg(
		RectText(available),
		RectText(bounds),
		available.contains(bounds) ? u"1"_q : u"0"_q);
}

// A plain show() raises nothing; the QWindow exists from here on, so its
// recorder goes in now. The field becomes the window's focus child, which
// is where QWidgetWindow sends keys, without activating anything.
void ShowSubject(not_null<Subject*> subject) {
	const auto window = subject->window.get();
	window->show();
	if (const auto handle = window->windowHandle()) {
		InstallRecorder(handle, &subject->windowEvents);
	}
	subject->field->setFocusFast();
}

// Readiness only, never a success: a shown native QWindow handle.
[[nodiscard]] bool SubjectReady(const Subject &subject) {
	const auto window = subject.window.get();
	const auto handle = window ? window->windowHandle() : nullptr;
	return handle && handle->isVisible();
}

[[nodiscard]] QString SubjectText(const Subject &subject) {
	const auto window = subject.window.get();
	if (!window) {
		return u"%1=[gone]"_q.arg(subject.name);
	}
	const auto handle = window->windowHandle();
	const auto raw = subject.field
		? subject.field->rawTextEdit().get()
		: nullptr;
	const auto focus = window->focusWidget();
	const auto focusChild = !focus
		? u"none"_q
		: (focus == raw)
		? u"field"_q
		: u"other"_q;
	return u"%1=[geometry=%2 handle=%3 visible=%4 platformWindow=%5 dpr=%6 "
		u"focusChild=%7]"_q.arg(
			subject.name,
			RectText(window->geometry()),
			handle ? u"1"_q : u"0"_q,
			(handle && handle->isVisible()) ? u"1"_q : u"0"_q,
			(handle && handle->handle()) ? u"1"_q : u"0"_q,
			QString::number(window->devicePixelRatio()),
			focusChild);
}

[[nodiscard]] QString FixtureText(const State &state) {
	return u"qt=%1 platformInput=%2 shortcut=%3 cursor=%4 %5 %6 %7 %8"_q.arg(
		QString::fromLatin1(qVersion()),
		kPlatformInput ? u"1"_q : u"0"_q,
		ShortcutSequence().toString(QKeySequence::PortableText),
		PointText(QCursor::pos()),
		state.placement,
		SubjectText(state.a),
		SubjectText(state.b),
		SubjectText(state.c));
}

[[nodiscard]] QScreen *FixtureScreen(QPoint cursor) {
	const auto screen = QGuiApplication::screenAt(cursor);
	return screen ? screen : QGuiApplication::primaryScreen();
}

// The refusal that makes the fixture gate fail, or empty with A, B and C
// built, placed and shown.
[[nodiscard]] QString BuildFixture(not_null<State*> state) {
	const auto shield = ReadConsoleShield();
	if (shield.armed) {
		return u"the console shield is already armed (%1), and the "
			u"self-test measures only arms it makes itself"_q.arg(
				ConsoleShieldSummary(shield));
	}
	const auto cursor = QCursor::pos();
	const auto screen = FixtureScreen(cursor);
	if (!screen) {
		return u"no screen: neither one at the cursor nor a primary one"_q;
	}
	BuildSubject(&state->a, u"A"_q, true);
	BuildSubject(&state->b, u"B"_q, true);
	BuildSubject(&state->c, u"C"_q, true);
	const auto subjects = std::vector<not_null<Subject*>>{
		&state->a,
		&state->b,
		&state->c,
	};
	state->placement = PlaceColumn(
		subjects,
		screen->availableGeometry(),
		cursor);
	for (const auto subject : subjects) {
		ShowSubject(subject);
	}
	return QString();
}

[[nodiscard]] QString BuildFinishSubject(not_null<State*> state) {
	const auto cursor = QCursor::pos();
	const auto screen = FixtureScreen(cursor);
	if (!screen) {
		return u"no screen: neither one at the cursor nor a primary one"_q;
	}
	BuildSubject(&state->d, u"D"_q, false);
	state->finishPlacement = PlaceColumn(
		{ &state->d },
		screen->availableGeometry(),
		cursor);
	ShowSubject(&state->d);
	return QString();
}

// Releases the shield only while the self-test's own measured arm holds
// it, so no cleanup ever preempts the finish-only arm on D.
bool ReleaseOwnedArm(const QString &by) {
	const auto reading = ReadConsoleShield();
	return reading.armed
		&& (reading.label == kArmLabel)
		&& ReleaseConsoleShield(by);
}

[[nodiscard]] int FixtureWindows(const State &state) {
	return (state.a.window ? 1 : 0)
		+ (state.b.window ? 1 : 0)
		+ (state.c.window ? 1 : 0);
}

void DestroyFixture(not_null<State*> state) {
	state->newer = nullptr;
	state->a.window = nullptr;
	state->b.window = nullptr;
	state->c.window = nullptr;
}

// Each class measured on its own, outside Test::Settle, with its
// baselines read right before its delivery. The shortcut needs its window
// to be Qt's active window: ForceWindowActive flushes the queued platform
// events before the baseline is read, and the saved focus window and
// widget are given back before the close.
[[nodiscard]] ForeignLegs RunForeignLegs(
		not_null<Subject*> subject,
		NewerFilter *newer) {
	auto result = ForeignLegs();
	if (!subject->window || !subject->field || !subject->button) {
		return result;
	}
	const auto window = subject->window->windowHandle();
	subject->field->setFocusFast();

	const auto newerBefore = newer ? newer->keys() : 0;
	if (newer) {
		newer->setCounting(true);
	}
	result.keys = MeasureLeg(*subject, [&] {
		const auto sent = PlatformKeys(window);
		SettlePostponedCalls();
		return sent;
	});
	if (newer) {
		newer->setCounting(false);
		result.newerKeys = newer->keys() - newerBefore;
		result.newerPresent = true;
	}

	auto click = PlatformClickReading();
	result.mouse = MeasureLeg(*subject, [&] {
		click = PlatformClick(subject->button.data());
		SettlePostponedCalls();
		return click.sent;
	});
	result.points = PlatformClickText(click);

	const auto saved = SaveFocus();
	result.activation = ForceWindowActive(subject->field.data());
	result.subjectActive
		= (QApplication::activeWindow() == subject->window.get());
	result.shortcut = MeasureLeg(*subject, [&] {
		const auto handled = PlatformShortcut(window);
		SettlePostponedCalls();
		return handled;
	});
	result.focusRestored = RestoreFocus(saved);
	result.focus = FocusText(saved);

	result.close = MeasureLeg(*subject, [&] {
		const auto accepted = PlatformClose(window);
		SettlePostponedCalls();
		return accepted;
	});
	return result;
}

[[nodiscard]] QString ShortcutText(
		const LegReading &leg,
		const WindowActivation &activation,
		bool subjectActive) {
	return u"%1 subjectActive=%2 sequence=%3 %4"_q.arg(
		WindowActivationDetails(activation),
		subjectActive ? u"1"_q : u"0"_q,
		ShortcutSequence().toString(QKeySequence::PortableText),
		LegText(leg));
}

// B before the arm and C after the release: the same four deliveries
// reach their receivers and close the window, with no veto.
void CheckReached(const QString &who, const ForeignLegs &legs) {
	const auto keys = Difference(legs.keys);
	Check(
		Unarmed(legs.keys)
			&& (keys.window.keyPress == 1)
			&& (keys.window.keyRelease == 1)
			&& (keys.field.keyPress == 1)
			&& (keys.field.keyRelease == 1)
			&& (keys.field.spontaneous == 2)
			&& (keys.foreign == 1)
			&& (Vetoes(legs.keys) == 0),
		u"console shield self-test: %1: a foreign key press and release "
		u"through QWindowSystemInterface reach the window and then the "
		u"field, spontaneous, and type the synthetic letter, with no "
		u"veto"_q.arg(who),
		LegText(legs.keys));
	const auto mouse = Difference(legs.mouse);
	Check(
		Unarmed(legs.mouse)
			&& (mouse.window.mousePress == 1)
			&& (mouse.window.mouseRelease == 1)
			&& (mouse.button.mousePress == 1)
			&& (mouse.button.mouseRelease == 1)
			&& (mouse.button.mouseDouble == 0)
			&& (mouse.clicks == 1)
			&& (Vetoes(legs.mouse) == 0),
		u"console shield self-test: %1: a foreign converted mouse press "
		u"and release reach the window and then the button, which "
		u"clicks, with no veto"_q.arg(who),
		legs.points + u" "_q + LegText(legs.mouse));
	const auto shortcut = Difference(legs.shortcut);
	Check(
		Unarmed(legs.shortcut)
			&& (shortcut.activations == 1)
			&& (Vetoes(legs.shortcut) == 0),
		u"console shield self-test: %1: a foreign Ctrl+Alt+Shift+F12 "
		u"through handleShortcutEvent activates the window's shortcut, "
		u"with no veto"_q.arg(who),
		ShortcutText(legs.shortcut, legs.activation, legs.subjectActive));
	Check(
		legs.focusRestored,
		u"console shield self-test: %1: the focus window and focus widget "
		u"the shortcut leg's activation replaced are restored"_q.arg(who),
		legs.focus);
	const auto close = Difference(legs.close);
	Check(
		Unarmed(legs.close)
			&& legs.close.returned
			&& legs.close.after.hidden
			&& !legs.close.after.platformWindow
			&& (close.window.close == 1)
			&& (close.widget.close == 1)
			&& (Vetoes(legs.close) == 0),
		u"console shield self-test: %1: a foreign close request through "
		u"handleCloseEvent closes the window and destroys its platform "
		u"window, with no veto"_q.arg(who),
		LegText(legs.close));
}

// Armed A: the same four deliveries are vetoed at the window, counted per
// class exactly as sent, and no receiver sees them. The companions Qt
// synthesizes - a shortcut override, a pre-press pointer move - are
// counted in their own classes and printed, never as keys or buttons.
void CheckVetoed(const ForeignLegs &legs) {
	const auto keys = Difference(legs.keys);
	const auto keyVetoes = Vetoed(legs.keys, ConsoleInputClass::Key);
	const auto keyOverrides = Vetoed(
		legs.keys,
		ConsoleInputClass::ShortcutOverride);
	Check(
		ArmedThrough(legs.keys, kArmLabel)
			&& (keyVetoes == 2)
			&& (Vetoes(legs.keys) == keyVetoes + keyOverrides)
			&& (keys.window.keyPress == 0)
			&& (keys.window.keyRelease == 0)
			&& (keys.field.keyPress == 0)
			&& (keys.field.keyRelease == 0)
			&& (legs.keys.after.text == legs.keys.before.text),
		u"console shield self-test: armed A: a foreign key press and "
		u"release are vetoed and counted key=2, and neither the window nor "
		u"the field sees them or their text"_q,
		LegText(legs.keys));
	const auto mouse = Difference(legs.mouse);
	const auto buttons = Vetoed(legs.mouse, ConsoleInputClass::MouseButton);
	const auto moves = Vetoed(legs.mouse, ConsoleInputClass::Pointer);
	Check(
		ArmedThrough(legs.mouse, kArmLabel)
			&& (buttons == 2)
			&& (Vetoes(legs.mouse) == buttons + moves)
			&& (mouse.window.mousePress == 0)
			&& (mouse.window.mouseRelease == 0)
			&& (mouse.button.mousePress == 0)
			&& (mouse.button.mouseRelease == 0)
			&& (mouse.clicks == 0),
		u"console shield self-test: armed A: a foreign converted mouse press "
		u"and release are vetoed and counted mouse-button=2, and neither "
		u"the window nor the button sees them"_q,
		legs.points + u" "_q + LegText(legs.mouse));
	const auto shortcut = Difference(legs.shortcut);
	const auto shortcuts = Vetoed(legs.shortcut, ConsoleInputClass::Shortcut);
	const auto shortcutOverrides = Vetoed(
		legs.shortcut,
		ConsoleInputClass::ShortcutOverride);
	Check(
		ArmedThrough(legs.shortcut, kArmLabel)
			&& (shortcuts == 1)
			&& (Vetoes(legs.shortcut) == shortcuts + shortcutOverrides)
			&& (shortcut.activations == 0),
		u"console shield self-test: armed A: a foreign Ctrl+Alt+Shift+F12 "
		u"is vetoed and counted shortcut=1, and the window's shortcut does "
		u"not fire"_q,
		ShortcutText(legs.shortcut, legs.activation, legs.subjectActive));
	Check(
		legs.focusRestored,
		u"console shield self-test: armed A: the focus window and focus "
		u"widget the shortcut leg's activation replaced are restored"_q,
		legs.focus);
	const auto close = Difference(legs.close);
	Check(
		ArmedThrough(legs.close, kArmLabel)
			&& (Vetoed(legs.close, ConsoleInputClass::Close) == 1)
			&& (Vetoes(legs.close) == 1)
			&& !legs.close.returned
			&& (close.window.close == 1)
			&& (close.widget.close == 0)
			&& !legs.close.after.hidden
			&& legs.close.after.platformWindow,
		u"console shield self-test: armed A: a foreign close request is "
		u"refused and counted close=1: the window-level Close passes, the "
		u"widget never gets its own, and the window stays shown with its "
		u"platform window"_q,
		LegText(legs.close));
}

void RunHarnessLegs(not_null<Runner*> runner, not_null<State*> state) {
	auto &legs = state->harness;
	const auto subject = &state->a;
	ArmConsoleShield(runner, kArmLabel);
	legs.armed = ReadConsoleShield();
	if (!subject->window || !subject->field || !subject->button) {
		return;
	}
	const auto button = subject->button.data();
	const auto raw = subject->field->rawTextEdit();
	legs.click = MeasureLeg(*subject, [&] {
		Click(button);
		return true;
	});
	legs.typed = MeasureLeg(*subject, [&] {
		TypeText(raw, kHarnessTyped);
		return true;
	});
	legs.backspace = MeasureLeg(*subject, [&] {
		PressKey(raw, Qt::Key_Backspace);
		return true;
	});
	legs.committed = MeasureLeg(*subject, [&] {
		CommitText(raw, kHarnessCommitted);
		return true;
	});
}

void CheckHarnessLegs(const HarnessLegs &legs) {
	Check(
		legs.armed.armed
			&& (legs.armed.label == kArmLabel)
			&& (legs.armed.vetoes == 0),
		u"console shield self-test: armed A: ArmConsoleShield makes a fresh "
		u"arm with the self-test's label"_q,
		ReadingText(legs.armed));
	const auto click = Difference(legs.click);
	Check(
		ArmedThrough(legs.click, kArmLabel)
			&& (click.clicks == 1)
			&& (click.button.mousePress == 1)
			&& (click.button.mouseRelease == 1)
			&& (Vetoes(legs.click) == 0),
		u"console shield self-test: armed A: a harness Click reaches the "
		u"button and clicks it, with no veto"_q,
		LegText(legs.click));
	const auto typed = Difference(legs.typed);
	Check(
		ArmedThrough(legs.typed, kArmLabel)
			&& (legs.typed.after.text
				== legs.typed.before.text + kHarnessTyped)
			&& (typed.field.keyPress == kHarnessTyped.size())
			&& (typed.field.keyRelease == kHarnessTyped.size())
			&& (Vetoes(legs.typed) == 0),
		u"console shield self-test: armed A: a harness TypeText reaches "
		u"the field and types its text, with no veto"_q,
		LegText(legs.typed));
	const auto &before = legs.backspace.before.text;
	const auto erased = before.isEmpty()
		? QString()
		: before.left(before.size() - 1);
	const auto backspace = Difference(legs.backspace);
	Check(
		ArmedThrough(legs.backspace, kArmLabel)
			&& !before.isEmpty()
			&& (legs.backspace.after.text == erased)
			&& (backspace.field.keyPress == 1)
			&& (Vetoes(legs.backspace) == 0),
		u"console shield self-test: armed A: a harness PressKey Backspace "
		u"reaches the field and erases one character, with no veto"_q,
		LegText(legs.backspace));
	const auto committed = Difference(legs.committed);
	Check(
		ArmedThrough(legs.committed, kArmLabel)
			&& (legs.committed.after.text
				== legs.committed.before.text + kHarnessCommitted)
			&& (committed.field.inputMethod == 1)
			&& (Vetoes(legs.committed) == 0),
		u"console shield self-test: armed A: a harness CommitText, a sent "
		u"input-method event, reaches the field and commits its text, with "
		u"no veto"_q,
		LegText(legs.committed));
}

void RunHarnessPlatformLegs(not_null<State*> state) {
	auto &legs = state->harnessPlatform;
	const auto subject = &state->a;
	if (!subject->window || !subject->field) {
		return;
	}
	const auto window = subject->window->windowHandle();
	const auto raw = subject->field->rawTextEdit();
	subject->field->setFocusFast();

	const auto saved = SaveFocus();
	legs.activation = ForceWindowActive(subject->field.data());
	legs.subjectActive
		= (QApplication::activeWindow() == subject->window.get());
	legs.shortcut = MeasureLeg(*subject, [&] {
		PressKey(raw, kShortcutKey, kShortcutModifiers);
		return true;
	});
	legs.sentOutside = MeasureLeg(*subject, [&] {
		auto press = QKeyEvent(
			QEvent::KeyPress,
			kShortcutKey,
			kShortcutModifiers);
		QApplication::sendEvent(raw.get(), &press);
		auto release = QKeyEvent(
			QEvent::KeyRelease,
			kShortcutKey,
			kShortcutModifiers);
		QApplication::sendEvent(raw.get(), &release);
		return true;
	});
	legs.focusRestored = RestoreFocus(saved);
	legs.focus = FocusText(saved);

	subject->field->setFocusFast();
	legs.keys = MeasureLeg(*subject, [&] {
		auto sent = false;
		Settle([&] {
			sent = PlatformKeys(window);
		});
		return sent;
	});
}

void CheckHarnessPlatformLegs(const HarnessPlatformLegs &legs) {
	const auto shortcut = Difference(legs.shortcut);
	Check(
		ArmedThrough(legs.shortcut, kArmLabel)
			&& (shortcut.activations == 1)
			&& (Vetoes(legs.shortcut) == 0),
		u"console shield self-test: armed A: a harness PressKey of "
		u"Ctrl+Alt+Shift+F12 produces a sent QShortcutEvent inside Settle "
		u"that passes and fires the window's shortcut, with no veto"_q,
		ShortcutText(legs.shortcut, legs.activation, legs.subjectActive));
	const auto outside = Difference(legs.sentOutside);
	Check(
		ArmedThrough(legs.sentOutside, kArmLabel)
			&& (outside.activations == 0)
			&& (Vetoed(legs.sentOutside, ConsoleInputClass::Shortcut) == 1)
			&& (Vetoes(legs.sentOutside) == 1),
		u"console shield self-test: armed A: the same sent key pair outside "
		u"Settle is vetoed at its QShortcutEvent, counted shortcut=1, and "
		u"the shortcut does not fire - the scope decides, not "
		u"spontaneity"_q,
		ShortcutText(
			legs.sentOutside,
			legs.activation,
			legs.subjectActive));
	Check(
		legs.focusRestored,
		u"console shield self-test: armed A: the focus window and focus "
		u"widget the harness shortcut legs' activation replaced are "
		u"restored"_q,
		legs.focus);
	const auto keys = Difference(legs.keys);
	Check(
		ArmedThrough(legs.keys, kArmLabel)
			&& legs.keys.returned
			&& (keys.field.keyPress == 1)
			&& (keys.field.keyRelease == 1)
			&& (keys.field.spontaneous == 2)
			&& (keys.foreign == 1)
			&& (Vetoes(legs.keys) == 0),
		u"console shield self-test: armed A: a QWindowSystemInterface key "
		u"pair wrapped in Settle is spontaneous, yet passes as harness "
		u"input and types into the field, with no veto"_q,
		LegText(legs.keys));
}

void RunNewerFilterControl(not_null<State*> state) {
	const auto subject = &state->a;
	state->newer = std::make_unique<NewerFilter>();
	state->newerInstalledAt = crl::now();
	if (!subject->window || !subject->field) {
		return;
	}
	const auto window = subject->window->windowHandle();
	subject->field->setFocusFast();
	state->newer->setCounting(true);
	state->newerFront = MeasureLeg(*subject, [&] {
		const auto sent = PlatformKeys(window);
		SettlePostponedCalls();
		return sent;
	});
	state->newer->setCounting(false);
	state->newerFrontKeys = state->newer->keys();
}

// D's arm was left armed on purpose. Registered after that arm, so after
// the shield's own finish release: the release must already have run.
void FinishCheck(not_null<State*> state) {
	const auto reading = ReadConsoleShield();
	Check(
		!reading.armed && (reading.label == kFinishLabel),
		u"console shield self-test: finish: Runner::onFinish released D's "
		u"arm, which no teardown stage released, before this "
		u"later-registered check"_q,
		ReadingText(reading));
	const auto subject = &state->d;
	if (!kPlatformInput) {
		Skipped(
			u"console shield self-test: finish: after the finish release a "
			u"foreign key pair reaches D's field"_q,
			PlatformInputSkip());
	} else if (subject->window && subject->field) {
		const auto window = subject->window->windowHandle();
		subject->field->setFocusFast();
		const auto leg = MeasureLeg(*subject, [&] {
			const auto sent = PlatformKeys(window);
			SettlePostponedCalls();
			return sent;
		});
		const auto keys = Difference(leg);
		Check(
			Unarmed(leg)
				&& (keys.field.keyPress == 1)
				&& (keys.field.keyRelease == 1)
				&& (keys.foreign == 1)
				&& (Vetoes(leg) == 0),
			u"console shield self-test: finish: after the finish release a "
			u"foreign key pair reaches D's field, with no veto"_q,
			LegText(leg));
	} else {
		Check(
			false,
			u"console shield self-test: finish: after the finish release a "
			u"foreign key pair reaches D's field"_q,
			u"D or its field is gone before the finish check"_q);
	}
	subject->window = nullptr;
	Note(u"console shield self-test: finish check done, D destroyed"_q);
}

} // namespace

void AppendConsoleShieldSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<State>();
	const auto gate = [=] {
		return state->gate;
	};

	// Registered before any arm, so on an early finish it runs before the
	// shield's own release: it releases only the self-test's measured arm,
	// before deleting A, B and C. Once those are gone it leaves D, and D's
	// arm, to the finish check whose measurement is that release.
	runner->onFinish([=] {
		state->newer = nullptr;
		if (FixtureWindows(*state)) {
			ReleaseOwnedArm(u"self-test-cleanup"_q);
			DestroyFixture(state.get());
		}
		if (state->d.window && !state->finishCheckRegistered) {
			state->d.window = nullptr;
		}
	});

	runner->add({
		.name = u"console shield self-test: fixture - three purpose-built "
			u"windows shown without activation, shield not armed"_q,
		.run = [=] {
			state->buildRefusal = BuildFixture(state.get());
		},
		.until = [=] {
			return !state->buildRefusal.isEmpty()
				|| (SubjectReady(state->a)
					&& SubjectReady(state->b)
					&& SubjectReady(state->c));
		},
		.then = [=] {
			const auto ready = state->buildRefusal.isEmpty()
				&& SubjectReady(state->a)
				&& SubjectReady(state->b)
				&& SubjectReady(state->c);
			state->gate = ready
				? QString()
				: state->buildRefusal.isEmpty()
				? u"fixture gate: the self-test's windows are not shown"_q
				: (u"fixture gate: "_q + state->buildRefusal);
			Check(
				ready,
				u"fixture gate: console shield self-test windows A, B and C "
				u"are shown and the shield is not armed"_q,
				state->buildRefusal.isEmpty()
					? FixtureText(*state)
					: state->buildRefusal);
		},
		.timeoutDetails = [=] {
			return FixtureText(*state);
		},
	});

	runner->addGated(gate, [&] {
		runner->add({
			.name = u"console shield self-test: unarmed control B - foreign "
				u"key, mouse, shortcut and close reach it and close it"_q,
			.skipReason = PlatformInputSkip,
			.run = [=] {
				state->control = RunForeignLegs(&state->b, nullptr);
			},
			.then = [=] {
				CheckReached(u"unarmed control B"_q, state->control);
			},
		});
		runner->add({
			.name = u"console shield self-test: arm, then harness Click, "
				u"TypeText, PressKey and CommitText reach A"_q,
			.run = [=] {
				RunHarnessLegs(runner, state.get());
			},
			.then = [=] {
				CheckHarnessLegs(state->harness);
			},
		});
		runner->add({
			.name = u"console shield self-test: armed A - harness shortcut and "
				u"Settle-wrapped platform keys pass, the same sent shortcut "
				u"outside Settle is vetoed"_q,
			.skipReason = PlatformInputSkip,
			.run = [=] {
				RunHarnessPlatformLegs(state.get());
			},
			.then = [=] {
				CheckHarnessPlatformLegs(state->harnessPlatform);
			},
		});
		runner->add({
			.name = u"console shield self-test: armed A - a newer application "
				u"filter runs first until the shield is re-fronted"_q,
			.skipReason = PlatformInputSkip,
			.run = [=] {
				RunNewerFilterControl(state.get());
			},
			.until = [=] {
				return !state->newer
					|| ((crl::now() - state->newerInstalledAt)
						>= kRefrontYield);
			},
			.then = [=] {
				state->newerWaited = crl::now() - state->newerInstalledAt;
				Check(
					ArmedThrough(state->newerFront, kArmLabel)
						&& (state->newerFrontKeys == 2)
						&& (Vetoed(state->newerFront, ConsoleInputClass::Key)
							== 2),
					u"console shield self-test: armed A: an application "
					u"filter installed after the arm runs first and sees the "
					u"foreign key pair the shield then vetoes (the re-front "
					u"control)"_q,
					u"newerKeys=%1 %2"_q.arg(
						QString::number(state->newerFrontKeys),
						LegText(state->newerFront)));
				Note(u"console shield self-test: yielded %1 ms for the "
					u"shield's re-front ticks, requested %2 ms, refrontMs=%3"_q
					.arg(state->newerWaited)
					.arg(kRefrontYield)
					.arg(kConsoleShieldRefront));
			},
		});
		runner->add({
			.name = u"console shield self-test: armed A - foreign key, mouse, "
				u"shortcut and close are vetoed and counted"_q,
			.skipReason = PlatformInputSkip,
			.run = [=] {
				state->vetoed = RunForeignLegs(&state->a, state->newer.get());
				state->newer = nullptr;
			},
			.then = [=] {
				CheckVetoed(state->vetoed);
				Check(
					state->vetoed.newerPresent
						&& (state->vetoed.newerKeys == 0),
					u"console shield self-test: armed A: after yielding past "
					u"re-front ticks the shield runs before the newer "
					u"application filter, which sees none of the foreign "
					u"keys"_q,
					u"newerPresent=%1 newerKeys=%2 yieldedMs=%3"_q
						.arg(state->vetoed.newerPresent ? 1 : 0)
						.arg(state->vetoed.newerKeys)
						.arg(state->newerWaited));
			},
		});
		runner->add({
			.name = u"console shield self-test: armed A - QWidget::close() "
				u"passes"_q,
			.run = [=] {
				const auto subject = &state->a;
				state->productClose = MeasureLeg(*subject, [&] {
					return subject->window && subject->window->close();
				});
			},
			.then = [=] {
				const auto &leg = state->productClose;
				const auto close = Difference(leg);
				Check(
					ArmedThrough(leg, kArmLabel)
						&& leg.returned
						&& leg.after.hidden
						&& !leg.after.platformWindow
						&& (close.widget.close == 1)
						&& (close.widget.spontaneous == 0)
						&& (Vetoes(leg) == 0),
					u"console shield self-test: armed A: QWidget::close() "
					u"closes the window and destroys its platform window with "
					u"no veto: the widget's own Close arrives sent"_q,
					LegText(leg));
			},
		});
		runner->add({
			.name = u"console shield self-test: release"_q,
			.run = [=] {
				state->releaseBefore = ReadConsoleShield();
				state->firstReleased = ReleaseConsoleShield(u"self-test"_q);
				state->releaseAfter = ReadConsoleShield();
			},
			.then = [=] {
				const auto &before = state->releaseBefore;
				const auto &after = state->releaseAfter;
				Check(
					state->firstReleased
						&& before.armed
						&& !after.armed
						&& (after.label == kArmLabel)
						&& (after.label == before.label)
						&& (after.armedAt == before.armedAt)
						&& (after.vetoes == before.vetoes)
						&& (after.counts == before.counts),
					u"console shield self-test: release: ReleaseConsoleShield "
					u"returns true, ends the arm and keeps its per-class "
					u"counts"_q,
					u"released=%1 before: %2 after: %3"_q.arg(
						state->firstReleased ? u"1"_q : u"0"_q,
						ReadingText(before),
						ReadingText(after)));
			},
		});
		runner->add({
			.name = u"console shield self-test: released - foreign key, "
				u"mouse, shortcut and close reach C and close it"_q,
			.skipReason = PlatformInputSkip,
			.run = [=] {
				state->replay = RunForeignLegs(&state->c, nullptr);
			},
			.then = [=] {
				CheckReached(u"released, C"_q, state->replay);
				Check(
					SameReading(
						state->releaseAfter,
						state->replay.keys.shieldBefore)
						&& SameReading(
							state->releaseAfter,
							state->replay.close.shieldAfter),
					u"console shield self-test: released: the replayed "
					u"foreign classes leave the released reading "
					u"unchanged"_q,
					u"released: %1 replayed: %2"_q.arg(
						ReadingText(state->releaseAfter),
						ReadingText(state->replay.close.shieldAfter)));
			},
		});
		runner->add({
			.name = u"console shield self-test: a second release is a "
				u"no-op"_q,
			.run = [=] {
				state->secondBefore = ReadConsoleShield();
				state->secondReleased = ReleaseConsoleShield(
					u"self-test-second"_q);
				state->secondAfter = ReadConsoleShield();
			},
			.then = [=] {
				Check(
					!state->secondReleased
						&& !state->secondAfter.armed
						&& SameReading(
							state->secondBefore,
							state->secondAfter),
					u"console shield self-test: a second release returns "
					u"false and changes nothing in the reading"_q,
					u"released=%1 before: %2 after: %3"_q.arg(
						state->secondReleased ? u"1"_q : u"0"_q,
						ReadingText(state->secondBefore),
						ReadingText(state->secondAfter)));
			},
		});
	});

	runner->addRecovery({
		.name = u"console shield self-test: teardown - release an owned arm "
			u"before the closes, then destroy A, B and C"_q,
		.run = [=] {
			const auto released = ReleaseOwnedArm(u"self-test-teardown"_q);
			const auto windows = FixtureWindows(*state);
			DestroyFixture(state.get());
			Note(u"console shield self-test: teardown released=%1 "
				u"windows=%2->%3 %4"_q.arg(
					released ? u"1"_q : u"0"_q,
					QString::number(windows),
					QString::number(FixtureWindows(*state)),
					ReadingText(ReadConsoleShield())));
		},
	});

	runner->addGated(gate, [&] {
		runner->add({
			.name = u"console shield self-test: finish - arm on D and leave "
				u"it armed for Runner::onFinish"_q,
			.run = [=] {
				state->finishRefusal = BuildFinishSubject(state.get());
			},
			.until = [=] {
				return !state->finishRefusal.isEmpty()
					|| SubjectReady(state->d);
			},
			.then = [=] {
				const auto ready = state->finishRefusal.isEmpty()
					&& SubjectReady(state->d);
				Check(
					ready,
					u"fixture gate: console shield self-test window D is "
					u"shown"_q,
					state->finishRefusal.isEmpty()
						? (state->finishPlacement
							+ u" "_q
							+ SubjectText(state->d))
						: state->finishRefusal);
				if (!ready) {
					return;
				}
				ArmConsoleShield(runner, kFinishLabel);
				const auto armed = ReadConsoleShield();
				Check(
					armed.armed
						&& (armed.label == kFinishLabel)
						&& (armed.vetoes == 0),
					u"console shield self-test: finish: D's arm is armed, "
					u"and no teardown stage releases it"_q,
					ReadingText(armed));
				runner->onFinish([=] {
					FinishCheck(state.get());
				});
				state->finishCheckRegistered = true;
			},
			.timeoutDetails = [=] {
				return SubjectText(state->d);
			},
		});
	});
}

} // namespace Test

#endif // _DEBUG
