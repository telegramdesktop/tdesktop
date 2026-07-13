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
#include "info/profile/tabs/info_profile_tabs_host.h"
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

Info::Section Memento::section() const {
	return Info::Section(Info::Section::Type::Profile);
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
, _inner(UseClassicProfileScroll()
	? setupFlexibleInnerWidget(
		object_ptr<InnerWidget>(this, controller, origin),
		_flexibleScroll)
	: setInnerWidget(
		object_ptr<InnerWidget>(this, controller, origin)))
, _pinnedToTop(_inner->createPinnedToTop(this))
, _pinnedToBottom(_inner->createPinnedToBottom(this)) {
	controller->setSearchEnabledByContent(false);

	const auto classic = UseClassicProfileScroll();
	const auto tabs = (_inner->tabsHost() != nullptr);
	const auto flexible = _pinnedToTop
		&& _pinnedToTop->minimumHeight()
		&& _inner->hasFlexibleTopBar();
	if (classic) {
		_inner->move(0, 0);
	}

	_inner->scrollToRequests(
	) | rpl::on_next([this, tabs](Ui::ScrollToRequest request) {
		if (request.ymin < 0) {
			scrollTopRestore(
				qMin(scrollTopSave(), request.ymax));
		} else if (!tabs) {
			scrollTo(request);
		} else {
			// Inner coordinates miss the flexible cover top padding.
			const auto reserve = innerTopReserve();
			scrollTo({
				request.ymin + reserve,
				(request.ymax < 0) ? -1 : (request.ymax + reserve),
			});
		}
	}, lifetime());

	_inner->backRequest() | rpl::on_next([=] {
		checkBeforeClose([=] { controller->showBackFromStack(); });
	}, _inner->lifetime());

	setSwipeInterceptor([this](Ui::Controls::SwipeHandlerInitData data) {
		return swipeTabsFinishData(data);
	});

	if (!classic && flexible) {
		setupFlexibleRegularScroll(_inner, _pinnedToTop.get(), tabs);
	} else if (_pinnedToTop) {
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

	if (classic && flexible) {
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
			_flexibleScroll,
			tabs);
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

	setupTabsStripFloat();
}

void Widget::setupTabsStripFloat() {
	const auto host = _inner->tabsHost();
	if (!host || !_pinnedToTop) {
		return;
	}
	_tabsHost = base::make_weak(host);
	_tabsStrip = base::make_weak(host->stripWidget().get());
	_inner->tabsDockedValue(
	) | rpl::distinct_until_changed(
	) | rpl::on_next([=](bool docked) {
		const auto host = _tabsHost.get();
		const auto strip = _tabsStrip.get();
		if (!host || !strip) {
			return;
		}
		if (docked) {
			if (!_tabsStripFloat) {
				_tabsStripFloat = base::make_unique_q<Ui::RpWidget>(this);
				ContentWidget::setViewport(_tabsStripFloat->events(
				) | rpl::filter([](not_null<QEvent*> e) {
					return (e->type() == QEvent::Wheel);
				}));
			}
			strip->setParent(_tabsStripFloat.get());
			strip->moveToLeft(0, 0);
			strip->show();
			_tabsStripFloat->show();
			_tabsStripFloat->raise();
			updateTabsStripFloatGeometry();
		} else if (_tabsStripFloat) {
			_tabsStripFloat->hide();
			host->returnStrip();
		}
	}, lifetime());

	rpl::combine(
		widthValue(),
		host->stripWidget()->heightValue(),
		host->stripWidget()->naturalWidthValue()
	) | rpl::on_next([=] {
		updateTabsStripFloatGeometry();
	}, lifetime());
}

void Widget::updateTabsStripFloatGeometry() {
	const auto strip = _tabsStrip.get();
	if (!strip
		|| !_tabsStripFloat
		|| _tabsStripFloat->isHidden()
		|| !_pinnedToTop) {
		return;
	}
	const auto stripWidth = std::min(strip->naturalWidth(), width());
	strip->resizeToWidth(stripWidth);
	strip->moveToLeft(0, 0);
	_tabsStripFloat->setGeometry(
		(width() - stripWidth) / 2,
		_pinnedToTop->minimumHeight(),
		stripWidth,
		strip->height());
}

auto Widget::swipeTabsFinishData(Ui::Controls::SwipeHandlerInitData data)
-> Ui::Controls::SwipeHandlerFinishData {
	const auto host = _inner->tabsHost();
	if (!host) {
		return {};
	}
	const auto shift = Ui::MapFrom(_inner, host, QPoint());
	const auto body = host->bodyGeometry().translated(shift);
	if (!body.contains(data.cursorPosition)) {
		return {};
	}
	const auto toNextTab = (data.direction == Qt::LeftToRight);
	auto callback = host->prepareSwitch(toNextTab);
	return callback
		? Ui::Controls::DefaultSwipeBackHandlerFinishData(
			std::move(callback))
		: Ui::Controls::SwipeHandlerFinishData();
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
