/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/editor_link_pill.h"
#include "editor/scene/scene_item_base.h"

namespace Editor {

class ItemLink final : public ItemBase {
public:
	enum { Type = ItemBase::Type + 6 };

	using EditCallback = Fn<void(not_null<ItemLink*>)>;

	ItemLink(LinkPreview link, ItemBase::Data data);

	[[nodiscard]] static LinkPill MakePill(
		const LinkPreview &link,
		QSize imageSize);

	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	int type() const override;

	[[nodiscard]] const LinkPreview &link() const;
	void setLink(LinkPreview link);
	[[nodiscard]] LinkStyle style() const;
	void setStyle(LinkStyle style);
	void nextStyle();
	void setEditCallback(EditCallback callback);

protected:
	[[nodiscard]] QRectF visibleRect() const override;
	[[nodiscard]] bool flippable() const override;
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
	void fillContextMenu(not_null<Ui::PopupMenu*> menu) override;
	std::shared_ptr<ItemBase> duplicate(ItemBase::Data data) const override;

private:
	LinkPreview _link;
	const QSize _imageSize;
	LinkPill _pill;
	EditCallback _edit;
	bool _wasSelected = false;
	bool _styleCycled = false;

};

} // namespace Editor
