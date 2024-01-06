/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "info/info_content_widget.h"

namespace Dialogs {
class InnerWidget;
} // namespace Dialogs

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Info::Saved {

class SublistsMemento final : public ContentMemento {
public:
	explicit SublistsMemento(not_null<Main::Session*> session);

	object_ptr<ContentWidget> createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) override;

	Section section() const override;

	~SublistsMemento();

private:

};

class SublistsWidget final : public ContentWidget {
public:
	SublistsWidget(
		QWidget *parent,
		not_null<Controller*> controller);

	bool showInternal(
		not_null<ContentMemento*> memento) override;

	void setInternalState(
		const QRect &geometry,
		not_null<SublistsMemento*> memento);

	rpl::producer<QString> title() override;
	rpl::producer<QString> subtitle() override;

private:
	void saveState(not_null<SublistsMemento*> memento);
	void restoreState(not_null<SublistsMemento*> memento);

	std::shared_ptr<ContentMemento> doCreateMemento() override;

	void setupOtherTypes();

	const not_null<Ui::VerticalLayout*> _layout;
	Dialogs::InnerWidget *_list = nullptr;

};

} // namespace Info::Saved
