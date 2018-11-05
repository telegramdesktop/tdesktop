/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/layer_widget.h"
#include "media/player/media_player_float.h"

namespace Window {
class Controller;
} // namespace Window

namespace Info {

class Memento;
class MoveMemento;
class WrapWidget;
class TopBar;

class LayerWidget
	: public Window::LayerWidget
	, private ::Media::Player::FloatDelegate {
public:
	LayerWidget(
		not_null<Window::Controller*> controller,
		not_null<Memento*> memento);
	LayerWidget(
		not_null<Window::Controller*> controller,
		not_null<MoveMemento*> memento);

	void showFinished() override;
	void parentResized() override;

	bool takeToThirdSection() override;
	bool showSectionInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;

	bool closeByOutsideClick() const override;

	static int MinimalSupportedWidth();

	~LayerWidget();

protected:
	int resizeGetHeight(int newWidth) override;
	void doSetInnerFocus() override;

	void paintEvent(QPaintEvent *e) override;

private:
	not_null<::Media::Player::FloatDelegate*> floatPlayerDelegate();
	not_null<Ui::RpWidget*> floatPlayerWidget() override;
	not_null<Window::Controller*> floatPlayerController() override;
	not_null<Window::AbstractSectionWidget*> floatPlayerGetSection(
		Window::Column column) override;
	void floatPlayerEnumerateSections(Fn<void(
		not_null<Window::AbstractSectionWidget*> widget,
		Window::Column widgetColumn)> callback) override;
	bool floatPlayerIsVisible(not_null<HistoryItem*> item) override;

	void setupHeightConsumers();

	not_null<Window::Controller*> _controller;
	object_ptr<WrapWidget> _content;

	int _desiredHeight = 0;
	bool _inResize = false;
	bool _tillBottom = false;

};

} // namespace Info
