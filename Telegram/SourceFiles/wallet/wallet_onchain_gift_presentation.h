/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Ui::Text {
class String;
} // namespace Ui::Text

namespace Wallet {

struct CollectibleView;
struct TransferItem;
enum class TransferOnchainGift : uchar;

struct CollectibleRowLook {
	QString service;
	QString kind;
	int64 amountNano = 0;
	bool incoming = false;
};

[[nodiscard]] bool ShowsOnchainGift(const TransferItem &item);
[[nodiscard]] CollectibleRowLook ComputeCollectibleRowLook(
	const TransferItem &item);

void SetCollectibleChipSubtitle(
	Ui::Text::String &text,
	const CollectibleView &view,
	TransferOnchainGift action,
	int64 amountNano);

void AddOnchainGiftPill(
	not_null<Ui::VerticalLayout*> layout,
	const TransferItem &item,
	int bottomSkip);

} // namespace Wallet
