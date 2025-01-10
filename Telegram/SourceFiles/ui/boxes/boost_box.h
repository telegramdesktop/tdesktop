/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

namespace Ui {

void StartFireworks(not_null<QWidget*> parent);

class Show;
class RpWidget;
class GenericBox;
class VerticalLayout;
class FlatLabel;

struct BoostCounters {
	int level = 0;
	int boosts = 0;
	int thisLevelBoosts = 0;
	int nextLevelBoosts = 0; // Zero means no next level is available.
	int mine = 0;

	friend inline constexpr bool operator==(
		BoostCounters,
		BoostCounters) = default;
};

struct BoostFeatures {
	base::flat_map<int, int> nameColorsByLevel;
	base::flat_map<int, int> linkStylesByLevel;
	int linkLogoLevel = 0;
	int transcribeLevel = 0;
	int emojiPackLevel = 0;
	int emojiStatusLevel = 0;
	int wallpaperLevel = 0;
	int wallpapersCount = 0;
	int customWallpaperLevel = 0;
	int sponsoredLevel = 0;
};

struct BoostBoxData {
	QString name;
	BoostCounters boost;
	BoostFeatures features;
	int lifting = 0;
	bool allowMulti = false;
	bool group = false;
};

void BoostBox(
	not_null<GenericBox*> box,
	BoostBoxData data,
	Fn<void(Fn<void(BoostCounters)>)> boost);

void BoostBoxAlready(not_null<GenericBox*> box, bool group);
void GiftForBoostsBox(
	not_null<GenericBox*> box,
	QString channel,
	int receive,
	bool again);
void GiftedNoBoostsBox(not_null<GenericBox*> box, bool group);
void PremiumForBoostsBox(
	not_null<GenericBox*> box,
	bool group,
	Fn<void()> buyPremium);

struct AskBoostChannelColor {
	int requiredLevel = 0;
};

struct AskBoostWallpaper {
	int requiredLevel = 0;
	bool group = false;
};

struct AskBoostEmojiStatus {
	int requiredLevel = 0;
	bool group = false;
};

struct AskBoostEmojiPack {
	int requiredLevel = 0;
};

struct AskBoostCustomReactions {
	int count = 0;
};

struct AskBoostCpm {
	int requiredLevel = 0;
};

struct AskBoostWearCollectible {
	int requiredLevel = 0;
};

struct AskBoostReason {
	std::variant<
		AskBoostChannelColor,
		AskBoostWallpaper,
		AskBoostEmojiStatus,
		AskBoostEmojiPack,
		AskBoostCustomReactions,
		AskBoostCpm,
		AskBoostWearCollectible> data;
};

struct AskBoostBoxData {
	QString link;
	BoostCounters boost;
	AskBoostReason reason;
};

void AskBoostBox(
	not_null<GenericBox*> box,
	AskBoostBoxData data,
	Fn<void()> openStatistics,
	Fn<void()> startGiveaway);

[[nodiscard]] object_ptr<RpWidget> MakeLinkLabel(
	not_null<QWidget*> parent,
	rpl::producer<QString> text,
	rpl::producer<QString> link,
	std::shared_ptr<Show> show,
	object_ptr<RpWidget> right);

void FillBoostLimit(
	rpl::producer<> showFinished,
	not_null<VerticalLayout*> container,
	rpl::producer<BoostCounters> data,
	style::margins limitLinePadding);

[[nodiscard]] object_ptr<Ui::FlatLabel> MakeBoostFeaturesBadge(
	not_null<QWidget*> parent,
	rpl::producer<QString> text,
	Fn<QBrush(QRect)> bg);

} // namespace Ui
