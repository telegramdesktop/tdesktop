/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

#include <QtCore/QString>

namespace Ui {
class InputField;
enum class InputSubmitSettings;
} // namespace Ui

namespace Test {

class Runner;

// Submits a Ui::InputField with the key and modifiers its submit setting
// requires, and reports whether the field's submits() fired.
//
// The setting followed: Core::App().settings().sendSubmitWay() - the
// compose fields' setting, application-wide - or the one the caller names
// for a field that sets its own; Ui::InputField has no getter for it.
// Enter and Both press Return with no modifier, CtrlEnter Return with
// Qt::ControlModifier (Ui::InputField reads Meta as Control as well). The
// helper never writes sendSubmitWay() or the field's settings.
//
// The key route: one Test::PressKey pair to field->rawTextEdit(), whose
// keyPressEvent hands the key to InputField::keyPressEventInner, where
// submits() fires; the wrapper does not take keys. The field need not be
// the focus widget, and its window need not be active or on screen;
// product handlers that depend on focus remain the caller's to establish.
// A single-line field submits on any Return.
//
// The report. |submits| counts what a subscription made right before the
// press observed; submitted() is true for an observed emission, even when
// the field died afterwards. A live field with none reads not submitted -
// its own setting differs, like an inline bot's None, or a handler took the
// key - never as sent. Refusals:
// - no-submit-key: the setting is None, which names no submit key; nothing
//   was sent. Ctrl+Shift+Return, which InputField::ShouldSubmit() accepts
//   even under None, is never pressed in its place;
// - target-destroyed: no emission was observed and the field died during
//   the press. An earlier submits() handler that destroys the field ends
//   the stream before this observer runs, so the key may have submitted:
//   the outcome is unobserved, neither a missed submit nor grounds for a
//   retry.
// The field is not touched after the press. On return Qt's cached keyboard
// modifiers read Qt::NoModifier, from PressKey; built with
// DESKTOP_APP_DISABLE_PRIVATE_QT_API that reset does not exist and a
// CtrlEnter submit leaves Control cached. The helper neither Notes nor
// Fails, and the details never print the field's text. Reference:
// docs/helpers/test_field_submit.md.
enum class FieldSubmitRefusal {
	None,
	NoSubmitKey,
	TargetDestroyed,
};

struct FieldSubmit {
	Ui::InputSubmitSettings setting = {};
	int submits = 0;
	FieldSubmitRefusal refusal = FieldSubmitRefusal::None;

	[[nodiscard]] bool submitted() const {
		return (submits > 0);
	}
};

// none, no-submit-key, target-destroyed.
[[nodiscard]] QString FieldSubmitRefusalName(FieldSubmitRefusal refusal);

// "field submit: outcome=<submitted|not-submitted|refused|unobserved>
// setting=<Enter|CtrlEnter|Both|None> submits=<n> refusal=<name>".
[[nodiscard]] QString FieldSubmitDetails(const FieldSubmit &reading);

FieldSubmit SubmitField(not_null<Ui::InputField*> field);
FieldSubmit SubmitField(
	not_null<Ui::InputField*> field,
	Ui::InputSubmitSettings setting);

// The helper measuring itself on a purpose-built multi-line
// Ui::InputField in a top-level window it creates with
// Qt::WA_DontShowOnScreen and never focuses, with no account, chats list or
// network, holding typed text, beside an independent submits() count and a
// raw-editor key log. Under Enter, Both and CtrlEnter the helper submits
// once with the text unchanged and Qt's cached modifiers released; a plain
// PressKey(Return) right after the CtrlEnter submit inserts a new line and
// does not submit; on a field set to None the caller's CtrlEnter reads not
// submitted, and None is refused by name with no key; the default overload
// follows the app setting, which no call changes; a field that an earlier
// submits() handler destroys reads target-destroyed. Built with
// DESKTOP_APP_DISABLE_PRIVATE_QT_API every stage is N/A. Reference:
// docs/helpers/test_field_submit.md.
void AppendFieldSubmitSelfTest(not_null<Runner*> runner);

} // namespace Test
