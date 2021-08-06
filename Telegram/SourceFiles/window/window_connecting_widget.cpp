/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_connecting_widget.h"

#include "ui/widgets/buttons.h"
#include "ui/effects/radial_animation.h"
#include "ui/ui_utility.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/facade.h"
#include "main/main_account.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/update_checker.h"
#include "boxes/connection_box.h"
#include "boxes/abstract_box.h"
#include "lang/lang_keys.h"
#include "styles/style_window.h"

namespace Window {
namespace {

constexpr auto kIgnoreStartConnectingFor = crl::time(3000);
constexpr auto kConnectingStateDelay = crl::time(1000);
constexpr auto kRefreshTimeout = crl::time(200);
constexpr auto kMinimalWaitingStateDuration = crl::time(4000);

class Progress : public Ui::RpWidget {
public:
	Progress(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void animationStep();

	Ui::InfiniteRadialAnimation _animation;

};

Progress::Progress(QWidget *parent)
: RpWidget(parent)
, _animation([=] { animationStep(); }, st::connectingRadial) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_TransparentForMouseEvents);
	resize(st::connectingRadial.size);
	_animation.start(st::connectingRadial.sineDuration);
}

void Progress::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::windowBg);
	const auto &st = st::connectingRadial;
	const auto shift = st.thickness - (st.thickness / 2);
	_animation.draw(
		p,
		{ shift, shift },
		QSize(st.size.width() - 2 * shift, st.size.height() - 2 * shift),
		width());
}

void Progress::animationStep() {
	if (!anim::Disabled()) {
		update();
	}
}

} // namespace

class ConnectionState::Widget : public Ui::AbstractButton {
public:
	Widget(
		QWidget *parent,
		not_null<Main::Account*> account,
		const Layout &layout);

	void refreshRetryLink(bool hasRetry);
	void setLayout(const Layout &layout);
	void setProgressVisibility(bool visible);

	rpl::producer<> refreshStateRequests() const;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

	void onStateChanged(State was, StateChangeSource source) override;

private:
	class ProxyIcon;
	using State = ConnectionState::State;
	using Layout = ConnectionState::Layout;

	void updateRetryGeometry();
	QRect innerRect() const;
	QRect contentRect() const;
	QRect textRect() const;

	const not_null<Main::Account*> _account;
	Layout _currentLayout;
	base::unique_qptr<Ui::LinkButton> _retry;
	QPointer<Ui::RpWidget> _progress;
	QPointer<ProxyIcon> _proxyIcon;
	rpl::event_stream<> _refreshStateRequests;

};

class ConnectionState::Widget::ProxyIcon final : public Ui::RpWidget {
public:
	ProxyIcon(QWidget *parent);

	void setToggled(bool toggled);
	void setOpacity(float64 opacity);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void refreshCacheImages();

	float64 _opacity = 1.;
	QPixmap _cacheOn;
	QPixmap _cacheOff;
	bool _toggled = true;

};

ConnectionState::Widget::ProxyIcon::ProxyIcon(QWidget *parent) : RpWidget(parent) {
	resize(
		std::max(
			st::connectingRadial.size.width(),
			st::connectingProxyOn.width()),
		std::max(
			st::connectingRadial.size.height(),
			st::connectingProxyOn.height()));

	style::PaletteChanged(
	) | rpl::start_with_next([=] {
		refreshCacheImages();
	}, lifetime());

	refreshCacheImages();
}

void ConnectionState::Widget::ProxyIcon::refreshCacheImages() {
	const auto prepareCache = [&](const style::icon &icon) {
		auto image = QImage(
			size() * cIntRetinaFactor(),
			QImage::Format_ARGB32_Premultiplied);
		image.setDevicePixelRatio(cRetinaFactor());
		image.fill(st::windowBg->c);
		{
			Painter p(&image);
			icon.paint(
				p,
				(width() - icon.width()) / 2,
				(height() - icon.height()) / 2,
				width());
		}
		return Ui::PixmapFromImage(std::move(image));
	};
	_cacheOn = prepareCache(st::connectingProxyOn);
	_cacheOff = prepareCache(st::connectingProxyOff);
}

void ConnectionState::Widget::ProxyIcon::setToggled(bool toggled) {
	if (_toggled != toggled) {
		_toggled = toggled;
		update();
	}
}

void ConnectionState::Widget::ProxyIcon::setOpacity(float64 opacity) {
	_opacity = opacity;
	if (_opacity == 0.) {
		hide();
	} else if (isHidden()) {
		show();
	}
	update();
}

void ConnectionState::Widget::ProxyIcon::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.setOpacity(_opacity);
	p.drawPixmap(0, 0, _toggled ? _cacheOn : _cacheOff);
}

bool ConnectionState::State::operator==(const State &other) const {
	return (type == other.type)
		&& (useProxy == other.useProxy)
		&& (underCursor == other.underCursor)
		&& (updateReady == other.updateReady)
		&& (waitTillRetry == other.waitTillRetry);
}

ConnectionState::ConnectionState(
	not_null<Ui::RpWidget*> parent,
	not_null<Main::Account*> account,
	rpl::producer<bool> shown)
: _account(account)
, _parent(parent)
, _refreshTimer([=] { refreshState(); })
, _currentLayout(computeLayout(_state)) {
	rpl::combine(
		std::move(shown),
		visibility()
	) | rpl::start_with_next([=](bool shown, float64 visible) {
		if (!shown || visible == 0.) {
			_widget = nullptr;
		} else if (!_widget) {
			createWidget();
		}
	}, _lifetime);

	if (!Core::UpdaterDisabled()) {
		Core::UpdateChecker checker;
		rpl::merge(
			rpl::single(rpl::empty_value()),
			checker.ready()
		) | rpl::start_with_next([=] {
			refreshState();
		}, _lifetime);
	}

	Core::App().settings().proxy().connectionTypeValue(
	) | rpl::start_with_next([=] {
		refreshState();
	}, _lifetime);
}

void ConnectionState::createWidget() {
	_widget = base::make_unique_q<Widget>(_parent, _account, _currentLayout);
	_widget->setVisible(!_forceHidden);

	updateWidth();

	rpl::combine(
		visibility(),
		_parent->heightValue()
	) | rpl::start_with_next([=](float64 visible, int height) {
		_widget->moveToLeft(0, anim::interpolate(
			height - st::connectingMargin.top(),
			height - _widget->height(),
			visible));
	}, _widget->lifetime());

	_widget->refreshStateRequests(
	) | rpl::start_with_next([=] {
		refreshState();
	}, _widget->lifetime());
}

void ConnectionState::raise() {
	if (_widget) {
		_widget->raise();
	}
}

void ConnectionState::finishAnimating() {
	if (_contentWidth.animating()) {
		_contentWidth.stop();
		updateWidth();
	}
	if (_visibility.animating()) {
		_visibility.stop();
		updateVisibility();
	}
}

void ConnectionState::setForceHidden(bool hidden) {
	_forceHidden = hidden;
	if (_widget) {
		_widget->setVisible(!hidden);
	}
}

void ConnectionState::refreshState() {
	using Checker = Core::UpdateChecker;
	const auto state = [&]() -> State {
		const auto under = _widget && _widget->isOver();
		const auto ready = (Checker().state() == Checker::State::Ready);
		const auto state = _account->mtp().dcstate();
		const auto proxy = Core::App().settings().proxy().isEnabled();
		if (state == MTP::ConnectingState
			|| state == MTP::DisconnectedState
			|| (state < 0 && state > -600)) {
			return { State::Type::Connecting, proxy, under, ready };
		} else if (state < 0
			&& state >= -kMinimalWaitingStateDuration
			&& _state.type != State::Type::Waiting) {
			return { State::Type::Connecting, proxy, under, ready };
		} else if (state < 0) {
			const auto wait = ((-state) / 1000) + 1;
			return { State::Type::Waiting, proxy, under, ready, wait };
		}
		return { State::Type::Connected, proxy, under, ready };
	}();
	if (state.waitTillRetry > 0) {
		_refreshTimer.callOnce(kRefreshTimeout);
	}
	if (state == _state) {
		return;
	} else if (state.type == State::Type::Connecting
		&& _state.type == State::Type::Connected) {
		const auto now = crl::now();
		if (!_connectingStartedAt) {
			_connectingStartedAt = now;
			_refreshTimer.callOnce(kConnectingStateDelay);
			return;
		}
		const auto applyConnectingAt = std::max(
			_connectingStartedAt + kConnectingStateDelay,
			kIgnoreStartConnectingFor);
		if (now < applyConnectingAt) {
			_refreshTimer.callOnce(applyConnectingAt - now);
			return;
		}
	}
	applyState(state);
}

void ConnectionState::applyState(const State &state) {
	const auto newLayout = computeLayout(state);
	const auto guard = gsl::finally([&] { updateWidth(); });

	_state = state;
	if (_currentLayout.visible != newLayout.visible) {
		changeVisibilityWithLayout(newLayout);
		return;
	}
	if (_currentLayout.contentWidth != newLayout.contentWidth) {
		if (!_currentLayout.contentWidth
			|| !newLayout.contentWidth
			|| _contentWidth.animating()) {
			_contentWidth.start(
				[=] { updateWidth(); },
				_currentLayout.contentWidth,
				newLayout.contentWidth,
				st::connectingDuration);
		}
	}
	const auto saved = _currentLayout;
	setLayout(newLayout);
	if (_currentLayout.text.isEmpty()
		&& !saved.text.isEmpty()
		&& _contentWidth.animating()) {
		_currentLayout.text = saved.text;
		_currentLayout.textWidth = saved.textWidth;
	}
}

void ConnectionState::changeVisibilityWithLayout(const Layout &layout) {
	Expects(_currentLayout.visible != layout.visible);

	const auto changeLayout = !_currentLayout.visible;
	_visibility.start(
		[=] { updateVisibility(); },
		layout.visible ? 0. : 1.,
		layout.visible ? 1. : 0.,
		st::connectingDuration);
	if (_contentWidth.animating()) {
		_contentWidth.start(
			[=] { updateWidth(); },
			_currentLayout.contentWidth,
			(changeLayout ? layout : _currentLayout).contentWidth,
			st::connectingDuration);
	}
	if (changeLayout) {
		setLayout(layout);
	} else {
		_currentLayout.visible = layout.visible;
	}
}

void ConnectionState::setLayout(const Layout &layout) {
	_currentLayout = layout;
	if (_widget) {
		_widget->setLayout(layout);
	}
	refreshProgressVisibility();
}

void ConnectionState::refreshProgressVisibility() {
	if (_widget) {
		_widget->setProgressVisibility(_contentWidth.animating()
			|| _currentLayout.progressShown);
	}
}

void ConnectionState::updateVisibility() {
	const auto value = currentVisibility();
	if (value == 0. && _contentWidth.animating()) {
		_contentWidth.stop();
		updateWidth();
	}
	_visibilityValues.fire_copy(value);
}

float64 ConnectionState::currentVisibility() const {
	return _visibility.value(_currentLayout.visible ? 1. : 0.);
}

rpl::producer<float64> ConnectionState::visibility() const {
	return _visibilityValues.events_starting_with(currentVisibility());
}

auto ConnectionState::computeLayout(const State &state) const -> Layout {
	auto result = Layout();
	result.proxyEnabled = state.useProxy;
	result.progressShown = (state.type != State::Type::Connected);
	result.visible = !state.updateReady
		&& (state.useProxy
			|| state.type == State::Type::Connecting
			|| state.type == State::Type::Waiting);
	switch (state.type) {
	case State::Type::Connecting:
		result.text = state.underCursor
			? tr::lng_connecting(tr::now)
			: QString();
		break;

	case State::Type::Waiting:
		Assert(state.waitTillRetry > 0);
		result.text = tr::lng_reconnecting(
			tr::now,
			lt_count,
			state.waitTillRetry);
		break;
	}
	result.textWidth = st::normalFont->width(result.text);
	result.contentWidth = (result.textWidth > 0)
		? (st::connectingTextPadding.left()
			+ result.textWidth
			+ st::connectingTextPadding.right())
		: 0;
	if (state.type == State::Type::Waiting) {
		result.contentWidth += st::connectingRetryLink.padding.left()
			+ st::connectingRetryLink.font->width(
				tr::lng_reconnecting_try_now(tr::now))
			+ st::connectingRetryLink.padding.right();
	}
	result.hasRetry = (state.type == State::Type::Waiting);
	return result;
}

void ConnectionState::updateWidth() {
	const auto current = _contentWidth.value(_currentLayout.contentWidth);
	const auto height = st::connectingLeft.height();
	const auto desired = QRect(0, 0, current, height).marginsAdded(
		style::margins(
			st::connectingLeft.width(),
			0,
			st::connectingRight.width(),
			0)
	).marginsAdded(
		st::connectingMargin
	);
	if (_widget) {
		_widget->resize(desired.size());
		_widget->update();
	}
	refreshProgressVisibility();
}

ConnectionState::Widget::Widget(
	QWidget *parent,
	not_null<Main::Account*> account,
	const Layout &layout)
: AbstractButton(parent)
, _account(account)
, _currentLayout(layout) {
	_proxyIcon = Ui::CreateChild<ProxyIcon>(this);
	_progress = Ui::CreateChild<Progress>(this);

	addClickHandler([=] {
		Ui::show(ProxiesBoxController::CreateOwningBox(account));
	});
}

void ConnectionState::Widget::onStateChanged(
		AbstractButton::State was,
		StateChangeSource source) {
	Ui::PostponeCall(crl::guard(this, [=] {
		_refreshStateRequests.fire({});
	}));
}

rpl::producer<> ConnectionState::Widget::refreshStateRequests() const {
	return _refreshStateRequests.events();
}

void ConnectionState::Widget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	PainterHighQualityEnabler hq(p);

	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBg);
	const auto inner = innerRect();
	const auto content = contentRect();
	const auto text = textRect();
	const auto left = inner.topLeft();
	const auto right = content.topLeft() + QPoint(content.width(), 0);
	st::connectingLeftShadow.paint(p, left, width());
	st::connectingLeft.paint(p, left, width());
	st::connectingRightShadow.paint(p, right, width());
	st::connectingRight.paint(p, right, width());
	st::connectingBodyShadow.fill(p, content);
	st::connectingBody.fill(p, content);

	const auto available = text.width();
	if (available > 0 && !_currentLayout.text.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::windowSubTextFg);
		if (available >= _currentLayout.textWidth) {
			p.drawTextLeft(
				text.x(),
				text.y(),
				width(),
				_currentLayout.text,
				_currentLayout.textWidth);
		} else {
			p.drawTextLeft(
				text.x(),
				text.y(),
				width(),
				st::normalFont->elided(_currentLayout.text, available));
		}
	}
}

QRect ConnectionState::Widget::innerRect() const {
	return rect().marginsRemoved(
		st::connectingMargin
	);
}

QRect ConnectionState::Widget::contentRect() const {
	return innerRect().marginsRemoved(style::margins(
		st::connectingLeft.width(),
		0,
		st::connectingRight.width(),
		0));
}

QRect ConnectionState::Widget::textRect() const {
	return contentRect().marginsRemoved(
		st::connectingTextPadding
	);
}

void ConnectionState::Widget::resizeEvent(QResizeEvent *e) {
	{
		const auto xShift = (height() - _progress->width()) / 2;
		const auto yShift = (height() - _progress->height()) / 2;
		_progress->moveToLeft(xShift, yShift);
	}
	{
		const auto xShift = (height() - _proxyIcon->width()) / 2;
		const auto yShift = (height() - _proxyIcon->height()) / 2;
		_proxyIcon->moveToRight(xShift, yShift);
	}
	updateRetryGeometry();
}

void ConnectionState::Widget::updateRetryGeometry() {
	if (!_retry) {
		return;
	}
	const auto text = textRect();
	const auto available = text.width() - _currentLayout.textWidth;
	if (available <= 0) {
		_retry->hide();
	} else {
		_retry->show();
		_retry->resize(
			std::min(available, _retry->naturalWidth()),
			innerRect().height());
		_retry->moveToLeft(
			text.x() + text.width() - _retry->width(),
			st::connectingMargin.top());
	}
}

void ConnectionState::Widget::setLayout(const Layout &layout) {
	_currentLayout = layout;
	_proxyIcon->setToggled(_currentLayout.proxyEnabled);
	refreshRetryLink(_currentLayout.hasRetry);
}

void ConnectionState::Widget::setProgressVisibility(bool visible) {
	if (_progress->isHidden() == visible) {
		_progress->setVisible(visible);
	}
}

void ConnectionState::Widget::refreshRetryLink(bool hasRetry) {
	if (hasRetry && !_retry) {
		_retry = base::make_unique_q<Ui::LinkButton>(
			this,
			tr::lng_reconnecting_try_now(tr::now),
			st::connectingRetryLink);
		_retry->addClickHandler([=] {
			_account->mtp().restart();
		});
		updateRetryGeometry();
	} else if (!hasRetry) {
		_retry = nullptr;
	}
}

} // namespace Window
