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
#include "lang.h"

#include "connectionbox.h"
#include "mainwidget.h"
#include "window.h"

ConnectionBox::ConnectionBox() :
    _saveButton(this, lang(lng_connection_save), st::btnSelectDone),
    _cancelButton(this, lang(lng_cancel), st::btnSelectCancel),
    _hostInput(this, st::inpConnectionHost, lang(lng_connection_host_ph), cConnectionProxy().host),
    _portInput(this, st::inpConnectionPort, lang(lng_connection_port_ph), QString::number(cConnectionProxy().port)),
    _userInput(this, st::inpConnectionUser, lang(lng_connection_user_ph), cConnectionProxy().user),
	_passwordInput(this, st::inpConnectionPassword, lang(lng_connection_password_ph), cConnectionProxy().password),
	_autoRadio(this, qsl("conn_type"), dbictAuto, lang(lng_connection_auto_rb), (cConnectionType() == dbictAuto)),
	_httpProxyRadio(this, qsl("conn_type"), dbictHttpProxy, lang(lng_connection_http_proxy_rb), (cConnectionType() == dbictHttpProxy)),
	_tcpProxyRadio(this, qsl("conn_type"), dbictTcpProxy, lang(lng_connection_tcp_proxy_rb), (cConnectionType() == dbictTcpProxy)),
	a_opacity(0, 1), _hiding(false) {

	_width = st::addContactWidth;

	connect(&_saveButton, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onCancel()));

	connect(&_autoRadio, SIGNAL(changed()), this, SLOT(onChange()));
	connect(&_httpProxyRadio, SIGNAL(changed()), this, SLOT(onChange()));
	connect(&_tcpProxyRadio, SIGNAL(changed()), this, SLOT(onChange()));

	_passwordInput.setEchoMode(QLineEdit::Password);

	showAll();
	_cache = myGrab(this, rect());
	hideAll();
}

void ConnectionBox::hideAll() {
	_autoRadio.hide();
	_httpProxyRadio.hide();
	_tcpProxyRadio.hide();

	_hostInput.hide();
	_portInput.hide();
	_userInput.hide();
	_passwordInput.hide();

	_saveButton.hide();
	_cancelButton.hide();
}

void ConnectionBox::showAll() {
	_autoRadio.show();
	_httpProxyRadio.show();
	_tcpProxyRadio.show();

	_autoRadio.move(st::boxPadding.left(), st::addContactTitleHeight + st::connectionSkip);
	_httpProxyRadio.move(st::boxPadding.left(), _autoRadio.y() + _autoRadio.height() + st::connectionSkip);

	int32 inputy = 0;
	if (_httpProxyRadio.checked()) {
		inputy = _httpProxyRadio.y() + _httpProxyRadio.height() + st::boxPadding.top();
		_tcpProxyRadio.move(st::boxPadding.left(), inputy + st::boxPadding.top() + 2 * _hostInput.height() + st::connectionSkip);
	} else {
		_tcpProxyRadio.move(st::boxPadding.left(), _httpProxyRadio.y() + _httpProxyRadio.height() + st::connectionSkip);
		if (_tcpProxyRadio.checked()) {
			inputy = _tcpProxyRadio.y() + _tcpProxyRadio.height() + st::boxPadding.top();
		}
	}

	if (inputy) {
		_hostInput.show();
		_portInput.show();
		_userInput.show();
		_passwordInput.show();
		_hostInput.move(st::boxPadding.left() + st::rbDefFlat.textLeft, inputy);
		_portInput.move(_width - st::boxPadding.right() - _portInput.width(), inputy);
		_userInput.move(st::boxPadding.left() + st::rbDefFlat.textLeft, _hostInput.y() + _hostInput.height() + st::boxPadding.top());
		_passwordInput.move(_width - st::boxPadding.right() - _passwordInput.width(), _userInput.y());
	} else {
		_hostInput.hide();
		_portInput.hide();
		_userInput.hide();
		_passwordInput.hide();
	}

	_saveButton.show();
	_cancelButton.show();

	int32 buttony = (_tcpProxyRadio.checked() ? (_userInput.y() + _userInput.height()) : (_tcpProxyRadio.y() + _tcpProxyRadio.height())) + st::connectionSkip;

	_saveButton.move(_width - _saveButton.width(), buttony);
	_cancelButton.move(0, buttony);

	_height = _saveButton.y() + _saveButton.height();
	resize(_width, _height);
}

void ConnectionBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
	} else if (e->key() == Qt::Key_Escape) {
		onCancel();
	}
}

void ConnectionBox::parentResized() {
	QSize s = parentWidget()->size();
	setGeometry((s.width() - _width) / 2, (s.height() - _height) / 2, _width, _height);
	update();
}

void ConnectionBox::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	if (_cache.isNull()) {
		if (!_hiding || a_opacity.current() > 0.01) {
			// fill bg
			p.fillRect(0, 0, _width, _height, st::boxBG->b);

			// paint shadows
			p.fillRect(0, st::addContactTitleHeight, _width, st::scrollDef.topsh, st::scrollDef.shColor->b);
			p.fillRect(0, _height - st::btnSelectCancel.height - st::scrollDef.bottomsh, _width, st::scrollDef.bottomsh, st::scrollDef.shColor->b);

			// paint button sep
			p.setPen(st::btnSelectSep->p);
			p.drawLine(st::btnSelectCancel.width, _height - st::btnSelectCancel.height, st::btnSelectCancel.width, _height - 1);

			// draw box title / text
			p.setFont(st::addContactTitleFont->f);
			p.setPen(st::black->p);
			p.drawText(st::addContactTitlePos.x(), st::addContactTitlePos.y() + st::addContactTitleFont->ascent, lang(lng_connection_header));
		}
	} else {
		p.setOpacity(a_opacity.current());
		p.drawPixmap(0, 0, _cache);
	}
}

void ConnectionBox::animStep(float64 dt) {
	if (dt >= 1) {
		a_opacity.finish();
		_cache = QPixmap();
		if (!_hiding) {
			showAll();
			if (!_hostInput.isHidden()) {
				_hostInput.setFocus();
			}
		}
	} else {
		a_opacity.update(dt, anim::linear);
	}
	update();
}

void ConnectionBox::onChange() {
	showAll();
	if (_httpProxyRadio.checked() || _tcpProxyRadio.checked()) {
		_hostInput.setFocus();
		if (_httpProxyRadio.checked() && !_portInput.text().toInt()) {
			_portInput.setText(qsl("80"));
			_portInput.updatePlaceholder();
		}
	}
	update();
}

void ConnectionBox::onSave() {
	if (_httpProxyRadio.checked() || _tcpProxyRadio.checked()) {
		ConnectionProxy p;
		p.host = _hostInput.text().trimmed();
		p.user = _userInput.text().trimmed();
		p.password = _passwordInput.text().trimmed();
		p.port = _portInput.text().toInt();
		if (p.host.isEmpty()) {
			_hostInput.setFocus();
			return;
		} else if (!p.port) {
			_portInput.setFocus();
			return;
		}
		if (_httpProxyRadio.checked()) {
			cSetConnectionType(dbictHttpProxy);
		} else {
			cSetConnectionType(dbictTcpProxy);
		}
		cSetConnectionProxy(p);
	} else {
		cSetConnectionType(dbictAuto);
		cSetConnectionProxy(ConnectionProxy());
		QNetworkProxyFactory::setUseSystemConfiguration(false);
		QNetworkProxyFactory::setUseSystemConfiguration(true);
	}
	App::writeConfig();
	MTP::restart();
	emit closed();
}

void ConnectionBox::onCancel() {
	emit closed();
}

void ConnectionBox::startHide() {
	_hiding = true;
	if (_cache.isNull()) {
		_cache = myGrab(this, rect());
		hideAll();
	}
	a_opacity.start(0);
}

ConnectionBox::~ConnectionBox() {
}
