/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Wallet {

enum class TonConnectBoxPhase : uchar {
	Loading,
	Confirm,
	Connecting,
	Notice,
};

struct TonConnectBoxState {
	TonConnectBoxPhase phase = TonConnectBoxPhase::Loading;
	QString name;
	QString domain;
	QString address;
	bool proof = false;
	QString notice;
	QString error;

	friend bool operator==(
		const TonConnectBoxState &,
		const TonConnectBoxState &) = default;
};

struct TonConnectBoxArgs {
	rpl::producer<TonConnectBoxState> state;
	Fn<void()> connect;
	Fn<void()> dismissed;
};

void TonConnectBox(not_null<Ui::GenericBox*> box, TonConnectBoxArgs args);

} // namespace Wallet
