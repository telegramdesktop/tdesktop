/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_widget.h"

#include "dialogs/ui/dialogs_stories_content.h"
#include "history/history.h"
#include "info/profile/info_profile_inner_widget.h"
#include "info/profile/info_profile_members.h"
#include "info/settings/info_settings_widget.h"
#include "ui/widgets/scroll_area.h"
#include "ui/ui_utility.h"
#include "data/data_peer.h"
#include "data/data_channel.h"
#include "data/data_forum_topic.h"
#include "data/data_saved_sublist.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "info/info_controller.h"
#include "styles/style_info.h"

#include <QtWidgets/QApplication>
#include <QtWidgets/QScrollBar>

namespace Info::Settings {
struct SectionCustomTopBarData;
} // namespace Info::Settings

namespace Info::Profile {

using Info::Settings::SectionCustomTopBarData;

Memento::Memento(not_null<Controller*> controller)
: Memento(
	controller->peer(),
	controller->topic(),
	controller->sublist(),
	controller->migratedPeerId(),
	{ v::null }) {
}

Memento::Memento(
	not_null<PeerData*> peer,
	PeerId migratedPeerId,
	Origin origin)
: Memento(peer, nullptr, nullptr, migratedPeerId, origin) {
}

Memento::Memento(
	not_null<PeerData*> peer,
	Data::ForumTopic *topic,
	Data::SavedSublist *sublist,
	PeerId migratedPeerId,
	Origin origin)
: ContentMemento(peer, topic, sublist, migratedPeerId)
, _origin(origin) {
}

Memento::Memento(not_null<Data::ForumTopic*> topic)
: ContentMemento(topic->peer(), topic, nullptr, 0) {
}

Memento::Memento(not_null<Data::SavedSublist*> sublist)
: ContentMemento(sublist->owningHistory()->peer, nullptr, sublist, 0) {
}

Section Memento::section() const {
	return Section(Section::Type::Profile);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller, _origin);
	result->setInternalState(geometry, this);
	return result;
}

void Memento::setMembersState(std::unique_ptr<MembersState> state) {
	_membersState = std::move(state);
}

std::unique_ptr<MembersState> Memento::membersState() {
	return std::move(_membersState);
}

Memento::~Memento() = default;

Widget::Widget(
	QWidget *parent,
	not_null<Controller*> controller,
	Origin origin)
: ContentWidget(parent, controller)
, _inner(
	setupFlexibleInnerWidget(
		object_ptr<InnerWidget>(this, controller, origin),
		_flexibleScroll))
, _pinnedToTop(_inner->createPinnedToTop(this))
, _pinnedToBottom(_inner->createPinnedToBottom(this)) {
	controller->setSearchEnabledByContent(false);

	_inner->move(0, 0);
	_inner->scrollToRequests(
	) | rpl::on_next([this](Ui::ScrollToRequest request) {
		if (request.ymin < 0) {
			scrollTopRestore(
				qMin(scrollTopSave(), request.ymax));
		} else {
			scrollTo(request);
		}
	}, lifetime());

	_inner->backRequest() | rpl::on_next([=] {
		checkBeforeClose([=] { controller->showBackFromStack(); });
	}, _inner->lifetime());

	if (_pinnedToTop) {
		_inner->widthValue(
		) | rpl::on_next([=](int w) {
			_pinnedToTop->resizeToWidth(w);
			setScrollTopSkip(_pinnedToTop->height());
		}, _pinnedToTop->lifetime());

		_pinnedToTop->heightValue(
		) | rpl::on_next([=](int h) {
			setScrollTopSkip(h);
		}, _pinnedToTop->lifetime());
	}

	if (_pinnedToBottom) {
		const auto processHeight = [=] {
			setScrollBottomSkip(_pinnedToBottom->height());
			_pinnedToBottom->moveToLeft(
				_pinnedToBottom->x(),
				height() - _pinnedToBottom->height());
		};

		_inner->sizeValue(
		) | rpl::on_next([=](const QSize &s) {
			_pinnedToBottom->resizeToWidth(s.width());
		}, _pinnedToBottom->lifetime());

		rpl::combine(
			_pinnedToBottom->heightValue(),
			heightValue()
		) | rpl::on_next(processHeight, _pinnedToBottom->lifetime());
	}

	if (_pinnedToTop
		&& _pinnedToTop->minimumHeight()
		&& _inner->hasFlexibleTopBar()) {
		_flexibleScrollHelper = std::make_unique<FlexibleScrollHelper>(
			scroll(),
			_inner,
			_pinnedToTop.get(),
			[=](QMargins margins) {
				ContentWidget::setPaintPadding(std::move(margins));
			},
			[=](rpl::producer<not_null<QEvent*>> &&events) {
				ContentWidget::setViewport(std::move(events));
			},
			_flexibleScroll);
	}
}

void Widget::setInnerFocus() {
	_inner->setFocus();
}

void Widget::enableBackButton() {
	_inner->enableBackButton();
}

void Widget::showFinished() {
	_inner->showFinished();
}

rpl::producer<QString> Widget::title() {
	if (const auto topic = controller()->key().topic()) {
		return topic->peer()->isBot()
			? tr::lng_info_thread_title()
			: tr::lng_info_topic_title();
	} else if (controller()->key().sublist()
		&& controller()->key().sublist()->parentChat()) {
		return tr::lng_profile_direct_messages();
	}
	const auto peer = controller()->key().peer();
	if (const auto user = peer->asUser()) {
		return (user->isBot() && !user->isSupport())
			? tr::lng_info_bot_title()
			: tr::lng_info_user_title();
	} else if (const auto channel = peer->asChannel()) {
		return channel->isMonoforum()
			? tr::lng_profile_direct_messages()
			: channel->isMegagroup()
			? tr::lng_info_group_title()
			: tr::lng_info_channel_title();
	} else if (peer->isChat()) {
		return tr::lng_info_group_title();
	}
	Unexpected("Bad peer type in Info::TitleValue()");
}

rpl::producer<Dialogs::Stories::Content> Widget::titleStories() {
	const auto peer = controller()->key().peer();
	if (peer && !peer->isChat()) {
		return Dialogs::Stories::LastForPeer(peer);
	}
	return nullptr;
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto profileMemento = dynamic_cast<Memento*>(memento.get())) {
		restoreState(profileMemento);
		return true;
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

} // namespace Info::Profile
