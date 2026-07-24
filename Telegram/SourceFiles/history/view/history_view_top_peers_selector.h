// This file is part of Telegram Desktop,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
//
#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Main {
class SessionShow;
} // namespace Main

namespace HistoryView {

void ShowTopPeersSelector(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Main::SessionShow> show,
	FullMsgId fullId,
	QPoint globalPos);

} // namespace HistoryView
