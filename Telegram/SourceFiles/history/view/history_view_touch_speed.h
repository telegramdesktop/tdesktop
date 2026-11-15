/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
enum class TouchScrollState;
} // namespace Ui

namespace HistoryView {

void UpdateTouchSpeed(
	QPoint &speed,
	crl::time &speedTime,
	QPoint &prevPos,
	bool &prevPosValid,
	QPoint pos,
	Ui::TouchScrollState state);

} // namespace HistoryView
