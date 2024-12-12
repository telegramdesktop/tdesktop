/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/stories/info_stories_widget.h"

#include "data/data_peer.h"
#include "info/stories/info_stories_inner_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "ui/widgets/scroll_area.h"
#include "lang/lang_keys.h"
#include "ui/ui_utility.h"

namespace Info::Stories {

Memento::Memento(not_null<Controller*> controller)
: ContentMemento(Tag{ controller->storiesPeer(), controller->storiesTab() })
, _media(controller) {
}

Memento::Memento(not_null<PeerData*> peer, Tab tab)
: ContentMemento(Tag{ peer, tab })
, _media(peer, 0, Media::Type::PhotoVideo) {
}

Memento::~Memento() = default;

Section Memento::section() const {
	return Section(Section::Type::Stories);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(
		this,
		controller));
	_inner->setScrollHeightValue(scrollHeightValue());
	_inner->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		scrollTo(request);
	}, _inner->lifetime());
}

void Widget::setIsStackBottom(bool isStackBottom) {
	ContentWidget::setIsStackBottom(isStackBottom);
	_inner->setIsStackBottom(isStackBottom);
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto storiesMemento = dynamic_cast<Memento*>(memento.get())) {
		const auto tab = controller()->key().storiesTab();
		if (storiesMemento->storiesTab() == tab) {
			restoreState(storiesMemento);
			return true;
		}
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
		setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(controller());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
	scrollTopRestore(memento->scrollTop());
}

rpl::producer<SelectedItems> Widget::selectedListValue() const {
	return _inner->selectedListValue();
}

void Widget::selectionAction(SelectionAction action) {
	_inner->selectionAction(action);
}

rpl::producer<QString> Widget::title() {
	const auto peer = controller()->key().storiesPeer();
	return (controller()->key().storiesTab() == Tab::Archive)
		? tr::lng_stories_archive_title()
		: (peer && peer->isSelf())
		? tr::lng_menu_my_profile()
		: tr::lng_stories_my_title();
}

std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer, Tab tab) {
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(
			1,
			std::make_shared<Memento>(peer, tab)));
}

} // namespace Info::Stories

