/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/unique_qptr.h"
#include "editor/scene/scene_item_base.h"

namespace Ui {
class PopupMenu;
} // namespace Ui

namespace Editor {

[[nodiscard]] QColor EffectiveTextColor(const QColor &color, TextStyle style);

struct TextLayoutSpec {
	QFont font;
	int padding = 0;
	int maxTextWidth = 0;
};

[[nodiscard]] TextLayoutSpec ComputeTextLayoutSpec(
	float64 fontSize,
	const QSize &imageSize,
	TextStyle style,
	TextTypeface typeface);

struct TextBackgroundLine {
	float64 left = 0;
	float64 top = 0;
	float64 right = 0;
	float64 bottom = 0;
};

[[nodiscard]] QPainterPath BuildTextBackgroundPath(
	std::vector<TextBackgroundLine> lines,
	float64 fontSize);
[[nodiscard]] QColor TextBackgroundColor(
	const QColor &color,
	TextStyle style);
[[nodiscard]] int TextBackgroundPadding(float64 fontSize, TextStyle style);

class ItemText : public ItemBase {
public:
	enum { Type = ItemBase::Type + 2 };

	ItemText(
		const QString &text,
		const QColor &color,
		float64 fontSize,
		TextStyle style,
		TextTypeface typeface,
		TextAlignment alignment,
		const QSize &imageSize,
		ItemBase::Data data);

	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	int type() const override;

	[[nodiscard]] const QString &text() const;
	void setText(const QString &text);

	[[nodiscard]] const QColor &color() const;
	void setColor(const QColor &color);

	[[nodiscard]] float64 fontSize() const;
	void setFontSize(float64 fontSize);

	[[nodiscard]] TextStyle textStyle() const;
	void setTextStyle(TextStyle style);

	[[nodiscard]] TextTypeface typeface() const;
	void setTypeface(TextTypeface typeface);

	[[nodiscard]] TextAlignment alignment() const;
	void setAlignment(TextAlignment alignment);

	[[nodiscard]] float64 editScale() const;
	void bakeScale();

	[[nodiscard]] Placement placement() const override;
	void applyPlacement(const Placement &placement) override;

	[[nodiscard]] static QSize computeContentSize(
		const QString &text,
		float64 fontSize,
		const QSize &imageSize,
		TextStyle style,
		TextTypeface typeface);

	void save(SaveState state) override;
	void restore(SaveState state) override;

protected:
	void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
	void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override;
	void actionFlip() override;
	void performFlip() override;
	std::shared_ptr<ItemBase> duplicate(ItemBase::Data data) const override;

private:
	void renderContent();
	void notifyPrefsUsed();

	QString _text;
	QColor _color;
	float64 _fontSize;
	TextStyle _textStyle = TextStyle::Plain;
	TextTypeface _typeface = TextTypeface::Default;
	TextAlignment _alignment = TextAlignment::Center;
	QSize _imageSize;
	QPixmap _pixmap;
	base::unique_qptr<Ui::PopupMenu> _contextMenu;

	struct SavedText {
		QString text;
		QColor color;
		float64 fontSize = 0.;
		TextStyle textStyle = TextStyle::Plain;
		TextTypeface typeface = TextTypeface::Default;
		TextAlignment alignment = TextAlignment::Center;
	};
	SavedText _savedState, _keepedState;
};

} // namespace Editor
