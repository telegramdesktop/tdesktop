/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/polls/info_polls_results_widget.h"

#include "info/polls/info_polls_results_inner_widget.h"
#include "boxes/peer_list_box.h"

namespace Info {
namespace Polls {

Memento::Memento(not_null<PollData*> poll, FullMsgId contextId)
: ContentMemento(poll, contextId) {
}

Section Memento::section() const {
	return Section(Section::Type::PollResults);
}

void Memento::setListStates(base::flat_map<
		QByteArray,
		std::unique_ptr<PeerListState>> states) {
	_listStates = std::move(states);
}

auto Memento::listStates()
-> base::flat_map<QByteArray, std::unique_ptr<PeerListState>> {
	return std::move(_listStates);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

Memento::~Memento() = default;

Widget::Widget(QWidget *parent, not_null<Controller*> controller)
: ContentWidget(parent, controller)
, _inner(setInnerWidget(
	object_ptr<InnerWidget>(
		this,
		controller,
		controller->poll(),
		controller->pollContextId()))) {
	_inner->showPeerInfoRequests(
	) | rpl::start_with_next([=](not_null<PeerData*> peer) {
		controller->showPeerInfo(peer);
	}, _inner->lifetime());
	_inner->scrollToRequests(
	) | rpl::start_with_next([=](const Ui::ScrollToRequest &request) {
		scrollTo(request);
	}, _inner->lifetime());

	controller->setCanSaveChanges(rpl::single(false));
}

not_null<PollData*> Widget::poll() const {
	return _inner->poll();
}

FullMsgId Widget::contextId() const {
	return _inner->contextId();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	//if (const auto myMemento = dynamic_cast<Memento*>(memento.get())) {
	//	Assert(myMemento->self() == self());

	//	if (_inner->showInternal(myMemento)) {
	//		return true;
	//	}
	//}
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
	auto result = std::make_shared<Memento>(poll(), contextId());
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

} // namespace Polls
} // namespace Info
