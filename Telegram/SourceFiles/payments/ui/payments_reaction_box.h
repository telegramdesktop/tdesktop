/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "calls/group/ui/calls_group_stars_coloring.h"

namespace style {
struct RoundCheckbox;
struct MediaSlider;
} // namespace style

namespace Main {
class Session;
} // namespace Main

namespace Ui::Premium {
class BubbleWidget;
} // namespace Ui::Premium

namespace Ui {

class AbstractButton;
class BoxContent;
class GenericBox;
class DynamicImage;
class VerticalLayout;

struct PaidReactionTop {
	QString name;
	std::shared_ptr<DynamicImage> photo;
	uint64 barePeerId = 0;
	int count = 0;
	Fn<void()> click;
	bool my = false;
};

struct PaidReactionBoxArgs {
	int min = 0;
	int explicitlyAllowed = 0;
	int chosen = 0;
	int max = 0;

	std::vector<PaidReactionTop> top;

	not_null<Main::Session*> session;
	QString name;
	Fn<rpl::producer<TextWithEntities>(rpl::producer<int> amount)> submit;
	std::vector<Calls::Group::Ui::StarsColoring> colorings;
	rpl::producer<CreditsAmount> balanceValue;
	Fn<void(int, uint64)> send;
	bool videoStreamChoosing = false;
	bool videoStreamSending = false;
	bool videoStreamAdmin = false;
	bool dark = false;
};

void PaidReactionsBox(
	not_null<GenericBox*> box,
	PaidReactionBoxArgs &&args);

[[nodiscard]] object_ptr<BoxContent> MakePaidReactionBox(
	PaidReactionBoxArgs &&args);

[[nodiscard]] int MaxTopPaidDonorsShown();

[[nodiscard]] QImage GenerateSmallBadgeImage(
	QString text,
	const style::icon &icon,
	QColor bg,
	QColor fg,
	const style::RoundCheckbox *borderSt = nullptr);

struct StarSelectDiscreter {
	Fn<int(float64)> ratioToValue;
	Fn<float64(int)> valueToRatio;
};

[[nodiscard]] StarSelectDiscreter StarSelectDiscreterForMax(int max);

void PaidReactionSlider(
	not_null<VerticalLayout*> container,
	const style::MediaSlider &st,
	int min,
	int explicitlyAllowed,
	rpl::producer<int> current,
	int max,
	Fn<void(int)> changed,
	Fn<QColor(int)> activeFgOverride = nullptr);

void AddStarSelectBalance(
	not_null<GenericBox*> box,
	not_null<Main::Session*> session,
	rpl::producer<CreditsAmount> balanceValue,
	bool dark = false);

not_null<Premium::BubbleWidget*> AddStarSelectBubble(
	not_null<VerticalLayout*> container,
	rpl::producer<> showFinishes,
	rpl::producer<int> value,
	int max,
	Fn<QColor(int)> activeFgOverride = nullptr);

struct StarSelectInfoBlock {
	rpl::producer<TextWithEntities> title;
	rpl::producer<QString> subtext;
	Fn<void()> click;
};
[[nodiscard]] object_ptr<RpWidget> MakeStarSelectInfoBlocks(
	not_null<RpWidget*> parent,
	std::vector<StarSelectInfoBlock> blocks,
	Text::MarkedContext context,
	bool dark = false);

} // namespace Ui
