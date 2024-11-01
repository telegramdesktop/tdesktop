/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/statistics/info_statistics_widget.h"

#include "data/data_session.h"
#include "data/data_stories.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "info/statistics/info_statistics_inner_widget.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/ui_utility.h"

namespace Info::Statistics {

Memento::Memento(not_null<Controller*> controller)
: ContentMemento(controller->statisticsTag()) {
}

Memento::Memento(not_null<PeerData*> peer, FullMsgId contextId)
: ContentMemento(Tag{ peer, contextId, {} }) {
}

Memento::Memento(not_null<PeerData*> peer, FullStoryId storyId)
: ContentMemento(Tag{ peer, {}, storyId }) {
}

Memento::~Memento() = default;

Section Memento::section() const {
	return Section(Section::Type::Statistics);
}

void Memento::setState(SavedState state) {
	_state = std::move(state);
}

SavedState Memento::state() {
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
		controller->statisticsTag().peer,
		controller->statisticsTag().contextId,
		controller->statisticsTag().storyId))) {
	_inner->showRequests(
	) | rpl::start_with_next([=](InnerWidget::ShowRequest request) {
		if (request.history) {
			controller->showPeerHistory(
				request.history.peer,
				Window::SectionShow::Way::Forward,
				request.history.msg);
		} else if (request.info) {
			controller->showPeerInfo(request.info);
		} else if (request.messageStatistic || request.storyStatistic) {
			controller->showSection(Make(
				controller->statisticsTag().peer,
				request.messageStatistic,
				request.storyStatistic));
		} else if (const auto &s = request.story) {
			if (const auto peer = controller->session().data().peer(s.peer)) {
				controller->parentController()->openPeerStory(
					peer,
					s.story,
					{ Data::StoriesContextSingle() });
			}
		}
	}, _inner->lifetime());
	_inner->scrollToRequests(
	) | rpl::start_with_next([this](const Ui::ScrollToRequest &request) {
		scrollTo(request);
	}, _inner->lifetime());
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	return false;
}

rpl::producer<QString> Widget::title() {
	return controller()->statisticsTag().contextId
		? tr::lng_stats_message_title()
		: controller()->statisticsTag().storyId
		? tr::lng_stats_story_title()
		: tr::lng_stats_title();
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

std::shared_ptr<Info::Memento> Make(
		not_null<PeerData*> peer,
		FullMsgId contextId,
		FullStoryId storyId) {
	const auto memento = storyId
		? std::make_shared<Memento>(peer, storyId)
		: std::make_shared<Memento>(peer, contextId);
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(1, std::move(memento)));
}

} // namespace Info::Statistics

