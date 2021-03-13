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
#include "base/call_delayed.h"
#include "core/application.h"
#include "main/main_account.h"
#include "mtproto/facade.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/toast/toast.h"
#include "ui/effects/animations.h"
#include "ui/effects/radial_animation.h"
#include "ui/text/text_options.h"
#include "facades.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace {

constexpr auto kSaveSettingsDelayedTimeout = crl::time(1000);

using ProxyData = MTP::ProxyData;

class HostInput : public Ui::MaskedInputField {
public:
	HostInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &val);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;
};

HostInput::HostInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: MaskedInputField(parent, st, std::move(placeholder), val) {
}

void HostInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText;
	int newCursor = nowCursor;
	newText.reserve(now.size());
	for (auto i = 0, l = now.size(); i < l; ++i) {
		if (now[i] == ',') {
			newText.append('.');
		} else {
			newText.append(now[i]);
		}
	}
	setCorrectedText(now, nowCursor, newText, newCursor);
}

class Base64UrlInput : public Ui::MaskedInputField {
public:
	Base64UrlInput(
		QWidget *parent,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &val);

protected:
	void correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) override;

};

Base64UrlInput::Base64UrlInput(
	QWidget *parent,
	const style::InputField &st,
	rpl::producer<QString> placeholder,
	const QString &val)
: MaskedInputField(parent, st, std::move(placeholder), val) {
	if (!QRegularExpression("^[a-zA-Z0-9_\\-]+$").match(val).hasMatch()) {
		setText(QString());
	}
}

void Base64UrlInput::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	QString newText;
	newText.reserve(now.size());
	auto newPos = nowCursor;
	for (auto i = 0, l = now.size(); i < l; ++i) {
		const auto ch = now[i];
		if ((ch >= '0' && ch <= '9')
			|| (ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z')
			|| (ch == '-')
			|| (ch == '_')) {
			newText.append(ch);
		} else if (i < nowCursor) {
			--newPos;
		}
	}
	setCorrectedText(now, nowCursor, newText, newPos);
}

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
	void radialAnimationCallback();
	void paintCheck(Painter &p);
	void showMenu();

	View _view;

	Ui::Text::String _title;
	object_ptr<Ui::IconButton> _menuToggle;
	rpl::event_stream<> _deleteClicks;
	rpl::event_stream<> _restoreClicks;
	rpl::event_stream<> _editClicks;
	rpl::event_stream<> _shareClicks;
	base::unique_qptr<Ui::DropdownMenu> _menu;

	bool _set = false;
	Ui::Animations::Simple _toggled;
	Ui::Animations::Simple _setAnimation;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _progress;
	std::unique_ptr<Ui::InfiniteRadialAnimation> _checking;

	int _skipLeft = 0;
	int _skipRight = 0;

};

class ProxiesBox : public Ui::BoxContent {
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

class ProxyBox final : public Ui::BoxContent {
public:
	ProxyBox(
		QWidget*,
		const ProxyData &data,
		Fn<void(ProxyData)> callback,
		Fn<void(ProxyData)> shareCallback);

private:
	using Type = ProxyData::Type;

	void prepare() override;
	void setInnerFocus() override {
		_host->setFocusFast();
	}

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
	QPointer<HostInput> _host;
	QPointer<Ui::NumberInput> _port;
	QPointer<Ui::InputField> _user;
	QPointer<Ui::PasswordInput> _password;
	QPointer<Base64UrlInput> _secret;

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
	_toggled.stop();
	_setAnimation.stop();

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
				[=] { radialAnimationCallback(); },
				st::proxyCheckingAnimation);
		}
		_progress->start();
	} else if (_progress) {
		_progress->stop();
	}
	if (state == State::Checking) {
		if (!_checking) {
			_checking = std::make_unique<Ui::InfiniteRadialAnimation>(
				[=] { radialAnimationCallback(); },
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

void ProxyRow::radialAnimationCallback() {
	if (!anim::Disabled()) {
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

	if (!_view.deleted) {
		paintRipple(p, 0, 0);
	}

	const auto left = _skipLeft;
	const auto availableWidth = countAvailableWidth();
	auto top = st::proxyRowPadding.top();

	if (_view.deleted) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	}

	paintCheck(p);

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
			return tr::lng_proxy_available(
				tr::now,
				lt_ping,
				QString::number(_view.ping));
		case State::Checking:
			return tr::lng_proxy_checking(tr::now);
		case State::Connecting:
			return tr::lng_proxy_connecting(tr::now);
		case State::Online:
			return tr::lng_proxy_online(tr::now);
		case State::Unavailable:
			return tr::lng_proxy_unavailable(tr::now);
		}
		Unexpected("State in ProxyRow::paintEvent.");
	}();
	p.setPen(_view.deleted ? st::proxyRowStatusFg : statusFg);
	p.setFont(st::normalFont);

	auto statusLeft = left;
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
	p.drawTextLeft(statusLeft, top, width(), status);
	top += st::normalFont->height + st::proxyRowPadding.bottom();
}

void ProxyRow::paintCheck(Painter &p) {
	const auto loading = _progress
		? _progress->computeState()
		: Ui::RadialState{ 0., 0, FullArcLength };
	const auto toggled = _toggled.value(_view.selected ? 1. : 0.)
		* (1. - loading.shown);
	const auto _st = &st::defaultRadio;
	const auto set = _setAnimation.value(_set ? 1. : 0.);

	PainterHighQualityEnabler hq(p);

	const auto left = st::proxyRowPadding.left();
	const auto top = (height() - _st->diameter - _st->thickness) / 2;
	const auto outerWidth = width();

	auto pen = anim::pen(_st->untoggledFg, _st->toggledFg, toggled * set);
	pen.setWidth(_st->thickness);
	pen.setCapStyle(Qt::RoundCap);
	p.setPen(pen);
	p.setBrush(_st->bg);
	const auto rect = style::rtlrect(QRectF(left, top, _st->diameter, _st->diameter).marginsRemoved(QMarginsF(_st->thickness / 2., _st->thickness / 2., _st->thickness / 2., _st->thickness / 2.)), outerWidth);
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
		p.drawEllipse(style::rtlrect(QRectF(left, top, _st->diameter, _st->diameter).marginsRemoved(QMarginsF(checkSkip, checkSkip, checkSkip, checkSkip)), outerWidth));
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
	addAction(tr::lng_proxy_menu_edit(tr::now), [=] {
		_editClicks.fire({});
	});
	if (_view.supportsShare) {
		addAction(tr::lng_proxy_edit_share(tr::now), [=] {
			_shareClicks.fire({});
		});
	}
	if (_view.deleted) {
		addAction(tr::lng_proxy_menu_restore(tr::now), [=] {
			_restoreClicks.fire({});
		});
	} else {
		addAction(tr::lng_proxy_menu_delete(tr::now), [=] {
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
	setTitle(tr::lng_proxy_settings());

	addButton(tr::lng_proxy_add(), [=] { addNewProxy(); });
	addButton(tr::lng_close(), [=] { closeBox(); });

	setupContent();
}

void ProxiesBox::setupContent() {
	const auto inner = setInnerWidget(object_ptr<Ui::VerticalLayout>(this));

	_tryIPv6 = inner->add(
		object_ptr<Ui::Checkbox>(
			inner,
			tr::lng_connection_try_ipv6(tr::now),
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
			tr::lng_proxy_disable(tr::now)),
		st::proxyUsePadding);
	inner->add(
		object_ptr<Ui::Radioenum<ProxyData::Settings>>(
			inner,
			_proxySettings,
			ProxyData::Settings::System,
			tr::lng_proxy_use_system_settings(tr::now)),
		st::proxyUsePadding);
	inner->add(
		object_ptr<Ui::Radioenum<ProxyData::Settings>>(
			inner,
			_proxySettings,
			ProxyData::Settings::Enabled,
			tr::lng_proxy_use_custom(tr::now)),
		st::proxyUsePadding);
	_proxyForCalls = inner->add(
		object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
			inner,
			object_ptr<Ui::Checkbox>(
				inner,
				tr::lng_proxy_use_for_calls(tr::now),
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
				tr::lng_proxy_about(tr::now),
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
		if (_noRows) {
			_noRows.reset();
		}
		wrap->resizeToWidth(width());
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
		tr::lng_proxy_description(tr::now),
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
	setTitle(tr::lng_proxy_edit());

	connect(_host.data(), &HostInput::changed, [=] {
		Ui::PostponeCall(_host, [=] {
			const auto host = _host->getLastText().trimmed();
			static const auto mask = u"^\\d+\\.\\d+\\.\\d+\\.\\d+:(\\d*)$"_q;
			const auto match = QRegularExpression(mask).match(host);
			if (_host->cursorPosition() == host.size()
				&& match.hasMatch()) {
				const auto port = match.captured(1);
				_port->setText(port);
				_port->setCursorPosition(port.size());
				_port->setFocus();
				_host->setText(host.mid(0, host.size() - port.size() - 1));
			}
		});
	});
	_port.data()->events(
	) | rpl::start_with_next([=](not_null<QEvent*> e) {
		if (e->type() == QEvent::KeyPress
			&& (static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Backspace)
			&& _port->cursorPosition() == 0) {
			_host->setCursorPosition(_host->getLastText().size());
			_host->setFocus();
		}
	}, _port->lifetime());

	refreshButtons();
	setDimensionsToContent(st::boxWideWidth, _content);
}

void ProxyBox::refreshButtons() {
	clearButtons();
	addButton(tr::lng_settings_save(), [=] { save(); });
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	const auto type = _type->value();
	if (type == Type::Socks5 || type == Type::Mtproto) {
		addLeftButton(tr::lng_proxy_share(), [=] { share(); });
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
	for (const auto &[type, label] : types) {
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
				tr::lng_proxy_sponsor_warning(tr::now),
				st::boxDividerLabel),
			st::proxyAboutSponsorPadding)));
}

void ProxyBox::setupSocketAddress(const ProxyData &data) {
	addLabel(_content, tr::lng_proxy_address_label(tr::now));
	const auto address = _content->add(
		object_ptr<Ui::FixedHeightWidget>(
			_content,
			st::connectionHostInputField.heightMin),
		st::proxyEditInputPadding);
	_host = Ui::CreateChild<HostInput>(
		address,
		st::connectionHostInputField,
		tr::lng_connection_host_ph(),
		data.host);
	_port = Ui::CreateChild<Ui::NumberInput>(
		address,
		st::connectionPortInputField,
		tr::lng_connection_port_ph(),
		data.port ? QString::number(data.port) : QString(),
		65535);
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
	addLabel(credentials, tr::lng_proxy_credentials_optional(tr::now));
	_user = credentials->add(
		object_ptr<Ui::InputField>(
			credentials,
			st::connectionUserInputField,
			tr::lng_connection_user_ph(),
			data.user),
		st::proxyEditInputPadding);

	auto passwordWrap = object_ptr<Ui::RpWidget>(credentials);
	_password = Ui::CreateChild<Ui::PasswordInput>(
		passwordWrap.data(),
		st::connectionPasswordInputField,
		tr::lng_connection_password_ph(),
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
	addLabel(mtproto, tr::lng_proxy_credentials(tr::now));

	auto secretWrap = object_ptr<Ui::RpWidget>(mtproto);
	_secret = Ui::CreateChild<Base64UrlInput>(
		secretWrap.data(),
		st::connectionUserInputField,
		tr::lng_connection_proxy_secret_ph(),
		(data.type == Type::Mtproto) ? data.password : QString());
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
			st::proxyEditTitle),
		st::proxyEditTitlePadding);
}

} // namespace

ProxiesBoxController::ProxiesBoxController(not_null<Main::Account*> account)
: _account(account)
, _saveTimer([] { Local::writeSettings(); }) {
	_list = ranges::views::all(
		Global::ProxiesList()
	) | ranges::views::transform([&](const ProxyData &proxy) {
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
		const auto text = tr::lng_sure_enable_socks(
			tr::now,
			lt_server,
			server,
			lt_port,
			QString::number(port))
			+ (proxy.type == Type::Mtproto
				? "\n\n" + tr::lng_proxy_sponsor_warning(tr::now)
				: QString());
		auto callback = [=](Fn<void()> &&close) {
			auto &proxies = Global::RefProxiesList();
			if (!ranges::contains(proxies, proxy)) {
				proxies.push_back(proxy);
			}
			Core::App().setCurrentProxy(
				proxy,
				ProxyData::Settings::Enabled);
			Local::writeSettings();
			close();
		};
		Ui::show(
			Box<ConfirmBox>(
				text,
				tr::lng_sure_enable(tr::now),
				std::move(callback)),
			Ui::LayerOption::KeepOther);
	} else {
		Ui::show(Box<InformBox>(
			(proxy.status() == ProxyData::Status::Unsupported
				? tr::lng_proxy_unsupported(tr::now)
				: tr::lng_proxy_invalid(tr::now))));
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
	const auto mtproto = &_account->mtp();
	const auto dcId = mtproto->mainDcId();

	item.state = ItemState::Checking;
	const auto setup = [&](Checker &checker, const bytes::vector &secret) {
		checker = MTP::details::AbstractConnection::Create(
			mtproto,
			type,
			QThread::currentThread(),
			secret,
			item.data);
		setupChecker(item.id, checker);
	};
	if (item.data.type == Type::Mtproto) {
		const auto secret = item.data.secretFromMtprotoPassword();
		setup(item.checker, secret);
		item.checker->connectToServer(
			item.data.host,
			item.data.port,
			secret,
			dcId);
		item.checkerv6 = nullptr;
	} else {
		const auto options = mtproto->dcOptions().lookup(
			dcId,
			MTP::DcType::Regular,
			true);
		const auto connect = [&](
				Checker &checker,
				Variants::Address address) {
			const auto &list = options.data[address][type];
			if (list.empty()
				|| (address == Variants::IPv6 && !Global::TryIPv6())) {
				checker = nullptr;
				return;
			}
			const auto &endpoint = list.front();
			setup(checker, endpoint.secret);
			checker->connectToServer(
				QString::fromStdString(endpoint.ip),
				endpoint.port,
				endpoint.secret,
				dcId);
		};
		connect(item.checker, Variants::IPv4);
		connect(item.checkerv6, Variants::IPv6);
		if (!item.checker && !item.checkerv6) {
			item.state = ItemState::Unavailable;
		}
	}
}

void ProxiesBoxController::setupChecker(int id, const Checker &checker) {
	using Connection = MTP::details::AbstractConnection;
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

object_ptr<Ui::BoxContent> ProxiesBoxController::CreateOwningBox(
		not_null<Main::Account*> account) {
	auto controller = std::make_unique<ProxiesBoxController>(account);
	auto box = controller->create();
	Ui::AttachAsChild(box, std::move(controller));
	return box;
}

object_ptr<Ui::BoxContent> ProxiesBoxController::create() {
	auto result = Box<ProxiesBox>(this);
	for (const auto &item : _list) {
		updateView(item);
	}
	return result;
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

	Core::App().setCurrentProxy(
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
				Core::App().setCurrentProxy(
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
				Core::App().setCurrentProxy(
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

object_ptr<Ui::BoxContent> ProxiesBoxController::editItemBox(int id) {
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

object_ptr<Ui::BoxContent> ProxiesBoxController::addNewItemBox() {
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
	Core::App().setCurrentProxy(Global::SelectedProxy(), value);
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
	_account->mtp().restart();
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
		} else if (_account->mtp().dcstate() == MTP::ConnectedState) {
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
	QGuiApplication::clipboard()->setText(link);
	Ui::Toast::Show(tr::lng_username_copied(tr::now));
}

ProxiesBoxController::~ProxiesBoxController() {
	if (_saveTimer.isActive()) {
		base::call_delayed(
			kSaveSettingsDelayedTimeout,
			QCoreApplication::instance(),
			[] { Local::writeSettings(); });
	}
}
