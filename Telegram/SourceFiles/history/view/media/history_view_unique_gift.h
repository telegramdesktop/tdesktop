/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class Painter;

namespace Data {
class MediaGiftBox;
struct UniqueGift;
} // namespace Data

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {

class Element;
class MediaGenericPart;

auto GenerateUniqueGiftMedia(
	not_null<Element*> parent,
	Element *replacing,
	not_null<Data::UniqueGift*> gift)
-> Fn<void(Fn<void(std::unique_ptr<MediaGenericPart>)>)>;

[[nodiscard]] Fn<void(Painter&, const Ui::ChatPaintContext &)> UniqueGiftBg(
	not_null<Element*> view,
	not_null<Data::UniqueGift*> gift);

[[nodiscard]] std::unique_ptr<MediaGenericPart> MakeGenericButtonPart(
	const QString &text,
	QMargins margins,
	Fn<void()> repaint,
	ClickHandlerPtr link,
	QColor bg = QColor(0, 0, 0, 0));

} // namespace HistoryView
