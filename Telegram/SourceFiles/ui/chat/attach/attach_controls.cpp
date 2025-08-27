/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_controls.h"

#include "styles/style_chat_helpers.h"

namespace Ui {

AttachControls::AttachControls()
: _rect(st::sendBoxAlbumGroupRadius, st::roundedBg) {
}

void AttachControls::paint(QPainter &p, int x, int y) {
	const auto groupWidth = width();
	const auto groupHeight = height();
	const auto full = (_type == Type::Full);

	QRect groupRect(x, y, groupWidth, groupHeight);
	_rect.paint(p, groupRect);

	if (full) {
		const auto groupHalfWidth = groupWidth / 2;
		const auto groupHalfHeight = groupHeight / 2;
		const auto editRect = _vertical
			? QRect(x, y, groupWidth, groupHalfHeight)
			: QRect(x, y, groupHalfWidth, groupHeight);
		st::sendBoxAlbumGroupButtonMediaEdit.paintInCenter(p, editRect);
		const auto deleteRect = _vertical
			? QRect(x, y + groupHalfHeight, groupWidth, groupHalfHeight)
			: QRect(x + groupHalfWidth, y, groupHalfWidth, groupHeight);
		st::sendBoxAlbumGroupButtonMediaDelete.paintInCenter(p, deleteRect);
	} else if (_type == Type::EditOnly) {
		st::sendBoxAlbumButtonMediaEdit.paintInCenter(p, groupRect);
	}
}

int AttachControls::width() const {
	return (_type == Type::Full)
		? (_vertical
			? st::sendBoxAlbumGroupSizeVertical.width()
			: st::sendBoxAlbumGroupSize.width())
		: (_type == Type::EditOnly)
		? st::sendBoxAlbumSmallGroupSize.width()
		: 0;
}

int AttachControls::height() const {
	return (_type == Type::Full)
		? (_vertical
			? st::sendBoxAlbumGroupSizeVertical.height()
			: st::sendBoxAlbumGroupSize.height())
		: (_type == Type::EditOnly)
		? st::sendBoxAlbumSmallGroupSize.height()
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
	resize(w, _controls.height());

	if (type == AttachControls::Type::Full) {
		_edit->resize(w / 2, _controls.height());
		_delete->resize(w / 2, _controls.height());

		_edit->moveToLeft(0, 0, w);
		_delete->moveToRight(0, 0, w);
	} else if (type == AttachControls::Type::EditOnly) {
		_edit->resize(w, _controls.height());
		_edit->moveToLeft(0, 0, w);
	}

	paintRequest(
	) | rpl::start_with_next([=] {
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
