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
#include "base/qthelp_url.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "auth_session.h"
#include "data/data_session.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/toast/toast.h"
#include "ui/text_options.h"
#include "history/history_location_manager.h"
#include "application.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

namespace {

constexpr auto kSaveSettingsDelayedTimeout = TimeMs(1000);

class ProxyRow : public Ui::RippleButton {
public:
	using View = ProxiesBoxController::ItemView;

	ProxyRow(QWidget *parent, View &&view);

	void updateFields(View &&view);

	rpl::producer<> deleteClicks() const;
	rpl::producer<> restoreClicks() const;
	rpl::producer<> editClicks() const;

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	void setupControls(View &&view);
	int countAvailableWidth() const;

	View _view;

	Text _title;
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _edit;
	object_ptr<Ui::FadeWrapScaled<Ui::IconButton>> _delete;
	object_ptr<Ui::FadeWrapScaled<Ui::RoundButton>> _restore;
	int _skipLeft = 0;
	int _skipRight = 0;

};

class ProxiesBox : public BoxContent {
public:
	using View = ProxiesBoxController::ItemView;

	ProxiesBox(QWidget*, not_null<ProxiesBoxController*> controller);

protected:
	void prepare() override;

private:
	void setupContent();
	void createNoRowsLabel();
	void addNewProxy();
	void applyView(View &&view);
	void setupButtons(int id, not_null<ProxyRow*> button);
	int rowHeight() const;

	not_null<ProxiesBoxController*> _controller;
	object_ptr<Ui::PaddingWrap<Ui::Checkbox>> _useProxy;
	object_ptr<Ui::PaddingWrap<Ui::Checkbox>> _tryIPv6;
	base::unique_qptr<Ui::RpWidget> _noRows;
	object_ptr<Ui::VerticalLayout> _initialWrap;
	QPointer<Ui::VerticalLayout> _wrap;

	base::flat_map<int, base::unique_qptr<ProxyRow>> _rows;

};

class ProxyBox : public BoxContent {
public:
	ProxyBox(
		QWidget*,
		const ProxyData &data,
		base::lambda<void(ProxyData)> callback);

protected:
	void prepare() override;

private:
	using Type = ProxyData::Type;

	void refreshButtons();
	ProxyData collectData();
	void save();
	void share();
	void setupControls(const ProxyData &data);
	void setupTypes();
	void setupSocketAddress(const ProxyData &data);
	void setupCredentials(const ProxyData &data);
	void setupMtprotoCredentials(const ProxyData &data);

	void addLabel(
		not_null<Ui::VerticalLayout*> parent,
		const QString &text) const;

	base::lambda<void(ProxyData)> _callback;

	object_ptr<Ui::VerticalLayout> _content;

	std::shared_ptr<Ui::RadioenumGroup<Type>> _type;

	QPointer<Ui::InputField> _host;
	QPointer<Ui::PortInput> _port;
	QPointer<Ui::InputField> _user;
	QPointer<Ui::PasswordInput> _password;
	QPointer<Ui::HexInput> _secret;

	QPointer<Ui::SlideWrap<Ui::VerticalLayout>> _credentials;
	QPointer<Ui::SlideWrap<Ui::VerticalLayout>> _mtprotoCredentials;

};

ProxyRow::ProxyRow(QWidget *parent, View &&view)
: RippleButton(parent, st::proxyRowRipple)
, _edit(this, object_ptr<Ui::IconButton>(this, st::proxyRowEdit))
, _delete(this, object_ptr<Ui::IconButton>(this, st::stickersRemove))
, _restore(
	this,
	object_ptr<Ui::RoundButton>(
		this,
		langFactory(lng_proxy_undo_delete),
		st::stickersUndoRemove)) {
	setupControls(std::move(view));
}

rpl::producer<> ProxyRow::deleteClicks() const {
	return _delete->entity()->clicks();
}

rpl::producer<> ProxyRow::restoreClicks() const {
	return _restore->entity()->clicks();
}

rpl::producer<> ProxyRow::editClicks() const {
	return _edit->entity()->clicks();
}

void ProxyRow::setupControls(View &&view) {
	updateFields(std::move(view));

	_delete->finishAnimating();
	_restore->finishAnimating();
	_edit->finishAnimating();
}

int ProxyRow::countAvailableWidth() const {
	return width() - _skipLeft - _skipRight;
}

void ProxyRow::updateFields(View &&view) {
	_view = std::move(view);
	const auto endpoint = _view.host + ':' + QString::number(_view.port);
	_title.setText(
		st::proxyRowTitleStyle,
		_view.type + ' ' + textcmdLink(1, endpoint),
		Ui::ItemTextDefaultOptions());
	_delete->toggle(!_view.deleted, anim::type::instant);
	_edit->toggle(!_view.deleted, anim::type::instant);
	_restore->toggle(_view.deleted, anim::type::instant);

	setPointerCursor(!_view.deleted);

	update();
}

int ProxyRow::resizeGetHeight(int newWidth) {
	const auto result = st::proxyRowPadding.top()
		+ st::semiboldFont->height
		+ st::proxyRowSkip
		+ st::normalFont->height
		+ st::proxyRowPadding.bottom();
	auto right = st::proxyRowPadding.right();
	_delete->moveToRight(
		right,
		(result - _delete->height()) / 2,
		newWidth);
	_restore->moveToRight(
		right,
		(result - _restore->height()) / 2,
		newWidth);
	right += _delete->width();
	_edit->moveToRight(
		right,
		(result - _edit->height()) / 2,
		newWidth);
	right -= _edit->width();
	_skipRight = right;
	_skipLeft = st::proxyRowPadding.left()
		+ st::proxyRowIconSkip;
	return result;
}

void ProxyRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_view.deleted) {
		const auto ms = getms();
		paintRipple(p, 0, 0, ms);
	}

	const auto left = _skipLeft;
	const auto availableWidth = countAvailableWidth();
	auto top = st::proxyRowPadding.top();

	if (_view.deleted) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	} else if (_view.selected) {
		st::proxyRowSelectedIcon.paint(
			p,
			st::proxyRowPadding.left(),
			(height() - st::proxyRowSelectedIcon.height()) / 2,
			width());
	}

	p.setPen(st::proxyRowTitleFg);
	p.setFont(st::semiboldFont);
	p.setTextPalette(st::proxyRowTitlePalette);
	_title.drawLeftElided(p, left, top, availableWidth, width());
	top += st::semiboldFont->height + st::proxyRowSkip;

	const auto statusFg = [&] {
		switch (_view.state) {
		case View::State::Online:
			return st::proxyRowStatusFgOnline;
		case View::State::Unavailable:
			return st::proxyRowStatusFgOffline;
		default:
			return st::proxyRowStatusFg;
		}
	}();
	const auto status = [&] {
		switch (_view.state) {
		case View::State::Available:
			return lang(lng_proxy_available);
		case View::State::Checking:
			return lang(lng_proxy_available);
		case View::State::Connecting:
			return lang(lng_proxy_connecting);
		case View::State::Online:
			return lang(lng_proxy_online);
		case View::State::Unavailable:
			return lang(lng_proxy_unavailable);
		}
		Unexpected("State in ProxyRow::paintEvent.");
	}();
	p.setPen(statusFg);
	p.setFont(st::normalFont);
	p.drawTextLeft(left, top, width(), status);
	top += st::normalFont->height + st::proxyRowPadding.bottom();

}

ProxiesBox::ProxiesBox(
	QWidget*,
	not_null<ProxiesBoxController*> controller)
: _controller(controller)
, _useProxy(
	this,
	object_ptr<Ui::Checkbox>(
		this,
		lang(lng_proxy_use),
		Global::UseProxy()),
	st::proxyUsePadding)
, _tryIPv6(
	this,
	object_ptr<Ui::Checkbox>(
		this,
		lang(lng_connection_try_ipv6),
		Global::TryIPv6()),
	st::proxyTryIPv6Padding)
, _initialWrap(this) {
	_controller->views(
	) | rpl::start_with_next([=](View &&view) {
		applyView(std::move(view));
	}, lifetime());
}

void ProxiesBox::prepare() {
	setTitle(langFactory(lng_proxy_settings));

	addButton(langFactory(lng_proxy_add), [=] { addNewProxy(); });
	addButton(langFactory(lng_close), [=] { closeBox(); });

	setupContent();
}

void ProxiesBox::setupContent() {
	_useProxy->resizeToWidth(st::boxWideWidth);
	_useProxy->moveToLeft(0, 0);
	subscribe(_useProxy->entity()->checkedChanged, [=](bool checked) {
		if (!_controller->setProxyEnabled(checked)) {
			_useProxy->entity()->setChecked(false);
			addNewProxy();
		}
	});
	subscribe(_tryIPv6->entity()->checkedChanged, [=](bool checked) {
		_controller->setTryIPv6(checked);
	});
	subscribe(Global::RefConnectionTypeChanged(), [=] {
		_useProxy->entity()->setChecked(Global::UseProxy());
	});

	_tryIPv6->resizeToWidth(st::boxWideWidth);

	const auto topSkip = _useProxy->heightNoMargins();
	const auto bottomSkip = _tryIPv6->heightNoMargins();
	const auto inner = setInnerWidget(
		object_ptr<Ui::VerticalLayout>(this),
		topSkip,
		bottomSkip);
	inner->add(object_ptr<Ui::FixedHeightWidget>(
		inner,
		st::proxyRowPadding.top()));
	_wrap = inner->add(std::move(_initialWrap));
	inner->add(object_ptr<Ui::FixedHeightWidget>(
		inner,
		st::proxyRowPadding.bottom()));

	if (_rows.empty()) {
		createNoRowsLabel();
	}

	inner->resizeToWidth(st::boxWideWidth);

	inner->heightValue(
	) | rpl::map([=](int height) {
		return std::min(
			topSkip + std::max(height, 3 * rowHeight()) + bottomSkip,
			st::boxMaxListHeight);
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, inner->lifetime());

	heightValue(
	) | rpl::start_with_next([=](int height) {
		_tryIPv6->moveToLeft(0, height - _tryIPv6->heightNoMargins());
	}, _tryIPv6->lifetime());
}

int ProxiesBox::rowHeight() const {
	return st::proxyRowPadding.top()
		+ st::semiboldFont->height
		+ st::proxyRowSkip
		+ st::normalFont->height
		+ st::proxyRowPadding.bottom();
}

void ProxiesBox::addNewProxy() {
	Ui::show(_controller->addNewItemBox(), LayerOption::KeepOther);
}

void ProxiesBox::applyView(View &&view) {
	const auto id = view.id;
	const auto i = _rows.find(id);
	if (i == _rows.end()) {
		const auto wrap = _wrap
			? _wrap.data()
			: _initialWrap.data();
		const auto [i, ok] = _rows.emplace(id, nullptr);
		i->second.reset(wrap->insert(
			0,
			object_ptr<ProxyRow>(
				wrap,
				std::move(view))));
		setupButtons(id, i->second.get());
		_noRows.reset();
	} else if (view.host.isEmpty()) {
		_rows.erase(i);
	} else {
		i->second->updateFields(std::move(view));
	}
}

void ProxiesBox::createNoRowsLabel() {
	_noRows.reset(_wrap->add(
		object_ptr<Ui::FixedHeightWidget>(
			_wrap,
			rowHeight()),
		st::proxyEmptyListPadding));
	_noRows->resize(
		(st::boxWideWidth
			- st::proxyEmptyListPadding.left()
			- st::proxyEmptyListPadding.right()),
		_noRows->height());
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		_noRows.get(),
		lang(lng_proxy_description),
		Ui::FlatLabel::InitType::Simple,
		st::proxyEmptyListLabel);
	_noRows->widthValue(
	) | rpl::start_with_next([=](int width) {
		label->resizeToWidth(width);
		label->moveToLeft(0, 0);
	});
}

void ProxiesBox::setupButtons(int id, not_null<ProxyRow*> button) {
	button->deleteClicks(
	) | rpl::start_with_next([=] {
		_controller->deleteItem(id);
	}, button->lifetime());

	button->restoreClicks(
	) | rpl::start_with_next([=] {
		_controller->restoreItem(id);
	}, button->lifetime());

	button->editClicks(
	) | rpl::start_with_next([=] {
		Ui::show(_controller->editItemBox(id), LayerOption::KeepOther);
	}, button->lifetime());

	button->clicks(
	) | rpl::start_with_next([=] {
		_controller->applyItem(id);
	}, button->lifetime());
}

ProxyBox::ProxyBox(
	QWidget*,
	const ProxyData &data,
	base::lambda<void(ProxyData)> callback)
: _callback(std::move(callback))
, _content(this) {
	setupControls(data);
}

void ProxyBox::prepare() {
	setTitle(langFactory(lng_proxy_edit));

	refreshButtons();

	_content->heightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWidth, height);
	}, _content->lifetime());
}

void ProxyBox::refreshButtons() {
	clearButtons();
	addButton(langFactory(lng_settings_save), [=] { save(); });
	addButton(langFactory(lng_cancel), [=] { closeBox(); });

	const auto type = _type->value();
	if (type == Type::Socks5 || type == Type::Mtproto) {
		addLeftButton(langFactory(lng_proxy_share), [=] { share(); });
	}
}

void ProxyBox::save() {
	if (const auto data = collectData()) {
		_callback(data);
		closeBox();
	}
}

void ProxyBox::share() {
	if (const auto data = collectData()) {
		if (data.type == Type::Http) {
			return;
		}
		const auto link = qsl("https://t.me/")
			+ (data.type == Type::Socks5 ? "socks" : "proxy")
			+ "?server=" + data.host + "&port=" + QString::number(data.port)
			+ ((data.type == Type::Socks5 && !data.user.isEmpty())
				? "&user=" + qthelp::url_encode(data.user) : "")
			+ ((data.type == Type::Socks5 && !data.password.isEmpty())
				? "&pass=" + qthelp::url_encode(data.password) : "")
			+ ((data.type == Type::Mtproto && !data.password.isEmpty())
				? "&secret=" + data.password : "");
		Application::clipboard()->setText(link);
		Ui::Toast::Show(lang(lng_username_copied));
	}
}

ProxyData ProxyBox::collectData() {
	auto result = ProxyData();
	result.type = _type->value();
	result.host = _host->getLastText().trimmed();
	result.port = _port->getLastText().trimmed().toInt();
	result.user = (result.type == Type::Mtproto)
		? QString()
		: _user->getLastText();
	result.password = (result.type == Type::Mtproto)
		? _secret->getLastText()
		: _password->getLastText();
	if (result.host.isEmpty()) {
		_host->showError();
	} else if (!result.port) {
		_port->showError();
	} else if ((result.type == Type::Http || result.type == Type::Socks5)
		&& !result.password.isEmpty() && result.user.isEmpty()) {
		_user->showError();
	} else if (result.type == Type::Mtproto
		&& result.password.size() != 32) {
		_secret->showError();
	} else if (!result) {
		_host->showError();
	} else {
		return result;
	}
	return ProxyData();
}

void ProxyBox::setupTypes() {
	const auto types = std::map<Type, QString>{
		{ Type::Http, "HTTP" },
		{ Type::Socks5, "SOCKS5" },
		{ Type::Mtproto, "MTPROTO" },
	};
	for (const auto [type, label] : types) {
		_content->add(
			object_ptr<Ui::Radioenum<Type>>(
				_content,
				_type,
				type,
				label),
			st::proxyEditTypePadding);
	}
}

void ProxyBox::setupSocketAddress(const ProxyData &data) {
	addLabel(_content, lang(lng_proxy_address_label));
	const auto address = _content->add(
		object_ptr<Ui::FixedHeightWidget>(
			_content,
			st::connectionHostInputField.heightMin),
		st::proxyEditInputPadding);
	_host = Ui::CreateChild<Ui::InputField>(
		address,
		st::connectionHostInputField,
		langFactory(lng_connection_host_ph),
		data.host);
	_port = Ui::CreateChild<Ui::PortInput>(
		address,
		st::connectionPortInputField,
		langFactory(lng_connection_port_ph),
		data.port ? QString::number(data.port) : QString());
	address->widthValue(
	) | rpl::start_with_next([=](int width) {
		_port->moveToRight(0, 0);
		_host->resize(
			width - _port->width() - st::proxyEditSkip,
			_host->height());
		_host->moveToLeft(0, 0);
	}, address->lifetime());
}

void ProxyBox::setupCredentials(const ProxyData &data) {
		_credentials = _content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_content,
			object_ptr<Ui::VerticalLayout>(_content)));
	const auto credentials = _credentials->entity();
	addLabel(credentials, lang(lng_proxy_credentials_optional));
	_user = credentials->add(
		object_ptr<Ui::InputField>(
			credentials,
			st::connectionUserInputField,
			langFactory(lng_connection_user_ph),
			data.user),
		st::proxyEditInputPadding);

	auto passwordWrap = object_ptr<Ui::RpWidget>(credentials);
	_password = Ui::CreateChild<Ui::PasswordInput>(
		passwordWrap.data(),
		st::connectionPasswordInputField,
		langFactory(lng_connection_password_ph),
		(data.type == Type::Mtproto) ? QString() : data.password);
	_password->move(0, 0);
	_password->heightValue(
	) | rpl::start_with_next([=, wrap = passwordWrap.data()](int height) {
		wrap->resize(wrap->width(), height);
	}, _password->lifetime());
	passwordWrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		_password->resize(width, _password->height());
	}, _password->lifetime());
	credentials->add(std::move(passwordWrap), st::proxyEditInputPadding);
}

void ProxyBox::setupMtprotoCredentials(const ProxyData &data) {
	_mtprotoCredentials = _content->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_content,
			object_ptr<Ui::VerticalLayout>(_content)));
	const auto mtproto = _mtprotoCredentials->entity();
	addLabel(mtproto, lang(lng_proxy_credentials));

	auto secretWrap = object_ptr<Ui::RpWidget>(mtproto);
	_secret = Ui::CreateChild<Ui::HexInput>(
		secretWrap.data(),
		st::connectionUserInputField,
		langFactory(lng_connection_proxy_secret_ph),
		(data.type == Type::Mtproto) ? data.password : QString());
	_secret->setMaxLength(32);
	_secret->move(0, 0);
	_secret->heightValue(
	) | rpl::start_with_next([=, wrap = secretWrap.data()](int height) {
		wrap->resize(wrap->width(), height);
	}, _secret->lifetime());
	secretWrap->widthValue(
	) | rpl::start_with_next([=](int width) {
		_secret->resize(width, _secret->height());
	}, _secret->lifetime());
	mtproto->add(std::move(secretWrap), st::proxyEditInputPadding);
}

void ProxyBox::setupControls(const ProxyData &data) {
	_type = std::make_shared<Ui::RadioenumGroup<Type>>(
		(data.type == Type::None
			? Type::Socks5
			: data.type));
	_content.create(this);
	_content->resizeToWidth(st::boxWidth);
	_content->moveToLeft(0, 0);

	setupTypes();
	setupSocketAddress(data);
	setupCredentials(data);
	setupMtprotoCredentials(data);

	_content->resizeToWidth(st::boxWidth);

	const auto handleType = [=](Type type) {
		_credentials->toggle(
			type == Type::Http || type == Type::Socks5,
			anim::type::instant);
		_mtprotoCredentials->toggle(
			type == Type::Mtproto,
			anim::type::instant);
	};
	_type->setChangedCallback([=](Type type) {
		handleType(type);
		refreshButtons();
	});
	handleType(_type->value());
}

void ProxyBox::addLabel(
		not_null<Ui::VerticalLayout*> parent,
		const QString &text) const {
	parent->add(
		object_ptr<Ui::FlatLabel>(
			parent,
			text,
			Ui::FlatLabel::InitType::Simple,
			st::proxyEditTitle),
		st::proxyEditTitlePadding);
}

} // namespace

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
				proxies.push_back(proxy);
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

ProxiesBoxController::ProxiesBoxController()
: _saveTimer([] { Local::writeSettings(); }) {
	_list = ranges::view::all(
		Global::ProxiesList()
	) | ranges::view::transform([&](const ProxyData &proxy) {
		return Item{ ++_idCounter, proxy };
	}) | ranges::to_vector;
}

object_ptr<BoxContent> ProxiesBoxController::CreateOwningBox() {
	auto controller = std::make_unique<ProxiesBoxController>();
	auto box = controller->create();
	Ui::AttachAsChild(box, std::move(controller));
	return box;
}

object_ptr<BoxContent> ProxiesBoxController::create() {
	auto result = Box<ProxiesBox>(this);
	for (const auto &item : base::reversed(_list)) {
		updateView(item);
	}
	return std::move(result);
}

auto ProxiesBoxController::findById(int id) -> std::vector<Item>::iterator {
	const auto result = ranges::find(
		_list,
		id,
		[](const Item &item) { return item.id; });
	Assert(result != end(_list));
	return result;
}

auto ProxiesBoxController::findByProxy(const ProxyData &proxy)
->std::vector<Item>::iterator {
	return ranges::find(
		_list,
		proxy,
		[](const Item &item) { return item.data; });
}

void ProxiesBoxController::deleteItem(int id) {
	setDeleted(id, true);
}

void ProxiesBoxController::restoreItem(int id) {
	setDeleted(id, false);
}

void ProxiesBoxController::applyItem(int id) {
	auto item = findById(id);
	if (Global::UseProxy() && Global::SelectedProxy() == item->data) {
		return;
	} else if (item->deleted) {
		return;
	}

	auto j = findByProxy(Global::SelectedProxy());

	Global::SetSelectedProxy(item->data);
	Global::SetUseProxy(true);
	applyChanges();

	if (j != end(_list)) {
		updateView(*j);
	}
	updateView(*item);
}

void ProxiesBoxController::setDeleted(int id, bool deleted) {
	auto item = findById(id);
	item->deleted = deleted;

	if (deleted) {
		auto &proxies = Global::RefProxiesList();
		proxies.erase(ranges::remove(proxies, item->data), end(proxies));

		if (item->data == Global::SelectedProxy()) {
			_lastSelectedProxy = base::take(Global::RefSelectedProxy());
			if (Global::UseProxy()) {
				_lastSelectedProxyUsed = true;
				Global::SetUseProxy(false);
				applyChanges();
			} else {
				_lastSelectedProxyUsed = false;
			}
		}
	} else {
		auto &proxies = Global::RefProxiesList();
		if (ranges::find(proxies, item->data) == end(proxies)) {
			auto insertBefore = item + 1;
			while (insertBefore != end(_list) && insertBefore->deleted) {
				++insertBefore;
			}
			auto insertBeforeIt = (insertBefore == end(_list))
				? end(proxies)
				: ranges::find(proxies, insertBefore->data);
			proxies.insert(insertBeforeIt, item->data);
		}

		if (!Global::SelectedProxy() && _lastSelectedProxy == item->data) {
			Assert(!Global::UseProxy());

			Global::SetSelectedProxy(base::take(_lastSelectedProxy));
			if (base::take(_lastSelectedProxyUsed)) {
				Global::SetUseProxy(true);
				applyChanges();
			}
		}
	}
	saveDelayed();
	updateView(*item);
}

object_ptr<BoxContent> ProxiesBoxController::editItemBox(int id) {
	return Box<ProxyBox>(findById(id)->data, [=](const ProxyData &result) {
		auto i = findById(id);
		auto j = ranges::find(
			_list,
			result,
			[](const Item &item) { return item.data; });
		if (j != end(_list) && j != i) {
			replaceItemWith(i, j);
		} else {
			replaceItemValue(i, result);
		}
	});
}

void ProxiesBoxController::replaceItemWith(
		std::vector<Item>::iterator which,
		std::vector<Item>::iterator with) {
	auto &proxies = Global::RefProxiesList();
	proxies.erase(ranges::remove(proxies, which->data), end(proxies));

	_views.fire({ which->id });
	_list.erase(which);

	if (with->deleted) {
		restoreItem(with->id);
	}
	applyItem(with->id);
	saveDelayed();
}

void ProxiesBoxController::replaceItemValue(
		std::vector<Item>::iterator which,
		const ProxyData &proxy) {
	if (which->deleted) {
		restoreItem(which->id);
	}

	auto &proxies = Global::RefProxiesList();
	const auto i = ranges::find(proxies, which->data);
	Assert(i != end(proxies));
	*i = proxy;
	which->data = proxy;

	applyItem(which->id);
	saveDelayed();
}

object_ptr<BoxContent> ProxiesBoxController::addNewItemBox() {
	return Box<ProxyBox>(ProxyData(), [=](const ProxyData &result) {
		auto j = ranges::find(
			_list,
			result,
			[](const Item &item) { return item.data; });
		if (j != end(_list)) {
			if (j->deleted) {
				restoreItem(j->id);
			}
			applyItem(j->id);
		} else {
			addNewItem(result);
		}
	});
}

void ProxiesBoxController::addNewItem(const ProxyData &proxy) {
	auto &proxies = Global::RefProxiesList();
	proxies.push_back(proxy);

	_list.push_back({ ++_idCounter, proxy });
	applyItem(_list.back().id);
}

bool ProxiesBoxController::setProxyEnabled(bool enabled) {
	if (enabled) {
		if (Global::ProxiesList().empty()) {
			return false;
		} else if (!Global::SelectedProxy()) {
			Global::SetSelectedProxy(Global::ProxiesList().back());
			auto j = findByProxy(Global::SelectedProxy());
			if (j != end(_list)) {
				updateView(*j);
			}
		}
	}
	Global::SetUseProxy(enabled);
	applyChanges();
	return true;
}

void ProxiesBoxController::setTryIPv6(bool enabled) {
	if (Global::TryIPv6() == enabled) {
		return;
	}
	Global::SetTryIPv6(enabled);
	applyChanges();
}

void ProxiesBoxController::applyChanges() {
	Sandbox::refreshGlobalProxy();
	Global::RefConnectionTypeChanged().notify();
	MTP::restart();
	saveDelayed();
}

void ProxiesBoxController::saveDelayed() {
	_saveTimer.callOnce(kSaveSettingsDelayedTimeout);
}

auto ProxiesBoxController::views() const -> rpl::producer<ItemView> {
	return _views.events();
}

void ProxiesBoxController::updateView(const Item &item) {
	const auto state = ItemView::State::Checking;
	const auto ping = 0;
	const auto selected = (Global::SelectedProxy() == item.data);
	const auto deleted = item.deleted;
	const auto type = [&] {
		switch (item.data.type) {
		case ProxyData::Type::Http:
			return qsl("HTTP");
		case ProxyData::Type::Socks5:
			return qsl("SOCKS5");
		case ProxyData::Type::Mtproto:
			return qsl("MTPROTO");
		}
		Unexpected("Proxy type in ProxiesBoxController::updateView.");
	}();
	_views.fire({
		item.id,
		type,
		item.data.host,
		item.data.port,
		ping,
		!deleted && selected,
		deleted,
		state });
}

ProxiesBoxController::~ProxiesBoxController() {
	if (_saveTimer.isActive()) {
		App::CallDelayed(
			kSaveSettingsDelayedTimeout,
			QApplication::instance(),
			[] { Local::writeSettings(); });
	}
}
