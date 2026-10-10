/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/image/image_location.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Wallet {

struct TonConnectEmulation;
struct TonConnectSignData;
struct TonConnectTransfer;

enum class TonConnectRequestPhase : uchar {
	Loading,
	Locked,
	Restore,
	Confirm,
	Notice,
	Unhandled,
};

struct TonConnectRequestBoxState {
	TonConnectRequestPhase phase = TonConnectRequestPhase::Loading;
	QString name;
	QString domain;
	WebFileLocation icon;
	QString topic;
	std::shared_ptr<const TonConnectTransfer> transfer;
	std::optional<int64> feeNano;
	std::shared_ptr<const TonConnectEmulation> emulation;
	std::shared_ptr<const TonConnectSignData> signData;
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
	Fn<void()> restore;
	Fn<void()> confirm;
	Fn<void()> decline;
	Fn<void()> dismissed;
};

void TonConnectRequestBox(
	not_null<Ui::GenericBox*> box,
	TonConnectRequestBoxArgs args);

} // namespace Wallet
