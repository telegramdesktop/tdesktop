/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/round_checkbox.h"

namespace style {
struct PremiumLimits;
} // namespace style

namespace tr {
template <typename ...>
struct phrase;
} // namespace tr

enum lngtag_count : int;

namespace Data {
struct SubscriptionOption;
} // namespace Data

namespace style {
struct RoundImageCheckbox;
struct PremiumOption;
struct TextStyle;
struct PremiumBubble;
} // namespace style

namespace Ui {

class GenericBox;
class RadiobuttonGroup;
class VerticalLayout;

namespace Premium {

inline constexpr auto kLimitRowRatio = 0.5;

void AddBubbleRow(
	not_null<Ui::VerticalLayout*> parent,
	const style::PremiumBubble &st,
	rpl::producer<> showFinishes,
	int min,
	int current,
	int max,
	bool premiumPossible,
	std::optional<tr::phrase<lngtag_count>> phrase,
	const style::icon *icon);

struct BubbleRowState {
	int counter = 0;
	float64 ratio = 0.;
	bool animateFromZero = false;
	bool dynamic = false;
};
void AddBubbleRow(
	not_null<Ui::VerticalLayout*> parent,
	const style::PremiumBubble &st,
	rpl::producer<> showFinishes,
	rpl::producer<BubbleRowState> state,
	bool premiumPossible,
	Fn<QString(int)> text,
	const style::icon *icon,
	const style::margins &outerPadding);

void AddLimitRow(
	not_null<Ui::VerticalLayout*> parent,
	const style::PremiumLimits &st,
	QString max,
	QString min = {},
	float64 ratio = kLimitRowRatio);

void AddLimitRow(
	not_null<Ui::VerticalLayout*> parent,
	const style::PremiumLimits &st,
	int max,
	std::optional<tr::phrase<lngtag_count>> phrase,
	int min = 0,
	float64 ratio = kLimitRowRatio);

struct LimitRowLabels {
	rpl::producer<QString> leftLabel;
	rpl::producer<QString> leftCount;
	rpl::producer<QString> rightLabel;
	rpl::producer<QString> rightCount;
};

struct LimitRowState {
	float64 ratio = 0.;
	bool animateFromZero = false;
	bool dynamic = false;
};

void AddLimitRow(
	not_null<Ui::VerticalLayout*> parent,
	const style::PremiumLimits &st,
	LimitRowLabels labels,
	rpl::producer<LimitRowState> state,
	const style::margins &padding);

struct AccountsRowArgs final {
	std::shared_ptr<Ui::RadiobuttonGroup> group;
	const style::RoundImageCheckbox &st;
	const style::TextStyle &stName;
	const style::color &stNameFg;
	struct Entry final {
		QString name;
		Ui::RoundImageCheckbox::PaintRoundImage paintRoundImage;
	};
	std::vector<Entry> entries;
};

void AddAccountsRow(
	not_null<Ui::VerticalLayout*> parent,
	AccountsRowArgs &&args);

[[nodiscard]] QGradientStops LimitGradientStops();
[[nodiscard]] QGradientStops ButtonGradientStops();
[[nodiscard]] QGradientStops LockGradientStops();
[[nodiscard]] QGradientStops FullHeightGradientStops();
[[nodiscard]] QGradientStops GiftGradientStops();
[[nodiscard]] QGradientStops CreditsIconGradientStops();

struct ListEntry final {
	rpl::producer<QString> title;
	rpl::producer<TextWithEntities> about;
	int leftNumber = 0;
	int rightNumber = 0;
	std::optional<QString> customRightText;
	const style::icon *icon = nullptr;
};
void ShowListBox(
	not_null<Ui::GenericBox*> box,
	const style::PremiumLimits &st,
	std::vector<ListEntry> entries);

void AddGiftOptions(
	not_null<Ui::VerticalLayout*> parent,
	std::shared_ptr<Ui::RadiobuttonGroup> group,
	std::vector<Data::SubscriptionOption> gifts,
	const style::PremiumOption &st,
	bool topBadges = false);

} // namespace Premium
} // namespace Ui
