/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Payments {
struct CreditsFormData;
} // namespace Payments

namespace Ui {

class GenericBox;

void SendCreditsBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Payments::CreditsFormData> data,
	Fn<void()> sent);

} // namespace Ui
