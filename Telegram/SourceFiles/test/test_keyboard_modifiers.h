/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace Test {

class Runner;

// Test::PressKey (test_widgets.h) and QpaMouseSequence::end()
// (test_qpa_mouse.h) leave Qt's cached keyboard modifiers,
// QGuiApplication::keyboardModifiers(), released, which
// base::IsCtrlPressed(), IsShiftPressed() and IsAltPressed() compare
// exactly; this is that guarantee measuring itself. A focusable recorder,
// shown and raised on the primary window, reads each key and mouse
// event's modifiers and the cached state in its handlers. For Control,
// Shift and Alt, a raw sent Key_B pair is the control that leaves the
// modifier cached; PressKey(Key_B, M) and PressKey(<M's key>, NoModifier)
// must deliver M during the press and leave Qt::NoModifier, with every
// base::Is*Pressed() false, right on return. A Ctrl+B Qt::WidgetShortcut
// on the recorder, focused in the window ForceWindowActive activates,
// must fire once with Control cached and consume the press; a Ctrl left
// click through one QpaMouseSequence must keep Control cached until end().
//
// Needs no account, session or network: only a primary window, whose
// absence fails the fixture gate and leaves every later stage N/A. Built
// with DESKTOP_APP_DISABLE_PRIVATE_QT_API every stage is N/A. It emits no
// deliberate failure, and destroys its recorder in a recovery stage and on
// finish. Reference: docs/helpers/test_keyboard_modifiers.md.
void AppendKeyboardModifiersSelfTest(not_null<Runner*> runner);

} // namespace Test
