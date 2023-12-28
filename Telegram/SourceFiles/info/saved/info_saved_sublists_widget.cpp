/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/saved/info_saved_sublists_widget.h"
//
#include "data/data_saved_messages.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_inner_widget.h"
#include "history/view/history_view_sublist_section.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"

namespace Info::Saved {

SublistsMemento::SublistsMemento(not_null<Main::Session*> session)
: ContentMemento(session->user(), nullptr, PeerId()) {
}

Section SublistsMemento::section() const {
	return Section(Section::Type::SavedSublists);
}

object_ptr<ContentWidget> SublistsMemento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<SublistsWidget>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

SublistsMemento::~SublistsMemento() = default;

SublistsWidget::SublistsWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<Dialogs::InnerWidget>(
		this,
		controller->parentController(),
		rpl::single(Dialogs::InnerWidget::ChildListShown())));
	_inner->showSavedSublists();
	_inner->setNarrowRatio(0.);

	_inner->chosenRow() | rpl::start_with_next([=](Dialogs::ChosenRow row) {
		if (const auto sublist = row.key.sublist()) {
			controller->showSection(
				std::make_shared<HistoryView::SublistMemento>(sublist),
				Window::SectionShow::Way::Forward);
		}
	}, _inner->lifetime());

	const auto saved = &controller->session().data().savedMessages();
	_inner->heightValue() | rpl::start_with_next([=] {
		if (!saved->supported()) {
			crl::on_main(controller, [=] {
				controller->showSection(
					Memento::Default(controller->session().user()),
					Window::SectionShow::Way::Backward);
			});
		}
	}, lifetime());

	_inner->setLoadMoreCallback([=] {
		saved->loadMore();
	});
}

rpl::producer<QString> SublistsWidget::title() {
	return tr::lng_saved_messages();
}

bool SublistsWidget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto my = dynamic_cast<SublistsMemento*>(memento.get())) {
		restoreState(my);
		return true;
	}
	return false;
}

void SublistsWidget::setInternalState(
		const QRect &geometry,
		not_null<SublistsMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> SublistsWidget::doCreateMemento() {
	auto result = std::make_shared<SublistsMemento>(
		&controller()->session());
	saveState(result.get());
	return result;
}

void SublistsWidget::saveState(not_null<SublistsMemento*> memento) {
	memento->setScrollTop(scrollTopSave());
}

void SublistsWidget::restoreState(not_null<SublistsMemento*> memento) {
	scrollTopRestore(memento->scrollTop());
}

} // namespace Info::Saved
