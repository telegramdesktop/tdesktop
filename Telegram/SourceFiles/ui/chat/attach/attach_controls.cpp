/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_controls.h"

#include "ui/painter.h"
#include "styles/style_chat_helpers.h"

namespace Ui {

void AttachControls::paint(QPainter &p, int x, int y) {
	const auto groupWidth = width();
	const auto groupHeight = height();
	const auto full = (_type == Type::Full);

	const auto groupRect = QRect(x, y, groupWidth, groupHeight);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::roundedBg);
		if (_type == Type::EditOnly) {
			const auto desired = st::sendBoxAlbumSmallGroupCircleSize;
			const auto available = std::min(groupWidth, groupHeight);
			const auto side = std::min(desired, available);
			const auto circleRect = QRect(
				x + ((groupWidth - side) / 2),
				y + ((groupHeight - side) / 2),
				side,
				side);
			p.drawEllipse(circleRect);
		} else {
			const auto radius = std::min(groupWidth, groupHeight) / 2.;
			p.drawRoundedRect(groupRect, radius, radius);
		}
	}

	if (full) {
		const auto groupHalfWidth = groupWidth / 2;
		const auto groupHalfHeight = groupHeight / 2;
		const auto editRect = _vertical
			? QRect(x, y, groupWidth, groupHalfHeight)
			: QRect(x, y, groupHalfWidth, groupHeight);
		st::sendBoxAlbumGroupButtonMediaMore.paintInCenter(p, editRect);
		const auto deleteRect = _vertical
			? QRect(x, y + groupHalfHeight, groupWidth, groupHalfHeight)
			: QRect(x + groupHalfWidth, y, groupHalfWidth, groupHeight);
		st::sendBoxAlbumGroupButtonMediaDelete.paintInCenter(p, deleteRect);
	} else if (_type == Type::EditOnly) {
		st::sendBoxAlbumButtonMediaMore.paintInCenter(p, groupRect);
	}
}

int AttachControls::width() const {
	return (_type == Type::Full)
		? (_vertical
			? st::sendBoxAlbumGroupSizeVertical.width()
			: st::sendBoxAlbumGroupSize.width())
		: (_type == Type::EditOnly)
		? ((st::sendBoxAlbumSmallGroupSize.width()
			> st::sendBoxAlbumSmallGroupCircleSize)
			? st::sendBoxAlbumSmallGroupSize.width()
			: st::sendBoxAlbumSmallGroupCircleSize)
		: 0;
}

int AttachControls::height() const {
	return (_type == Type::Full)
		? (_vertical
			? st::sendBoxAlbumGroupSizeVertical.height()
			: st::sendBoxAlbumGroupSize.height())
		: (_type == Type::EditOnly)
		? ((st::sendBoxAlbumSmallGroupSize.height()
			> st::sendBoxAlbumSmallGroupCircleSize)
			? st::sendBoxAlbumSmallGroupSize.height()
			: st::sendBoxAlbumSmallGroupCircleSize)
		: 0;
}

AttachControls::Type AttachControls::type() const {
	return _type;
}

bool AttachControls::vertical() const {
	return _vertical;
}

void AttachControls::setType(Type type) {
	if (_type != type) {
		_type = type;
	}
}

void AttachControls::setVertical(bool vertical) {
	_vertical = vertical;
}

AttachControlsWidget::AttachControlsWidget(
	not_null<RpWidget*> parent,
	AttachControls::Type type)
: RpWidget(parent)
, _edit(base::make_unique_q<AbstractButton>(this))
, _delete(base::make_unique_q<AbstractButton>(this)) {
	_controls.setType(type);

	const auto w = _controls.width();
	const auto h = _controls.height();
	resize(w, h);

	if (type == AttachControls::Type::Full) {
		const auto leftWidth = w / 2;
		const auto rightWidth = w - leftWidth;
		_edit->setGeometryToLeft(0, 0, leftWidth, h, w);
		_delete->setGeometryToLeft(leftWidth, 0, rightWidth, h, w);
	} else if (type == AttachControls::Type::EditOnly) {
		_edit->setGeometryToLeft(0, 0, w, h, w);
	}

	paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(this);
		_controls.paint(p, 0, 0);
	}, lifetime());
}

rpl::producer<> AttachControlsWidget::editRequests() const {
	return _edit->clicks() | rpl::to_empty;
}

rpl::producer<> AttachControlsWidget::deleteRequests() const {
	return _delete->clicks() | rpl::to_empty;
}

} // namespace Ui
