/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2015 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "application.h"
#include "mainwidget.h"
#include "photocropbox.h"
#include "fileuploader.h"

PhotoCropBox::PhotoCropBox(const QImage &img, const PeerId &peer, bool upload) : AbstractBox()
, _downState(0)
, _done(this, lang(lng_settings_save), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _img(img)
, _peerId(peer) {
	if (peerIsChannel(_peerId)) {
		_title = lang(lng_create_channel_crop);
	} else if (peerIsChat(_peerId)) {
		_title = lang(lng_create_group_crop);
	} else {
		_title = lang(lng_settings_crop_profile);
	}

	connect(&_done, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	if (_peerId && upload) {
		connect(this, SIGNAL(ready(const QImage&)), this, SLOT(onReady(const QImage&)));
	}

	int32 s = st::boxWideWidth - st::boxPhotoPadding.left() - st::boxPhotoPadding.right();
	_thumb = QPixmap::fromImage(img.scaled(s, s, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly);
	_thumbw = _thumb.width();
	_thumbh = _thumb.height();
	if (_thumbw > _thumbh) {
		_cropw = _thumbh - 20;
	} else {
		_cropw = _thumbw - 20;
	}
	_cropx = (_thumbw - _cropw) / 2;
	_cropy = (_thumbh - _cropw) / 2;

	_thumbx = (st::boxWideWidth - _thumbw) / 2;
	_thumby = st::boxPhotoPadding.top();
	setMouseTracking(true);

	resizeMaxHeight(st::boxWideWidth, st::boxPhotoPadding.top() + _thumbh + st::boxPhotoPadding.bottom() + st::boxTextFont->height + st::cropSkip + st::boxButtonPadding.top() + _done.height() + st::boxButtonPadding.bottom());
}

void PhotoCropBox::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) return LayeredWidget::mousePressEvent(e);

	_downState = mouseState(e->pos());
	_fromposx = e->pos().x();
	_fromposy = e->pos().y();
	_fromcropx = _cropx;
	_fromcropy = _cropy;
	_fromcropw = _cropw;

	return LayeredWidget::mousePressEvent(e);
}

int32 PhotoCropBox::mouseState(QPoint p) {
	p -= QPoint(_thumbx, _thumby);
	int32 delta = st::cropPointSize, mdelta(-delta / 2);
	if (QRect(_cropx + mdelta, _cropy + mdelta, delta, delta).contains(p)) {
		return 1;
	} else if (QRect(_cropx + _cropw + mdelta, _cropy + mdelta, delta, delta).contains(p)) {
		return 2;
	} else if (QRect(_cropx + _cropw + mdelta, _cropy + _cropw + mdelta, delta, delta).contains(p)) {
		return 3;
	} else if (QRect(_cropx + mdelta, _cropy + _cropw + mdelta, delta, delta).contains(p)) {
		return 4;
	} else if (QRect(_cropx, _cropy, _cropw, _cropw).contains(p)) {
		return 5;
	}
	return 0;
}

void PhotoCropBox::mouseReleaseEvent(QMouseEvent *e) {
	if (_downState) {
		_downState = 0;
		mouseMoveEvent(e);
	}
}

void PhotoCropBox::mouseMoveEvent(QMouseEvent *e) {
	if (_downState && !(e->buttons() & Qt::LeftButton)) {
		mouseReleaseEvent(e);
	}
	if (_downState) {
		if (_downState == 1) {
			int32 dx = e->pos().x() - _fromposx, dy = e->pos().y() - _fromposy, d = (dx < dy) ? dx : dy;
			if (_fromcropx + d < 0) {
				d = -_fromcropx;
			}
			if (_fromcropy + d < 0) {
				d = -_fromcropy;
			}
			if (_fromcropw - d < st::cropMinSize) {
				d = _fromcropw - st::cropMinSize;
			}
			if (_cropx != _fromcropx + d || _cropy != _fromcropy + d || _cropw != _fromcropw - d) {
				_cropx = _fromcropx + d;
				_cropy = _fromcropy + d;
				_cropw = _fromcropw - d;
				update();
			}
		} else if (_downState == 2) {
			int32 dx = _fromposx - e->pos().x(), dy = e->pos().y() - _fromposy, d = (dx < dy) ? dx : dy;
			if (_fromcropx + _fromcropw - d > _thumbw) {
				d = _fromcropx + _fromcropw - _thumbw;
			}
			if (_fromcropy + d < 0) {
				d = -_fromcropy;
			}
			if (_fromcropw - d < st::cropMinSize) {
				d = _fromcropw - st::cropMinSize;
			}
			if (_cropy != _fromcropy + d || _cropw != _fromcropw - d) {
				_cropy = _fromcropy + d;
				_cropw = _fromcropw - d;
				update();
			}
		} else if (_downState == 3) {
			int32 dx = _fromposx - e->pos().x(), dy = _fromposy - e->pos().y(), d = (dx < dy) ? dx : dy;
			if (_fromcropx + _fromcropw - d > _thumbw) {
				d = _fromcropx + _fromcropw - _thumbw;
			}
			if (_fromcropy + _fromcropw - d > _thumbh) {
				d = _fromcropy + _fromcropw - _thumbh;
			}
			if (_fromcropw - d < st::cropMinSize) {
				d = _fromcropw - st::cropMinSize;
			}
			if (_cropw != _fromcropw - d) {
				_cropw = _fromcropw - d;
				update();
			}
		} else if (_downState == 4) {
			int32 dx = e->pos().x() - _fromposx, dy = _fromposy - e->pos().y(), d = (dx < dy) ? dx : dy;
			if (_fromcropx + d < 0) {
				d = -_fromcropx;
			}
			if (_fromcropy + _fromcropw - d > _thumbh) {
				d = _fromcropy + _fromcropw - _thumbh;
			}
			if (_fromcropw - d < st::cropMinSize) {
				d = _fromcropw - st::cropMinSize;
			}
			if (_cropx != _fromcropx + d || _cropw != _fromcropw - d) {
				_cropx = _fromcropx + d;
				_cropw = _fromcropw - d;
				update();
			}
		} else if (_downState == 5) {
			int32 dx = e->pos().x() - _fromposx, dy = e->pos().y() - _fromposy;
			if (_fromcropx + dx < 0) {
				dx = -_fromcropx;
			} else if (_fromcropx + _fromcropw + dx > _thumbw) {
				dx = _thumbw - _fromcropx - _fromcropw;
			}
			if (_fromcropy + dy < 0) {
				dy = -_fromcropy;
			} else if (_fromcropy + _fromcropw + dy > _thumbh) {
				dy = _thumbh - _fromcropy - _fromcropw;
			}
			if (_cropx != _fromcropx + dx || _cropy != _fromcropy + dy) {
				_cropx = _fromcropx + dx;
				_cropy = _fromcropy + dy;
				update();
			}
		}
	}
	int32 cursorState = _downState ? _downState : mouseState(e->pos());
	QCursor cur(style::cur_default);
	if (cursorState == 1 || cursorState == 3) {
		cur = style::cur_sizefdiag;
	} else if (cursorState == 2 || cursorState == 4) {
		cur = style::cur_sizebdiag;
	} else if (cursorState == 5) {
		cur = style::cur_sizeall;
	}
	setCursor(cur);
}

void PhotoCropBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onSend();
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void PhotoCropBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;
	
	p.setFont(st::boxTextFont);
	p.setPen(st::boxPhotoTextFg);
	p.drawText(QRect(st::boxPhotoPadding.left(), st::boxPhotoPadding.top() + _thumbh + st::boxPhotoPadding.bottom(), width() - st::boxPhotoPadding.left() - st::boxPhotoPadding.right(), st::boxTextFont->height), _title, style::al_top);

	p.translate(_thumbx, _thumby);
	p.drawPixmap(0, 0, _thumb);
	p.setOpacity(0.5);
	if (_cropy > 0) {
		p.fillRect(QRect(0, 0, _cropx + _cropw, _cropy), st::black->b);
	}
	if (_cropx + _cropw < _thumbw) {
		p.fillRect(QRect(_cropx + _cropw, 0, _thumbw - _cropx - _cropw, _cropy + _cropw), st::black->b);
	}
	if (_cropy + _cropw < _thumbh) {
		p.fillRect(QRect(_cropx, _cropy + _cropw, _thumbw - _cropx, _thumbh - _cropy - _cropw), st::black->b);
	}
	if (_cropx > 0) {
		p.fillRect(QRect(0, _cropy, _cropx, _thumbh - _cropy), st::black->b);
	}

	int32 delta = st::cropPointSize, mdelta(-delta / 2);
	p.fillRect(QRect(_cropx + mdelta, _cropy + mdelta, delta, delta), st::white->b);
	p.fillRect(QRect(_cropx + _cropw + mdelta, _cropy + mdelta, delta, delta), st::white->b);
	p.fillRect(QRect(_cropx + _cropw + mdelta, _cropy + _cropw + mdelta, delta, delta), st::white->b);
	p.fillRect(QRect(_cropx + mdelta, _cropy + _cropw + mdelta, delta, delta), st::white->b);
}

void PhotoCropBox::resizeEvent(QResizeEvent *e) {
	_done.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _done.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _done.width() + st::boxButtonPadding.left(), _done.y());
}

void PhotoCropBox::onSend() {
	QImage from(_img);
	if (_img.width() < _thumb.width()) {
		from = _thumb.toImage();
	}
	float64 x = float64(_cropx) / _thumbw, y = float64(_cropy) / _thumbh, w = float64(_cropw) / _thumbw;
	int32 ix = int32(x * from.width()), iy = int32(y * from.height()), iw = int32(w * from.width());
	if (ix < 0) {
		ix = 0;
	}
	if (ix + iw > from.width()) {
		iw = from.width() - ix;
	}
	if (iy < 0) {
		iy = 0;
	}
	if (iy + iw > from.height()) {
		iw = from.height() - iy;
	}
	int32 offset = ix * from.depth() / 8 + iy * from.bytesPerLine();
	QImage cropped(from.bits() + offset, iw, iw, from.bytesPerLine(), from.format()), tosend;
	if (cropped.width() > 1280) {
		tosend = cropped.scaled(1280, 1280, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	} else if (cropped.width() < 320) {
		tosend = cropped.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	} else {
		tosend = cropped.copy();
	}

	emit ready(tosend);
	onClose();
}

void PhotoCropBox::onReady(const QImage &tosend) {
	App::app()->uploadProfilePhoto(tosend, _peerId);
}

void PhotoCropBox::hideAll() {
	_done.hide();
	_cancel.hide();
}

void PhotoCropBox::showAll() {
	_done.show();
	_cancel.show();
}
