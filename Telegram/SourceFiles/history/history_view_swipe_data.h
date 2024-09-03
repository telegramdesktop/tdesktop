/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HistoryView {

struct ChatPaintGestureHorizontalData {
	float64 ratio = 0.;
	float64 reachRatio = 0.;
	int64 msgBareId = 0;
	int translation = 0;
	int cursorTop = 0;
};

} // namespace HistoryView
