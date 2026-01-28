/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace st {
extern const style::margins &giveawayGiftCodeLabelMargin;
extern const style::margins &giveawayGiftCodeValueMargin;
} // namespace st

namespace ChatHelpers {
class Show;
} // namespace ChatHelpers

namespace Ui {

class RpWidget;
class TableLayout;
class FlatLabel;
class ImportantTooltip;

void AddTableRow(
	not_null<TableLayout*> table,
	rpl::producer<QString> label,
	object_ptr<RpWidget> value,
	style::margins valueMargins = st::giveawayGiftCodeValueMargin);

not_null<FlatLabel*> AddTableRow(
	not_null<TableLayout*> table,
	rpl::producer<QString> label,
	rpl::producer<TextWithEntities> value,
	const Text::MarkedContext &context = {});

void AddTableRow(
	not_null<TableLayout*> table,
	rpl::producer<QString> label,
	std::shared_ptr<ChatHelpers::Show> show,
	PeerId id);


[[nodiscard]] object_ptr<RpWidget> MakeValueWithSmallButton(
	not_null<TableLayout*> table,
	not_null<RpWidget*> value,
	rpl::producer<QString> buttonText,
	Fn<void(not_null<RpWidget*> button)> handler = nullptr,
	int topSkip = 0);
[[nodiscard]] object_ptr<RpWidget> MakePeerTableValue(
	not_null<TableLayout*> table,
	std::shared_ptr<ChatHelpers::Show> show,
	PeerId id,
	rpl::producer<QString> button = nullptr,
	Fn<void()> handler = nullptr);
[[nodiscard]] object_ptr<RpWidget> MakePeerWithStatusValue(
	not_null<TableLayout*> table,
	std::shared_ptr<ChatHelpers::Show> show,
	PeerId id,
	Fn<void(not_null<RpWidget*>, EmojiStatusId)> pushStatusId);
[[nodiscard]] object_ptr<RpWidget> MakeHiddenPeerTableValue(
	not_null<TableLayout*> table);

struct TableRowTooltipData {
	not_null<RpWidget*> parent;
	ImportantTooltip *raw = nullptr;
};
void ShowTableRowTooltip(
	std::shared_ptr<TableRowTooltipData> data,
	not_null<QWidget*> target,
	rpl::producer<TextWithEntities> text,
	int duration,
	const Text::MarkedContext &context = {});

[[nodiscard]] object_ptr<RpWidget> MakeTableValueWithTooltip(
	not_null<TableLayout*> table,
	std::shared_ptr<TableRowTooltipData> data,
	TextWithEntities price,
	TextWithEntities tooltip,
	const Text::MarkedContext &context = {});

} // namespace Ui
