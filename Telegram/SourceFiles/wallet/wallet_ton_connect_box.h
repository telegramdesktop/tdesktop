/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

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
	Notice,
};

struct TonConnectBoxState {
	TonConnectBoxPhase phase = TonConnectBoxPhase::Loading;
	QString name;
	QString domain;
	QString iconUrl;
	bool proof = false;
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
	Fn<void()> dismissed;
};

struct TonConnectHeaderState {
	QString title;
	QString domain;
	QString iconUrl;
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

[[nodiscard]] TonConnectButtons AddTonConnectButtons(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> secondary,
	rpl::producer<QString> primary);

void TonConnectBox(not_null<Ui::GenericBox*> box, TonConnectBoxArgs args);

void TonConnectAppsBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show);

} // namespace Wallet
