/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/info_layer_widget.h"

#include <rpl/mappers.h>
#include "info/info_content_widget.h"
#include "info/info_top_bar.h"
#include "info/info_memento.h"
#include "ui/rp_widget.h"
#include "ui/focus_persister.h"
#include "ui/widgets/buttons.h"
#include "window/section_widget.h"
#include "window/window_controller.h"
#include "window/main_window.h"
#include "auth_session.h"
#include "styles/style_info.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"

namespace Info {

LayerWidget::LayerWidget(
	not_null<Window::Controller*> controller,
	not_null<Memento*> memento)
: _controller(controller)
, _content(this, controller, Wrap::Layer, memento) {
	setupHeightConsumers();
}

LayerWidget::LayerWidget(
	not_null<Window::Controller*> controller,
	not_null<MoveMemento*> memento)
: _controller(controller)
, _content(memento->takeContent(this, Wrap::Layer)) {
	setupHeightConsumers();
}

void LayerWidget::setupHeightConsumers() {
	_content->scrollTillBottomChanges(
	) | rpl::filter([this] {
		return !_inResize;
	}) | rpl::start_with_next([this] {
		resizeToWidth(width());
	}, lifetime());
	_content->desiredHeightValue(
	) | rpl::start_with_next([this](int height) {
		accumulate_max(_desiredHeight, height);
		if (_content && !_inResize) {
			resizeToWidth(width());
		}
	}, lifetime());
}

void LayerWidget::showFinished() {
}

void LayerWidget::parentResized() {
	auto parentSize = parentWidget()->size();
	auto parentWidth = parentSize.width();
	if (parentWidth < MinimalSupportedWidth()) {
		Ui::FocusPersister persister(this);
		auto localCopy = _controller;
		auto memento = MoveMemento(std::move(_content));
		localCopy->hideSpecialLayer(anim::type::instant);
		localCopy->showSection(
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
	//auto memento = MoveMemento(std::move(_content));
	//localCopy->hideSpecialLayer(anim::type::instant);

	//// When creating third section in response to the window
	//// size allowing it to fit without window resize we want
	//// to save that we didn't extend the window while showing
	//// the third section, so that when we close it we won't
	//// shrink the window size.
	////
	//// See https://github.com/telegramdesktop/tdesktop/issues/4091
	//Auth().settings().setThirdSectionExtendedBy(0);

	//Auth().settings().setThirdSectionInfoEnabled(true);
	//Auth().saveSettingsDelayed();
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
	if (_content->showInternal(memento, params)) {
		if (params.activation != anim::activation::background) {
			Ui::hideLayer();
		}
		return true;
	}
	return false;
}

int LayerWidget::MinimalSupportedWidth() {
	auto minimalMargins = 2 * st::infoMinimalLayerMargin;
	return st::infoMinimalWidth + minimalMargins;
}

int LayerWidget::resizeGetHeight(int newWidth) {
	if (!parentWidget() || !_content) {
		return 0;
	}
	_inResize = true;
	auto guard = gsl::finally([&] { _inResize = false; });

	auto parentSize = parentWidget()->size();
	auto windowWidth = parentSize.width();
	auto windowHeight = parentSize.height();
	auto newLeft = (windowWidth - newWidth) / 2;
	auto newTop = snap(
		windowHeight / 24,
		st::infoLayerTopMinimal,
		st::infoLayerTopMaximal);
	auto newBottom = newTop;
	auto desiredHeight = st::boxRadius + _desiredHeight + st::boxRadius;
	accumulate_min(desiredHeight, windowHeight - newTop - newBottom);

	// First resize content to new width and get the new desired height.
	auto contentLeft = 0;
	auto contentTop = st::boxRadius;
	auto contentBottom = st::boxRadius;
	auto contentWidth = newWidth;
	auto contentHeight = desiredHeight - contentTop - contentBottom;
	auto scrollTillBottom = _content->scrollTillBottom(contentHeight);
	auto additionalScroll = std::min(scrollTillBottom, newBottom);

	desiredHeight += additionalScroll;
	contentHeight += additionalScroll;
	_tillBottom = (newTop + desiredHeight >= windowHeight);
	if (_tillBottom) {
		contentHeight += contentBottom;
		additionalScroll += contentBottom;
	}
	_content->updateGeometry({
		contentLeft,
		contentTop,
		contentWidth,
		contentHeight }, additionalScroll);

	auto newGeometry = QRect(newLeft, newTop, newWidth, desiredHeight);
	if (newGeometry != geometry()) {
		_content->forceContentRepaint();
	}
	if (newGeometry.topLeft() != geometry().topLeft()) {
		move(newGeometry.topLeft());
	}

	return desiredHeight;
}

void LayerWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();
	auto r = st::boxRadius;
	auto parts = RectPart::None | 0;
	if (clip.intersects({ 0, 0, width(), r })) {
		parts |= RectPart::FullTop;
	}
	if (!_tillBottom) {
		if (clip.intersects({ 0, height() - r, width(), r })) {
			parts |= RectPart::FullBottom;
		}
	}
	if (parts) {
		App::roundRect(
			p,
			rect(),
			st::boxBg,
			BoxCorners,
			nullptr,
			parts);
	}
}

} // namespace Info
