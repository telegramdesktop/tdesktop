/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Data {
struct UniqueGift;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Ui {

class VerticalLayout;

void ChooseStarGiftRecipient(
	not_null<Window::SessionController*> controller);

void ShowStarGiftBox(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer);

void AddUniqueGiftCover(
	not_null<VerticalLayout*> container,
	rpl::producer<Data::UniqueGift> data,
	rpl::producer<QString> subtitleOverride = nullptr);

void PaintPoints(
	QPainter &p,
	base::flat_map<float64, QImage> &cache,
	not_null<Text::CustomEmoji*> emoji,
	const Data::UniqueGift &gift,
	const QRect &rect,
	float64 shown = 1.);

void ShowStarGiftUpgradeBox(
	not_null<Window::SessionController*> controller,
	uint64 stargiftId,
	not_null<UserData*> user,
	MsgId itemId,
	int stars,
	Fn<void(bool)> ready);

} // namespace Ui
