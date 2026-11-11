/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class ChannelData;

namespace Ui {
class BoxContent;
class RippleButton;
class Show;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Api {

void RestrictSponsored(
	not_null<ChannelData*> channel,
	bool restricted,
	Fn<void(QString)> failed);

struct RewardReceiver final {
	PeerData *currencyReceiver = nullptr;
	PeerData *creditsReceiver = nullptr;
	Fn<uint64()> creditsAmount;
};

void HandleWithdrawalButton(
	RewardReceiver receiver,
	not_null<Ui::RippleButton*> button,
	std::shared_ptr<Ui::Show> show);

void HandleWalletWithdrawalButton(
	not_null<Window::SessionController*> window,
	not_null<Ui::RippleButton*> button,
	Fn<CreditsAmount()> available,
	Fn<void()> withdrawn);

struct WalletWithdrawal final {
	Fn<void()> withdrawn;
	Fn<void()> disabled;
	Fn<void()> failed;
	Fn<void()> dismissed;
};

void WithdrawEarningsToWallet(
	not_null<Window::SessionController*> window,
	not_null<Ui::BoxContent*> confirmation,
	int64 nanos,
	WalletWithdrawal withdrawal);

} // namespace Api
