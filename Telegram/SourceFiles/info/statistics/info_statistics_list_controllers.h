/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Ui {
class SettingsButton;
template <typename Widget>
class SlideWrap;
class VerticalLayout;
} // namespace Ui

namespace Data {
struct Boost;
struct BoostsListSlice;
struct CreditsHistoryEntry;
struct CreditsStatusSlice;
struct PublicForwardsSlice;
struct RecentPostId;
struct SubscriptionEntry;
struct SupergroupStatistics;
} // namespace Data

namespace Main {
class SessionShow;
} // namespace Main

namespace Info::Statistics {

void AddPublicForwards(
	const Data::PublicForwardsSlice &firstSlice,
	not_null<Ui::VerticalLayout*> container,
	Fn<void(Data::RecentPostId)> requestShow,
	not_null<PeerData*> peer,
	Data::RecentPostId contextId);

void AddMembersList(
	Data::SupergroupStatistics data,
	not_null<Ui::VerticalLayout*> container,
	Fn<void(not_null<PeerData*>)> showPeerInfo,
	not_null<PeerData*> peer,
	rpl::producer<QString> title);

void AddBoostsList(
	const Data::BoostsListSlice &firstSlice,
	not_null<Ui::VerticalLayout*> container,
	Fn<void(const Data::Boost &)> boostClickedCallback,
	not_null<PeerData*> peer,
	rpl::producer<QString> title);

using Clicked = Fn<void(
	const Data::CreditsHistoryEntry &,
	const Data::SubscriptionEntry &)>;
void AddCreditsHistoryList(
	std::shared_ptr<Main::SessionShow> show,
	const Data::CreditsStatusSlice &firstSlice,
	not_null<Ui::VerticalLayout*> container,
	Clicked entryClickedCallback,
	not_null<PeerData*> peer,
	bool in,
	bool out,
	bool subscription = false);

[[nodiscard]] not_null<Ui::SlideWrap<Ui::SettingsButton>*> AddShowMoreButton(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> title);

} // namespace Info::Statistics
