/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class GenericBox;
class VerticalLayout;
} // namespace Ui

class PeerData;

void UsernamesBox(
	not_null<Ui::GenericBox*> box,
	not_null<PeerData*> peer);

struct UsernameCheckInfo final {
	[[nodiscard]] static UsernameCheckInfo PurchaseAvailable(
		const QString &username,
		not_null<PeerData*> peer);

	enum class Type {
		Good,
		Error,
		Default,
	};
	Type type;
	TextWithEntities text;
};

void AddUsernameCheckLabel(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<UsernameCheckInfo> checkInfo);
