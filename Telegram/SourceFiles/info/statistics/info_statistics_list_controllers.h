/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Data {
struct Boost;
struct BoostsListSlice;
struct PublicForwardsSlice;
struct SupergroupStatistics;
} // namespace Data

namespace Info::Statistics {

void AddPublicForwards(
	const Data::PublicForwardsSlice &firstSlice,
	not_null<Ui::VerticalLayout*> container,
	Fn<void(FullMsgId)> showPeerHistory,
	not_null<PeerData*> peer,
	FullMsgId contextId);

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

} // namespace Info::Statistics
