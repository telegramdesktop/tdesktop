/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/effects/round_checkbox.h"

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
} // namespace style

namespace Ui {

class GenericBox;
class RadiobuttonGroup;
class VerticalLayout;

namespace Premium {

void AddBubbleRow(
	not_null<Ui::VerticalLayout*> parent,
	rpl::producer<> showFinishes,
	int min,
	int current,
	int max,
	bool premiumPossible,
	std::optional<tr::phrase<lngtag_count>> phrase,
	const style::icon *icon);

void AddLimitRow(
	not_null<Ui::VerticalLayout*> parent,
	QString max,
	QString min = {});

void AddLimitRow(
	not_null<Ui::VerticalLayout*> parent,
	int max,
	std::optional<tr::phrase<lngtag_count>> phrase,
	int min = 0);

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

struct ListEntry final {
	rpl::producer<QString> subtitle;
	rpl::producer<TextWithEntities> description;
	int leftNumber = 0;
	int rightNumber = 0;
	std::optional<QString> customRightText;
};
void ShowListBox(
	not_null<Ui::GenericBox*> box,
	std::vector<ListEntry> entries);

void AddGiftOptions(
	not_null<Ui::VerticalLayout*> parent,
	std::shared_ptr<Ui::RadiobuttonGroup> group,
	std::vector<Data::SubscriptionOption> gifts,
	const style::PremiumOption &st,
	bool topBadges = false);

} // namespace Premium
} // namespace Ui
