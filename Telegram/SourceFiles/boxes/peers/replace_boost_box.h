/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace style {
struct UserpicsRow;
} // namespace style

class ChannelData;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
struct BoostCounters;
struct BoostFeatures;
class BoxContent;
class RpWidget;
} // namespace Ui

struct TakenBoostSlot {
	int id = 0;
	TimeId expires = 0;
	PeerId peerId = 0;
	TimeId cooldown = 0;
};

struct ForChannelBoostSlots {
	std::vector<int> free;
	std::vector<int> already;
	std::vector<TakenBoostSlot> other;
};

[[nodiscard]] ForChannelBoostSlots ParseForChannelBoostSlots(
	not_null<ChannelData*> channel,
	const QVector<MTPMyBoost> &boosts);

[[nodiscard]] Ui::BoostCounters ParseBoostCounters(
	const MTPpremium_BoostsStatus &status);

[[nodiscard]] Ui::BoostFeatures LookupBoostFeatures(
	not_null<ChannelData*> channel);

[[nodiscard]] int BoostsForGift(not_null<Main::Session*> session);

object_ptr<Ui::BoxContent> ReassignBoostsBox(
	not_null<ChannelData*> to,
	std::vector<TakenBoostSlot> from,
	Fn<void(std::vector<int> slots, int groups, int channels)> reassign,
	Fn<void()> cancel);

enum class UserpicsTransferType {
	BoostReplace,
	StarRefJoin,
};
[[nodiscard]] object_ptr<Ui::RpWidget> CreateUserpicsTransfer(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<std::vector<not_null<PeerData*>>> from,
	not_null<PeerData*> to,
	UserpicsTransferType type);

[[nodiscard]] object_ptr<Ui::RpWidget> CreateUserpicsWithMoreBadge(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<std::vector<not_null<PeerData*>>> peers,
	const style::UserpicsRow &st,
	int limit);
