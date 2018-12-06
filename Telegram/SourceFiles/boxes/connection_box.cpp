/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "boxes/connection_box.h"

#include "boxes/confirm_box.h"
#include "lang/lang_keys.h"
#include "storage/localstorage.h"
#include "base/qthelp_url.h"
#include "messenger.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/toast/toast.h"
#include "ui/effects/radial_animation.h"
#include "ui/text_options.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"

namespace {

constexpr auto kSaveSettingsDelayedTimeout = TimeMs(1000);

class ProxyRow : public Ui::RippleButton {
public:
	using View = ProxiesBoxController::ItemView;
	using State = ProxiesBoxController::ItemState;

	ProxyRow(QWidget *parent, View &&view);

	void updateFields(View &&view);

	rpl::producer<> deleteClicks() const;
	rpl::producer<> restoreClicks() const;
	rpl::producer<> editClicks() const;
	rpl::producer<> shareClicks() const;

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	void setupControls(View &&view);
	int countAvailableWidth() const;
	void step_radial(TimeMs ms, bool timer);
	void paintCheck(Painter &p, TimeMs ms);
	void showMenu();

	View _view;

	Text _title;
	object_ptr<Ui::IconButton> _menuToggle;
	rpl::event_stream<> _deleteClicks;
	rpl::event_stream<> _restoreClicks;
	rpl::event_stream<> _editClicks;
	rpl::event_stream<> _shareClicks;
	base::unique_qptr<Ui::DropdownMenu> _menu;

	bool _set = false;
	Animation _toggled;
	Animation _setAnimation;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _progress;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _checking;

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
	void refreshProxyForCalls();

	not_null<ProxiesBoxController*> _controller;
	QPointer<Ui::Checkbox> _tryIPv6;
	std::shared_ptr<Ui::RadioenumGroup<ProxyData::Settings>> _proxySettings;
	QPointer<Ui::SlideWrap<Ui::Checkbox>> _proxyForCalls;
	QPointer<Ui::DividerLabel> _about;
	base::unique_qptr<Ui::RpWidget> _noRows;
	object_ptr<Ui::VerticalLayout> _initialWrap;
	QPointer<Ui::VerticalLayout> _wrap;
	int _currentProxySupportsCallsId = 0;

	base::flat_map<int, base::unique_qptr<ProxyRow>> _rows;

};

class ProxyBox : public BoxContent {
public:
	ProxyBox(
		QWidget*,
		const ProxyData &data,
		Fn<void(ProxyData)> callback,
		Fn<void(ProxyData)> shareCallback);

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

	Fn<void(ProxyData)> _callback;
	Fn<void(ProxyData)> _shareCallback;

	object_ptr<Ui::VerticalLayout> _content;

	std::shared_ptr<Ui::RadioenumGroup<Type>> _type;

	QPointer<Ui::SlideWrap<>> _aboutSponsored;
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
, _menuToggle(this, st::topBarMenuToggle) {
	setupControls(std::move(view));
}

rpl::producer<> ProxyRow::deleteClicks() const {
	return _deleteClicks.events();
}

rpl::producer<> ProxyRow::restoreClicks() const {
	return _restoreClicks.events();
}

rpl::producer<> ProxyRow::editClicks() const {
	return _editClicks.events();
}

rpl::producer<> ProxyRow::shareClicks() const {
	return _shareClicks.events();
}

void ProxyRow::setupControls(View &&view) {
	updateFields(std::move(view));
	_toggled.finish();
	_setAnimation.finish();

	_menuToggle->addClickHandler([=] { showMenu(); });
}

int ProxyRow::countAvailableWidth() const {
	return width() - _skipLeft - _skipRight;
}

void ProxyRow::updateFields(View &&view) {
	if (_view.selected != view.selected) {
		_toggled.start(
			[=] { update(); },
			view.selected ? 0. : 1.,
			view.selected ? 1. : 0.,
			st::defaultRadio.duration);
	}
	_view = std::move(view);
	const auto endpoint = _view.host + ':' + QString::number(_view.port);
	_title.setText(
		st::proxyRowTitleStyle,
		_view.type + ' ' + textcmdLink(1, endpoint),
		Ui::ItemTextDefaultOptions());

	const auto state = _view.state;
	if (state == State::Connecting) {
		if (!_progress) {
			_progress = std::make_unique<Ui::InfiniteRadialAnimation>(
				animation(this, &ProxyRow::step_radial),
				st::proxyCheckingAnimation);
		}
		_progress->start();
	} else if (_progress) {
		_progress->stop();
	}
	if (state == State::Checking) {
		if (!_checking) {
			_checking = std::make_unique<Ui::InfiniteRadialAnimation>(
				animation(this, &ProxyRow::step_radial),
				st::proxyCheckingAnimation);
			_checking->start();
		}
	} else {
		_checking = nullptr;
	}
	const auto set = (state == State::Connecting || state == State::Online);
	if (_set != set) {
		_set = set;
		_setAnimation.start(
			[=] { update(); },
			_set ? 0. : 1.,
			_set ? 1. : 0.,
			st::defaultRadio.duration);
	}

	setPointerCursor(!_view.deleted);

	update();
}

void ProxyRow::step_radial(TimeMs ms, bool timer) {
	if (timer && !anim::Disabled()) {
		update();
	}
}

int ProxyRow::resizeGetHeight(int newWidth) {
	const auto result = st::proxyRowPadding.top()
		+ st::semiboldFont->height
		+ st::proxyRowSkip
		+ st::normalFont->height
		+ st::proxyRowPadding.bottom();
	auto right = st::proxyRowPadding.right();
	_menuToggle->moveToRight(
		right,
		(result - _menuToggle->height()) / 2,
		newWidth);
	right += _menuToggle->width();
	_skipRight = right;
	_skipLeft = st::proxyRowPadding.left()
		+ st::proxyRowIconSkip;
	return result;
}

void ProxyRow::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto ms = getms();
	if (!_view.deleted) {
		paintRipple(p, 0, 0, ms);
	}

	const auto left = _skipLeft;
	const auto availableWidth = countAvailableWidth();
	auto top = st::proxyRowPadding.top();

	if (_view.deleted) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	}

	paintCheck(p, ms);

	p.setPen(st::proxyRowTitleFg);
	p.setFont(st::semiboldFont);
	p.setTextPalette(st::proxyRowTitlePalette);
	_title.drawLeftElided(p, left, top, availableWidth, width());
	top += st::semiboldFont->height + st::proxyRowSkip;

	const auto statusFg = [&] {
		switch (_view.state) {
		case State::Online:
			return st::proxyRowStatusFgOnline;
		case State::Unavailable:
			return st::proxyRowStatusFgOffline;
		case State::Available:
			return st::proxyRowStatusFgAvailable;
		default:
			return st::proxyRowStatusFg;
		}
	}();
	const auto status = [&] {
		switch (_view.state) {
		case State::Available:
			return lng_proxy_available(
				lt_ping,
				QString::number(_view.ping));
		case State::Checking:
			return lang(lng_proxy_checking);
		case State::Connecting:
			return lang(lng_proxy_connecting);
		case State::Online:
			return lang(lng_proxy_online);
		case State::Unavailable:
			return lang(lng_proxy_unavailable);
		}
		Unexpected("State in ProxyRow::paintEvent.");
	}();
	p.setPen(_view.deleted ? st::proxyRowStatusFg : statusFg);
	p.setFont(st::normalFont);

	auto statusLeft = left;
	if (_checking) {
		_checking->step(ms);
		if (_checking) {
			_checking->draw(
				p,
				{
					st::proxyCheckingPosition.x() + statusLeft,
					st::proxyCheckingPosition.y() + top
				},
				width());
			statusLeft += st::proxyCheckingPosition.x()
				+ st::proxyCheckingAnimation.size.width()
				+ st::proxyCheckingSkip;
		}
	}
	p.drawTextLeft(statusLeft, top, width(), status);
	top += st::normalFont->height + st::proxyRowPadding.bottom();
}

void ProxyRow::paintCheck(Painter &p, TimeMs ms) {
	if (_progress) {
		_progress->step(ms);
	}
	const auto loading = _progress
		? _progress->computeState()
		: Ui::InfiniteRadialAnimation::State{ 0., 0, FullArcLength };
	const auto toggled = _toggled.current(ms, _view.selected ? 1. : 0.)
		* (1. - loading.shown);
	const auto _st = &st::defaultRadio;
	const auto set = _setAnimation.current(ms, _set ? 1. : 0.);

	PainterHighQualityEnabler hq(p);

	const auto left = st::proxyRowPadding.left();
	const auto top = (height() - _st->diameter - _st->thickness) / 2;
	const auto outerWidth = width();

	auto pen = anim::pen(_st->untoggledFg, _st->toggledFg, toggled * set);
	pen.setWidth(_st->thickness);
	p.setPen(pen);
	p.setBrush(_st->bg);
	const auto rect = rtlrect(QRectF(left, top, _st->diameter, _st->diameter).marginsRemoved(QMarginsF(_st->thickness / 2., _st->thickness / 2., _st->thickness / 2., _st->thickness / 2.)), outerWidth);
	if (_progress && loading.shown > 0 && anim::Disabled()) {
		anim::DrawStaticLoading(
			p,
			rect,
			_st->thickness,
			pen.color(),
			_st->bg);
	} else if (loading.arcLength < FullArcLength) {
		p.drawArc(rect, loading.arcFrom, loading.arcLength);
	} else {
		p.drawEllipse(rect);
	}

	if (toggled > 0 && (!_progress || !anim::Disabled())) {
		p.setPen(Qt::NoPen);
		p.setBrush(anim::brush(_st->untoggledFg, _st->toggledFg, toggled * set));

		auto skip0 = _st->diameter / 2., skip1 = _st->skip / 10., checkSkip = skip0 * (1. - toggled) + skip1 * toggled;
		p.drawEllipse(rtlrect(QRectF(left, top, _st->diameter, _st->diameter).marginsRemoved(QMarginsF(checkSkip, checkSkip, checkSkip, checkSkip)), outerWidth));
	}
}

void ProxyRow::showMenu() {
	if (_menu) {
		return;
	}
	_menu = base::make_unique_q<Ui::DropdownMenu>(window());
	const auto weak = _menu.get();
	_menu->setHiddenCallback([=] {
		weak->deleteLater();
		if (_menu == weak) {
			_menuToggle->setForceRippled(false);
		}
	});
	_menu->setShowStartCallback([=] {
		if (_menu == weak) {
			_menuToggle->setForceRippled(true);
		}
	});
	_menu->setHideStartCallback([=] {
		if (_menu == weak) {
			_menuToggle->setForceRippled(false);
		}
	});
	_menuToggle->installEventFilter(_menu);
	const auto addAction = [&](
			const QString &text,
			Fn<void()> callback) {
		return _menu->addAction(text, std::move(callback));
	};
	addAction(lang(lng_proxy_menu_edit), [=] {
		_editClicks.fire({});
	});
	if (_view.supportsShare) {
		addAction(lang(lng_proxy_edit_share), [=] {
			_shareClicks.fire({});
		});
	}
	if (_view.deleted) {
		addAction(lang(lng_proxy_menu_restore), [=] {
			_restoreClicks.fire({});
		});
	} else {
		addAction(lang(lng_proxy_menu_delete), [=] {
			_deleteClicks.fire({});
		});
	}
	const auto parentTopLeft = window()->mapToGlobal({ 0, 0 });
	const auto buttonTopLeft = _menuToggle->mapToGlobal({ 0, 0 });
	const auto parent = QRect(parentTopLeft, window()->size());
	const auto button = QRect(buttonTopLeft, _menuToggle->size());
	const auto bottom = button.y()
		+ st::proxyDropdownDownPosition.y()
		+ _menu->height()
		- parent.y();
	const auto top = button.y()
		+ st::proxyDropdownUpPosition.y()
		- _menu->height()
		- parent.y();
	if (bottom > parent.height() && top >= 0) {
		const auto left = button.x()
			+ button.width()
			+ st::proxyDropdownUpPosition.x()
			- _menu->width()
			- parent.x();
		_menu->move(left, top);
		_menu->showAnimated(Ui::PanelAnimation::Origin::BottomRight);
	} else {
		const auto left = button.x()
			+ button.width()
			+ st::proxyDropdownDownPosition.x()
			- _menu->width()
			- parent.x();
		_menu->move(left, bottom - _menu->height());
		_menu->showAnimated(Ui::PanelAnimation::Origin::TopRight);
	}
}

ProxiesBox::ProxiesBox(
	QWidget*,
	not_null<ProxiesBoxController*> controller)
: _controller(controller)
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
	const auto inner = setInnerWidget(object_ptr<Ui::VerticalLayout>(this));

	_tryIPv6 = inner->add(
		object_ptr<Ui::Checkbox>(
			inner,
			lang(lng_connection_try_ipv6),
			Global::TryIPv6()),
		st::proxyTryIPv6Padding);
	_proxySettings
		= std::make_shared<Ui::RadioenumGroup<ProxyData::Settings>>(
			Global::ProxySettings());
	inner->add(
		object_ptr<Ui::Radioenum<ProxyData::Settings>>(
			inner,
			_proxySettings,
			ProxyData::Settings::Disabled,
			lang(lng_proxy_disable)),
		st::proxyUsePadding);
	inner->add(
		object_ptr<Ui::Radioenum<ProxyData::Settings>>(
			inner,
			_proxySettings,
			ProxyData::Settings::System,
			lang(lng_proxy_use_system_settings)),
		st::proxyUsePadding);
	inner->add(
		object_ptr<Ui::Radioenum<ProxyData::Settings>>(
			inner,
			_proxySettings,
			ProxyData::Settings::Enabled,
			lang(lng_proxy_use_custom)),
		st::proxyUsePadding);
	_proxyForCalls = inner->add(
		object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
			inner,
			object_ptr<Ui::Checkbox>(
				inner,
				lang(lng_proxy_use_for_calls),
				Global::UseProxyForCalls()),
			style::margins(
				0,
				st::proxyUsePadding.top(),
				0,
				st::proxyUsePadding.bottom())),
		style::margins(
			st::proxyTryIPv6Padding.left(),
			0,
			st::proxyTryIPv6Padding.right(),
			st::proxyTryIPv6Padding.top()));

	_about = inner->add(
		object_ptr<Ui::DividerLabel>(
			inner,
			object_ptr<Ui::FlatLabel>(
				inner,
				lang(lng_proxy_about),
				Ui::FlatLabel::InitType::Simple,
				st::boxDividerLabel),
			st::proxyAboutPadding),
		style::margins(0, 0, 0, st::proxyRowPadding.top()));

	_wrap = inner->add(std::move(_initialWrap));
	inner->add(object_ptr<Ui::FixedHeightWidget>(
		inner,
		st::proxyRowPadding.bottom()));

	_proxySettings->setChangedCallback([=](ProxyData::Settings value) {
		if (!_controller->setProxySettings(value)) {
			_proxySettings->setValue(Global::ProxySettings());
			addNewProxy();
		}
		refreshProxyForCalls();
	});
	_tryIPv6->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		_controller->setTryIPv6(checked);
	}, _tryIPv6->lifetime());

	_controller->proxySettingsValue(
	) | rpl::start_with_next([=](ProxyData::Settings value) {
		_proxySettings->setValue(value);
	}, inner->lifetime());

	_proxyForCalls->entity()->checkedChanges(
	) | rpl::start_with_next([=](bool checked) {
		_controller->setProxyForCalls(checked);
	}, _proxyForCalls->lifetime());

	if (_rows.empty()) {
		createNoRowsLabel();
	}
	refreshProxyForCalls();
	_proxyForCalls->finishAnimating();

	inner->resizeToWidth(st::boxWideWidth);

	inner->heightValue(
	) | rpl::map([=](int height) {
		return std::min(
			std::max(height, _about->y()
				+ _about->height()
				+ 3 * rowHeight()),
			st::boxMaxListHeight);
	}) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, inner->lifetime());
}

void ProxiesBox::refreshProxyForCalls() {
	if (!_proxyForCalls) {
		return;
	}
	_proxyForCalls->toggle(
		(_proxySettings->value() == ProxyData::Settings::Enabled
			&& _currentProxySupportsCallsId != 0),
		anim::type::normal);
}

int ProxiesBox::rowHeight() const {
	return st::proxyRowPadding.top()
		+ st::semiboldFont->height
		+ st::proxyRowSkip
		+ st::normalFont->height
		+ st::proxyRowPadding.bottom();
}

void ProxiesBox::addNewProxy() {
	getDelegate()->show(_controller->addNewItemBox());
}

void ProxiesBox::applyView(View &&view) {
	if (view.selected) {
		_currentProxySupportsCallsId = view.supportsCalls ? view.id : 0;
	} else if (view.id == _currentProxySupportsCallsId) {
		_currentProxySupportsCallsId = 0;
	}
	refreshProxyForCalls();

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
	}, label->lifetime());
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
		getDelegate()->show(_controller->editItemBox(id));
	}, button->lifetime());

	button->shareClicks(
	) | rpl::start_with_next([=] {
		_controller->shareItem(id);
	}, button->lifetime());

	button->clicks(
	) | rpl::start_with_next([=] {
		_controller->applyItem(id);
	}, button->lifetime());
}

ProxyBox::ProxyBox(
	QWidget*,
	const ProxyData &data,
	Fn<void(ProxyData)> callback,
	Fn<void(ProxyData)> shareCallback)
: _callback(std::move(callback))
, _shareCallback(std::move(shareCallback))
, _content(this) {
	setupControls(data);
}

void ProxyBox::prepare() {
	setTitle(langFactory(lng_proxy_edit));

	refreshButtons();
	setDimensionsToContent(st::boxWideWidth, _content);
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
		_shareCallback(data);
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
	} else if (result.type == Type::Mtproto && !result.valid()) {
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
	_aboutSponsored = _content->add(object_ptr<Ui::SlideWrap<>>(
		_content,
		object_ptr<Ui::PaddingWrap<>>(
			_content,
			object_ptr<Ui::FlatLabel>(
				_content,
				lang(lng_proxy_sponsor_warning),
				Ui::FlatLabel::InitType::Simple,
				st::boxDividerLabel),
			st::proxyAboutSponsorPadding)));
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
	_secret->setMaxLength(ProxyData::MaxMtprotoPasswordLength());
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
	_content->resizeToWidth(st::boxWideWidth);
	_content->moveToLeft(0, 0);

	setupTypes();
	setupSocketAddress(data);
	setupCredentials(data);
	setupMtprotoCredentials(data);

	const auto handleType = [=](Type type) {
		_credentials->toggle(
			type == Type::Http || type == Type::Socks5,
			anim::type::instant);
		_mtprotoCredentials->toggle(
			type == Type::Mtproto,
			anim::type::instant);
		_aboutSponsored->toggle(
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

ProxiesBoxController::ProxiesBoxController()
: _saveTimer([] { Local::writeSettings(); }) {
	_list = ranges::view::all(
		Global::ProxiesList()
	) | ranges::view::transform([&](const ProxyData &proxy) {
		return Item{ ++_idCounter, proxy };
	}) | ranges::to_vector;

	subscribe(Global::RefConnectionTypeChanged(), [=] {
		_proxySettingsChanges.fire_copy(Global::ProxySettings());
		const auto i = findByProxy(Global::SelectedProxy());
		if (i != end(_list)) {
			updateView(*i);
		}
	});

	for (auto &item : _list) {
		refreshChecker(item);
	}
}

void ProxiesBoxController::ShowApplyConfirmation(
		Type type,
		const QMap<QString, QString> &fields) {
	const auto server = fields.value(qsl("server"));
	const auto port = fields.value(qsl("port")).toUInt();
	auto proxy = ProxyData();
	proxy.type = type;
	proxy.host = server;
	proxy.port = port;
	if (type == Type::Socks5) {
		proxy.user = fields.value(qsl("user"));
		proxy.password = fields.value(qsl("pass"));
	} else if (type == Type::Mtproto) {
		proxy.password = fields.value(qsl("secret"));
	}
	if (proxy) {
		const auto box = std::make_shared<QPointer<ConfirmBox>>();
		const auto text = lng_sure_enable_socks(
			lt_server,
			server,
			lt_port,
			QString::number(port))
			+ (proxy.type == Type::Mtproto
				? "\n\n" + lang(lng_proxy_sponsor_warning)
				: QString());
		*box = Ui::show(Box<ConfirmBox>(text, lang(lng_sure_enable), [=] {
			auto &proxies = Global::RefProxiesList();
			if (ranges::find(proxies, proxy) == end(proxies)) {
				proxies.push_back(proxy);
			}
			Messenger::Instance().setCurrentProxy(
				proxy,
				ProxyData::Settings::Enabled);
			Local::writeSettings();
			if (const auto strong = box->data()) {
				strong->closeBox();
			}
		}), LayerOption::KeepOther);
	}
}

auto ProxiesBoxController::proxySettingsValue() const
-> rpl::producer<ProxyData::Settings> {
	return _proxySettingsChanges.events_starting_with_copy(
		Global::ProxySettings()
	) | rpl::distinct_until_changed();
}

void ProxiesBoxController::refreshChecker(Item &item) {
	using Variants = MTP::DcOptions::Variants;
	const auto type = (item.data.type == Type::Http)
		? Variants::Http
		: Variants::Tcp;
	const auto mtproto = Messenger::Instance().mtp();
	const auto dcId = mtproto->mainDcId();

	item.state = ItemState::Checking;
	const auto setup = [&](Checker &checker) {
		checker = MTP::internal::AbstractConnection::Create(
			mtproto,
			type,
			QThread::currentThread(),
			item.data);
		setupChecker(item.id, checker);
	};
	setup(item.checker);
	if (item.data.type == Type::Mtproto) {
		item.checkerv6 = nullptr;
		item.checker->connectToServer(
			item.data.host,
			item.data.port,
			item.data.secretFromMtprotoPassword(),
			dcId);
	} else {
		const auto options = mtproto->dcOptions()->lookup(
			dcId,
			MTP::DcType::Regular,
			true);
		const auto endpoint = options.data[Variants::IPv4][type];
		const auto endpointv6 = options.data[Variants::IPv6][type];
		if (endpoint.empty()) {
			item.checker = nullptr;
		}
		if (Global::TryIPv6() && !endpointv6.empty()) {
			setup(item.checkerv6);
		} else {
			item.checkerv6 = nullptr;
		}
		if (!item.checker && !item.checkerv6) {
			item.state = ItemState::Unavailable;
			return;
		}
		const auto connect = [&](
				const Checker &checker,
				const std::vector<MTP::DcOptions::Endpoint> &endpoints) {
			if (checker) {
				checker->connectToServer(
					QString::fromStdString(endpoints.front().ip),
					endpoints.front().port,
					endpoints.front().secret,
					dcId);
			}
		};
		connect(item.checker, endpoint);
		connect(item.checkerv6, endpointv6);
	}
}

void ProxiesBoxController::setupChecker(int id, const Checker &checker) {
	using Connection = MTP::internal::AbstractConnection;
	const auto pointer = checker.get();
	pointer->connect(pointer, &Connection::connected, [=] {
		const auto item = findById(id);
		const auto pingTime = pointer->pingTime();
		item->checker = nullptr;
		item->checkerv6 = nullptr;
		if (item->state == ItemState::Checking) {
			item->state = ItemState::Available;
			item->ping = pingTime;
			updateView(*item);
		}
	});
	const auto failed = [=] {
		const auto item = findById(id);
		if (item->checker == pointer) {
			item->checker = nullptr;
		} else if (item->checkerv6 == pointer) {
			item->checkerv6 = nullptr;
		}
		if (!item->checker
			&& !item->checkerv6
			&& item->state == ItemState::Checking) {
			item->state = ItemState::Unavailable;
			updateView(*item);
		}
	};
	pointer->connect(pointer, &Connection::disconnected, failed);
	pointer->connect(pointer, &Connection::error, failed);
}

object_ptr<BoxContent> ProxiesBoxController::CreateOwningBox() {
	auto controller = std::make_unique<ProxiesBoxController>();
	auto box = controller->create();
	Ui::AttachAsChild(box, std::move(controller));
	return box;
}

object_ptr<BoxContent> ProxiesBoxController::create() {
	auto result = Box<ProxiesBox>(this);
	for (const auto &item : _list) {
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

void ProxiesBoxController::shareItem(int id) {
	share(findById(id)->data);
}

void ProxiesBoxController::applyItem(int id) {
	auto item = findById(id);
	if ((Global::ProxySettings() == ProxyData::Settings::Enabled)
		&& Global::SelectedProxy() == item->data) {
		return;
	} else if (item->deleted) {
		return;
	}

	auto j = findByProxy(Global::SelectedProxy());

	Messenger::Instance().setCurrentProxy(
		item->data,
		ProxyData::Settings::Enabled);
	saveDelayed();

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
			if (Global::ProxySettings() == ProxyData::Settings::Enabled) {
				_lastSelectedProxyUsed = true;
				Messenger::Instance().setCurrentProxy(
					ProxyData(),
					ProxyData::Settings::System);
				saveDelayed();
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
			Assert(Global::ProxySettings() != ProxyData::Settings::Enabled);

			if (base::take(_lastSelectedProxyUsed)) {
				Messenger::Instance().setCurrentProxy(
					base::take(_lastSelectedProxy),
					ProxyData::Settings::Enabled);
			} else {
				Global::SetSelectedProxy(base::take(_lastSelectedProxy));
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
	}, [=](const ProxyData &proxy) {
		share(proxy);
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
	refreshChecker(*which);

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
	}, [=](const ProxyData &proxy) {
		share(proxy);
	});
}

void ProxiesBoxController::addNewItem(const ProxyData &proxy) {
	auto &proxies = Global::RefProxiesList();
	proxies.push_back(proxy);

	_list.push_back({ ++_idCounter, proxy });
	refreshChecker(_list.back());
	applyItem(_list.back().id);
}

bool ProxiesBoxController::setProxySettings(ProxyData::Settings value) {
	if (Global::ProxySettings() == value) {
		return true;
	} else if (value == ProxyData::Settings::Enabled) {
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
	Messenger::Instance().setCurrentProxy(
		Global::SelectedProxy(),
		value);
	saveDelayed();
	return true;
}

void ProxiesBoxController::setProxyForCalls(bool enabled) {
	if (Global::UseProxyForCalls() == enabled) {
		return;
	}
	Global::SetUseProxyForCalls(enabled);
	if ((Global::ProxySettings() == ProxyData::Settings::Enabled)
		&& Global::SelectedProxy().supportsCalls()) {
		Global::RefConnectionTypeChanged().notify();
	}
	saveDelayed();
}

void ProxiesBoxController::setTryIPv6(bool enabled) {
	if (Global::TryIPv6() == enabled) {
		return;
	}
	Global::SetTryIPv6(enabled);
	MTP::restart();
	Global::RefConnectionTypeChanged().notify();
	saveDelayed();
}

void ProxiesBoxController::saveDelayed() {
	_saveTimer.callOnce(kSaveSettingsDelayedTimeout);
}

auto ProxiesBoxController::views() const -> rpl::producer<ItemView> {
	return _views.events();
}

void ProxiesBoxController::updateView(const Item &item) {
	const auto ping = 0;
	const auto selected = (Global::SelectedProxy() == item.data);
	const auto deleted = item.deleted;
	const auto type = [&] {
		switch (item.data.type) {
		case Type::Http:
			return qsl("HTTP");
		case Type::Socks5:
			return qsl("SOCKS5");
		case Type::Mtproto:
			return qsl("MTPROTO");
		}
		Unexpected("Proxy type in ProxiesBoxController::updateView.");
	}();
	const auto state = [&] {
		if (!selected
			|| (Global::ProxySettings() != ProxyData::Settings::Enabled)) {
			return item.state;
		} else if (MTP::dcstate() == MTP::ConnectedState) {
			return ItemState::Online;
		}
		return ItemState::Connecting;
	}();
	const auto supportsShare = (item.data.type == Type::Socks5)
		|| (item.data.type == Type::Mtproto);
	const auto supportsCalls = item.data.supportsCalls();
	_views.fire({
		item.id,
		type,
		item.data.host,
		item.data.port,
		item.ping,
		!deleted && selected,
		deleted,
		!deleted && supportsShare,
		supportsCalls,
		state });
}

void ProxiesBoxController::share(const ProxyData &proxy) {
	if (proxy.type == Type::Http) {
		return;
	}
	const auto link = qsl("https://t.me/")
		+ (proxy.type == Type::Socks5 ? "socks" : "proxy")
		+ "?server=" + proxy.host + "&port=" + QString::number(proxy.port)
		+ ((proxy.type == Type::Socks5 && !proxy.user.isEmpty())
			? "&user=" + qthelp::url_encode(proxy.user) : "")
		+ ((proxy.type == Type::Socks5 && !proxy.password.isEmpty())
			? "&pass=" + qthelp::url_encode(proxy.password) : "")
		+ ((proxy.type == Type::Mtproto && !proxy.password.isEmpty())
			? "&secret=" + proxy.password : "");
	QApplication::clipboard()->setText(link);
	Ui::Toast::Show(lang(lng_username_copied));
}

ProxiesBoxController::~ProxiesBoxController() {
	if (_saveTimer.isActive()) {
		App::CallDelayed(
			kSaveSettingsDelayedTimeout,
			QApplication::instance(),
			[] { Local::writeSettings(); });
	}
}
