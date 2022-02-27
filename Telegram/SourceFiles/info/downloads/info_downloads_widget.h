/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"
#include "info/media/info_media_widget.h"

namespace Ui {
class SearchFieldController;
} // namespace Ui

namespace Info::Downloads {

class InnerWidget;

class Memento final : public ContentMemento {
public:
	Memento(not_null<Controller*> controller);
	Memento(not_null<UserData*> self);
	~Memento();

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	[[nodiscard]] Media::Memento &media() {
		return _media;
	}
	[[nodiscard]] const Media::Memento &media() const {
		return _media;
	}

private:
	Media::Memento _media;

};

class Widget final : public ContentWidget {
public:
	Widget(QWidget *parent, not_null<Controller*> controller);

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento);

	rpl::producer<SelectedItems> selectedListValue() const override;
	void selectionAction(SelectionAction action) override;

private:
	void saveState(not_null<Memento*> memento);
	void restoreState(not_null<Memento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	InnerWidget *_inner = nullptr;

};

[[nodiscard]] std::shared_ptr<Info::Memento> Make(not_null<UserData*> self);

} // namespace Info::Downloads
