/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
struct BoostCounters;
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

[[nodiscard]] int BoostsForGift(not_null<Main::Session*> session);

object_ptr<Ui::BoxContent> ReassignBoostsBox(
	not_null<ChannelData*> to,
	std::vector<TakenBoostSlot> from,
	Fn<void(std::vector<int> slots, int sources)> reassign,
	Fn<void()> cancel);

[[nodiscard]] object_ptr<Ui::RpWidget> CreateBoostReplaceUserpics(
	not_null<Ui::RpWidget*> parent,
	rpl::producer<std::vector<not_null<PeerData*>>> from,
	not_null<PeerData*> to);
