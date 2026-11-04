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

struct LocationData {
	float64 lat = 0.;
	float64 lon = 0.;
	QString title;
	QString address;
	QString countryIso2;
	QString city;
	QString provider;
	QString venueId;
	QString venueType;

	friend inline bool operator==(
		const LocationData &,
		const LocationData &) = default;
};

class ItemLocation final : public ItemBase {
public:
	enum { Type = ItemBase::Type + 8 };

	using EditCallback = Fn<void(not_null<ItemLocation*>)>;

	ItemLocation(LocationData location, ItemBase::Data data);

	[[nodiscard]] static LinkPill MakePill(
		const LocationData &location,
		QSize imageSize);

	void paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *widget) override;
	int type() const override;

	[[nodiscard]] const LocationData &location() const;
	void setLocation(LocationData location);
	[[nodiscard]] LinkStyle style() const;
	void setStyle(LinkStyle style);
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
	void requestPick();

	LocationData _location;
	const QSize _imageSize;
	LinkPill _pill;
	EditCallback _edit;
	bool _wasSelected = false;
	bool _styleCycled = false;

};

} // namespace Editor
