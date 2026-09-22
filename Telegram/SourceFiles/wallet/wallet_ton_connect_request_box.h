/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Wallet {

struct TonConnectTransfer;

enum class TonConnectRequestPhase : uchar {
	Loading,
	Locked,
	Confirm,
	Notice,
};

struct TonConnectRequestBoxState {
	TonConnectRequestPhase phase = TonConnectRequestPhase::Loading;
	QString name;
	QString domain;
	QString iconUrl;
	QString topic;
	std::shared_ptr<const TonConnectTransfer> transfer;
	std::optional<int64> feeNano;
	QString notice;
	QString error;
	bool feeLoading = false;
	bool confirmable = false;
	bool busy = false;
	bool declining = false;

	friend bool operator==(
		const TonConnectRequestBoxState &,
		const TonConnectRequestBoxState &) = default;
};

struct TonConnectRequestBoxArgs {
	not_null<Main::Session*> session;
	rpl::producer<TonConnectRequestBoxState> state;
	Fn<void()> unlock;
	Fn<void()> confirm;
	Fn<void()> decline;
	Fn<void()> dismissed;
};

void TonConnectRequestBox(
	not_null<Ui::GenericBox*> box,
	TonConnectRequestBoxArgs args);

} // namespace Wallet
