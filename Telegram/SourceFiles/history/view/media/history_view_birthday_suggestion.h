/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/media/history_view_media_generic.h"

class Painter;

namespace Data {
class MediaGiftBox;
struct UniqueGift;
class Birthday;
} // namespace Data

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {

class Element;
class MediaGeneric;
class MediaGenericPart;

[[nodiscard]] auto GenerateSuggetsBirthdayMedia(
	not_null<Element*> parent,
	Element *replacing,
	Data::Birthday birthday)
-> Fn<void(
	not_null<MediaGeneric*>,
	Fn<void(std::unique_ptr<MediaGenericPart>)>)>;

class BirthdayTable final : public MediaGenericPart {
public:
	BirthdayTable(Data::Birthday birthday, QMargins margins);

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	struct Part {
		Ui::Text::String label;
		Ui::Text::String value;
		int labelLeft = 0;
		int valueLeft = 0;
	};

	std::vector<Part> _parts;
	QMargins _margins;
	Fn<QColor(const PaintContext &)> _labelColor;
	Fn<QColor(const PaintContext &)> _valueColor;

};

} // namespace HistoryView
