/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class RpWidget;
class Show;
} // namespace Ui

class HistoryItem;

namespace Menu {

void ShowSponsored(
	not_null<Ui::RpWidget*> parent,
	std::shared_ptr<Ui::Show> show,
	not_null<HistoryItem*> item);

} // namespace Menu
