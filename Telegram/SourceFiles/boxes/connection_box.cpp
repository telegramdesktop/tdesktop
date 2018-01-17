/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/connection_box.h"

#include "data/data_photo.h"
#include "data/data_document.h"
#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "data/data_session.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "history/history_location_manager.h"
#include "styles/style_boxes.h"

void ConnectionBox::ShowApplyProxyConfirmation(const QMap<QString, QString> &fields) {
	auto server = fields.value(qsl("server"));
	auto port = fields.value(qsl("port")).toInt();
	if (!server.isEmpty() && port != 0) {
		auto weakBox = std::make_shared<QPointer<ConfirmBox>>(nullptr);
		auto box = Ui::show(Box<ConfirmBox>(lng_sure_enable_socks(lt_server, server, lt_port, QString::number(port)), lang(lng_sure_enable), [fields, weakBox] {
			auto p = ProxyData();
			p.host = fields.value(qsl("server"));
			p.user = fields.value(qsl("user"));
			p.password = fields.value(qsl("pass"));
			p.port = fields.value(qsl("port")).toInt();
			Global::SetConnectionType(dbictTcpProxy);
			Global::SetLastProxyType(dbictTcpProxy);
			Global::SetConnectionProxy(p);
			Local::writeSettings();
			Global::RefConnectionTypeChanged().notify();
			MTP::restart();
			reinitLocationManager();
			reinitWebLoadManager();
			if (*weakBox) (*weakBox)->closeBox();
		}), LayerOption::KeepOther);
		*weakBox = box;
	}
}

ConnectionBox::ConnectionBox(QWidget *parent)
: _hostInput(this, st::connectionHostInputField, langFactory(lng_connection_host_ph), Global::ConnectionProxy().host)
, _portInput(this, st::connectionPortInputField, langFactory(lng_connection_port_ph), QString::number(Global::ConnectionProxy().port))
, _userInput(this, st::connectionUserInputField, langFactory(lng_connection_user_ph), Global::ConnectionProxy().user)
, _passwordInput(this, st::connectionPasswordInputField, langFactory(lng_connection_password_ph), Global::ConnectionProxy().password)
, _typeGroup(std::make_shared<Ui::RadioenumGroup<DBIConnectionType>>(Global::ConnectionType()))
, _autoRadio(this, _typeGroup, dbictAuto, lang(lng_connection_auto_rb), st::defaultBoxCheckbox)
, _httpProxyRadio(this, _typeGroup, dbictHttpProxy, lang(lng_connection_http_proxy_rb), st::defaultBoxCheckbox)
, _tcpProxyRadio(this, _typeGroup, dbictTcpProxy, lang(lng_connection_tcp_proxy_rb), st::defaultBoxCheckbox)
, _tryIPv6(this, lang(lng_connection_try_ipv6), Global::TryIPv6(), st::defaultBoxCheckbox) {
}

void ConnectionBox::prepare() {
	setTitle(langFactory(lng_connection_header));

	addButton(langFactory(lng_connection_save), [this] { onSave(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	_typeGroup->setChangedCallback([this](DBIConnectionType value) { typeChanged(value); });

	connect(_hostInput, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_portInput, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_userInput, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_passwordInput, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(_hostInput, SIGNAL(focused()), this, SLOT(onFieldFocus()));
	connect(_portInput, SIGNAL(focused()), this, SLOT(onFieldFocus()));
	connect(_userInput, SIGNAL(focused()), this, SLOT(onFieldFocus()));
	connect(_passwordInput, SIGNAL(focused()), this, SLOT(onFieldFocus()));

	updateControlsVisibility();
}

bool ConnectionBox::badProxyValue() const {
	return (_hostInput->getLastText().isEmpty() || !_portInput->getLastText().toInt());
}

void ConnectionBox::updateControlsVisibility() {
	auto newHeight = st::boxOptionListPadding.top() + _autoRadio->heightNoMargins() + st::boxOptionListSkip + _httpProxyRadio->heightNoMargins() + st::boxOptionListSkip + _tcpProxyRadio->heightNoMargins() + st::boxOptionListSkip + st::connectionIPv6Skip + _tryIPv6->heightNoMargins() + st::defaultCheckbox.margin.bottom() + st::boxOptionListPadding.bottom() + st::boxPadding.bottom();
	if (_typeGroup->value() == dbictAuto && badProxyValue()) {
		_hostInput->hide();
		_portInput->hide();
		_userInput->hide();
		_passwordInput->hide();
	} else {
		newHeight += 2 * st::boxOptionInputSkip + 2 * _hostInput->height();
		_hostInput->show();
		_portInput->show();
		_userInput->show();
		_passwordInput->show();
	}

	setDimensions(st::boxWidth, newHeight);
	updateControlsPosition();
}

void ConnectionBox::setInnerFocus() {
	if (_typeGroup->value() == dbictAuto) {
		setFocus();
	} else {
		_hostInput->setFocusFast();
	}
}

void ConnectionBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	updateControlsPosition();
}

void ConnectionBox::updateControlsPosition() {
	auto type = _typeGroup->value();
	_autoRadio->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _autoRadio->getMargins().top() + st::boxOptionListPadding.top());
	_httpProxyRadio->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _autoRadio->bottomNoMargins() + st::boxOptionListSkip);

	auto inputy = 0;
	auto fieldsVisible = (type != dbictAuto) || (!badProxyValue() && Global::LastProxyType() != dbictAuto);
	auto fieldsBelowHttp = fieldsVisible && (type == dbictHttpProxy || (type == dbictAuto && Global::LastProxyType() == dbictHttpProxy));
	auto fieldsBelowTcp = fieldsVisible && (type == dbictTcpProxy || (type == dbictAuto && Global::LastProxyType() == dbictTcpProxy));
	if (fieldsBelowHttp) {
		inputy = _httpProxyRadio->bottomNoMargins() + st::boxOptionInputSkip;
		_tcpProxyRadio->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), inputy + st::boxOptionInputSkip + 2 * _hostInput->height() + st::boxOptionListSkip);
	} else {
		_tcpProxyRadio->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), _httpProxyRadio->bottomNoMargins() + st::boxOptionListSkip);
		if (fieldsBelowTcp) {
			inputy = _tcpProxyRadio->bottomNoMargins() + st::boxOptionInputSkip;
		}
	}

	if (inputy) {
		_hostInput->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left() + st::defaultCheck.diameter + st::defaultBoxCheckbox.textPosition.x() - st::defaultInputField.textMargins.left(), inputy);
		_portInput->moveToRight(st::boxPadding.right(), inputy);
		_userInput->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left() + st::defaultCheck.diameter + st::defaultBoxCheckbox.textPosition.x() - st::defaultInputField.textMargins.left(), _hostInput->y() + _hostInput->height() + st::boxOptionInputSkip);
		_passwordInput->moveToRight(st::boxPadding.right(), _userInput->y());
	}

	auto tryipv6y = (fieldsBelowTcp ? _userInput->bottomNoMargins() : _tcpProxyRadio->bottomNoMargins()) + st::boxOptionListSkip + st::connectionIPv6Skip;
	_tryIPv6->moveToLeft(st::boxPadding.left() + st::boxOptionListPadding.left(), tryipv6y);
}

void ConnectionBox::typeChanged(DBIConnectionType type) {
	if (type == dbictAuto) {
		setFocus();
	}
	updateControlsVisibility();
	if (type != dbictAuto) {
		Global::SetLastProxyType(type);
		if (!_hostInput->hasFocus() && !_portInput->hasFocus() && !_userInput->hasFocus() && !_passwordInput->hasFocus()) {
			_hostInput->setFocusFast();
		}
		if ((type == dbictHttpProxy) && !_portInput->getLastText().toInt()) {
			_portInput->setText(qsl("80"));
			_portInput->finishAnimating();
		}
	}
	update();
}

void ConnectionBox::onFieldFocus() {
	if (Global::LastProxyType() == dbictHttpProxy) {
		_typeGroup->setValue(dbictHttpProxy);
	} else if (Global::LastProxyType() == dbictTcpProxy) {
		_typeGroup->setValue(dbictTcpProxy);
	}
}

void ConnectionBox::onSubmit() {
	onFieldFocus();
	if (_hostInput->hasFocus()) {
		if (!_hostInput->getLastText().trimmed().isEmpty()) {
			_portInput->setFocus();
		} else {
			_hostInput->showError();
		}
	} else if (_portInput->hasFocus()) {
		if (_portInput->getLastText().trimmed().toInt() > 0) {
			_userInput->setFocus();
		} else {
			_portInput->showError();
		}
	} else if (_userInput->hasFocus()) {
		_passwordInput->setFocus();
	} else if (_passwordInput->hasFocus()) {
		if (_hostInput->getLastText().trimmed().isEmpty()) {
			_hostInput->setFocus();
			_hostInput->showError();
		} else if (_portInput->getLastText().trimmed().toInt() <= 0) {
			_portInput->setFocus();
			_portInput->showError();
		} else {
			onSave();
		}
	}
}

void ConnectionBox::onSave() {
	auto p = ProxyData();
	p.host = _hostInput->getLastText().trimmed();
	p.user = _userInput->getLastText().trimmed();
	p.password = _passwordInput->getLastText().trimmed();
	p.port = _portInput->getLastText().toInt();

	auto type = _typeGroup->value();
	if (type == dbictAuto) {
		if (p.host.isEmpty() || !p.port) {
			p = ProxyData();
		}
#ifndef TDESKTOP_DISABLE_NETWORK_PROXY
		QNetworkProxyFactory::setUseSystemConfiguration(false);
		QNetworkProxyFactory::setUseSystemConfiguration(true);
#endif // !TDESKTOP_DISABLE_NETWORK_PROXY
	} else {
		if (p.host.isEmpty()) {
			_hostInput->showError();
			return;
		} else if (!p.port) {
			_portInput->showError();
			return;
		}
		Global::SetLastProxyType(type);
	}
	Global::SetConnectionType(type);
	Global::SetConnectionProxy(p);
	if (cPlatform() == dbipWindows && Global::TryIPv6() != _tryIPv6->checked()) {
		Global::SetTryIPv6(_tryIPv6->checked());
		Local::writeSettings();
		Global::RefConnectionTypeChanged().notify();

		App::restart();
	} else {
		Global::SetTryIPv6(_tryIPv6->checked());
		Local::writeSettings();
		Global::RefConnectionTypeChanged().notify();

		MTP::restart();
		reinitLocationManager();
		reinitWebLoadManager();
		closeBox();
	}
}

AutoDownloadBox::AutoDownloadBox(QWidget *parent)
: _photoPrivate(this, lang(lng_media_auto_private_chats), !(cAutoDownloadPhoto() & dbiadNoPrivate), st::defaultBoxCheckbox)
, _photoGroups(this,  lang(lng_media_auto_groups), !(cAutoDownloadPhoto() & dbiadNoGroups), st::defaultBoxCheckbox)
, _audioPrivate(this, lang(lng_media_auto_private_chats), !(cAutoDownloadAudio() & dbiadNoPrivate), st::defaultBoxCheckbox)
, _audioGroups(this, lang(lng_media_auto_groups), !(cAutoDownloadAudio() & dbiadNoGroups), st::defaultBoxCheckbox)
, _gifPrivate(this, lang(lng_media_auto_private_chats), !(cAutoDownloadGif() & dbiadNoPrivate), st::defaultBoxCheckbox)
, _gifGroups(this, lang(lng_media_auto_groups), !(cAutoDownloadGif() & dbiadNoGroups), st::defaultBoxCheckbox)
, _gifPlay(this, lang(lng_media_auto_play), cAutoPlayGif(), st::defaultBoxCheckbox)
, _sectionHeight(st::boxTitleHeight + 2 * (st::defaultCheck.diameter + st::setLittleSkip)) {
}

void AutoDownloadBox::prepare() {
	addButton(langFactory(lng_connection_save), [this] { onSave(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	setDimensions(st::boxWidth, 3 * _sectionHeight - st::autoDownloadTopDelta + st::setLittleSkip + _gifPlay->heightNoMargins() + st::setLittleSkip);
}

void AutoDownloadBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	p.setPen(st::boxTitleFg);
	p.setFont(st::autoDownloadTitleFont);
	p.drawTextLeft(st::autoDownloadTitlePosition.x(), st::autoDownloadTitlePosition.y(), width(), lang(lng_media_auto_photo));
	p.drawTextLeft(st::autoDownloadTitlePosition.x(), _sectionHeight + st::autoDownloadTitlePosition.y(), width(), lang(lng_media_auto_audio));
	p.drawTextLeft(st::autoDownloadTitlePosition.x(), 2 * _sectionHeight + st::autoDownloadTitlePosition.y(), width(), lang(lng_media_auto_gif));
}

void AutoDownloadBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	auto top = st::boxTitleHeight - st::autoDownloadTopDelta;
	_photoPrivate->moveToLeft(st::boxTitlePosition.x(), top + st::setLittleSkip);
	_photoGroups->moveToLeft(st::boxTitlePosition.x(), _photoPrivate->bottomNoMargins() + st::setLittleSkip);

	_audioPrivate->moveToLeft(st::boxTitlePosition.x(), _sectionHeight + top + st::setLittleSkip);
	_audioGroups->moveToLeft(st::boxTitlePosition.x(), _audioPrivate->bottomNoMargins() + st::setLittleSkip);

	_gifPrivate->moveToLeft(st::boxTitlePosition.x(), 2 * _sectionHeight + top + st::setLittleSkip);
	_gifGroups->moveToLeft(st::boxTitlePosition.x(), _gifPrivate->bottomNoMargins() + st::setLittleSkip);
	_gifPlay->moveToLeft(st::boxTitlePosition.x(), _gifGroups->bottomNoMargins() + st::setLittleSkip);
}

void AutoDownloadBox::onSave() {
	auto photosChanged = false;
	auto documentsChanged = false;
	auto autoplayChanged = false;
	auto photosEnabled = false;
	auto voiceEnabled = false;
	auto animationsEnabled = false;
	auto autoDownloadPhoto = (_photoPrivate->checked() ? 0 : dbiadNoPrivate)
		| (_photoGroups->checked() ? 0 : dbiadNoGroups);
	if (cAutoDownloadPhoto() != autoDownloadPhoto) {
		const auto enabledPrivate = (cAutoDownloadPhoto() & dbiadNoPrivate)
			&& !(autoDownloadPhoto & dbiadNoPrivate);
		const auto enabledGroups = (cAutoDownloadPhoto() & dbiadNoGroups)
			&& !(autoDownloadPhoto & dbiadNoGroups);
		photosEnabled = enabledPrivate || enabledGroups;
		photosChanged = true;
		cSetAutoDownloadPhoto(autoDownloadPhoto);
	}
	auto autoDownloadAudio = (_audioPrivate->checked() ? 0 : dbiadNoPrivate)
		| (_audioGroups->checked() ? 0 : dbiadNoGroups);
	if (cAutoDownloadAudio() != autoDownloadAudio) {
		const auto enabledPrivate = (cAutoDownloadAudio() & dbiadNoPrivate)
			&& !(autoDownloadAudio & dbiadNoPrivate);
		const auto enabledGroups = (cAutoDownloadAudio() & dbiadNoGroups)
			&& !(autoDownloadAudio & dbiadNoGroups);
		voiceEnabled = enabledPrivate || enabledGroups;
		documentsChanged = true;
		cSetAutoDownloadAudio(autoDownloadAudio);
	}
	auto autoDownloadGif = (_gifPrivate->checked() ? 0 : dbiadNoPrivate)
		| (_gifGroups->checked() ? 0 : dbiadNoGroups);
	if (cAutoDownloadGif() != autoDownloadGif) {
		const auto enabledPrivate = (cAutoDownloadGif() & dbiadNoPrivate)
			&& !(autoDownloadGif & dbiadNoPrivate);
		const auto enabledGroups = (cAutoDownloadGif() & dbiadNoGroups)
			&& !(autoDownloadGif & dbiadNoGroups);
		animationsEnabled = enabledPrivate || enabledGroups;
		documentsChanged = true;
		cSetAutoDownloadGif(autoDownloadGif);
	}
	if (cAutoPlayGif() != _gifPlay->checked()) {
		cSetAutoPlayGif(_gifPlay->checked());
		if (!cAutoPlayGif()) {
			Auth().data().stopAutoplayAnimations();
		}
		autoplayChanged = true;
	}
	if (photosChanged || documentsChanged || autoplayChanged) {
		Local::writeUserSettings();
	}
	if (photosEnabled) {
		Auth().data().photoLoadSettingsChanged();
	}
	if (voiceEnabled) {
		Auth().data().voiceLoadSettingsChanged();
	}
	if (animationsEnabled) {
		Auth().data().animationLoadSettingsChanged();
	}
	closeBox();
}
