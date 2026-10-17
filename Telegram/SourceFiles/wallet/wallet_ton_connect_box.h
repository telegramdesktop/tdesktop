/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/image/image_location.h"

namespace anim {
enum class type : uchar;
} // namespace anim

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Ui {
class GenericBox;
class RoundButton;
class VerticalLayout;
} // namespace Ui

namespace Wallet {

enum class TonConnectBoxPhase : uchar {
	Loading,
	Confirm,
	Connecting,
	Restore,
	Notice,
};

struct TonConnectBoxState {
	TonConnectBoxPhase phase = TonConnectBoxPhase::Loading;
	QString name;
	QString domain;
	WebFileLocation icon;
	bool proof = false;
	bool busy = false;
	QString notice;
	QString error;

	friend bool operator==(
		const TonConnectBoxState &,
		const TonConnectBoxState &) = default;
};

struct TonConnectBoxArgs {
	std::shared_ptr<Main::SessionShow> show;
	rpl::producer<TonConnectBoxState> state;
	Fn<void()> connect;
	Fn<void()> restore;
	Fn<void()> dismissed;
};

struct TonConnectHeaderState {
	QString title;
	QString domain;
	WebFileLocation icon;
	bool loading = false;
};

[[nodiscard]] auto AddTonConnectHeader(
	not_null<Ui::VerticalLayout*> container,
	not_null<Main::Session*> session)
-> Fn<void(const TonConnectHeaderState &, anim::type)>;

struct TonConnectButtons {
	Ui::RoundButton *secondary = nullptr;
	Ui::RoundButton *primary = nullptr;
};

[[nodiscard]] TonConnectButtons SetTonConnectButtons(
	not_null<Ui::GenericBox*> box,
	rpl::producer<QString> secondary,
	rpl::producer<QString> primary);

void TonConnectBox(not_null<Ui::GenericBox*> box, TonConnectBoxArgs args);

void TonConnectAppsBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show);

} // namespace Wallet
