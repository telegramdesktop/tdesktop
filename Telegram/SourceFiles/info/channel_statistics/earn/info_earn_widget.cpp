/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/channel_statistics/earn/info_earn_widget.h"

#include "info/channel_statistics/earn/info_earn_inner_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"

namespace Info::ChannelEarn {

Memento::Memento(not_null<Controller*> controller)
: ContentMemento(Info::Statistics::Tag{
	controller->statisticsPeer(),
	{},
	{},
}) {
}

Memento::Memento(not_null<PeerData*> peer)
: ContentMemento(Info::Statistics::Tag{ peer, {}, {} }) {
}

Memento::~Memento() = default;

Section Memento::section() const {
	return Section(Section::Type::ChannelEarn);
}

void Memento::setState(SavedState state) {
	_state = std::move(state);
}

Memento::SavedState Memento::state() {
	return base::take(_state);
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
: ContentWidget(parent, controller)
, _inner(setInnerWidget(
	object_ptr<InnerWidget>(
		this,
		controller,
		controller->statisticsPeer()))) {
	_inner->showRequests(
	) | rpl::start_with_next([=](InnerWidget::ShowRequest request) {
	}, _inner->lifetime());
	_inner->scrollToRequests(
	) | rpl::start_with_next([=](const Ui::ScrollToRequest &request) {
		scrollTo(request);
	}, _inner->lifetime());
}

not_null<PeerData*> Widget::peer() const {
	return _inner->peer();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	return (memento->statisticsPeer() == peer());
}

rpl::producer<QString> Widget::title() {
	return tr::lng_channel_earn_title();
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

rpl::producer<bool> Widget::desiredShadowVisibility() const {
	return rpl::single<bool>(true);
}

void Widget::showFinished() {
	_inner->showFinished();
}

void Widget::setInnerFocus() {
	_inner->setInnerFocus();
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

std::shared_ptr<Info::Memento> Make(not_null<PeerData*> peer) {
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(
			1,
			std::make_shared<Memento>(peer)));
}

} // namespace Info::ChannelEarn
