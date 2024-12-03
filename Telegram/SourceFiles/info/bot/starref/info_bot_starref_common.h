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
class Show;
} // namespace Ui

namespace style {
struct RoundButton;
} // namespace style

namespace Main {
class Session;
} // namespace Main

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
[[nodiscard]] QString FormatProgramDuration(int durationMonths);
[[nodiscard]] rpl::producer<TextWithEntities> FormatForProgramDuration(
	int durationMonths);

[[nodiscard]] not_null<Ui::AbstractButton*> AddViewListButton(
	not_null<Ui::VerticalLayout*> parent,
	rpl::producer<QString> title,
	rpl::producer<QString> subtitle,
	bool newBadge = false);

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
	not_null<PeerData*> initialRecipient,
	std::vector<not_null<PeerData*>> recipients,
	Fn<void(ConnectedBotState)> done = nullptr);
[[nodiscard]] object_ptr<Ui::BoxContent> ConfirmEndBox(Fn<void()> finish);

void ResolveRecipients(
	not_null<Main::Session*> session,
	Fn<void(std::vector<not_null<PeerData*>>)> done);

std::unique_ptr<Ui::AbstractButton> MakePeerBubbleButton(
	not_null<QWidget*> parent,
	not_null<PeerData*> peer,
	Ui::RpWidget *right = nullptr,
	const style::color *bgOverride = nullptr);

void ConfirmUpdate(
	std::shared_ptr<Ui::Show> show,
	not_null<UserData*> bot,
	const StarRefProgram &program,
	bool exists,
	Fn<void(Fn<void(bool)> done)> update);
void UpdateProgram(
	std::shared_ptr<Ui::Show> show,
	not_null<UserData*> bot,
	const StarRefProgram &program,
	Fn<void(bool)> done);
void FinishProgram(
	std::shared_ptr<Ui::Show> show,
	not_null<UserData*> bot,
	Fn<void(bool)> done);

[[nodiscard]] ConnectedBots Parse(
	not_null<Main::Session*> session,
	const MTPpayments_ConnectedStarRefBots &bots);

} // namespace Info::BotStarRef
