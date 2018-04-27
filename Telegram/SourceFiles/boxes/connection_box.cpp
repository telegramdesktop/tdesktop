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
#include "application.h"
#include "styles/style_boxes.h"

void ConnectionBox::ShowApplyProxyConfirmation(
		ProxyData::Type type,
		const QMap<QString, QString> &fields) {
	const auto server = fields.value(qsl("server"));
	const auto port = fields.value(qsl("port")).toUInt();
	auto proxy = ProxyData();
	proxy.type = type;
	proxy.host = server;
	proxy.port = port;
	if (type == ProxyData::Type::Socks5) {
		proxy.user = fields.value(qsl("user"));
		proxy.password = fields.value(qsl("pass"));
	} else if (type == ProxyData::Type::Mtproto) {
		proxy.password = fields.value(qsl("secret"));
	}
	if (proxy) {
		const auto box = std::make_shared<QPointer<ConfirmBox>>();
		const auto text = lng_sure_enable_socks(
			lt_server,
			server,
			lt_port,
			QString::number(port));
		*box = Ui::show(Box<ConfirmBox>(text, lang(lng_sure_enable), [=] {
			auto &proxies = Global::RefProxiesList();
			if (ranges::find(proxies, proxy) == end(proxies)) {
				proxies.insert(begin(proxies), proxy);
			}
			Global::SetSelectedProxy(proxy);
			Global::SetUseProxy(true);
			Local::writeSettings();
			Sandbox::refreshGlobalProxy();
			Global::RefConnectionTypeChanged().notify();
			MTP::restart();
			if (const auto strong = box->data()) {
				strong->closeBox();
			}
		}), LayerOption::KeepOther);
	}
}

ConnectionBox::ConnectionBox(QWidget *parent)
: _hostInput(this, st::connectionHostInputField, langFactory(lng_connection_host_ph), Global::SelectedProxy().host)
, _portInput(this, st::connectionPortInputField, langFactory(lng_connection_port_ph), QString::number(Global::SelectedProxy().port))
, _userInput(this, st::connectionUserInputField, langFactory(lng_connection_user_ph), Global::SelectedProxy().user)
, _passwordInput(this, st::connectionPasswordInputField, langFactory(lng_connection_password_ph), Global::SelectedProxy().password)
, _typeGroup(std::make_shared<Ui::RadioenumGroup<Type>>(Global::SelectedProxy().type))
, _autoRadio(this, _typeGroup, Type::None, lang(lng_connection_auto_rb), st::defaultBoxCheckbox)
, _httpProxyRadio(this, _typeGroup, Type::Http, lang(lng_connection_http_proxy_rb), st::defaultBoxCheckbox)
, _tcpProxyRadio(this, _typeGroup, Type::Socks5, lang(lng_connection_tcp_proxy_rb), st::defaultBoxCheckbox)
, _tryIPv6(this, lang(lng_connection_try_ipv6), Global::TryIPv6(), st::defaultBoxCheckbox) {
}

void ConnectionBox::prepare() {
	setTitle(langFactory(lng_connection_header));

	addButton(langFactory(lng_connection_save), [this] { onSave(); });
	addButton(langFactory(lng_cancel), [this] { closeBox(); });

	_typeGroup->setChangedCallback([this](Type value) { typeChanged(value); });

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
	if (!proxyFieldsVisible()) {
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

bool ConnectionBox::proxyFieldsVisible() const {
	return (_typeGroup->value() == ProxyData::Type::Http
		|| _typeGroup->value() == ProxyData::Type::Socks5);
}

void ConnectionBox::setInnerFocus() {
	if (proxyFieldsVisible()) {
		_hostInput->setFocusFast();
	} else {
		setFocus();
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
	auto fieldsVisible = proxyFieldsVisible();
	auto fieldsBelowHttp = fieldsVisible && (type == ProxyData::Type::Http);
	auto fieldsBelowTcp = fieldsVisible && (type == ProxyData::Type::Socks5);
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

void ConnectionBox::typeChanged(Type type) {
	if (!proxyFieldsVisible()) {
		setFocus();
	}
	updateControlsVisibility();
	if (proxyFieldsVisible()) {
		if (!_hostInput->hasFocus() && !_portInput->hasFocus() && !_userInput->hasFocus() && !_passwordInput->hasFocus()) {
			_hostInput->setFocusFast();
		}
		if ((type == Type::Http) && !_portInput->getLastText().toInt()) {
			_portInput->setText(qsl("80"));
			_portInput->finishAnimating();
		}
	}
	update();
}

void ConnectionBox::onFieldFocus() {
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
	auto proxy = ProxyData();
	proxy.host = _hostInput->getLastText().trimmed();
	proxy.user = _userInput->getLastText().trimmed();
	proxy.password = _passwordInput->getLastText().trimmed();
	proxy.port = _portInput->getLastText().toUInt();

	auto type = _typeGroup->value();
	if (type == Type::None) {
		proxy = ProxyData();
	} else if (type == Type::Mtproto) {
		proxy = Global::SelectedProxy();
	} else {
		if (proxy.host.isEmpty()) {
			_hostInput->showError();
			return;
		} else if (!proxy.port) {
			_portInput->showError();
			return;
		}
		proxy.type = type;
	}
	Global::SetSelectedProxy(proxy ? proxy : ProxyData());
	Global::SetUseProxy(proxy ? true : false);
	if (cPlatform() == dbipWindows && Global::TryIPv6() != _tryIPv6->checked()) {
		Global::SetTryIPv6(_tryIPv6->checked());
		Local::writeSettings();
		Global::RefConnectionTypeChanged().notify();

		App::restart();
	} else {
		Global::SetTryIPv6(_tryIPv6->checked());
		Local::writeSettings();
		Sandbox::refreshGlobalProxy();
		Global::RefConnectionTypeChanged().notify();

		MTP::restart();
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
