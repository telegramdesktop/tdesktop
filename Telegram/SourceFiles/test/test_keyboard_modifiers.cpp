/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_keyboard_modifiers.h"

#include "base/qt/qt_key_modifiers.h"
#include "base/unique_qptr.h"
#include "core/application.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_qpa_mouse.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "window/window_controller.h"

#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QKeySequence>
#include <QtGui/QMouseEvent>
#include <QtGui/QPainter>
#include <QtGui/QShortcut>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>

#include <vector>

#include "styles/palette.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

// The recorder is kRecorderCells cells wide and one cell high, a cell
// being st::defaultActiveButton.height. Every chord presses kChordKey,
// which no product shortcut binds with Control, Shift or Alt.
constexpr auto kRecorderCells = 4;
constexpr auto kChordKey = Qt::Key_B;

#ifdef DESKTOP_APP_DISABLE_PRIVATE_QT_API
constexpr auto kPrivateApi = false;
#else // DESKTOP_APP_DISABLE_PRIVATE_QT_API
constexpr auto kPrivateApi = true;
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

// One modifier the legs press: as the chord's modifier, and as its own key
// pressed alone. |name| labels the legs, |modifierName| and |keyName| the
// calls.
struct ModifierCase {
	QString name;
	QString modifierName;
	QString keyName;
	Qt::KeyboardModifier modifier = Qt::NoModifier;
	Qt::Key key = Qt::Key_unknown;
};

// A key or mouse event as the recorder read it in its handler: |event| is
// the event's own modifiers() (QKeyEvent::modifiers() for a key, which
// toggles a modifier key's own flag), |cached| is
// QGuiApplication::keyboardModifiers() at that moment.
struct RecordedEvent {
	QEvent::Type type = QEvent::None;
	int key = 0;
	Qt::KeyboardModifiers event = Qt::NoModifier;
	Qt::KeyboardModifiers cached = Qt::NoModifier;
	bool spontaneous = false;
};

// Qt's cached modifiers and the three exact-match readings product code
// takes of them, read together.
struct ModifierReading {
	Qt::KeyboardModifiers cached = Qt::NoModifier;
	bool ctrl = false;
	bool shift = false;
	bool alt = false;
};

// One action: the cached state right before it, right after it returned,
// and every event the recorder got meanwhile. |measured| is false when the
// recorder was gone before or after the action.
struct Measurement {
	ModifierReading before;
	ModifierReading after;
	std::vector<RecordedEvent> events;
	bool measured = false;
};

// Per modifier: the raw sent pair (the control), the chord and the
// modifier key alone, measured in this order in one turn.
struct CaseLegs {
	Measurement raw;
	Measurement chord;
	Measurement alone;
};

// The Ctrl+B widget shortcut leg. |handler| is read inside the shortcut's
// activated(); |focus| describes QApplication::focusWidget() right before
// the press.
struct ShortcutLeg {
	Measurement press;
	WindowActivation activation;
	ModifierReading handler;
	QString focus;
	int activations = 0;
	int ambiguous = 0;
	bool focused = false;
};

// The Ctrl left click through one QpaMouseSequence. |beforeEnd| is read
// after the release and right before end(), |after| right after it.
struct QpaLeg {
	QpaMouseDelivery press;
	QpaMouseDelivery release;
	QpaMouseEnd end;
	ModifierReading before;
	ModifierReading beforeEnd;
	ModifierReading after;
	std::vector<RecordedEvent> events;
	QString recorder;
	bool measured = false;
};

// A focusable widget that accepts every key and mouse press and release
// and records each one.
class Recorder final : public Ui::RpWidget {
public:
	explicit Recorder(QWidget *parent);

	void reset();
	[[nodiscard]] const std::vector<RecordedEvent> &events() const;

protected:
	void keyPressEvent(QKeyEvent *e) override;
	void keyReleaseEvent(QKeyEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	void record(
		not_null<QInputEvent*> e,
		int key,
		Qt::KeyboardModifiers modifiers);

	std::vector<RecordedEvent> _events;

};

// The stages and the finish callback share it. |gate| stays a refusal
// until the fixture stage accepts the recorder.
struct State {
	base::unique_qptr<Ui::RpWidget> container;
	QPointer<Recorder> recorder;
	std::vector<CaseLegs> legs;
	ShortcutLeg shortcut;
	QpaLeg qpa;
	QString gate = u"fixture gate: the fixture stage has not run"_q;
	QString buildRefusal;
};

Recorder::Recorder(QWidget *parent)
: RpWidget(parent) {
	setFocusPolicy(Qt::StrongFocus);
}

void Recorder::reset() {
	_events.clear();
}

const std::vector<RecordedEvent> &Recorder::events() const {
	return _events;
}

void Recorder::keyPressEvent(QKeyEvent *e) {
	record(e, e->key(), e->modifiers());
}

void Recorder::keyReleaseEvent(QKeyEvent *e) {
	record(e, e->key(), e->modifiers());
}

void Recorder::mousePressEvent(QMouseEvent *e) {
	record(e, 0, e->modifiers());
}

void Recorder::mouseReleaseEvent(QMouseEvent *e) {
	record(e, 0, e->modifiers());
}

void Recorder::record(
		not_null<QInputEvent*> e,
		int key,
		Qt::KeyboardModifiers modifiers) {
	_events.push_back({
		.type = e->type(),
		.key = key,
		.event = modifiers,
		.cached = QGuiApplication::keyboardModifiers(),
		.spontaneous = e->spontaneous(),
	});
	e->accept();
}

[[nodiscard]] std::vector<ModifierCase> ModifierCases() {
	return {
		{
			.name = u"ctrl"_q,
			.modifierName = u"ControlModifier"_q,
			.keyName = u"Key_Control"_q,
			.modifier = Qt::ControlModifier,
			.key = Qt::Key_Control,
		},
		{
			.name = u"shift"_q,
			.modifierName = u"ShiftModifier"_q,
			.keyName = u"Key_Shift"_q,
			.modifier = Qt::ShiftModifier,
			.key = Qt::Key_Shift,
		},
		{
			.name = u"alt"_q,
			.modifierName = u"AltModifier"_q,
			.keyName = u"Key_Alt"_q,
			.modifier = Qt::AltModifier,
			.key = Qt::Key_Alt,
		},
	};
}

[[nodiscard]] QString UnsupportedReason() {
	return u"unsupported: built with DESKTOP_APP_DISABLE_PRIVATE_QT_API, "
		u"so PressKey and QpaMouseSequence::end() do not reset Qt's cached "
		u"keyboard modifiers"_q;
}

[[nodiscard]] ModifierReading ReadModifiers() {
	return {
		.cached = QGuiApplication::keyboardModifiers(),
		.ctrl = base::IsCtrlPressed(),
		.shift = base::IsShiftPressed(),
		.alt = base::IsAltPressed(),
	};
}

[[nodiscard]] bool Released(const ModifierReading &reading) {
	return (reading.cached == Qt::NoModifier)
		&& !reading.ctrl
		&& !reading.shift
		&& !reading.alt;
}

// Reads the cached state right before |action| and right after it
// returns, before anything else runs, and what the recorder got.
[[nodiscard]] Measurement Measure(
		const QPointer<Recorder> &recorder,
		Fn<void(not_null<Recorder*>)> action) {
	auto result = Measurement();
	const auto strong = recorder.data();
	if (!strong) {
		return result;
	}
	strong->reset();
	result.before = ReadModifiers();
	action(strong);
	result.after = ReadModifiers();
	if (const auto alive = recorder.data()) {
		result.events = alive->events();
		result.measured = true;
	}
	return result;
}

// PressKey as it was before its reset: a sent press and release of |key|
// carrying |modifiers|, each in its own Settle.
void SendRawPair(
		not_null<QWidget*> widget,
		int key,
		Qt::KeyboardModifiers modifiers) {
	auto press = QKeyEvent(QEvent::KeyPress, key, modifiers);
	Settle([&] {
		QApplication::sendEvent(widget.get(), &press);
	});
	auto release = QKeyEvent(QEvent::KeyRelease, key, modifiers);
	Settle([&] {
		QApplication::sendEvent(widget.get(), &release);
	});
}

// Exactly one press of |key| and then one release of it.
[[nodiscard]] bool KeyPair(const Measurement &measurement, int key) {
	const auto &events = measurement.events;
	return measurement.measured
		&& (events.size() == 2)
		&& (events[0].type == QEvent::KeyPress)
		&& (events[0].key == key)
		&& (events[1].type == QEvent::KeyRelease)
		&& (events[1].key == key);
}

[[nodiscard]] bool Carries(
		const RecordedEvent &event,
		Qt::KeyboardModifiers modifiers) {
	return (event.event == modifiers) && (event.cached == modifiers);
}

[[nodiscard]] int CountType(
		const std::vector<RecordedEvent> &events,
		QEvent::Type type) {
	return int(ranges::count(events, type, &RecordedEvent::type));
}

[[nodiscard]] int Presses(const std::vector<RecordedEvent> &events) {
	return CountType(events, QEvent::KeyPress)
		+ CountType(events, QEvent::MouseButtonPress);
}

[[nodiscard]] int Releases(const std::vector<RecordedEvent> &events) {
	return CountType(events, QEvent::KeyRelease)
		+ CountType(events, QEvent::MouseButtonRelease);
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString ModifiersText(Qt::KeyboardModifiers modifiers) {
	return u"0x"_q + QString::number(uint(modifiers.toInt()), 16);
}

// "<cached> isCtrl=<0|1> isShift=<0|1> isAlt=<0|1>".
[[nodiscard]] QString ReadingText(const ModifierReading &reading) {
	return u"%1 isCtrl=%2 isShift=%3 isAlt=%4"_q.arg(
		ModifiersText(reading.cached),
		FlagText(reading.ctrl),
		FlagText(reading.shift),
		FlagText(reading.alt));
}

[[nodiscard]] QString EventTypeName(QEvent::Type type) {
	switch (type) {
	case QEvent::KeyPress: return u"KeyPress"_q;
	case QEvent::KeyRelease: return u"KeyRelease"_q;
	case QEvent::MouseButtonPress: return u"MouseButtonPress"_q;
	case QEvent::MouseButtonRelease: return u"MouseButtonRelease"_q;
	default: break;
	}
	return QString::number(int(type));
}

[[nodiscard]] QString EventsText(const std::vector<RecordedEvent> &events) {
	auto parts = QStringList();
	for (const auto &event : events) {
		parts.push_back(
			u"%1 key=0x%2 event=%3 cached=%4 spontaneous=%5"_q.arg(
				EventTypeName(event.type),
				QString::number(event.key, 16),
				ModifiersText(event.event),
				ModifiersText(event.cached),
				FlagText(event.spontaneous)));
	}
	return parts.join(u"; "_q);
}

// "KEYBOARD_MODIFIERS: leg=<leg> call=<call> measured=<0|1>
// before=<cached> after=<reading> presses=<n> releases=<n>
// events=[<event>; ...]".
[[nodiscard]] QString MeasurementText(
		const QString &leg,
		const QString &call,
		const Measurement &measurement) {
	return u"KEYBOARD_MODIFIERS: leg=%1 call=%2 measured=%3 before=%4 "
		u"after=%5 presses=%6 releases=%7 events=[%8]"_q.arg(
			leg,
			call,
			FlagText(measurement.measured),
			ModifiersText(measurement.before.cached),
			ReadingText(measurement.after),
			QString::number(Presses(measurement.events)),
			QString::number(Releases(measurement.events)),
			EventsText(measurement.events));
}

[[nodiscard]] QString ShortcutText(const ShortcutLeg &leg) {
	return u"%1 focused=%2 focus=%3 activations=%4 ambiguous=%5 "
		u"handler=[%6] activation=[%7]"_q.arg(
			MeasurementText(
				u"shortcut-ctrl-b"_q,
				u"PressKey(Key_B,ControlModifier)"_q,
				leg.press),
			FlagText(leg.focused),
			leg.focus.isEmpty() ? u"none"_q : leg.focus,
			QString::number(leg.activations),
			QString::number(leg.ambiguous),
			ReadingText(leg.handler),
			WindowActivationDetails(leg.activation));
}

[[nodiscard]] QString QpaText(const QpaLeg &leg) {
	return u"KEYBOARD_MODIFIERS: leg=qpa-ctrl-click measured=%1 "
		u"recorder=%2 before=%3 beforeEnd=%4 after=%5 presses=%6 "
		u"releases=%7 events=[%8] press=[%9] release=[%10] end=[%11]"_q.arg(
			FlagText(leg.measured),
			leg.recorder.isEmpty() ? u"none"_q : leg.recorder,
			ModifiersText(leg.before.cached),
			ModifiersText(leg.beforeEnd.cached),
			ReadingText(leg.after),
			QString::number(Presses(leg.events)),
			QString::number(Releases(leg.events)),
			EventsText(leg.events),
			QpaMouseDeliveryText(leg.press),
			QpaMouseDeliveryText(leg.release),
			QpaMouseEndText(leg.end));
}

// A shown container centred on the primary window, raised above its
// siblings, holding the recorder. Every size comes from the live style.
[[nodiscard]] QString BuildFixture(State &state) {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return u"Core::App().activePrimaryWindow() is null"_q;
	}
	const auto parent = window->widget().get();
	const auto cell = st::defaultActiveButton.height;
	const auto skip = cell / 2;
	state.container = base::make_unique_q<Ui::RpWidget>(parent);
	const auto container = state.container.get();
	container->paintOn([=](QPainter &p) {
		p.fillRect(container->rect(), st::windowBgOver);
	});
	const auto recorder = Ui::CreateChild<Recorder>(container);
	recorder->setGeometry(skip, skip, cell * kRecorderCells, cell);
	auto geometry = QRect(
		QPoint(),
		QSize(
			recorder->width() + skip * 2,
			recorder->height() + skip * 2));
	geometry.moveCenter(parent->rect().center());
	container->setGeometry(geometry);
	recorder->show();
	container->show();
	container->raise();
	state.recorder = recorder;
	return QString();
}

// Readiness only: the recorder and its window's QWindow are visible.
[[nodiscard]] bool RecorderShown(const State &state) {
	const auto recorder = state.recorder.data();
	const auto handle = recorder
		? recorder->window()->windowHandle()
		: nullptr;
	return handle && handle->isVisible() && recorder->isVisible();
}

[[nodiscard]] QString FixtureText(const State &state) {
	const auto recorder = state.recorder.data();
	return u"KEYBOARD_MODIFIERS: fixture qt=%1 privateApi=%2 recorder=%3 "
		u"window=%4 shown=%5 cached=%6"_q.arg(
			QString::fromLatin1(qVersion()),
			FlagText(kPrivateApi),
			recorder ? WidgetDescription(recorder) : u"none"_q,
			recorder ? WidgetDescription(recorder->window()) : u"none"_q,
			FlagText(RecorderShown(state)),
			ModifiersText(QGuiApplication::keyboardModifiers()));
}

void FinishFixture(State &state) {
	auto refusal = state.buildRefusal;
	if (refusal.isEmpty() && !RecorderShown(state)) {
		refusal = u"the recorder is not shown"_q;
	}
	state.gate = refusal.isEmpty()
		? QString()
		: (u"fixture gate: "_q + refusal);
	const auto text = FixtureText(state);
	Check(
		refusal.isEmpty(),
		u"fixture gate: the keyboard modifiers self-test's recorder is "
		u"shown and raised on the primary window"_q,
		refusal.isEmpty() ? text : (text + u" - "_q + refusal));
}

void DestroyFixture(State &state) {
	state.recorder = nullptr;
	state.container = nullptr;
}

// One turn, no yield: per modifier the raw pair leaves it cached, the
// chord starts from that stale value, the modifier key alone from the
// chord's released one.
void RunPressLegs(State &state) {
	const auto cases = ModifierCases();
	state.legs.assign(cases.size(), CaseLegs());
	for (auto i = 0; i != int(cases.size()); ++i) {
		const auto &modifier = cases[i];
		auto &legs = state.legs[i];
		legs.raw = Measure(state.recorder, [&](not_null<Recorder*> to) {
			SendRawPair(to, kChordKey, modifier.modifier);
		});
		legs.chord = Measure(state.recorder, [&](not_null<Recorder*> to) {
			PressKey(to, kChordKey, modifier.modifier);
		});
		legs.alone = Measure(state.recorder, [&](not_null<Recorder*> to) {
			PressKey(to, modifier.key, Qt::NoModifier);
		});
	}
}

void CheckPressLegs(const State &state) {
	const auto cases = ModifierCases();
	for (auto i = 0; i != int(cases.size()); ++i) {
		const auto &modifier = cases[i];
		const auto legs = (i < int(state.legs.size()))
			? state.legs[i]
			: CaseLegs();
		const auto rawText = MeasurementText(
			u"raw-"_q + modifier.name,
			u"raw-pair(Key_B,%1)"_q.arg(modifier.modifierName),
			legs.raw);
		Check(
			KeyPair(legs.raw, kChordKey)
				&& (legs.raw.after.cached == modifier.modifier),
			u"keyboard modifiers self-test: control: a raw sent Key_B "
			u"press and release carrying %1, each in its own Settle and "
			u"with no reset, leave Qt's cached modifiers at %1"_q.arg(
				modifier.modifierName),
			rawText);

		const auto chordCall = u"PressKey(Key_B,%1)"_q.arg(
			modifier.modifierName);
		const auto chordText = MeasurementText(
			u"chord-"_q + modifier.name,
			chordCall,
			legs.chord);
		const auto chordPair = KeyPair(legs.chord, kChordKey);
		Check(
			chordPair && Released(legs.chord.after),
			u"keyboard modifiers self-test: PressKey(Key_B, %1) leaves "
			u"Qt's cached modifiers at NoModifier and every "
			u"base::Is*Pressed() false right on return"_q.arg(
				modifier.modifierName),
			chordText);
		Check(
			chordPair
				&& Carries(legs.chord.events[0], modifier.modifier)
				&& Carries(legs.chord.events[1], modifier.modifier),
			u"keyboard modifiers self-test: PressKey(Key_B, %1) still "
			u"delivers %1 to its receiver, in the event and in Qt's cached "
			u"state, during the press and the release"_q.arg(
				modifier.modifierName),
			chordText);

		const auto aloneCall = u"PressKey(%1,NoModifier)"_q.arg(
			modifier.keyName);
		const auto aloneText = MeasurementText(
			u"alone-"_q + modifier.name,
			aloneCall,
			legs.alone);
		const auto alonePair = KeyPair(legs.alone, modifier.key);
		Check(
			alonePair && Released(legs.alone.after),
			u"keyboard modifiers self-test: PressKey(%1, NoModifier) "
			u"leaves Qt's cached modifiers at NoModifier and every "
			u"base::Is*Pressed() false right on return"_q.arg(
				modifier.keyName),
			aloneText);
		Check(
			alonePair && Carries(legs.alone.events[0], modifier.modifier),
			u"keyboard modifiers self-test: PressKey(%1, NoModifier) "
			u"presses %2: its receiver reads %2 in the press's "
			u"QKeyEvent::modifiers() and in Qt's cached state"_q.arg(
				modifier.keyName,
				modifier.modifierName),
			aloneText);
	}
}

// One turn: the scoped shortcut, the activation, the focus and the press
// are all in force together, and the shortcut dies right after the press.
void RunShortcutLeg(State &state) {
	const auto recorder = state.recorder.data();
	if (!recorder) {
		return;
	}
	const auto leg = &state.shortcut;
	auto shortcut = base::make_unique_q<QShortcut>(
		QKeySequence(Qt::CTRL | kChordKey),
		recorder,
		nullptr,
		nullptr,
		Qt::WidgetShortcut);
	const auto raw = shortcut.get();
	QObject::connect(raw, &QShortcut::activated, raw, [=] {
		++leg->activations;
		leg->handler = ReadModifiers();
	});
	QObject::connect(raw, &QShortcut::activatedAmbiguously, raw, [=] {
		++leg->ambiguous;
	});
	leg->activation = ForceWindowActive(recorder);
	recorder->setFocus();
	const auto focused = QApplication::focusWidget();
	leg->focused = (focused == recorder);
	leg->focus = focused ? WidgetDescription(focused) : QString();
	leg->press = Measure(state.recorder, [&](not_null<Recorder*> to) {
		PressKey(to, kChordKey, Qt::ControlModifier);
	});
	shortcut = nullptr;
}

void CheckShortcutLeg(const State &state) {
	const auto &leg = state.shortcut;
	const auto prerequisite = leg.activation.injected && leg.focused;
	const auto text = ShortcutText(leg);
	Check(
		prerequisite,
		u"keyboard modifiers self-test: shortcut prerequisite: "
		u"ForceWindowActive injected the recorder window's activation and "
		u"the recorder is QApplication::focusWidget(), so its "
		u"Qt::WidgetShortcut can match"_q,
		text);
	Check(
		prerequisite
			&& leg.press.measured
			&& (leg.activations == 1)
			&& (leg.ambiguous == 0)
			&& (leg.handler.cached == Qt::ControlModifier)
			&& leg.handler.ctrl
			&& (CountType(leg.press.events, QEvent::KeyPress) == 0)
			&& Released(leg.press.after),
		u"keyboard modifiers self-test: PressKey(Key_B, ControlModifier) "
		u"on the focused recorder fires its Ctrl+B Qt::WidgetShortcut "
		u"once through Qt's shortcut map with ControlModifier cached in "
		u"the handler, the recorder gets no KeyPress, and Qt's cached "
		u"modifiers read NoModifier with every base::Is*Pressed() false "
		u"right on return"_q,
		text);
}

// One turn: press, release, the reading right before end(), end() and the
// reading right after it.
void RunQpaLeg(State &state) {
	const auto recorder = state.recorder.data();
	if (!recorder) {
		return;
	}
	auto &leg = state.qpa;
	leg.recorder = WidgetDescription(recorder);
	const auto point = recorder->rect().center();
	recorder->reset();
	leg.before = ReadModifiers();
	auto sequence = QpaMouseSequence(recorder);
	leg.press = sequence.deliver({
		.type = QpaMouseType::Press,
		.point = point,
		.button = Qt::LeftButton,
		.buttons = Qt::LeftButton,
		.modifiers = Qt::ControlModifier,
	});
	if (leg.press.sent) {
		leg.release = sequence.deliver({
			.type = QpaMouseType::Release,
			.point = point,
			.button = Qt::LeftButton,
			.buttons = Qt::NoButton,
			.modifiers = Qt::ControlModifier,
		});
	}
	leg.beforeEnd = ReadModifiers();
	leg.end = sequence.end();
	leg.after = ReadModifiers();
	if (const auto alive = state.recorder.data()) {
		leg.events = alive->events();
		leg.measured = true;
	}
}

void CheckQpaLeg(const State &state) {
	const auto &leg = state.qpa;
	const auto &events = leg.events;
	const auto delivered = leg.measured
		&& (leg.press.refusal == QpaMouseRefusal::None)
		&& leg.press.delivered
		&& (leg.press.receiver == leg.recorder)
		&& (leg.release.refusal == QpaMouseRefusal::None)
		&& leg.release.delivered
		&& (events.size() == 2)
		&& (events[0].type == QEvent::MouseButtonPress)
		&& Carries(events[0], Qt::ControlModifier)
		&& (events[1].type == QEvent::MouseButtonRelease)
		&& (leg.beforeEnd.cached == Qt::ControlModifier);
	const auto text = QpaText(leg);
	Check(
		delivered,
		u"keyboard modifiers self-test: control: a Ctrl left press and "
		u"release through one QpaMouseSequence reach the recorder, whose "
		u"press reads ControlModifier in the event and in Qt's cached "
		u"state, and Qt still caches ControlModifier right before end()"_q,
		text);
	Check(
		delivered && Released(leg.after) && leg.end.clean(),
		u"keyboard modifiers self-test: QpaMouseSequence::end() leaves "
		u"Qt's cached modifiers at NoModifier and every base::Is*Pressed() "
		u"false right on return, and ends clean"_q,
		text);
}

} // namespace

void AppendKeyboardModifiersSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<State>();
	const auto gate = [=] {
		return kPrivateApi ? state->gate : UnsupportedReason();
	};

	runner->add({
		.name = u"keyboard modifiers self-test: fixture - a focusable "
			u"recorder shown and raised on the primary window"_q,
		.skipReason = [] {
			return kPrivateApi ? QString() : UnsupportedReason();
		},
		.run = [=] {
			state->buildRefusal = BuildFixture(*state);
		},
		.until = [=] {
			return !state->buildRefusal.isEmpty() || RecorderShown(*state);
		},
		.then = [=] {
			FinishFixture(*state);
		},
		.timeoutDetails = [=] {
			return FixtureText(*state);
		},
	});

	runner->addGated(gate, [&] {
		runner->add({
			.name = u"keyboard modifiers self-test: PressKey chords and "
				u"lone modifier keys beside raw sent pairs"_q,
			.run = [=] {
				RunPressLegs(*state);
			},
			.then = [=] {
				CheckPressLegs(*state);
			},
		});
		runner->add({
			.name = u"keyboard modifiers self-test: a Ctrl+B widget "
				u"shortcut on the focused recorder"_q,
			.run = [=] {
				RunShortcutLeg(*state);
			},
			.then = [=] {
				CheckShortcutLeg(*state);
			},
		});
		runner->add({
			.name = u"keyboard modifiers self-test: a Ctrl QPA click and "
				u"QpaMouseSequence::end()"_q,
			.run = [=] {
				RunQpaLeg(*state);
			},
			.then = [=] {
				CheckQpaLeg(*state);
			},
		});
	});

	runner->addRecovery({
		.name = u"keyboard modifiers self-test: teardown - destroy the "
			u"recorder"_q,
		.run = [=] {
			const auto had = state->container ? 1 : 0;
			DestroyFixture(*state);
			Note(u"KEYBOARD_MODIFIERS: teardown container=%1->%2 "
				u"cached=%3"_q.arg(
					QString::number(had),
					QString::number(state->container ? 1 : 0),
					ModifiersText(QGuiApplication::keyboardModifiers())));
		},
	});

	runner->onFinish([=] {
		DestroyFixture(*state);
	});
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
