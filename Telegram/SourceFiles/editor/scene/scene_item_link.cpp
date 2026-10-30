/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "editor/scene/scene_item_link.h"

#include "lang/lang_keys.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_media_player.h"
#include "styles/style_menu_icons.h"

#include <QtWidgets/QGraphicsSceneMouseEvent>

namespace Editor {
namespace {

constexpr auto kMaxWidthRatio = 0.9;

[[nodiscard]] LinkStyle NextStyle(LinkStyle style) {
	switch (style) {
	case LinkStyle::Framed: return LinkStyle::SemiTransparent;
	case LinkStyle::SemiTransparent: return LinkStyle::Opaque;
	case LinkStyle::Opaque: return LinkStyle::Framed;
	}
	Unexpected("Link style in NextStyle.");
}

[[nodiscard]] LinkStyle PreviousStyle(LinkStyle style) {
	switch (style) {
	case LinkStyle::Framed: return LinkStyle::Opaque;
	case LinkStyle::SemiTransparent: return LinkStyle::Framed;
	case LinkStyle::Opaque: return LinkStyle::SemiTransparent;
	}
	Unexpected("Link style in PreviousStyle.");
}

} // namespace

ItemLink::ItemLink(LinkPreview link, ItemBase::Data data)
: ItemBase(data)
, _link(std::move(link))
, _imageSize(data.imageSize)
, _pill(MakePill(_link, _imageSize)) {
	setAspectRatio(_pill.size().height() / _pill.size().width());
}

LinkPill ItemLink::MakePill(const LinkPreview &link, QSize imageSize) {
	return LinkPill(
		link,
		LinkPill::DensityFor(imageSize.width()),
		int(imageSize.width() * kMaxWidthRatio));
}

int ItemLink::type() const {
	return Type;
}

const LinkPreview &ItemLink::link() const {
	return _link;
}

void ItemLink::setLink(LinkPreview link) {
	_link = std::move(link);
	const auto style = _pill.style();
	const auto scale = size() / _pill.size().width();
	_pill = MakePill(_link, _imageSize);
	_pill.setStyle(style);
	applyStretch(
		_pill.size().width() * scale,
		_pill.size().height() * scale,
		false);
	update();
}

LinkStyle ItemLink::style() const {
	return _pill.style();
}

void ItemLink::setStyle(LinkStyle style) {
	_pill.setStyle(style);
	update();
}

void ItemLink::nextStyle() {
	setStyle(NextStyle(_pill.style()));
}

void ItemLink::setEditCallback(EditCallback callback) {
	_edit = std::move(callback);
}

QRectF ItemLink::visibleRect() const {
	return fittedRect(_pill.size());
}

bool ItemLink::flippable() const {
	return false;
}

void ItemLink::paint(
		QPainter *p,
		const QStyleOptionGraphicsItem *option,
		QWidget *w) {
	const auto rect = visibleRect();
	_pill.paint(*p, rect.topLeft(), rect.width() / _pill.size().width());
	ItemBase::paint(p, option, w);
}

void ItemLink::mousePressEvent(QGraphicsSceneMouseEvent *event) {
	_wasSelected = isSelected() && (event->button() == Qt::LeftButton);
	_styleCycled = false;
	ItemBase::mousePressEvent(event);
}

void ItemLink::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
	const auto clicked = _wasSelected
		&& (event->button() == Qt::LeftButton)
		&& !isHandling()
		&& !dragThresholdPassed(event);
	ItemBase::mouseReleaseEvent(event);
	if (clicked) {
		_styleCycled = true;
		nextStyle();
	}
}

void ItemLink::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
	if (base::take(_styleCycled)) {
		setStyle(PreviousStyle(_pill.style()));
	}
	_wasSelected = false;
	if (_edit) {
		_edit(this);
	} else {
		ItemBase::mouseDoubleClickEvent(event);
	}
}

void ItemLink::fillContextMenu(not_null<Ui::PopupMenu*> menu) {
	const auto current = _pill.style();
	const auto addStyle = [&](
			const QString &text,
			LinkStyle style,
			const style::icon *icon) {
		const auto checked = (current == style);
		menu->addAction(
			text,
			[=] { setStyle(style); },
			checked ? &st::mediaPlayerMenuCheck : icon);
	};
	addStyle(
		tr::lng_photo_editor_text_style_framed(tr::now),
		LinkStyle::Framed,
		&st::mediaMenuIconTextStyleFramed);
	addStyle(
		tr::lng_photo_editor_text_style_semi_transparent(tr::now),
		LinkStyle::SemiTransparent,
		&st::mediaMenuIconTextStyleSemiTransparent);
	addStyle(
		tr::lng_photo_editor_text_style_opaque(tr::now),
		LinkStyle::Opaque,
		&st::mediaMenuIconTextStyleOpaque);
	if (_edit) {
		menu->addAction(
			tr::lng_menu_formatting_link_edit(tr::now),
			[=] { _edit(this); },
			&st::mediaMenuIconEdit);
	}
}

std::shared_ptr<ItemBase> ItemLink::duplicate(ItemBase::Data data) const {
	auto result = std::make_shared<ItemLink>(_link, std::move(data));
	result->_pill.setStyle(_pill.style());
	result->_edit = _edit;
	return result;
}

} // namespace Editor
