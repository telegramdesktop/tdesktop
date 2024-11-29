/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class AbstractButton;
class VerticalLayout;
} // namespace Ui

namespace Info::BotStarRef {

[[nodiscard]] not_null<Ui::AbstractButton*> AddViewListButton(
	not_null<Ui::VerticalLayout*> parent,
	rpl::producer<QString> title,
	rpl::producer<QString> subtitle);

[[nodiscard]] QString FormatStarRefCommission(ushort commission);

} // namespace Info::BotStarRef
