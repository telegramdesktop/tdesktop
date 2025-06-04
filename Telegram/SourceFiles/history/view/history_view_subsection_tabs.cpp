/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_subsection_tabs.h"

#include "core/ui_integration.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_channel.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_user.h"
#include "dialogs/dialogs_main_list.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/controls/subsection_tabs_slider.h"
#include "ui/effects/ripple_animation.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kDefaultLimit = 5; AssertIsDebug()// 10;

} // namespace

SubsectionTabs::SubsectionTabs(
	not_null<Window::SessionController*> controller,
	not_null<Ui::RpWidget*> parent,
	not_null<Data::Thread*> thread)
: _controller(controller)
, _history(thread->owningHistory())
, _active(thread)
, _around(thread)
, _beforeLimit(kDefaultLimit)
, _afterLimit(kDefaultLimit) {
	track();
	refreshSlice();
	setup(parent);

	dataChanged() | rpl::start_with_next([=] {
		if (_loading) {
			_loading = false;
			refreshSlice();
		}
	}, _lifetime);
}

SubsectionTabs::~SubsectionTabs() {
	delete base::take(_horizontal);
	delete base::take(_vertical);
	delete base::take(_shadow);
}

void SubsectionTabs::setup(not_null<Ui::RpWidget*> parent) {
	const auto peerId = _history->peer->id;
	if (session().settings().verticalSubsectionTabs(peerId)) {
		setupVertical(parent);
	} else {
		setupHorizontal(parent);
	}
}

void SubsectionTabs::setupHorizontal(not_null<QWidget*> parent) {
	delete base::take(_vertical);
	_horizontal = Ui::CreateChild<Ui::RpWidget>(parent);
	_horizontal->show();

	if (!_shadow) {
		_shadow = Ui::CreateChild<Ui::PlainShadow>(parent);
		_shadow->show();
	} else {
		_shadow->raise();
	}

	const auto toggle = Ui::CreateChild<Ui::IconButton>(
		_horizontal,
		st::chatTabsToggle);
	toggle->show();
	toggle->setClickedCallback([=] {
		toggleModes();
	});
	toggle->move(0, 0);
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		_horizontal,
		st::chatTabsScroll,
		true);
	scroll->show();
	const auto shadow = Ui::CreateChild<Ui::PlainShadow>(_horizontal);
	const auto slider = scroll->setOwnedWidget(
		object_ptr<Ui::HorizontalSlider>(scroll));
	setupSlider(scroll, slider, false);

	shadow->showOn(rpl::single(
		rpl::empty
	) | rpl::then(
		scroll->scrolls()
	) | rpl::map([=] { return scroll->scrollLeft() > 0; }));
	shadow->setAttribute(Qt::WA_TransparentForMouseEvents);

	_horizontal->resize(
		_horizontal->width(),
		std::max(toggle->height(), slider->height()));

	scroll->setCustomWheelProcess([=](not_null<QWheelEvent*> e) {
		const auto pixelDelta = e->pixelDelta();
		const auto angleDelta = e->angleDelta();
		if (std::abs(pixelDelta.x()) + std::abs(angleDelta.x())) {
			return false;
		}
		const auto y = pixelDelta.y() ? pixelDelta.y() : angleDelta.y();
		scroll->scrollToX(scroll->scrollLeft() - y);
		return true;
	});

	_horizontal->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto togglew = toggle->width();
		const auto height = size.height();
		scroll->setGeometry(togglew, 0, size.width() - togglew, height);
		shadow->setGeometry(togglew, 0, st::lineWidth, height);
	}, scroll->lifetime());

	_horizontal->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(_horizontal).fillRect(
			clip.intersected(
				_horizontal->rect().marginsRemoved(
					{ 0, 0, 0, st::lineWidth })),
			st::windowBg);
	}, _horizontal->lifetime());
}

void SubsectionTabs::setupVertical(not_null<QWidget*> parent) {
	delete base::take(_horizontal);
	_vertical = Ui::CreateChild<Ui::RpWidget>(parent);
	_vertical->show();

	if (!_shadow) {
		_shadow = Ui::CreateChild<Ui::PlainShadow>(parent);
		_shadow->show();
	}

	const auto toggle = Ui::CreateChild<Ui::IconButton>(
		_vertical,
		st::chatTabsToggle);
	toggle->show();
	const auto active = &st::chatTabsToggleActive;
	toggle->setIconOverride(active, active);
	toggle->setClickedCallback([=] {
		toggleModes();
	});
	toggle->move(0, 0);
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		_vertical,
		st::chatTabsScroll);
	scroll->show();
	const auto shadow = Ui::CreateChild<Ui::PlainShadow>(_vertical);
	const auto slider = scroll->setOwnedWidget(
		object_ptr<Ui::VerticalSlider>(scroll));
	setupSlider(scroll, slider, true);

	shadow->showOn(rpl::single(
		rpl::empty
	) | rpl::then(
		scroll->scrolls()
	) | rpl::map([=] { return scroll->scrollTop() > 0; }));
	shadow->setAttribute(Qt::WA_TransparentForMouseEvents);

	_vertical->resize(
		std::max(toggle->width(), slider->width()),
		_vertical->height());

	_vertical->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto toggleh = toggle->height();
		const auto width = size.width();
		scroll->setGeometry(0, toggleh, width, size.height() - toggleh);
		shadow->setGeometry(0, toggleh, width, st::lineWidth);
	}, scroll->lifetime());

	_vertical->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(_vertical).fillRect(clip, st::windowBg);
	}, _vertical->lifetime());
}

void SubsectionTabs::setupSlider(
		not_null<Ui::ScrollArea*> scroll,
		not_null<Ui::SubsectionSlider*> slider,
		bool vertical) {
	slider->sectionActivated() | rpl::start_with_next([=](int active) {
		if (active >= 0
			&& active < _slice.size()
			&& _active != _slice[active].thread) {
			auto params = Window::SectionShow();
			params.way = Window::SectionShow::Way::ClearStack;
			params.animated = anim::type::instant;
			_controller->showThread(_slice[active].thread, {}, params);
		}
	}, slider->lifetime());

	slider->sectionContextMenu() | rpl::start_with_next([=](int index) {
		if (index >= 0 && index < _slice.size()) {
			showThreadContextMenu(_slice[index].thread);
		}
	}, slider->lifetime());

	rpl::merge(
		scroll->scrolls(),
		_scrollCheckRequests.events(),
		(vertical
			? scroll->heightValue()
			: scroll->widthValue()) | rpl::skip(1) | rpl::map_to(rpl::empty)
	) | rpl::start_with_next([=] {
		const auto full = vertical ? scroll->height() : scroll->width();
		const auto scrollValue = vertical
			? scroll->scrollTop()
			: scroll->scrollLeft();
		const auto scrollMax = vertical
			? scroll->scrollTopMax()
			: scroll->scrollLeftMax();
		const auto availableFrom = scrollValue;
		const auto availableTill = (scrollMax - scrollValue);
		if (scrollMax <= 3 * full && _afterAvailable > 0) {
			_beforeLimit *= 2;
			_afterLimit *= 2;
		}
		const auto findMiddle = [&] {
			Expects(!_slice.empty());

			auto best = -1;
			auto bestDistance = -1;
			const auto ideal = scrollValue + (full / 2);
			for (auto i = 0, count = int(_slice.size()); i != count; ++i) {
				const auto a = slider->lookupSectionPosition(i);
				const auto b = (i + 1 == count)
					? (full + scrollMax)
					: slider->lookupSectionPosition(i + 1);
				const auto middle = (a + b) / 2;
				const auto distance = std::abs(middle - ideal);
				if (best < 0 || distance < bestDistance) {
					best = i;
					bestDistance = distance;
				}
			}

			Ensures(best >= 0);
			return best;
		};
		if (availableFrom < full
			&& _beforeSkipped.value_or(0) > 0
			&& !_slice.empty()) {
			_around = _slice[findMiddle()].thread;
			refreshSlice();
		} else if (availableTill < full) {
			if (_afterAvailable > 0) {
				_around = _slice[findMiddle()].thread;
				refreshSlice();
			} else if (!_afterSkipped.has_value()) {
				_loading = true;
				loadMore();
			}
		}
	}, scroll->lifetime());

	_refreshed.events_starting_with_copy(
		rpl::empty
	) | rpl::start_with_next([=] {
		const auto manager = &_history->owner().customEmojiManager();
		const auto paused = [=] {
			return _controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Any);
		};
		auto sections = std::vector<Ui::SubsectionTab>();
		auto activeIndex = -1;
		for (const auto &item : _slice) {
			const auto index = int(sections.size());
			if (item.thread == _active) {
				activeIndex = index;
			}
			const auto textFg = [=] {
				return anim::color(
					st::windowSubTextFg,
					st::windowActiveTextFg,
					slider->buttonActive(slider->buttonAt(index)));
			};
			if (const auto topic = item.thread->asTopic()) {
				if (vertical) {
					const auto general = topic->isGeneral();
					sections.push_back({
						.text = { item.name },
						.userpic = (item.iconId
							? Ui::MakeEmojiThumbnail(
								&topic->owner(),
								Data::SerializeCustomEmojiId(item.iconId),
								paused,
								textFg)
							: Ui::MakeEmojiThumbnail(
								&topic->owner(),
								Data::TopicIconEmojiEntity({
									.title = (general
										? Data::ForumGeneralIconTitle()
										: item.name),
									.colorId = (general
										? Data::ForumGeneralIconColor(
											st::windowSubTextFg->c)
										: topic->colorId()),
								}),
								paused,
								textFg)),
					});
				} else {
					sections.push_back({
						.text = topic->titleWithIcon(),
					});
				}
			} else if (const auto sublist = item.thread->asSublist()) {
				const auto peer = sublist->sublistPeer();
				if (vertical) {
					sections.push_back({
						.text = peer->shortName(),
						.userpic = Ui::MakeUserpicThumbnail(peer),
					});
				} else {
					sections.push_back({
						.text = TextWithEntities().append(
							Ui::Text::SingleCustomEmoji(
								manager->peerUserpicEmojiData(peer),
								u"@"_q)
						).append(' ').append(peer->shortName()),
					});
				}
			} else {
				sections.push_back({
					.text = tr::lng_filters_all_short(tr::now),
					.userpic = Ui::MakeAllSubsectionsThumbnail(textFg),
				});
			}
			auto &section = sections.back();
			section.badges = item.badges;
		}

		auto scrollSavingThread = (Data::Thread*)nullptr;
		auto scrollSavingShift = 0;
		auto scrollSavingIndex = -1;
		if (const auto count = slider->sectionsCount()) {
			const auto scrollValue = vertical
				? scroll->scrollTop()
				: scroll->scrollLeft();
			auto indexPosition = slider->lookupSectionPosition(0);
			for (auto index = 0; index != count; ++index) {
				const auto nextPosition = (index + 1 != count)
					? slider->lookupSectionPosition(index + 1)
					: (indexPosition + scrollValue + 1);
				if (indexPosition <= scrollValue && nextPosition > scrollValue) {
					scrollSavingThread = _sectionsSlice[index].thread;
					scrollSavingShift = scrollValue - indexPosition;
					break;
				}
				indexPosition = nextPosition;
			}
			scrollSavingIndex = scrollSavingThread
				? int(ranges::find(
					_slice,
					not_null(scrollSavingThread),
					&Item::thread
				) - begin(_slice))
				: -1;
			if (scrollSavingIndex == _slice.size()) {
				scrollSavingIndex = -1;
				for (auto index = 0; index != count; ++index) {
					const auto thread = _sectionsSlice[index].thread;
					const auto i = ranges::find(
						_slice,
						thread,
						&Item::thread);
					if (i != end(_slice)) {
						scrollSavingThread = thread;
						scrollSavingShift = scrollValue
							- slider->lookupSectionPosition(index);
						scrollSavingIndex = int(i - begin(_slice));
						break;
					}
				}
			}
		}
		slider->setSections({
			.tabs = std::move(sections),
			.context = Core::TextContext({
				.session = &session(),
			}),
		}, paused);
		slider->setActiveSectionFast(activeIndex);

		_sectionsSlice = _slice;
		if (scrollSavingIndex >= 0) {
			const auto position = scrollSavingShift
				+ slider->lookupSectionPosition(scrollSavingIndex);
			if (vertical) {
				scroll->scrollToY(position);
			} else {
				scroll->scrollToX(position);
			}
		}

		_scrollCheckRequests.fire({});
	}, scroll->lifetime());
}

void SubsectionTabs::showThreadContextMenu(not_null<Data::Thread*> thread) {
	_menu = nullptr;
	_menu = base::make_unique_q<Ui::PopupMenu>(
		_horizontal ? _horizontal : _vertical,
		st::popupMenuExpandedSeparator);

	const auto addAction = Ui::Menu::CreateAddActionCallback(_menu);
	Window::FillDialogsEntryMenu(
		_controller,
		Dialogs::EntryState{
			.key = Dialogs::Key{ thread },
			.section = Dialogs::EntryState::Section::SubsectionTabsMenu,
		},
		addAction);
	if (_menu->empty()) {
		_menu = nullptr;
	} else {
		_menu->popup(QCursor::pos());
	}
}

void SubsectionTabs::loadMore() {
	if (const auto forum = _history->peer->forum()) {
		forum->requestTopics();
	} else if (const auto monoforum = _history->peer->monoforum()) {
		monoforum->loadMore();
	} else {
		Unexpected("Peer in SubsectionTabs::loadMore.");
	}
}

rpl::producer<> SubsectionTabs::dataChanged() const {
	if (const auto forum = _history->peer->forum()) {
		return forum->chatsListChanges();
	} else if (const auto monoforum = _history->peer->monoforum()) {
		return monoforum->chatsListChanges();
	} else {
		Unexpected("Peer in SubsectionTabs::dataChanged.");
	}
}

void SubsectionTabs::toggleModes() {
	Expects((_horizontal || _vertical) && _shadow);

	if (_horizontal) {
		setupVertical(_horizontal->parentWidget());
	} else {
		setupHorizontal(_vertical->parentWidget());
	}
	const auto peerId = _history->peer->id;
	const auto vertical = (_vertical != nullptr);
	session().settings().setVerticalSubsectionTabs(peerId, vertical);
	session().saveSettingsDelayed();

	_layoutRequests.fire({});
}

rpl::producer<> SubsectionTabs::removeRequests() const {
	if (const auto forum = _history->peer->forum()) {
		return forum->destroyed();
	} else if (const auto monoforum = _history->peer->monoforum()) {
		return monoforum->destroyed();
	} else {
		Unexpected("Peer in SubsectionTabs::removeRequests.");
	}
}

void SubsectionTabs::extractToParent(not_null<Ui::RpWidget*> parent) {
	Expects((_horizontal || _vertical) && _shadow);

	if (_vertical) {
		_vertical->hide();
		_vertical->setParent(parent);
	} else {
		_horizontal->hide();
		_horizontal->setParent(parent);
	}
	_shadow->hide();
	_shadow->setParent(parent);
}

void SubsectionTabs::setBoundingRect(QRect boundingRect) {
	Expects((_horizontal || _vertical) && _shadow);

	if (_horizontal) {
		_horizontal->setGeometry(
			boundingRect.x(),
			boundingRect.y(),
			boundingRect.width(),
			_horizontal->height());
		_shadow->setGeometry(
			boundingRect.x(),
			_horizontal->y() + _horizontal->height() - st::lineWidth,
			boundingRect.width(),
			st::lineWidth);
	} else {
		_vertical->setGeometry(
			boundingRect.x(),
			boundingRect.y(),
			_vertical->width(),
			boundingRect.height());
		_shadow->setGeometry(
			_vertical->x() + _vertical->width(),
			boundingRect.y(),
			st::lineWidth,
			boundingRect.height());
	}
}

rpl::producer<> SubsectionTabs::layoutRequests() const {
	return _layoutRequests.events();
}

int SubsectionTabs::leftSkip() const {
	return _vertical ? _vertical->width() : 0;
}

int SubsectionTabs::topSkip() const {
	return _horizontal ? (_horizontal->height() - st::lineWidth) : 0;
}

void SubsectionTabs::raise() {
	Expects((_horizontal || _vertical) && _shadow);

	if (_horizontal) {
		_horizontal->raise();
	} else {
		_vertical->raise();
	}
	_shadow->raise();
}

void SubsectionTabs::show() {
	setVisible(true);
}

void SubsectionTabs::hide() {
	setVisible(false);
}

void SubsectionTabs::setVisible(bool shown) {
	Expects((_horizontal || _vertical) && _shadow);

	if (_horizontal) {
		_horizontal->setVisible(shown);
	} else {
		_vertical->setVisible(shown);
	}
	_shadow->setVisible(shown);
}

void SubsectionTabs::track() {
	using Event = Data::Session::ChatListEntryRefresh;
	if (const auto forum = _history->peer->forum()) {
		forum->topicDestroyed(
		) | rpl::start_with_next([=](not_null<Data::ForumTopic*> topic) {
			if (_around == topic) {
				_around = _history;
				refreshSlice();
			}
		}, _lifetime);

		forum->topicsList()->unreadStateChanges(
		) | rpl::start_with_next([=] {
			scheduleRefresh();
		}, _lifetime);

		forum->owner().chatListEntryRefreshes(
		) | rpl::filter([=](const Event &event) {
			const auto topic = event.filterId ? nullptr : event.key.topic();
			return (topic && topic->forum() == forum);
		}) | rpl::start_with_next([=] {
			scheduleRefresh();
		}, _lifetime);
	} else if (const auto monoforum = _history->peer->monoforum()) {
		monoforum->sublistDestroyed(
		) | rpl::start_with_next([=](not_null<Data::SavedSublist*> sublist) {
			if (_around == sublist) {
				_around = _history;
				refreshSlice();
			}
		}, _lifetime);

		monoforum->chatsList()->unreadStateChanges(
		) | rpl::start_with_next([=] {
			scheduleRefresh();
		}, _lifetime);

		monoforum->owner().chatListEntryRefreshes(
		) | rpl::filter([=](const Event &event) {
			const auto sublist = event.filterId
				? nullptr
				: event.key.sublist();
			return (sublist && sublist->parent() == monoforum);
		}) | rpl::start_with_next([=] {
			scheduleRefresh();
		}, _lifetime);
	} else {
		Unexpected("Peer in SubsectionTabs::track.");
	}
}

void SubsectionTabs::refreshSlice() {
	_refreshScheduled = false;

	const auto forum = _history->peer->forum();
	const auto monoforum = _history->peer->monoforum();
	Assert(forum || monoforum);

	const auto list = forum
		? forum->topicsList()
		: monoforum->chatsList();
	auto slice = std::vector<Item>();
	slice.reserve(_slice.size() + 10);
	const auto guard = gsl::finally([&] {
		if (_slice != slice) {
			_slice = std::move(slice);
			_refreshed.fire({});
		}
	});
	const auto push = [&](not_null<Data::Thread*> thread) {
		const auto topic = thread->asTopic();
		const auto sublist = thread->asSublist();
		auto badges = [&] {
			if (!topic && !sublist) {
				return Dialogs::BadgesState();
			} else if (thread->chatListUnreadState().known) {
				return thread->chatListBadgesState();
			}
			const auto i = ranges::find(_slice, thread, &Item::thread);
			if (i != end(_slice)) {
				// While the unread count is unknown (possibly loading)
				// we can preserve the old badges state, because it won't
				// glitch that way when we stop knowing it for a moment.
				return i->badges;
			}
			return thread->chatListBadgesState();
		}();
		if (topic) {
			// Don't show the small indicators for non-visited unread topics.
			badges.unread = false;
		}
		slice.push_back({
			.thread = thread,
			.badges = badges,
			.iconId = topic ? topic->iconId() : DocumentId(),
			.name = thread->chatListName(),
		});
	};
	if (!list) {
		push(_history);
		_beforeSkipped = _afterSkipped = 0;
		_afterAvailable = 0;
		return;
	}
	const auto &chats = list->indexed()->all();
	auto i = (_around == _history)
		? chats.end()
		: ranges::find(chats, _around, [](not_null<Dialogs::Row*> row) {
			return not_null(row->thread());
		});
	if (i == chats.end()) {
		i = chats.begin();
	}
	const auto takeBefore = std::min(_beforeLimit, int(i - chats.begin()));
	const auto takeAfter = std::min(_afterLimit, int(chats.end() - i));
	const auto from = i - takeBefore;
	const auto till = i + takeAfter;
	_beforeSkipped = std::max(0, int(from - chats.begin()));
	_afterAvailable = std::max(0, int(chats.end() - till));
	_afterSkipped = list->loaded() ? _afterAvailable : std::optional<int>();
	if (from == chats.begin()) {
		push(_history);
	}
	for (auto i = from; i != till; ++i) {
		push((*i)->thread());
	}
}

void SubsectionTabs::scheduleRefresh() {
	if (_refreshScheduled) {
		return;
	}
	_refreshScheduled = true;
	InvokeQueued(_shadow, [=] {
		if (_refreshScheduled) {
			refreshSlice();
		}
	});
}

Main::Session &SubsectionTabs::session() {
	return _history->session();
}

bool SubsectionTabs::switchTo(
		not_null<Data::Thread*> thread,
		not_null<Ui::RpWidget*> parent) {
	Expects((_horizontal || _vertical) && _shadow);

	if (thread->owningHistory() != _history) {
		return false;
	}
	_active = thread;
	if (_vertical) {
		_vertical->setParent(parent);
		_vertical->show();
	} else {
		_horizontal->setParent(parent);
		_horizontal->show();
	}
	_shadow->setParent(parent);
	_shadow->show();
	_refreshed.fire({});
	return true;
}

bool SubsectionTabs::UsedFor(not_null<Data::Thread*> thread) {
	const auto history = thread->owningHistory();
	if (history->amMonoforumAdmin()) {
		return true;
	}
	const auto channel = history->peer->asChannel();
	return channel && channel->useSubsectionTabs();
}

} // namespace HistoryView
