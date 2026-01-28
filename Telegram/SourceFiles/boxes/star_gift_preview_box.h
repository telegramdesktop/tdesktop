/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct StarGift;
struct UniqueGift;
struct UniqueGiftAttributes;
enum class GiftAttributeIdType;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {

class GenericBox;

void StarGiftPreviewBox(
	not_null<GenericBox*> box,
	const QString &title,
	const Data::UniqueGiftAttributes &attributes,
	Data::GiftAttributeIdType tab,
	std::shared_ptr<Data::UniqueGift> selected);

} // namespace Ui
