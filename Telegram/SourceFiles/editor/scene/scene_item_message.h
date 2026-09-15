/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "editor/scene/scene_item_base.h"

namespace Editor {

class MessageRenderer;
class MessageSource;

class ItemMessage final : public ItemBase, public base::has_weak_ptr {
public:
	enum { Type = ItemBase::Type + 5 };

	using EditCallback = Fn<void(not_null<ItemMessage*>)>;

	ItemMessage(
		std::shared_ptr<MessageSource> source,
		std::unique_ptr<MessageRenderer> renderer,
		ItemBase::Data data,
		std::optional<bool> dark = std::nullopt);
	~ItemMessage();

	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	int type() const override;

	[[nodiscard]] const std::shared_ptr<MessageSource> &source() const;
	void setSource(std::shared_ptr<MessageSource> source);
	[[nodiscard]] std::optional<bool> dark() const;
	void setDark(std::optional<bool> dark);
	void setEditCallback(EditCallback callback);
	void save(SaveState state) override;

protected:
	[[nodiscard]] QRectF visibleRect() const override;
	[[nodiscard]] bool flippable() const override;
	void fillContextMenu(not_null<Ui::PopupMenu*> menu) override;
	void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
	std::shared_ptr<ItemBase> duplicate(ItemBase::Data data) const override;

private:
	void attachRenderer();
	void scheduleRefresh();
	void refresh();
	void updateSize();
	void ensureRatio(int ratio);
	[[nodiscard]] bool editable() const;
	[[nodiscard]] int neededRatio(not_null<QPainter*> p) const;

	std::shared_ptr<MessageSource> _source;
	std::unique_ptr<MessageRenderer> _renderer;
	EditCallback _edit;
	QImage _image;
	QSize _size;
	std::optional<bool> _dark;
	int _ratio = 0;
	bool _refreshScheduled = false;

};

} // namespace Editor
