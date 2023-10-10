/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/statistics/info_statistics_widget.h"

#include "info/statistics/info_statistics_inner_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"

namespace Info::Statistics {

Memento::Memento(not_null<Controller*> controller)
: ContentMemento(Tag{
	controller->statisticsPeer(),
	controller->statisticsContextId(),
}) {
}

Memento::Memento(not_null<PeerData*> peer, FullMsgId contextId)
: ContentMemento(Tag{ peer, contextId }) {
}

Memento::~Memento() = default;

Section Memento::section() const {
	return Section(Section::Type::Statistics);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller);
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
		controller->statisticsPeer(),
		controller->statisticsContextId()))) {
	_inner->showRequests(
	) | rpl::start_with_next([=](InnerWidget::ShowRequest request) {
		if (request.history) {
			controller->showPeerHistory(
				request.history.peer,
				Window::SectionShow::Way::Forward,
				request.history.msg);
		} else if (request.info) {
			controller->showPeerInfo(request.info);
		} else if (request.messageStatistic) {
			controller->showSection(Make(
				controller->statisticsPeer(),
				request.messageStatistic));
		}
	}, _inner->lifetime());
	_inner->scrollToRequests(
	) | rpl::start_with_next([=](const Ui::ScrollToRequest &request) {
		scrollTo(request);
	}, _inner->lifetime());
}

not_null<PeerData*> Widget::peer() const {
	return _inner->peer();
}

FullMsgId Widget::contextId() const {
	return _inner->contextId();
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	return false;
}

rpl::producer<QString> Widget::title() {
	return _inner->contextId()
		? tr::lng_stats_message_title()
		: tr::lng_stats_title();
}

rpl::producer<bool> Widget::desiredShadowVisibility() const {
	return rpl::single<bool>(true);
}

void Widget::showFinished() {
	_inner->showFinished();
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(controller());
	return result;
}

std::shared_ptr<Info::Memento> Make(
		not_null<PeerData*> peer,
		FullMsgId contextId) {
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(
			1,
			std::make_shared<Memento>(peer, contextId)));
}

} // namespace Info::Statistics

