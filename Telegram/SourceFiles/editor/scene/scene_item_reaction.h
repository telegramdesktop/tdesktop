/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "editor/scene/scene_item_base.h"
#include "data/data_message_reaction_id.h"

namespace Data {
class DocumentMedia;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class AnimatedIcon;
} // namespace Ui

namespace Editor {

class ItemReaction final : public ItemBase {
public:
	enum { Type = ItemBase::Type + 7 };

	ItemReaction(
		not_null<Main::Session*> session,
		::Data::ReactionId reaction,
		bool dark,
		ItemBase::Data data);
	~ItemReaction();

	QRectF boundingRect() const override;
	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	int type() const override;
	bool bakes() const override;

	[[nodiscard]] ::Data::ReactionId reaction() const;
	void setReaction(::Data::ReactionId reaction);
	[[nodiscard]] bool dark() const;
	void setDark(bool dark);
	[[nodiscard]] bool iconReady() const;

protected:
	void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
	void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
	void performFlip() override;
	std::shared_ptr<ItemBase> duplicate(ItemBase::Data data) const override;
	void fillContextMenu(not_null<Ui::PopupMenu*> menu) override;

private:
	void updateIcon();
	[[nodiscard]] bool createIcon();
	void cacheBackground();
	void requestPick();

	const not_null<Main::Session*> _session;
	::Data::ReactionId _reaction;
	std::shared_ptr<::Data::DocumentMedia> _media;
	std::unique_ptr<Ui::AnimatedIcon> _icon;
	rpl::lifetime _loadingLifetime;
	QImage _background;
	bool _backgroundReady = false;
	QPointF _pressPosition;
	bool _pressedSelected = false;
	bool _dark = false;

};

} // namespace Editor
