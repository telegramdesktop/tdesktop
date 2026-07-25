/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/section_widget.h"
#include "window/section_memento.h"

namespace Ui {
class PlainShadow;
class ScrollArea;
} // namespace Ui

namespace Wallet {

class FixedBar;

class SectionMemento final : public Window::SectionMemento {
public:
	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

};

class SectionWidget final : public Window::SectionWidget {
public:
	SectionWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	~SectionWidget();

	bool hasTopBarShadow() const override {
		return true;
	}

	QPixmap grabForShowAnimation(
		const Window::SectionSlideParams &params) override;

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;

	void setInternalState(
		const QRect &geometry,
		not_null<SectionMemento*> memento);

	QRect floatPlayerAvailableRect() override;
	bool floatPlayerHandleWheelEvent(QEvent *e) override;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void showAnimatedHook(
		const Window::SectionSlideParams &params) override;
	void showFinishedHook() override;
	void doSetInnerFocus() override;

private:
	void setupContent();
	void updateAdaptiveLayout();

	object_ptr<Ui::ScrollArea> _scroll;
	object_ptr<FixedBar> _fixedBar;
	object_ptr<Ui::PlainShadow> _fixedBarShadow;
	Ui::RpWidget *_container = nullptr;

};

} // namespace Wallet
