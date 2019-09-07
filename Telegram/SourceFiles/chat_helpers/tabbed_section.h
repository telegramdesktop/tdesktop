/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "window/section_widget.h"
#include "window/section_memento.h"

namespace ChatHelpers {

class TabbedSelector;

class TabbedMemento : public Window::SectionMemento {
public:
	TabbedMemento() = default;
	TabbedMemento(TabbedMemento &&other) = default;
	TabbedMemento &operator=(TabbedMemento &&other) = default;

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

};

class TabbedSection : public Window::SectionWidget {
public:
	TabbedSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void beforeHiding();
	void afterShown();

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	bool forceAnimateBack() const override {
		return true;
	}
	// Float player interface.
	bool wheelEventFromFloatPlayer(QEvent *e) override;
	QRect rectForFloatPlayer() const override;

	~TabbedSection();

protected:
	void resizeEvent(QResizeEvent *e) override;

	void showFinishedHook() override {
		afterShown();
	}

private:
	const not_null<TabbedSelector*> _selector;

};

} // namespace ChatHelpers
