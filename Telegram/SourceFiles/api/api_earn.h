/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ChannelData;

namespace Ui {
class RippleButton;
class Show;
} // namespace Ui

namespace Api {

void RestrictSponsored(
	not_null<ChannelData*> channel,
	bool restricted,
	Fn<void(QString)> failed);

void HandleWithdrawalButton(
	not_null<ChannelData*> channel,
	not_null<Ui::RippleButton*> button,
	std::shared_ptr<Ui::Show> show);

} // namespace Api
