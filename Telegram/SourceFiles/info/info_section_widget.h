/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/event_stream.h>
#include "window/section_widget.h"

namespace Ui {
class SettingsSlider;
} // namespace Ui

namespace Window {
class ConnectionState;
} // namespace Window

namespace Info {

class Memento;
class MoveMemento;
class Controller;
class WrapWidget;
enum class Wrap;

class SectionWidget final : public Window::SectionWidget {
public:
	SectionWidget(
		QWidget *parent,
		not_null<Window::SessionController*> window,
		Wrap wrap,
		not_null<Memento*> memento);
	SectionWidget(
		QWidget *parent,
		not_null<Window::SessionController*> window,
		Wrap wrap,
		not_null<MoveMemento*> memento);

	Dialogs::RowDescriptor activeChat() const override;

	bool hasTopBarShadow() const override;
	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;

	object_ptr<Ui::LayerWidget> moveContentToLayer(
		QRect bodyGeometry) override;

	// Float player interface.
	bool floatPlayerHandleWheelEvent(QEvent *e) override;
	QRect floatPlayerAvailableRect() override;

protected:
	void doSetInnerFocus() override;
	void showFinishedHook() override;

	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;

private:
	void init();

	object_ptr<WrapWidget> _content;
	object_ptr<Ui::RpWidget> _topBarSurrogate = { nullptr };
	std::unique_ptr<Window::ConnectionState> _connecting;

};

} // namespace Info
