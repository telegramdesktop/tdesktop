/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_connecting_widget.h"

#include "ui/widgets/buttons.h"
#include "ui/effects/radial_animation.h"
#include "window/themes/window_theme.h"
#include "boxes/connection_box.h"
#include "lang/lang_keys.h"
#include "styles/style_window.h"

namespace Window {
namespace {

constexpr auto kIgnoreStartConnectingFor = TimeMs(3000);
constexpr auto kConnectingStateDelay = TimeMs(1000);
constexpr auto kRefreshTimeout = TimeMs(200);
constexpr auto kMinimalWaitingStateDuration = TimeMs(4000);

class Progress : public Ui::RpWidget {
public:
	Progress(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void step(TimeMs ms, bool timer);

	Ui::InfiniteRadialAnimation _animation;

};

Progress::Progress(QWidget *parent)
: RpWidget(parent)
, _animation(animation(this, &Progress::step), st::connectingRadial) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_TransparentForMouseEvents);
	resize(st::connectingRadial.size);
	_animation.start();
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

void Progress::step(TimeMs ms, bool timer) {
	if (timer) {
		update();
	}
}

} // namespace

class ConnectingWidget::ProxyIcon
	: public Ui::RpWidget
	, private base::Subscriber {
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

ConnectingWidget::ProxyIcon::ProxyIcon(QWidget *parent) : RpWidget(parent) {
	resize(
		std::max(
			st::connectingRadial.size.width(),
			st::connectingProxyOn.width()),
		std::max(
			st::connectingRadial.size.height(),
			st::connectingProxyOn.height()));

	using namespace Window::Theme;
	subscribe(Background(), [=](const BackgroundUpdate &update) {
		if (update.paletteChanged()) {
			refreshCacheImages();
		}
	});

	refreshCacheImages();
}

void ConnectingWidget::ProxyIcon::refreshCacheImages() {
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
		return App::pixmapFromImageInPlace(std::move(image));
	};
	_cacheOn = prepareCache(st::connectingProxyOn);
	_cacheOff = prepareCache(st::connectingProxyOff);
}

void ConnectingWidget::ProxyIcon::setToggled(bool toggled) {
	if (_toggled != toggled) {
		_toggled = toggled;
		update();
	}
}

void ConnectingWidget::ProxyIcon::setOpacity(float64 opacity) {
	_opacity = opacity;
	if (_opacity == 0.) {
		hide();
	} else if (isHidden()) {
		show();
	}
	update();
}

void ConnectingWidget::ProxyIcon::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.setOpacity(_opacity);
	p.drawPixmap(0, 0, _toggled ? _cacheOn : _cacheOff);
}

bool ConnectingWidget::State::operator==(const State &other) const {
	return (type == other.type)
		&& (useProxy == other.useProxy)
		&& (underCursor == other.underCursor)
		&& (waitTillRetry == other.waitTillRetry);
}

ConnectingWidget::ConnectingWidget(QWidget *parent)
: AbstractButton(parent)
, _refreshTimer([=] { refreshState(); })
, _currentLayout(computeLayout(_state)) {
	_proxyIcon = Ui::CreateChild<ProxyIcon>(this);
	_progress = Ui::CreateChild<Progress>(this);

	addClickHandler([=] {
		Ui::show(ProxiesBoxController::CreateOwningBox());
	});

	subscribe(Global::RefConnectionTypeChanged(), [=] {
		refreshState();
	});
	refreshState();
}

rpl::producer<float64> ConnectingWidget::visibility() const {
	return _visibilityValues.events_starting_with(currentVisibility());
}

void ConnectingWidget::finishAnimating() {
	if (_contentWidth.animating()) {
		_contentWidth.finish();
		updateWidth();
	}
	if (_visibility.animating()) {
		_visibility.finish();
		updateVisibility();
	}
}

void ConnectingWidget::setForceHidden(bool hidden) {
	if (_forceHidden == hidden) {
		return;
	}
	if (hidden) {
		const auto real = isHidden();
		if (!real) {
			hide();
		}
		_realHidden = real;
	}
	_forceHidden = hidden;
	if (!hidden && isHidden() != _realHidden) {
		setVisible(!_realHidden);
	}
}

void ConnectingWidget::setVisibleHook(bool visible) {
	if (_forceHidden) {
		_realHidden = !visible;
		return;
	}
	QWidget::setVisible(visible);
}

base::unique_qptr<ConnectingWidget> ConnectingWidget::CreateDefaultWidget(
		Ui::RpWidget *parent,
		rpl::producer<bool> shown) {
	auto result = base::make_unique_q<Window::ConnectingWidget>(parent);
	const auto weak = result.get();
	rpl::combine(
		result->visibility(),
		parent->heightValue()
	) | rpl::start_with_next([=](float64 visible, int height) {
		const auto hidden = (visible == 0.);
		if (weak->isHidden() != hidden) {
			weak->setVisible(!hidden);
		}
		const auto size = weak->size();
		weak->moveToLeft(0, anim::interpolate(
			height - st::connectingMargin.top(),
			height - weak->height(),
			visible));
	}, weak->lifetime());
	std::move(
		shown
	) | rpl::start_with_next([=](bool shown) {
		weak->setForceHidden(!shown);
	}, weak->lifetime());
	result->finishAnimating();
	return result;
}

void ConnectingWidget::onStateChanged(
		AbstractButton::State was,
		StateChangeSource source) {
	crl::on_main(this, [=] { refreshState(); });
}

void ConnectingWidget::paintEvent(QPaintEvent *e) {
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

QRect ConnectingWidget::innerRect() const {
	return rect().marginsRemoved(
		st::connectingMargin
	);
}

QRect ConnectingWidget::contentRect() const {
	return innerRect().marginsRemoved(style::margins(
		st::connectingLeft.width(),
		0,
		st::connectingRight.width(),
		0));
}

QRect ConnectingWidget::textRect() const {
	return contentRect().marginsRemoved(
		st::connectingTextPadding
	);
}

void ConnectingWidget::resizeEvent(QResizeEvent *e) {
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

void ConnectingWidget::updateRetryGeometry() {
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

void ConnectingWidget::refreshState() {
	const auto state = [&]() -> State {
		const auto under = isOver();
		const auto mtp = MTP::dcstate();
		const auto throughProxy = Global::UseProxy();
		if (mtp == MTP::ConnectingState
			|| mtp == MTP::DisconnectedState
			|| (mtp < 0 && mtp > -600)) {
			return { State::Type::Connecting, throughProxy, under };
		} else if (mtp < 0
			&& mtp >= -kMinimalWaitingStateDuration
			&& _state.type != State::Type::Waiting) {
			return { State::Type::Connecting, throughProxy, under };
		} else if (mtp < 0) {
			const auto seconds = ((-mtp) / 1000) + 1;
			return { State::Type::Waiting, throughProxy, under, seconds };
		}
		return { State::Type::Connected, throughProxy, under };
	}();
	if (state.waitTillRetry > 0) {
		_refreshTimer.callOnce(kRefreshTimeout);
	}
	if (state == _state) {
		return;
	} else if (state.type == State::Type::Connecting
		&& _state.type == State::Type::Connected) {
		const auto now = getms();
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

void ConnectingWidget::applyState(const State &state) {
	const auto newLayout = computeLayout(state);
	const auto guard = gsl::finally([&] {
		updateWidth();
		update();
	});

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
	refreshRetryLink(_currentLayout.hasRetry);
}

void ConnectingWidget::changeVisibilityWithLayout(const Layout &layout) {
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

void ConnectingWidget::setLayout(const Layout &layout) {
	_currentLayout = layout;
	_proxyIcon->setToggled(_currentLayout.proxyEnabled);
	_progress->setVisible(_contentWidth.animating()
		|| _currentLayout.progressShown);
}

void ConnectingWidget::refreshRetryLink(bool hasRetry) {
	if (hasRetry && !_retry) {
		_retry = base::make_unique_q<Ui::LinkButton>(
			this,
			lang(lng_reconnecting_try_now),
			st::connectingRetryLink);
		_retry->addClickHandler([=] {
			MTP::restart();
		});
		updateRetryGeometry();
	} else if (!hasRetry) {
		_retry = nullptr;
	}
}

void ConnectingWidget::updateVisibility() {
	const auto value = currentVisibility();
	if (value == 0. && _contentWidth.animating()) {
		_contentWidth.finish();
		updateWidth();
	}
	_visibilityValues.fire_copy(value);
}

float64 ConnectingWidget::currentVisibility() const {
	return _visibility.current(_currentLayout.visible ? 1. : 0.);
}

auto ConnectingWidget::computeLayout(const State &state) const -> Layout {
	auto result = Layout();
	result.proxyEnabled = state.useProxy;
	result.progressShown = (state.type != State::Type::Connected);
	result.visible = state.useProxy
		|| state.type == State::Type::Connecting
		|| state.type == State::Type::Waiting;
	switch (state.type) {
	case State::Type::Connecting:
		result.text = state.underCursor ? lang(lng_connecting) : QString();
		break;

	case State::Type::Waiting:
		Assert(state.waitTillRetry > 0);
		result.text = lng_reconnecting(lt_count, state.waitTillRetry);
		break;
	}
	result.textWidth = st::normalFont->width(result.text);
	const auto maxTextWidth = (state.type == State::Type::Waiting)
		? st::normalFont->width(lng_reconnecting(lt_count, 88))
		: result.textWidth;
	result.contentWidth = (result.textWidth > 0)
		? (st::connectingTextPadding.left()
			+ result.textWidth
			+ st::connectingTextPadding.right())
		: 0;
	if (state.type == State::Type::Waiting) {
		result.contentWidth += st::connectingRetryLink.padding.left()
			+ st::connectingRetryLink.font->width(
				lang(lng_reconnecting_try_now))
			+ st::connectingRetryLink.padding.right();
	}
	result.hasRetry = (state.type == State::Type::Waiting);
	return result;
}

void ConnectingWidget::updateWidth() {
	const auto current = _contentWidth.current(_currentLayout.contentWidth);
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
	resize(desired.size());
	if (!_contentWidth.animating()) {
		_progress->setVisible(_currentLayout.progressShown);
	}
	update();
}

rpl::producer<bool> AdaptiveIsOneColumn() {
	return rpl::single(
		Adaptive::OneColumn()
	) | rpl::then(base::ObservableViewer(
		Adaptive::Changed()
	) | rpl::map([] {
		return Adaptive::OneColumn();
	}));
}

} // namespace Window
