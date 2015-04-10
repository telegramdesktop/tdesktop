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

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "localstorage.h"

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
	_tcpProxyRadio(this, qsl("conn_type"), dbictTcpProxy, lang(lng_connection_tcp_proxy_rb), (cConnectionType() == dbictTcpProxy)) {

	connect(&_saveButton, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancelButton, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_autoRadio, SIGNAL(changed()), this, SLOT(onChange()));
	connect(&_httpProxyRadio, SIGNAL(changed()), this, SLOT(onChange()));
	connect(&_tcpProxyRadio, SIGNAL(changed()), this, SLOT(onChange()));

	_passwordInput.setEchoMode(QLineEdit::Password);

	prepare();
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

	int32 h = st::boxTitleHeight + st::connectionSkip + _autoRadio.height() + st::connectionSkip + _httpProxyRadio.height() + st::connectionSkip + _tcpProxyRadio.height() + st::connectionSkip;
	if (_httpProxyRadio.checked() || _tcpProxyRadio.checked()) {
		h += 2 * st::boxPadding.top() + 2 * _hostInput.height();
		_hostInput.show();
		_portInput.show();
		_userInput.show();
		_passwordInput.show();
	} else {
		_hostInput.hide();
		_portInput.hide();
		_userInput.hide();
		_passwordInput.hide();
	}

	_saveButton.show();
	_cancelButton.show();

	setMaxHeight(h + _saveButton.height());
	resizeEvent(0);
}

void ConnectionBox::showDone() {
	if (!_hostInput.isHidden()) {
		_hostInput.setFocus();
	}
}

void ConnectionBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(lng_connection_header), true);

	// paint shadow
	p.fillRect(0, height() - st::btnSelectCancel.height - st::scrollDef.bottomsh, width(), st::scrollDef.bottomsh, st::scrollDef.shColor->b);

	// paint button sep
	p.fillRect(st::btnSelectCancel.width, height() - st::btnSelectCancel.height, st::lineWidth, st::btnSelectCancel.height, st::btnSelectSep->b);
}

void ConnectionBox::resizeEvent(QResizeEvent *e) {
	_autoRadio.move(st::boxPadding.left(), st::boxTitleHeight + st::connectionSkip);
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
		_hostInput.move(st::boxPadding.left() + st::rbDefFlat.textLeft, inputy);
		_portInput.move(width() - st::boxPadding.right() - _portInput.width(), inputy);
		_userInput.move(st::boxPadding.left() + st::rbDefFlat.textLeft, _hostInput.y() + _hostInput.height() + st::boxPadding.top());
		_passwordInput.move(width() - st::boxPadding.right() - _passwordInput.width(), _userInput.y());
	}

	int32 buttony = (_tcpProxyRadio.checked() ? (_userInput.y() + _userInput.height()) : (_tcpProxyRadio.y() + _tcpProxyRadio.height())) + st::connectionSkip;

	_saveButton.move(width() - _saveButton.width(), buttony);
	_cancelButton.move(0, buttony);
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
	Local::writeSettings();
	MTP::restart();
	reinitImageLinkManager();
	emit closed();
}
