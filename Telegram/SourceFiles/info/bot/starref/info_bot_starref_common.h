/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "data/data_user.h"

namespace Ui {
class AbstractButton;
class RoundButton;
class VerticalLayout;
class BoxContent;
class RpWidget;
} // namespace Ui

namespace style {
struct RoundButton;
} // namespace style

namespace Info::BotStarRef {

struct ConnectedBotState {
	StarRefProgram program;
	QString link;
	TimeId date = 0;
	int users = 0;
	bool unresolved = false;
	bool revoked = false;
};
struct ConnectedBot {
	not_null<UserData*> bot;
	ConnectedBotState state;
};
using ConnectedBots = std::vector<ConnectedBot>;

[[nodiscard]] QString FormatCommission(ushort commission);
[[nodiscard]] rpl::producer<TextWithEntities> FormatProgramDuration(
	StarRefProgram program);

[[nodiscard]] not_null<Ui::AbstractButton*> AddViewListButton(
	not_null<Ui::VerticalLayout*> parent,
	rpl::producer<QString> title,
	rpl::producer<QString> subtitle);

[[nodiscard]] not_null<Ui::RoundButton*> AddFullWidthButton(
	not_null<Ui::BoxContent*> box,
	rpl::producer<QString> text,
	Fn<void()> callback = nullptr,
	const style::RoundButton *stOverride = nullptr);

void AddFullWidthButtonFooter(
	not_null<Ui::BoxContent*> box,
	not_null<Ui::RpWidget*> button,
	rpl::producer<TextWithEntities> text);

[[nodiscard]] object_ptr<Ui::BoxContent> StarRefLinkBox(
	ConnectedBot row,
	not_null<PeerData*> peer);
[[nodiscard]] object_ptr<Ui::BoxContent> JoinStarRefBox(
	ConnectedBot row,
	not_null<PeerData*> peer,
	Fn<void(ConnectedBotState)> done);

std::unique_ptr<Ui::AbstractButton> MakePeerBubbleButton(
	not_null<QWidget*> parent,
	not_null<PeerData*> peer,
	Ui::RpWidget *right = nullptr);

[[nodiscard]] ConnectedBots Parse(
	not_null<Main::Session*> session,
	const MTPpayments_ConnectedStarRefBots &bots);

} // namespace Info::BotStarRef
