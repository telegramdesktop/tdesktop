/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#if defined _DEBUG || defined TDESKTOP_RELEASE_TEST_HARNESS

#include "test/test_field_submit.h"

#include "base/event_filter.h"
#include "base/unique_qptr.h"
#include "base/weak_qptr.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "test/test_capture.h"
#include "test/test_log.h"
#include "test/test_runner.h"
#include "test/test_widgets.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"

#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QTextEdit>

#include <optional>
#include <vector>

#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Test {
namespace {

using Setting = Ui::InputSubmitSettings;

// What the self-test types: letters and spaces, which no instant replace
// or markdown rule rewrites.
const auto kTyped = u"harness field submit probe"_q;

#ifdef DESKTOP_APP_DISABLE_PRIVATE_QT_API
constexpr auto kPrivateApi = false;
#else // DESKTOP_APP_DISABLE_PRIVATE_QT_API
constexpr auto kPrivateApi = true;
#endif // !DESKTOP_APP_DISABLE_PRIVATE_QT_API

// The modifiers Return carries to submit under |setting|, none for None.
[[nodiscard]] std::optional<Qt::KeyboardModifiers> SubmitModifiers(
		Setting setting) {
	switch (setting) {
	case Setting::Enter:
	case Setting::Both: return Qt::KeyboardModifiers(Qt::NoModifier);
	case Setting::CtrlEnter:
		return Qt::KeyboardModifiers(Qt::ControlModifier);
	case Setting::None: return std::nullopt;
	}
	Unexpected("Setting in Test::SubmitField.");
}

[[nodiscard]] QString SettingName(Setting setting) {
	switch (setting) {
	case Setting::Enter: return u"Enter"_q;
	case Setting::CtrlEnter: return u"CtrlEnter"_q;
	case Setting::Both: return u"Both"_q;
	case Setting::None: return u"None"_q;
	}
	return QString::number(int(setting));
}

[[nodiscard]] QString OutcomeName(const FieldSubmit &reading) {
	switch (reading.refusal) {
	case FieldSubmitRefusal::NoSubmitKey: return u"refused"_q;
	case FieldSubmitRefusal::TargetDestroyed: return u"unobserved"_q;
	case FieldSubmitRefusal::None: break;
	}
	return reading.submitted() ? u"submitted"_q : u"not-submitted"_q;
}

} // namespace

QString FieldSubmitRefusalName(FieldSubmitRefusal refusal) {
	switch (refusal) {
	case FieldSubmitRefusal::None: return u"none"_q;
	case FieldSubmitRefusal::NoSubmitKey: return u"no-submit-key"_q;
	case FieldSubmitRefusal::TargetDestroyed: return u"target-destroyed"_q;
	}
	Unexpected("Refusal in Test::FieldSubmitRefusalName.");
}

QString FieldSubmitDetails(const FieldSubmit &reading) {
	return u"field submit: outcome=%1 setting=%2 submits=%3 refusal=%4"_q.arg(
		OutcomeName(reading),
		SettingName(reading.setting),
		QString::number(reading.submits),
		FieldSubmitRefusalName(reading.refusal));
}

FieldSubmit SubmitField(not_null<Ui::InputField*> field) {
	return SubmitField(field, Core::App().settings().sendSubmitWay());
}

FieldSubmit SubmitField(
		not_null<Ui::InputField*> field,
		Ui::InputSubmitSettings setting) {
	auto result = FieldSubmit{ .setting = setting };
	const auto modifiers = SubmitModifiers(setting);
	if (!modifiers) {
		result.refusal = FieldSubmitRefusal::NoSubmitKey;
		return result;
	}
	const auto alive = base::make_weak(field.get());
	auto observed = rpl::lifetime();
	field->submits() | rpl::on_next([&](Qt::KeyboardModifiers) {
		++result.submits;
	}, observed);
	PressKey(field->rawTextEdit(), Qt::Key_Return, *modifiers);
	observed.destroy();
	if (!result.submits && !alive.get()) {
		result.refusal = FieldSubmitRefusal::TargetDestroyed;
	}
	return result;
}

namespace {

// A key event as the raw editor's object filter saw it, before the field
// handled it.
struct KeyRecord {
	QEvent::Type type = QEvent::None;
	int key = 0;
	Qt::KeyboardModifiers modifiers = Qt::NoModifier;
};

// One call, measured. |submits| and |keys| are the fixture's own submits()
// count and raw-editor key log during it; |before| and |after| the field's
// text around it, never printed; |cached| Qt's cached keyboard modifiers
// right on its return, before anything else; |appBefore| and |appAfter|
// the app setting around it; |focused| and |active| the field's focus and
// its window's activation right before it. |helper| is false for the
// plain PressKey control, whose |result| stays empty.
struct SubmitLeg {
	QString name;
	QString call;
	FieldSubmit result;
	std::vector<KeyRecord> keys;
	QString before;
	QString after;
	Qt::KeyboardModifiers cached = Qt::NoModifier;
	Setting appBefore = Setting::Enter;
	Setting appAfter = Setting::Enter;
	int submits = 0;
	bool helper = true;
	bool focused = false;
	bool active = false;
	bool alive = false;
	bool measured = false;
};

// The stages and the finish callback share it. |gate| stays a refusal
// until the fixture stage accepts the field. The doomed field, in its own
// window, has an earlier submits() handler that counts and destroys it.
struct State {
	base::unique_qptr<Ui::RpWidget> window;
	QPointer<Ui::InputField> field;
	base::unique_qptr<Ui::RpWidget> doomedWindow;
	QPointer<Ui::InputField> doomed;
	std::vector<KeyRecord> keys;
	std::vector<SubmitLeg> settingLegs;
	std::vector<SubmitLeg> noneLegs;
	SubmitLeg defaultLeg;
	SubmitLeg destroyedLeg;
	rpl::lifetime lifetime;
	QString typed;
	QString gate = u"fixture gate: the fixture stage has not run"_q;
	Setting appFirst = Setting::Enter;
	int submits = 0;
	int doomedHandlerRuns = 0;
};

[[nodiscard]] Setting AppSetting() {
	return Core::App().settings().sendSubmitWay();
}

[[nodiscard]] QString UnsupportedReason() {
	return u"unsupported: built with DESKTOP_APP_DISABLE_PRIVATE_QT_API, so "
		u"PressKey does not release Qt's cached keyboard modifiers and a "
		u"CtrlEnter submit leaves Control cached"_q;
}

[[nodiscard]] QString FlagText(bool value) {
	return value ? u"1"_q : u"0"_q;
}

[[nodiscard]] QString ModifiersText(Qt::KeyboardModifiers modifiers) {
	return u"0x"_q + QString::number(uint(modifiers.toInt()), 16);
}

[[nodiscard]] bool Focused(not_null<Ui::InputField*> field) {
	return field->hasFocus()
		|| (QApplication::focusWidget() == field->rawTextEdit().get());
}

// A parentless top-level with Qt::WA_DontShowOnScreen: shown, it is marked
// visible but never mapped, so it takes neither activation nor focus.
// Qt::WA_QuitOnClose is cleared, as for every fixture window.
[[nodiscard]] base::unique_qptr<Ui::RpWidget> MakeWindow(
		const QString &name) {
	auto result = base::make_unique_q<Ui::RpWidget>(nullptr);
	const auto window = result.get();
	window->setObjectName(u"field_submit_self_test_"_q + name);
	window->setAttribute(Qt::WA_DontShowOnScreen);
	window->setAttribute(Qt::WA_QuitOnClose, false);
	return result;
}

// A multi-line field, st::boxWidth wide, filling |window|, both shown.
[[nodiscard]] not_null<Ui::InputField*> MakeField(
		not_null<Ui::RpWidget*> window) {
	const auto field = Ui::CreateChild<Ui::InputField>(
		window.get(),
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		rpl::single(u"Harness"_q));
	field->resize(st::boxWidth, field->height());
	window->resize(field->size());
	field->show();
	window->show();
	Ui::SendPendingMoveResizeEvents(window);
	return field;
}

// The field with its key log and independent submits() count, then the
// typed text through Test::TypeText on its raw editor.
void BuildFixture(not_null<State*> state) {
	state->appFirst = AppSetting();
	state->window = MakeWindow(u"field"_q);
	const auto field = MakeField(state->window.get());
	state->field = field;
	base::install_event_filter(field->rawTextEdit(), [=](
			not_null<QEvent*> e) {
		const auto type = e->type();
		if ((type == QEvent::KeyPress) || (type == QEvent::KeyRelease)) {
			const auto key = static_cast<QKeyEvent*>(e.get());
			state->keys.push_back({
				.type = type,
				.key = key->key(),
				.modifiers = key->modifiers(),
			});
		}
		return base::EventFilterResult::Continue;
	});
	field->submits() | rpl::on_next([=](Qt::KeyboardModifiers) {
		++state->submits;
	}, state->lifetime);
	TypeText(field->rawTextEdit(), kTyped);
	if (const auto alive = state->field.data()) {
		state->typed = alive->getLastText();
	}
}

[[nodiscard]] QString FixtureText(const State &state) {
	const auto window = state.window.get();
	const auto field = state.field.data();
	return u"FIELD_SUBMIT: fixture qt=%1 privateApi=%2 field=%3 window=%4 "
		u"shown=%5 dontShowOnScreen=%6 focused=%7 active=%8 typedLength=%9 "
		u"expectedLength=%10 typedSame=%11 submits=%12 app=%13 "
		u"cached=%14"_q.arg(
			QString::fromLatin1(qVersion()),
			FlagText(kPrivateApi),
			field ? WidgetDescription(field) : u"none"_q,
			window ? WidgetDescription(window) : u"none"_q,
			FlagText(window && window->isVisible()),
			FlagText(window
				&& window->testAttribute(Qt::WA_DontShowOnScreen)),
			FlagText(field && Focused(field)),
			FlagText(window && window->isActiveWindow()),
			QString::number(state.typed.size()),
			QString::number(kTyped.size()),
			FlagText(state.typed == kTyped),
			QString::number(state.submits),
			SettingName(state.appFirst),
			ModifiersText(QGuiApplication::keyboardModifiers()));
}

// Empty when the legs can start: the field holds exactly the typed text,
// typing did not submit, and neither the field has focus nor its window
// is active, so the legs measure the no-focus route.
[[nodiscard]] QString FixtureRefusal(const State &state) {
	const auto window = state.window.get();
	const auto field = state.field.data();
	if (!window || !field) {
		return u"the field was not built"_q;
	} else if (state.typed != kTyped) {
		return u"the typed text did not reach the field"_q;
	} else if (state.submits != 0) {
		return u"typing submitted the field"_q;
	} else if (Focused(field) || window->isActiveWindow()) {
		return u"the field has focus or its window is active, so the "
			u"no-focus route would not be measured"_q;
	}
	return QString();
}

void FinishFixture(not_null<State*> state) {
	const auto refusal = FixtureRefusal(*state);
	state->gate = refusal.isEmpty()
		? QString()
		: (u"fixture gate: "_q + refusal);
	const auto text = FixtureText(*state);
	Check(
		refusal.isEmpty(),
		u"fixture gate: the field submit self-test's multi-line field holds "
		u"the typed text in a top-level shown with Qt::WA_DontShowOnScreen, "
		u"with neither focus nor an active window"_q,
		refusal.isEmpty() ? text : (text + u" - "_q + refusal));
	state->keys.clear();
	state->submits = 0;
}

void DestroyFixture(State &state) {
	state.lifetime.destroy();
	state.field = nullptr;
	state.doomed = nullptr;
	state.doomedWindow = nullptr;
	state.window = nullptr;
}

// Clears the fixture's count and key log, runs |action| on the live field
// and reads Qt's cached modifiers right on its return, then the rest.
[[nodiscard]] SubmitLeg Measure(
		State &state,
		SubmitLeg leg,
		Fn<void(not_null<Ui::InputField*>, SubmitLeg&)> action) {
	const auto field = state.field.data();
	if (!field) {
		return leg;
	}
	state.keys.clear();
	state.submits = 0;
	leg.before = field->getLastText();
	leg.focused = Focused(field);
	leg.active = field->window()->isActiveWindow();
	leg.appBefore = AppSetting();
	action(field, leg);
	leg.cached = QGuiApplication::keyboardModifiers();
	leg.appAfter = AppSetting();
	leg.keys = state.keys;
	leg.submits = state.submits;
	if (const auto alive = state.field.data()) {
		leg.after = alive->getLastText();
		leg.alive = true;
	}
	leg.measured = true;
	return leg;
}

[[nodiscard]] SubmitLeg HelperLeg(
		State &state,
		const QString &name,
		Setting setting) {
	return Measure(state, {
		.name = name,
		.call = u"SubmitField(field, %1)"_q.arg(SettingName(setting)),
	}, [&](not_null<Ui::InputField*> field, SubmitLeg &leg) {
		leg.result = SubmitField(field, setting);
	});
}

// Puts the typed text back, with the cursor at its end, when a leg changed
// it; the next leg's |before| confirms it.
void RestoreText(State &state) {
	const auto field = state.field.data();
	if (field && (field->getLastText() != state.typed)) {
		Settle([&] {
			field->setText(state.typed);
			field->setCursorPosition(state.typed.size());
		});
	}
}

// One turn. The field's setting is the fixture's own, set before each
// helper leg; the plain Return follows the CtrlEnter submit with no
// setting written in between.
void RunSettingLegs(State &state) {
	state.settingLegs.clear();
	const auto settings = {
		Setting::Enter,
		Setting::Both,
		Setting::CtrlEnter,
	};
	for (const auto setting : settings) {
		if (const auto field = state.field.data()) {
			field->setSubmitSettings(setting);
		}
		state.settingLegs.push_back(
			HelperLeg(state, SettingName(setting), setting));
		if (setting == Setting::CtrlEnter) {
			state.settingLegs.push_back(Measure(state, {
				.name = u"plain-return"_q,
				.call = u"PressKey(rawTextEdit, Key_Return)"_q,
				.helper = false,
			}, [](not_null<Ui::InputField*> field, SubmitLeg &) {
				PressKey(field->rawTextEdit(), Qt::Key_Return);
			}));
		}
		RestoreText(state);
	}
}

// One turn: the field set to None, the caller's CtrlEnter, then None.
void RunNoneLegs(State &state) {
	state.noneLegs.clear();
	if (const auto field = state.field.data()) {
		field->setSubmitSettings(Setting::None);
	}
	state.noneLegs.push_back(
		HelperLeg(state, u"mismatch"_q, Setting::CtrlEnter));
	RestoreText(state);
	state.noneLegs.push_back(HelperLeg(state, u"none"_q, Setting::None));
	RestoreText(state);
}

// One turn: the field set to the app setting, read and never written,
// then the default overload.
void RunDefaultLeg(State &state) {
	if (const auto field = state.field.data()) {
		field->setSubmitSettings(AppSetting());
	}
	state.defaultLeg = Measure(state, {
		.name = u"default"_q,
		.call = u"SubmitField(field)"_q,
	}, [](not_null<Ui::InputField*> field, SubmitLeg &leg) {
		leg.result = SubmitField(field);
	});
	RestoreText(state);
}

// One turn: a separate field whose earlier submits() handler counts and
// destroys it together with its window, then the helper on it.
void RunDestroyedLeg(State &state) {
	state.doomedWindow = MakeWindow(u"doomed"_q);
	const auto doomed = MakeField(state.doomedWindow.get());
	state.doomed = doomed;
	const auto raw = &state;
	doomed->submits() | rpl::on_next([=](Qt::KeyboardModifiers) {
		++raw->doomedHandlerRuns;
		raw->doomedWindow = nullptr;
	}, state.lifetime);
	auto &leg = state.destroyedLeg;
	leg.name = u"destroyed"_q;
	leg.call = u"SubmitField(doomed, Enter)"_q;
	leg.appBefore = AppSetting();
	leg.result = SubmitField(doomed, Setting::Enter);
	leg.cached = QGuiApplication::keyboardModifiers();
	leg.appAfter = AppSetting();
	leg.alive = (state.doomed.data() != nullptr);
	leg.measured = true;
}

[[nodiscard]] QString KeyTypeName(QEvent::Type type) {
	switch (type) {
	case QEvent::KeyPress: return u"KeyPress"_q;
	case QEvent::KeyRelease: return u"KeyRelease"_q;
	default: break;
	}
	return QString::number(int(type));
}

[[nodiscard]] QString KeysText(const std::vector<KeyRecord> &keys) {
	auto parts = QStringList();
	for (const auto &key : keys) {
		parts.push_back(u"%1 key=0x%2 modifiers=%3"_q.arg(
			KeyTypeName(key.type),
			QString::number(key.key, 16),
			ModifiersText(key.modifiers)));
	}
	return parts.join(u"; "_q);
}

// "FIELD_SUBMIT: leg=<name> call=<call> measured=<0|1> alive=<0|1>
// focused=<0|1> active=<0|1> app=<before>-><after> fixtureSubmits=<n>
// keys=[...] length=<before>-><after> newLines=<before>-><after>
// typedBefore=<0|1> unchanged=<0|1> cached=<hex> result=[<details>]".
[[nodiscard]] QString LegText(const State &state, const SubmitLeg &leg) {
	return u"FIELD_SUBMIT: leg=%1 call=%2 measured=%3 alive=%4 focused=%5 "
		u"active=%6 app=%7->%8 fixtureSubmits=%9 keys=[%10] length=%11->%12 "
		u"newLines=%13->%14 typedBefore=%15 unchanged=%16 cached=%17 "
		u"result=[%18]"_q.arg(
			leg.name,
			leg.call,
			FlagText(leg.measured),
			FlagText(leg.alive),
			FlagText(leg.focused),
			FlagText(leg.active),
			SettingName(leg.appBefore),
			SettingName(leg.appAfter),
			QString::number(leg.submits),
			KeysText(leg.keys),
			QString::number(leg.before.size()),
			QString::number(leg.after.size()),
			QString::number(leg.before.count(u'\n')),
			QString::number(leg.after.count(u'\n')),
			FlagText(leg.before == state.typed),
			FlagText(leg.after == leg.before),
			ModifiersText(leg.cached),
			(leg.helper ? FieldSubmitDetails(leg.result) : u"none"_q));
}

// Exactly one press and one release of Return, both carrying |modifiers|.
[[nodiscard]] bool ReturnPair(
		const std::vector<KeyRecord> &keys,
		Qt::KeyboardModifiers modifiers) {
	return (keys.size() == 2)
		&& (keys[0].type == QEvent::KeyPress)
		&& (keys[0].key == Qt::Key_Return)
		&& (keys[0].modifiers == modifiers)
		&& (keys[1].type == QEvent::KeyRelease)
		&& (keys[1].key == Qt::Key_Return)
		&& (keys[1].modifiers == modifiers);
}

[[nodiscard]] bool AddsOneNewLine(
		const QString &before,
		const QString &after) {
	auto was = before;
	auto now = after;
	return (after.size() == before.size() + 1)
		&& (after.count(u'\n') == before.count(u'\n') + 1)
		&& (was.remove(u'\n') == now.remove(u'\n'));
}

// A live, measured leg on the unfocused fixture field, starting from the
// typed text, with the app setting unchanged across it.
[[nodiscard]] bool Prepared(const State &state, const SubmitLeg &leg) {
	return leg.measured
		&& leg.alive
		&& !leg.focused
		&& !leg.active
		&& (leg.before == state.typed)
		&& (leg.appAfter == leg.appBefore)
		&& (leg.appBefore == state.appFirst);
}

// The helper submitted |setting| once, by its own and the fixture's count,
// with the chord the setting names and the text unchanged.
[[nodiscard]] bool Submitted(const SubmitLeg &leg, Setting setting) {
	const auto modifiers = SubmitModifiers(setting);
	return modifiers
		&& (leg.result.setting == setting)
		&& (leg.result.refusal == FieldSubmitRefusal::None)
		&& (leg.result.submits == 1)
		&& leg.result.submitted()
		&& (leg.submits == 1)
		&& ReturnPair(leg.keys, *modifiers)
		&& (leg.after == leg.before);
}

void CheckReleased(const State &state, const SubmitLeg &leg) {
	Check(
		leg.measured && (leg.cached == Qt::NoModifier),
		u"field submit self-test: %1: Qt's cached keyboard modifiers read "
		u"NoModifier right after %2"_q.arg(leg.name, leg.call),
		LegText(state, leg));
}

void CheckSettingLegs(const State &state) {
	for (const auto &leg : state.settingLegs) {
		if (!leg.helper) {
			Check(
				leg.measured
					&& leg.alive
					&& (leg.submits == 0)
					&& ReturnPair(leg.keys, Qt::NoModifier)
					&& AddsOneNewLine(leg.before, leg.after),
				u"field submit self-test: control: right after the "
				u"CtrlEnter submit, with no setting written, a plain "
				u"PressKey(Return) on the same raw editor fires no "
				u"submits() and inserts one new line"_q,
				LegText(state, leg));
			continue;
		}
		const auto setting = leg.result.setting;
		Check(
			Prepared(state, leg) && Submitted(leg, setting),
			u"field submit self-test: %1: SubmitField under %1 presses its "
			u"Return chord on the raw editor of the unfocused field, "
			u"submits() fires exactly once by the helper's and the "
			u"fixture's count, the text is unchanged with no new line, and "
			u"the report reads submitted"_q.arg(leg.name),
			LegText(state, leg));
		CheckReleased(state, leg);
	}
}

void CheckNoneLegs(const State &state) {
	for (const auto &leg : state.noneLegs) {
		const auto details = FieldSubmitDetails(leg.result);
		if (leg.result.setting == Setting::CtrlEnter) {
			Check(
				Prepared(state, leg)
					&& (leg.result.refusal == FieldSubmitRefusal::None)
					&& (leg.result.submits == 0)
					&& !leg.result.submitted()
					&& (leg.submits == 0)
					&& ReturnPair(leg.keys, Qt::ControlModifier)
					&& AddsOneNewLine(leg.before, leg.after)
					&& details.contains(u"outcome=not-submitted"_q),
				u"field submit self-test: mismatch: on a field set to "
				u"None, SubmitField(CtrlEnter) presses Ctrl+Return, the "
				u"field inserts a new line and fires no submits(), and the "
				u"live field reads not-submitted with no refusal, never "
				u"as sent"_q,
				LegText(state, leg));
			CheckReleased(state, leg);
		} else {
			Check(
				Prepared(state, leg)
					&& (leg.result.setting == Setting::None)
					&& (leg.result.refusal
						== FieldSubmitRefusal::NoSubmitKey)
					&& (FieldSubmitRefusalName(leg.result.refusal)
						== u"no-submit-key"_q)
					&& (leg.result.submits == 0)
					&& (leg.submits == 0)
					&& leg.keys.empty()
					&& (leg.after == leg.before)
					&& details.contains(u"outcome=refused"_q),
				u"field submit self-test: refusal: SubmitField(None) "
				u"refuses by name as no-submit-key, sends no key and "
				u"leaves the text unchanged"_q,
				LegText(state, leg));
		}
	}
}

void CheckDefaultLeg(const State &state) {
	const auto &leg = state.defaultLeg;
	const auto app = state.appFirst;
	const auto none = (app == Setting::None);
	const auto refused = (leg.result.setting == Setting::None)
		&& (leg.result.refusal == FieldSubmitRefusal::NoSubmitKey)
		&& (leg.submits == 0)
		&& leg.keys.empty()
		&& (leg.after == leg.before);
	Check(
		Prepared(state, leg)
			&& (leg.result.setting == app)
			&& (none ? refused : Submitted(leg, app)),
		u"field submit self-test: default: SubmitField(field) follows "
		u"Core::App().settings().sendSubmitWay() (%1 here), which no call "
		u"changes, and submits the field set to it once with its own "
		u"chord"_q.arg(SettingName(app)),
		LegText(state, leg));
	if (!none) {
		CheckReleased(state, leg);
	}
}

void CheckDestroyedLeg(const State &state) {
	const auto &leg = state.destroyedLeg;
	Check(
		leg.measured
			&& (state.doomedHandlerRuns == 1)
			&& !leg.alive
			&& !state.doomedWindow
			&& (leg.result.setting == Setting::Enter)
			&& (leg.result.refusal == FieldSubmitRefusal::TargetDestroyed)
			&& (FieldSubmitRefusalName(leg.result.refusal)
				== u"target-destroyed"_q)
			&& (leg.result.submits == 0)
			&& !leg.result.submitted()
			&& FieldSubmitDetails(leg.result).contains(
				u"outcome=unobserved"_q)
			&& (leg.cached == Qt::NoModifier)
			&& (leg.appAfter == leg.appBefore),
		u"field submit self-test: destroyed: when an earlier submits() "
		u"handler destroys the field, SubmitField returns safely and reads "
		u"target-destroyed with an unobserved outcome, not a missed "
		u"submit"_q,
		u"FIELD_SUBMIT: leg=%1 call=%2 measured=%3 handlerRuns=%4 alive=%5 "
		u"window=%6 cached=%7 app=%8->%9 result=[%10]"_q.arg(
			leg.name,
			leg.call,
			FlagText(leg.measured),
			QString::number(state.doomedHandlerRuns),
			FlagText(leg.alive),
			FlagText(state.doomedWindow != nullptr),
			ModifiersText(leg.cached),
			SettingName(leg.appBefore),
			SettingName(leg.appAfter),
			FieldSubmitDetails(leg.result)));
}

} // namespace

void AppendFieldSubmitSelfTest(not_null<Runner*> runner) {
	const auto state = std::make_shared<State>();
	const auto gate = [=] {
		return kPrivateApi ? state->gate : UnsupportedReason();
	};

	runner->add({
		.name = u"field submit self-test: fixture - a multi-line field "
			u"holding typed text in a top-level shown with "
			u"Qt::WA_DontShowOnScreen"_q,
		.skipReason = [] {
			return kPrivateApi ? QString() : UnsupportedReason();
		},
		.run = [=] {
			BuildFixture(state.get());
		},
		.then = [=] {
			FinishFixture(state.get());
		},
		.timeoutDetails = [=] {
			return FixtureText(*state);
		},
	});

	runner->addGated(gate, [&] {
		runner->add({
			.name = u"field submit self-test: Enter, Both and CtrlEnter, "
				u"then a plain Return under CtrlEnter"_q,
			.run = [=] {
				RunSettingLegs(*state);
			},
			.then = [=] {
				CheckSettingLegs(*state);
			},
		});
		runner->add({
			.name = u"field submit self-test: a field set to None - the "
				u"caller's CtrlEnter, then None"_q,
			.run = [=] {
				RunNoneLegs(*state);
			},
			.then = [=] {
				CheckNoneLegs(*state);
			},
		});
		runner->add({
			.name = u"field submit self-test: the default overload follows "
				u"the app setting"_q,
			.run = [=] {
				RunDefaultLeg(*state);
			},
			.then = [=] {
				CheckDefaultLeg(*state);
			},
		});
		runner->add({
			.name = u"field submit self-test: a field an earlier submits() "
				u"handler destroys"_q,
			.run = [=] {
				RunDestroyedLeg(*state);
			},
			.then = [=] {
				CheckDestroyedLeg(*state);
			},
		});
	});

	runner->addRecovery({
		.name = u"field submit self-test: teardown - destroy the fields"_q,
		.run = [=] {
			const auto had = (state->window ? 1 : 0)
				+ (state->doomedWindow ? 1 : 0);
			DestroyFixture(*state);
			Note(u"FIELD_SUBMIT: teardown windows=%1->%2 cached=%3"_q.arg(
				QString::number(had),
				QString::number((state->window ? 1 : 0)
					+ (state->doomedWindow ? 1 : 0)),
				ModifiersText(QGuiApplication::keyboardModifiers())));
		},
	});

	runner->onFinish([=] {
		DestroyFixture(*state);
	});
}

} // namespace Test

#endif // _DEBUG || TDESKTOP_RELEASE_TEST_HARNESS
