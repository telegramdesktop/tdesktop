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

// A stack-built QWheelEvent delivered with QApplication::sendEvent is not
// spontaneous, and Qt 6's QApplication::notify therefore skips the parent
// ladder every other input class gets (qapplication.cpp, case QEvent::Wheel:
// if (!wheel->spontaneous()) { notify_helper; break; }). A wheel aimed at a
// covering Ui::AbstractButton reached QWidget::wheelEvent, whose body is
// event->ignore(), and died there. The real path does the opposite:
// QWidgetWindow::handleWheelEvent picks childAt and QGuiApplication::forwardEvent
// preserves spontaneity, so the same pixel climbs to the scroll area.
// A second trap sits behind the first: QAbstractScrollArea::event returns
// false for Wheel without ignoring, so a sendEvent to the Ui::ScrollArea
// itself comes back isAccepted() == true while scrolling nothing.
//
// AppendWheelSelfTest is that instrument measuring itself. It builds a
// shown Ui::ScrollArea with tall content and a Ui::AbstractButton covering
// the sampled point, plus a parentless window holding one button that has
// no scrolling ancestor. The subject wheel is aimed at the covering button;
// the control is the same wheel aimed at the viewport, which already worked
// today; the inert wheel is aimed at the ScrollArea itself; the refusal
// wheel is aimed at the parentless button. Every geometry number comes from
// the live scaled style token. It needs no session, no chats list, no
// network and no account fixture — only a primary window to parent the
// scrolling fixture to, and a missing one is a named fixture gate instead
// of a crash. It emits no deliberate failure. Its before-leg, the
// unchanged-position reading this repair removes, is produced by reverting
// the parent walk in Test::Wheel and re-running the identical scenario,
// never by a stage that fails on purpose.
void AppendWheelSelfTest(not_null<Runner*> runner);

} // namespace Test
