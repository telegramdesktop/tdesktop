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
#include "lang.h"

#include "localstorage.h"

#include "connectionbox.h"
#include "mainwidget.h"
#include "window.h"

ConnectionBox::ConnectionBox() : AbstractBox(st::boxWidth),
_hostInput(this, st::connectionHostInputField, lang(lng_connection_host_ph), cConnectionProxy().host),
_portInput(this, st::connectionPortInputField, lang(lng_connection_port_ph), QString::number(cConnectionProxy().port)),
_userInput(this, st::connectionUserInputField, lang(lng_connection_user_ph), cConnectionProxy().user),
_passwordInput(this, st::connectionPasswordInputField, lang(lng_connection_password_ph), cConnectionProxy().password),
_autoRadio(this, qsl("conn_type"), dbictAuto, lang(lng_connection_auto_rb), (cConnectionType() == dbictAuto)),
_httpProxyRadio(this, qsl("conn_type"), dbictHttpProxy, lang(lng_connection_http_proxy_rb), (cConnectionType() == dbictHttpProxy)),
_tcpProxyRadio(this, qsl("conn_type"), dbictTcpProxy, lang(lng_connection_tcp_proxy_rb), (cConnectionType() == dbictTcpProxy)),
_tryIPv6(this, lang(lng_connection_try_ipv6), cTryIPv6()),
_save(this, lang(lng_connection_save), st::defaultBoxButton),
_cancel(this, lang(lng_cancel), st::cancelBoxButton) {

	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_autoRadio, SIGNAL(changed()), this, SLOT(onChange()));
	connect(&_httpProxyRadio, SIGNAL(changed()), this, SLOT(onChange()));
	connect(&_tcpProxyRadio, SIGNAL(changed()), this, SLOT(onChange()));

	connect(&_hostInput, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(&_portInput, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(&_userInput, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(&_passwordInput, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));

	prepare();
}

void ConnectionBox::hideAll() {
	_autoRadio.hide();
	_httpProxyRadio.hide();
	_tcpProxyRadio.hide();
	_tryIPv6.hide();

	_hostInput.hide();
	_portInput.hide();
	_userInput.hide();
	_passwordInput.hide();

	_save.hide();
	_cancel.hide();
}

void ConnectionBox::showAll() {
	_autoRadio.show();
	_httpProxyRadio.show();
	_tcpProxyRadio.show();
	_tryIPv6.show();

	int32 h = st::boxTitleHeight + st::boxOptionListPadding.top() + _autoRadio.height() + st::boxOptionListPadding.top() + _httpProxyRadio.height() + st::boxOptionListPadding.top() + _tcpProxyRadio.height() + st::boxOptionListPadding.top() + st::connectionIPv6Skip + _tryIPv6.height() + st::boxOptionListPadding.bottom() + st::boxPadding.bottom() + st::boxButtonPadding.top() + _save.height() + st::boxButtonPadding.bottom();
	if (_httpProxyRadio.checked() || _tcpProxyRadio.checked()) {
		h += 2 * st::boxOptionListPadding.top() + 2 * _hostInput.height();
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

	_save.show();
	_cancel.show();

	setMaxHeight(h);
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

	paintTitle(p, lang(lng_connection_header));
}

void ConnectionBox::resizeEvent(QResizeEvent *e) {
	_autoRadio.moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), st::boxTitleHeight + st::boxOptionListPadding.top());
	_httpProxyRadio.moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _autoRadio.y() + _autoRadio.height() + st::boxOptionListPadding.top());

	int32 inputy = 0;
	if (_httpProxyRadio.checked()) {
		inputy = _httpProxyRadio.y() + _httpProxyRadio.height() + st::boxOptionListPadding.top();
		_tcpProxyRadio.moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), inputy + st::boxOptionListPadding.top() + 2 * _hostInput.height() + st::boxOptionListPadding.top());
	} else {
		_tcpProxyRadio.moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _httpProxyRadio.y() + _httpProxyRadio.height() + st::boxOptionListPadding.top());
		if (_tcpProxyRadio.checked()) {
			inputy = _tcpProxyRadio.y() + _tcpProxyRadio.height() + st::boxOptionListPadding.top();
		}
	}

	if (inputy) {
		_hostInput.moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left() + st::defaultRadiobutton.textPosition.x() - st::defaultInputField.textMargins.left(), inputy);
		_portInput.moveToRight(st::boxPadding.right(), inputy);
		_userInput.moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left() + st::defaultRadiobutton.textPosition.x() - st::defaultInputField.textMargins.left(), _hostInput.y() + _hostInput.height() + st::boxOptionListPadding.top());
		_passwordInput.moveToRight(st::boxPadding.right(), _userInput.y());
	}

	int32 tryipv6y = (_tcpProxyRadio.checked() ? (_userInput.y() + _userInput.height()) : (_tcpProxyRadio.y() + _tcpProxyRadio.height())) + st::boxOptionListPadding.top() + st::connectionIPv6Skip;
	_tryIPv6.moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), tryipv6y);

	_save.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _save.width() + st::boxButtonPadding.left(), _save.y());
}

void ConnectionBox::onChange() {
	showAll();
	if (_httpProxyRadio.checked() || _tcpProxyRadio.checked()) {
		_hostInput.setFocus();
		if (_httpProxyRadio.checked() && !_portInput.getLastText().toInt()) {
			_portInput.setText(qsl("80"));
			_portInput.updatePlaceholder();
		}
	}
	update();
}

void ConnectionBox::onSubmit() {
	if (_hostInput.hasFocus()) {
		if (!_hostInput.getLastText().trimmed().isEmpty()) {
			_portInput.setFocus();
		} else {
			_hostInput.showError();
		}
	} else if (_portInput.hasFocus()) {
		if (_portInput.getLastText().trimmed().toInt() > 0) {
			_userInput.setFocus();
		} else {
			_portInput.showError();
		}
	} else if (_userInput.hasFocus()) {
		_passwordInput.setFocus();
	} else if (_passwordInput.hasFocus()) {
		if (_hostInput.getLastText().trimmed().isEmpty()) {
			_hostInput.setFocus();
			_hostInput.showError();
		} else if (_portInput.getLastText().trimmed().toInt() <= 0) {
			_portInput.setFocus();
			_portInput.showError();
		} else {
			onSave();
		}
	}
}

void ConnectionBox::onSave() {
	if (_httpProxyRadio.checked() || _tcpProxyRadio.checked()) {
		ConnectionProxy p;
		p.host = _hostInput.getLastText().trimmed();
		p.user = _userInput.getLastText().trimmed();
		p.password = _passwordInput.getLastText().trimmed();
		p.port = _portInput.getLastText().toInt();
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
	if (cPlatform() == dbipWindows && cTryIPv6() != _tryIPv6.checked()) {
		cSetTryIPv6(_tryIPv6.checked());
		Local::writeSettings();
		cSetRestarting(true);
		cSetRestartingToSettings(true);
		App::quit();
	} else {
		cSetTryIPv6(_tryIPv6.checked());
		Local::writeSettings();
		MTP::restart();
		reinitImageLinkManager();
		emit closed();
	}
}
