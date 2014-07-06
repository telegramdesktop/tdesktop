/*
This file is part of Telegram Desktop,
an unofficial desktop messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://tdesktop.com
*/
#include "stdafx.h"
#include "style.h"
#include "lang.h"

#include "app.h"
#include "application.h"
#include "mainwidget.h"
#include "photocropbox.h"
#include "fileuploader.h"

PhotoCropBox::PhotoCropBox(const QImage &img, const PeerId &peer) : _downState(0),
	_sendButton(this, lang(lng_settings_save), st::btnSelectDone),
	_cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
    _img(img), _peerId(peer), a_opacity(0, 1) {

	connect(&_sendButton, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onCancel()));
	if (_peerId) {
		connect(this, SIGNAL(ready(const QImage &)), this, SLOT(onReady(const QImage &)));
	}

	int32 s = st::cropBoxWidth - st::boxPadding.left() - st::boxPadding.right();
	_thumb = QPixmap::fromImage(img.scaled(s, s, Qt::KeepAspectRatio, Qt::SmoothTransformation));
	_thumbw = _thumb.width();
	_thumbh = _thumb.height();
	if (_thumbw > _thumbh) {
		_cropw = _thumbh - 20;
	} else {
		_cropw = _thumbw - 20;
	}
	_cropx = (_thumbw - _cropw) / 2;
	_cropy = (_thumbh - _cropw) / 2;
	_width = st::cropBoxWidth;
	_height = _thumbh + st::boxPadding.top() + st::boxFont->height + st::boxPadding.top() + st::boxPadding.bottom() + _sendButton.height();
	_thumbx = (_width - _thumbw) / 2;
	_thumby = st::boxPadding.top() * 2 + st::boxFont->height;
	setMouseTracking(true);

	resize(_width, _height);
}

void PhotoCropBox::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) return;

	_downState = mouseState(e->pos());
	_fromposx = e->pos().x();
	_fromposy = e->pos().y();
	_fromcropx = _cropx;
	_fromcropy = _cropy;
	_fromcropw = _cropw;
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
	} else if (e->key() == Qt::Key_Escape) {
		onCancel();
	}
}

void PhotoCropBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	_sendButton.move(_width - _sendButton.width(), _height - _sendButton.height());
	_cancelButton.move(0, _height - _cancelButton.height());
	update();
}

void PhotoCropBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.setOpacity(a_opacity.current());
	
	// fill bg
	p.fillRect(QRect(QPoint(0, 0), size()), st::boxBG->b);

	// paint shadows
	p.fillRect(0, _height - st::btnSelectCancel.height - st::scrollDef.bottomsh, _width, st::scrollDef.bottomsh, st::scrollDef.shColor->b);

	// paint button sep
	p.fillRect(st::btnSelectCancel.width, _height - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);

	p.setFont(st::boxFont->f);
	p.setPen(st::boxGrayTitle->p);
	p.drawText(QRect(st::boxPadding.left(), st::boxPadding.top(), _width - st::boxPadding.left() - st::boxPadding.right(), st::boxFont->height), lang(lng_settings_crop_profile), style::al_center);

	p.translate(_thumbx, _thumby);
	p.drawPixmap(0, 0, _thumb);
	p.setOpacity(a_opacity.current() * 0.5);
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

void PhotoCropBox::animStep(float64 ms) {
	if (ms >= 1) {
		a_opacity.finish();
	} else {
		a_opacity.update(ms, anim::linear);
	}
	_sendButton.setOpacity(a_opacity.current());
	_cancelButton.setOpacity(a_opacity.current());
	update();
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
}

void PhotoCropBox::onReady(const QImage &tosend) {
	App::app()->uploadProfilePhoto(tosend, _peerId);
	emit closed();
}

void PhotoCropBox::onCancel() {
	emit closed();
}

void PhotoCropBox::startHide() {
	_hiding = true;
	a_opacity.start(0);
}

PhotoCropBox::~PhotoCropBox() {
}
