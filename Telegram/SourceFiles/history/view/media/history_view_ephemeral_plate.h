/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text.h"
#include "history/view/history_view_cursor_state.h"

class Painter;

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {

class Element;
using PaintContext = Ui::ChatPaintContext;

void RefreshEphemeralPlate(
	not_null<const Element*> view,
	Ui::Text::String &text);

[[nodiscard]] int EphemeralPlateMaxWidth(const Ui::Text::String &text);

[[nodiscard]] QSize EphemeralPlateSize(
	const Ui::Text::String &text,
	int available);

void PaintEphemeralPlate(
	Painter &p,
	const PaintContext &context,
	const Ui::Text::String &text,
	int x,
	int y,
	int width,
	int outerWidth);

[[nodiscard]] bool EphemeralPlateState(
	not_null<const Element*> view,
	const Ui::Text::String &text,
	QPoint point,
	int x,
	int y,
	int width,
	int height,
	StateRequest request,
	TextState &state);

} // namespace HistoryView
