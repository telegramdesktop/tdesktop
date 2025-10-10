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

class TextPartColored : public MediaGenericTextPart {
public:
	TextPartColored(
		TextWithEntities text,
		QMargins margins,
		Fn<QColor(const PaintContext &)> color,
		const style::TextStyle &st = st::defaultTextStyle,
		const base::flat_map<uint16, ClickHandlerPtr> &links = {},
		const Ui::Text::MarkedContext &context = {});

private:
	void setupPen(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context) const override;

	Fn<QColor(const PaintContext &)> _color;

};

class AttributeTable final : public MediaGenericPart {
public:
	struct Entry {
		QString label;
		TextWithEntities value;
	};

	AttributeTable(
		std::vector<Entry> entries,
		QMargins margins,
		Fn<QColor(const PaintContext &)> labelColor,
		Fn<QColor(const PaintContext &)> valueColor,
		const Ui::Text::MarkedContext &context = {});

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
	};

	std::vector<Part> _parts;
	QMargins _margins;
	Fn<QColor(const PaintContext &)> _labelColor;
	Fn<QColor(const PaintContext &)> _valueColor;
	int _valueLeft = 0;

};

} // namespace HistoryView
