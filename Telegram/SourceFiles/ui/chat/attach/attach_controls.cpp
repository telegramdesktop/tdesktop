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
	const auto groupHalfWidth = groupWidth / 2;
	const auto &internalSkip = st::sendBoxAlbumGroupEditInternalSkip;

	QRect groupRect(x, y, groupWidth, groupHeight);
	_rect.paint(p, groupRect);

	QRect leftRect(x, y, groupHalfWidth, groupHeight);
	QRect rightRect(x + groupHalfWidth, y, groupHalfWidth, groupHeight);
	st::sendBoxAlbumGroupButtonMediaEdit.paintInCenter(p, leftRect);
	st::sendBoxAlbumGroupButtonMediaDelete.paintInCenter(p, rightRect);
}

int AttachControls::width() const {
	return st::sendBoxAlbumGroupSize.width();
}

int AttachControls::height() const {
	return st::sendBoxAlbumGroupSize.height();
}

AttachControlsWidget::AttachControlsWidget(not_null<RpWidget*> parent)
: RpWidget(parent)
, _edit(base::make_unique_q<AbstractButton>(this))
, _delete(base::make_unique_q<AbstractButton>(this)) {
	const auto w = _controls.width();
	resize(w, _controls.height());
	_edit->resize(w / 2, _controls.height());
	_delete->resize(w / 2, _controls.height());

	_edit->moveToLeft(0, 0, w);
	_delete->moveToRight(0, 0, w);

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
