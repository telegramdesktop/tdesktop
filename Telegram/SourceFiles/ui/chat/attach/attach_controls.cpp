/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_controls.h"

#include "styles/style_boxes.h"

namespace Ui {

AttachControls::AttachControls()
: _rect(st::sendBoxAlbumGroupRadius, st::roundedBg) {
}

void AttachControls::paint(Painter &p, int x, int y) {
	const auto groupWidth = width();
	const auto groupHeight = height();
	const auto full = (_type == Type::Full);

	QRect groupRect(x, y, groupWidth, groupHeight);
	_rect.paint(p, groupRect);

	if (full) {
		const auto groupHalfWidth = groupWidth / 2;
		QRect leftRect(x, y, groupHalfWidth, groupHeight);
		st::sendBoxAlbumGroupButtonMediaEdit.paintInCenter(p, leftRect);
		QRect rightRect(x + groupHalfWidth, y, groupHalfWidth, groupHeight);
		st::sendBoxAlbumGroupButtonMediaDelete.paintInCenter(p, rightRect);
	} else if (_type == Type::EditOnly) {
		st::sendBoxAlbumButtonMediaEdit.paintInCenter(p, groupRect);
	}
}

int AttachControls::width() const {
	return (_type == Type::Full)
		? st::sendBoxAlbumGroupSize.width()
		: (_type == Type::EditOnly)
		? st::sendBoxAlbumSmallGroupSize.width()
		: 0;
}

int AttachControls::height() const {
	return (_type == Type::Full)
		? st::sendBoxAlbumGroupSize.height()
		: (_type == Type::EditOnly)
		? st::sendBoxAlbumSmallGroupSize.height()
		: 0;
}

AttachControls::Type AttachControls::type() const {
	return _type;
}

void AttachControls::setType(Type type) {
	if (_type != type) {
		_type = type;
	}
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
		Painter p(this);
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
