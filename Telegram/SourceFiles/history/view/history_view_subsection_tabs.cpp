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
#include "dialogs/dialogs_main_list.h"
#include "history/history.h"
#include "lang/lang_keys.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kDefaultLimit = 5;AssertIsDebug()// 10;

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
	setupHorizontal(parent);
}

SubsectionTabs::~SubsectionTabs() {
	delete base::take(_horizontal);
	delete base::take(_vertical);
	delete base::take(_shadow);
}

void SubsectionTabs::setupHorizontal(not_null<QWidget*> parent) {
	delete base::take(_vertical);
	_horizontal = Ui::CreateChild<Ui::RpWidget>(parent);
	_horizontal->show();

	if (!_shadow) {
		_shadow = Ui::CreateChild<Ui::PlainShadow>(parent);
		_shadow->show();
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
	const auto tabs = scroll->setOwnedWidget(
		object_ptr<Ui::SettingsSlider>(scroll, st::chatTabsSlider));
	tabs->sectionActivated() | rpl::start_with_next([=](int active) {
		if (active >= 0
			&& active < _slice.size()
			&& _active != _slice[active]) {
			auto params = Window::SectionShow();
			params.way = Window::SectionShow::Way::ClearStack;
			params.animated = anim::type::instant;
			_controller->showThread(_slice[active], {}, params);
		}
	}, tabs->lifetime());

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

	rpl::merge(
		scroll->scrolls(),
		_scrollCheckRequests.events(),
		scroll->widthValue() | rpl::skip(1) | rpl::map_to(rpl::empty)
	) | rpl::start_with_next([=] {
		const auto width = scroll->width();
		const auto left = scroll->scrollLeft();
		const auto max = scroll->scrollLeftMax();
		const auto availableLeft = left;
		const auto availableRight = (max - left);
		if (max <= 2 * width && _afterAvailable > 0) {
			_beforeLimit *= 2;
			_afterLimit *= 2;
		}
		if (availableLeft < width
			&& _beforeSkipped.value_or(0) > 0
			&& !_slice.empty()) {
			_around = _slice.front();
			refreshSlice();
		} else if (availableRight < width) {
			if (_afterAvailable > 0) {
				_around = _slice.back();
				refreshSlice();
			} else if (!_afterSkipped.has_value()) {
				_loading = true;
				loadMore();
			}
		}
	}, _horizontal->lifetime());

	dataChanged() | rpl::start_with_next([=] {
		if (_loading) {
			_loading = false;
			refreshSlice();
		}
	}, _horizontal->lifetime());

	_horizontal->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto togglew = toggle->width();
		const auto height = size.height();
		scroll->setGeometry(togglew, 0, size.width() - togglew, height);
	}, scroll->lifetime());

	_horizontal->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(_horizontal).fillRect(clip, st::windowBg);
	}, _horizontal->lifetime());

	_refreshed.events_starting_with_copy(
		rpl::empty
	) | rpl::start_with_next([=] {
		auto sections = std::vector<TextWithEntities>();
		const auto manager = &_history->owner().customEmojiManager();
		auto activeIndex = -1;
		for (const auto &thread : _slice) {
			if (thread == _active) {
				activeIndex = int(sections.size());
			}
			if (const auto topic = thread->asTopic()) {
				sections.push_back(topic->titleWithIcon());
			} else if (const auto sublist = thread->asSublist()) {
				const auto peer = sublist->sublistPeer();
				sections.push_back(TextWithEntities().append(
					Ui::Text::SingleCustomEmoji(
						manager->peerUserpicEmojiData(peer),
						u"@"_q)
				).append(' ').append(peer->shortName()));
			} else {
				sections.push_back(tr::lng_filters_all_short(
					tr::now,
					Ui::Text::WithEntities));
			}
		}
		const auto paused = [=] {
			return _controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::Any);
		};

		auto scrollSavingThread = (Data::Thread*)nullptr;
		auto scrollSavingShift = 0;
		auto scrollSavingIndex = -1;
		if (const auto count = tabs->sectionsCount()) {
			const auto scrollLeft = scroll->scrollLeft();
			auto indexLeft = tabs->lookupSectionLeft(0);
			for (auto index = 0; index != count; ++index) {
				const auto nextLeft = (index + 1 != count)
					? tabs->lookupSectionLeft(index + 1)
					: (indexLeft + scrollLeft + 1);
				if (indexLeft <= scrollLeft && nextLeft > scrollLeft) {
					scrollSavingThread = _sectionsSlice[index];
					scrollSavingShift = scrollLeft - indexLeft;
					break;
				}
				indexLeft = nextLeft;
			}
			scrollSavingIndex = scrollSavingThread
				? int(ranges::find(_slice, not_null(scrollSavingThread))
					- begin(_slice))
				: -1;
			if (scrollSavingIndex == _slice.size()) {
				scrollSavingIndex = -1;
				for (auto index = 0; index != count; ++index) {
					const auto thread = _sectionsSlice[index];
					if (ranges::contains(_slice, thread)) {
						scrollSavingThread = thread;
						scrollSavingShift = scrollLeft
							- tabs->lookupSectionLeft(index);
						scrollSavingIndex = index;
						break;
					}
				}
			}
		}

		tabs->setSections(sections, Core::TextContext({
			.session = &_history->session(),
		}), paused);
		tabs->fitWidthToSections();
		tabs->setActiveSectionFast(activeIndex);
		_sectionsSlice = _slice;
		_horizontal->resize(
			_horizontal->width(),
			std::max(toggle->height(), tabs->height()));
		if (scrollSavingIndex >= 0) {
			scroll->scrollToX(tabs->lookupSectionLeft(scrollSavingIndex)
				+ scrollSavingShift);
		}

		_scrollCheckRequests.fire({});
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
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(_vertical);
	scroll->show();

	_vertical->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto toggleh = toggle->height();
		const auto width = size.width();
		scroll->setGeometry(0, toggleh, width, size.height() - toggleh);
	}, scroll->lifetime());

	_vertical->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(_vertical).fillRect(clip, st::windowBg);
	}, _vertical->lifetime());

	_refreshed.events_starting_with_copy(
		rpl::empty
	) | rpl::start_with_next([=] {
		_vertical->resize(std::max(toggle->width(), 0), 0);
	}, _vertical->lifetime());
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
			_horizontal->y() + _horizontal->height(),
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
	return _horizontal ? _horizontal->height() : 0;
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
	if (const auto forum = _history->peer->forum()) {
		forum->topicDestroyed(
		) | rpl::start_with_next([=](not_null<Data::ForumTopic*> topic) {
			if (_around == topic) {
				_around = _history;
				refreshSlice();
			}
		}, _lifetime);
	} else if (const auto monoforum = _history->peer->monoforum()) {
		monoforum->sublistDestroyed(
		) | rpl::start_with_next([=](not_null<Data::SavedSublist*> sublist) {
			if (_around == sublist) {
				_around = _history;
				refreshSlice();
			}
		}, _lifetime);
	} else {
		Unexpected("Peer in SubsectionTabs::track.");
	}
}

void SubsectionTabs::refreshSlice() {
	const auto forum = _history->peer->forum();
	const auto monoforum = _history->peer->monoforum();
	Assert(forum || monoforum);

	const auto list = forum
		? forum->topicsList()
		: monoforum->chatsList();
	auto slice = std::vector<not_null<Data::Thread*>>();
	const auto guard = gsl::finally([&] {
		if (_slice != slice) {
			_slice = std::move(slice);
			_refreshed.fire({});
		}
	});
	if (!list) {
		slice.push_back(_history);
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
		slice.push_back(_history);
	}
	for (auto i = from; i != till; ++i) {
		slice.push_back((*i)->thread());
	}
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
	return true;
}

bool SubsectionTabs::UsedFor(not_null<Data::Thread*> thread) {
	const auto history = thread->owningHistory();
	if (history->amMonoforumAdmin()) {
		return true;
	}
	const auto channel = history->peer->asChannel();
	return channel
		&& channel->isForum()
		&& ((channel->flags() & ChannelDataFlag::ForumTabs) || true); AssertIsDebug();
}

} // namespace HistoryView
