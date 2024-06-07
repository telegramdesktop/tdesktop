/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui {

class GenericBox;
class DynamicImage;

enum class CollectibleType {
	Phone,
	Username,
};

[[nodiscard]] CollectibleType DetectCollectibleType(const QString &entity);

struct CollectibleInfo {
	QString entity;
	QString copyText;
	std::shared_ptr<DynamicImage> ownerUserpic;
	QString ownerName;
	uint64 cryptoAmount = 0;
	uint64 amount = 0;
	QString cryptoCurrency;
	QString currency;
	QString url;
	TimeId date = 0;
};

struct CollectibleDetails {
	TextWithEntities tonEmoji;
	Fn<std::any()> tonEmojiContext;
};

void CollectibleInfoBox(
	not_null<Ui::GenericBox*> box,
	CollectibleInfo info,
	CollectibleDetails details);

} // namespace Ui
