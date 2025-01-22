/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"

namespace Api {
struct GiftCode;
} // namespace Api

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Data {
struct Boost;
struct CreditsHistoryEntry;
struct GiveawayStart;
struct GiveawayResults;
struct SubscriptionEntry;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Settings {
struct CreditsEntryBoxStyleOverrides;
} // namespace Settings

namespace Ui {
class GenericBox;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionNavigation;
} // namespace Window

[[nodiscard]] rpl::producer<QString> GiftDurationValue(int months);
[[nodiscard]] QString GiftDuration(int months);

void GiftCodeBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionNavigation*> controller,
	const QString &slug);
void GiftCodePendingBox(
	not_null<Ui::GenericBox*> box,
	not_null<Window::SessionNavigation*> controller,
	const Api::GiftCode &data);
void ResolveGiftCode(
	not_null<Window::SessionNavigation*> controller,
	const QString &slug,
	PeerId fromId = 0,
	PeerId toId = 0);

void ResolveGiveawayInfo(
	not_null<Window::SessionNavigation*> controller,
	not_null<PeerData*> peer,
	MsgId messageId,
	std::optional<Data::GiveawayStart> start,
	std::optional<Data::GiveawayResults> results);

[[nodiscard]] QString TonAddressUrl(
	not_null<Main::Session*> session,
	const QString &address);

void AddStarGiftTable(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Ui::VerticalLayout*> container,
	Settings::CreditsEntryBoxStyleOverrides st,
	const Data::CreditsHistoryEntry &entry,
	Fn<void()> convertToStars,
	Fn<void()> startUpgrade);
void AddCreditsHistoryEntryTable(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Ui::VerticalLayout*> container,
	Settings::CreditsEntryBoxStyleOverrides st,
	const Data::CreditsHistoryEntry &entry);

void AddSubscriptionEntryTable(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Ui::VerticalLayout*> container,
	Settings::CreditsEntryBoxStyleOverrides st,
	const Data::SubscriptionEntry &s);
void AddSubscriberEntryTable(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Ui::VerticalLayout*> container,
	Settings::CreditsEntryBoxStyleOverrides st,
	not_null<PeerData*> peer,
	TimeId date);

void AddCreditsBoostTable(
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<Ui::VerticalLayout*> container,
	Settings::CreditsEntryBoxStyleOverrides st,
	const Data::Boost &boost);
