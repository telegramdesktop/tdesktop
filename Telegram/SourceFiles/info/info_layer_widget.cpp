/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_layer_widget.h"

#include "info/info_content_widget.h"
#include "info/info_top_bar.h"
#include "info/info_memento.h"
#include "ui/rp_widget.h"
#include "ui/focus_persister.h"
#include "ui/widgets/buttons.h"
#include "ui/cached_round_corners.h"
#include "window/section_widget.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/main_window.h"
#include "main/main_session.h"
#include "core/application.h"
#include "styles/style_info.h"
#include "styles/style_window.h"
#include "styles/style_layers.h"

namespace Info {

LayerWidget::LayerWidget(
	not_null<Window::SessionController*> controller,
	not_null<Memento*> memento)
: _controller(controller)
, _contentWrap(this, controller, Wrap::Layer, memento) {
	setupHeightConsumers();
	controller->window().replaceFloatPlayerDelegate(floatPlayerDelegate());
}

LayerWidget::LayerWidget(
	not_null<Window::SessionController*> controller,
	not_null<MoveMemento*> memento)
: _controller(controller)
, _contentWrap(memento->takeContent(this, Wrap::Layer)) {
	setupHeightConsumers();
	controller->window().replaceFloatPlayerDelegate(floatPlayerDelegate());
}

auto LayerWidget::floatPlayerDelegate()
-> not_null<::Media::Player::FloatDelegate*> {
	return static_cast<::Media::Player::FloatDelegate*>(this);
}

not_null<Ui::RpWidget*> LayerWidget::floatPlayerWidget() {
	return this;
}

void LayerWidget::floatPlayerToggleGifsPaused(bool paused) {
	constexpr auto kReason = Window::GifPauseReason::RoundPlaying;
	if (paused) {
		_controller->enableGifPauseReason(kReason);
	} else {
		_controller->disableGifPauseReason(kReason);
	}
}

auto LayerWidget::floatPlayerGetSection(Window::Column column)
-> not_null<::Media::Player::FloatSectionDelegate*> {
	Expects(_contentWrap != nullptr);

	return _contentWrap;
}

void LayerWidget::floatPlayerEnumerateSections(Fn<void(
		not_null<::Media::Player::FloatSectionDelegate*> widget,
		Window::Column widgetColumn)> callback) {
	Expects(_contentWrap != nullptr);

	callback(_contentWrap, Window::Column::Second);
}

bool LayerWidget::floatPlayerIsVisible(not_null<HistoryItem*> item) {
	return false;
}

void LayerWidget::floatPlayerDoubleClickEvent(
		not_null<const HistoryItem*> item) {
	_controller->showMessage(item);
}

void LayerWidget::setupHeightConsumers() {
	Expects(_contentWrap != nullptr);

	_contentWrap->scrollTillBottomChanges(
	) | rpl::filter([this] {
		if (!_inResize) {
			return true;
		}
		_pendingResize = true;
		return false;
	}) | rpl::start_with_next([this] {
		resizeToWidth(width());
	}, lifetime());

	_contentWrap->grabbingForExpanding(
	) | rpl::start_with_next([=](bool grabbing) {
		if (grabbing) {
			_savedHeight = _contentWrapHeight;
			_savedHeightAnimation = base::take(_heightAnimation);
			setContentHeight(_desiredHeight);
		} else {
			_heightAnimation = base::take(_savedHeightAnimation);
			setContentHeight(_savedHeight);
		}
	}, lifetime());

	_contentWrap->desiredHeightValue(
	) | rpl::start_with_next([this](int height) {
		if (!height) {
			// New content arrived.
			_heightAnimated = _heightAnimation.animating();
			return;
		}
		std::swap(_desiredHeight, height);
		if (!height
			|| (_heightAnimated && !_heightAnimation.animating())) {
			_heightAnimated = true;
			setContentHeight(_desiredHeight);
		} else {
			_heightAnimated = true;
			_heightAnimation.start([=] {
				setContentHeight(_heightAnimation.value(_desiredHeight));
			}, _contentWrapHeight, _desiredHeight, st::slideDuration);
			resizeToWidth(width());
		}
	}, lifetime());
}

void LayerWidget::setContentHeight(int height) {
	if (_contentWrapHeight == height) {
		return;
	}
	_contentWrapHeight = height;
	if (_inResize) {
		_pendingResize = true;
	} else if (_contentWrap) {
		resizeToWidth(width());
	}
}

void LayerWidget::showFinished() {
	floatPlayerShowVisible();
	_contentWrap->showFast();
}

void LayerWidget::parentResized() {
	if (!_contentWrap) {
		return;
	}

	auto parentSize = parentWidget()->size();
	auto parentWidth = parentSize.width();
	if (parentWidth < MinimalSupportedWidth()) {
		Ui::FocusPersister persister(this);
		restoreFloatPlayerDelegate();

		auto memento = std::make_shared<MoveMemento>(std::move(_contentWrap));

		// We want to call hideSpecialLayer synchronously to avoid glitches,
		// but we can't destroy LayerStackWidget from its' resizeEvent,
		// because QWidget has such code for resizing:
		//
		// QResizeEvent e(r.size(), olds);
		// QApplication::sendEvent(q, &e);
		// if (q->windowHandle())
		//   q->update();
		//
		// So we call it queued. It would be cool to call it 'right after'
		// the resize event handling was finished.
		InvokeQueued(this, [=] {
			_controller->hideSpecialLayer(anim::type::instant);
		});
		_controller->showSection(
			std::move(memento),
			Window::SectionShow(
				Window::SectionShow::Way::Forward,
				anim::type::instant,
				anim::activation::background));
	//
	// There was a layout logic which caused layer info to become a
	// third column info if the window size allows, but it was decided
	// to keep layer info and third column info separated.
	//
	//} else if (_controller->canShowThirdSectionWithoutResize()) {
	//	takeToThirdSection();
	} else {
		auto newWidth = qMin(
			parentWidth - 2 * st::infoMinimalLayerMargin,
			st::infoDesiredWidth);
		resizeToWidth(newWidth);
	}
}

bool LayerWidget::takeToThirdSection() {
	return false;
	//
	// There was a layout logic which caused layer info to become a
	// third column info if the window size allows, but it was decided
	// to keep layer info and third column info separated.
	//
	//Ui::FocusPersister persister(this);
	//auto localCopy = _controller;
	//auto memento = MoveMemento(std::move(_contentWrap));
	//localCopy->hideSpecialLayer(anim::type::instant);

	//// When creating third section in response to the window
	//// size allowing it to fit without window resize we want
	//// to save that we didn't extend the window while showing
	//// the third section, so that when we close it we won't
	//// shrink the window size.
	////
	//// See https://github.com/telegramdesktop/tdesktop/issues/4091
	//localCopy->session()().settings().setThirdSectionExtendedBy(0);

	//localCopy->session()().settings().setThirdSectionInfoEnabled(true);
	//localCopy->session()().saveSettingsDelayed();
	//localCopy->showSection(
	//	std::move(memento),
	//	Window::SectionShow(
	//		Window::SectionShow::Way::ClearStack,
	//		anim::type::instant,
	//		anim::activation::background));
	//return true;
}

bool LayerWidget::showSectionInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	if (_contentWrap && _contentWrap->showInternal(memento, params)) {
		if (params.activation != anim::activation::background) {
			_controller->parentController()->hideLayer();
		}
		return true;
	}
	return false;
}

bool LayerWidget::closeByOutsideClick() const {
	return _contentWrap ? _contentWrap->closeByOutsideClick() : true;
}

int LayerWidget::MinimalSupportedWidth() {
	const auto minimalMargins = 2 * st::infoMinimalLayerMargin;
	return st::infoMinimalWidth + minimalMargins;
}

int LayerWidget::resizeGetHeight(int newWidth) {
	if (!parentWidget() || !_contentWrap || !newWidth) {
		return 0;
	}
	constexpr auto kMaxAttempts = 16;
	auto attempts = 0;
	while (true) {
		_inResize = true;
		const auto newGeometry = countGeometry(newWidth);
		_inResize = false;
		if (!_pendingResize) {
			const auto oldGeometry = geometry();
			if (newGeometry != oldGeometry) {
				_contentWrap->forceContentRepaint();
			}
			if (newGeometry.topLeft() != oldGeometry.topLeft()) {
				move(newGeometry.topLeft());
			}
			floatPlayerUpdatePositions();
			return newGeometry.height();
		}
		_pendingResize = false;
		Assert(attempts++ < kMaxAttempts);
	}
}

QRect LayerWidget::countGeometry(int newWidth) {
	const auto &parentSize = parentWidget()->size();
	const auto windowWidth = parentSize.width();
	const auto windowHeight = parentSize.height();
	const auto newLeft = (windowWidth - newWidth) / 2;
	const auto newTop = std::clamp(
		windowHeight / 24,
		st::infoLayerTopMinimal,
		st::infoLayerTopMaximal);
	const auto newBottom = newTop;

	const auto bottomRadius = st::boxRadius;
	const auto maxVisibleHeight = windowHeight - newTop;
	// Top rounding is included in _contentWrapHeight.
	auto desiredHeight = _contentWrapHeight + bottomRadius;
	accumulate_min(desiredHeight, maxVisibleHeight - newBottom);

	// First resize content to new width and get the new desired height.
	const auto contentLeft = 0;
	const auto contentTop = 0;
	const auto contentBottom = bottomRadius;
	const auto contentWidth = newWidth;
	auto contentHeight = desiredHeight - contentTop - contentBottom;
	const auto scrollTillBottom = _contentWrap->scrollTillBottom(
		contentHeight);
	auto additionalScroll = std::min(scrollTillBottom, newBottom);

	const auto expanding = (_desiredHeight > _contentWrapHeight);

	desiredHeight += additionalScroll;
	contentHeight += additionalScroll;
	_tillBottom = (desiredHeight >= maxVisibleHeight);
	if (_tillBottom) {
		additionalScroll += contentBottom;
	}
	_contentTillBottom = _tillBottom && !_contentWrap->scrollBottomSkip();
	if (_contentTillBottom) {
		contentHeight += contentBottom;
	}
	_contentWrap->updateGeometry({
		contentLeft,
		contentTop,
		contentWidth,
		contentHeight,
	}, expanding, additionalScroll, maxVisibleHeight);

	return QRect(newLeft, newTop, newWidth, desiredHeight);
}

void LayerWidget::doSetInnerFocus() {
	if (_contentWrap) {
		_contentWrap->setInnerFocus();
	}
}

void LayerWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto clip = e->rect();
	const auto radius = st::boxRadius;
	const auto &corners = Ui::CachedCornerPixmaps(Ui::BoxCorners);
	if (!_tillBottom) {
		const auto bottom = QRect{ 0, height() - radius, width(), radius };
		if (clip.intersects(bottom)) {
			if (const auto rounding = _contentWrap->bottomSkipRounding()) {
				rounding->paint(p, rect(), RectPart::FullBottom);
			} else {
				Ui::FillRoundRect(p, bottom, st::boxBg, {
					.p = { QPixmap(), QPixmap(), corners.p[2], corners.p[3] }
				});
			}
		}
	} else if (!_contentTillBottom) {
		const auto rounding = _contentWrap->bottomSkipRounding();
		const auto &color = rounding ? rounding->color() : st::boxBg;
		p.fillRect(0, height() - radius, width(), radius, color);
	}
	if (_contentWrap->animatingShow()) {
		const auto top = QRect{ 0, 0, width(), radius };
		if (clip.intersects(top)) {
			Ui::FillRoundRect(p, top, st::boxBg, {
				.p = { corners.p[0], corners.p[1], QPixmap(), QPixmap() }
			});
		}
		p.fillRect(0, radius, width(), height() - 2 * radius, st::boxBg);
	}
}

void LayerWidget::restoreFloatPlayerDelegate() {
	if (!_floatPlayerDelegateRestored) {
		_floatPlayerDelegateRestored = true;
		_controller->window().restoreFloatPlayerDelegate(
			floatPlayerDelegate());
	}
}

void LayerWidget::closeHook() {
	restoreFloatPlayerDelegate();
}

LayerWidget::~LayerWidget() {
	if (!Core::Quitting()) {
		restoreFloatPlayerDelegate();
	}
}

} // namespace Info
