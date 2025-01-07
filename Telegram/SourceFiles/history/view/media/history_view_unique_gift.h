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
class MediaGeneric;
class MediaGenericPart;

[[nodiscard]] auto GenerateUniqueGiftMedia(
	not_null<Element*> parent,
	Element *replacing,
	std::shared_ptr<Data::UniqueGift> gift)
-> Fn<void(
	not_null<MediaGeneric*>,
	Fn<void(std::unique_ptr<MediaGenericPart>)>)>;

[[nodiscard]] auto UniqueGiftBg(
	not_null<Element*> view,
	std::shared_ptr<Data::UniqueGift> gift)
-> Fn<void(
	Painter&,
	const Ui::ChatPaintContext&,
	not_null<const MediaGeneric*>)>;

[[nodiscard]] auto GenerateUniqueGiftPreview(
	not_null<Element*> parent,
	Element *replacing,
	std::shared_ptr<Data::UniqueGift> gift)
-> Fn<void(
	not_null<MediaGeneric*>,
	Fn<void(std::unique_ptr<MediaGenericPart>)>)>;

[[nodiscard]] std::unique_ptr<MediaGenericPart> MakeGenericButtonPart(
	const QString &text,
	QMargins margins,
	Fn<void()> repaint,
	ClickHandlerPtr link,
	QColor bg = QColor(0, 0, 0, 0));

} // namespace HistoryView
