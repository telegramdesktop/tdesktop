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
#include "ui/effects/ripple_animation.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/dynamic_image.h"
#include "ui/dynamic_thumbnails.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"

namespace HistoryView {
namespace {

constexpr auto kDefaultLimit = 5; AssertIsDebug()// 10;
constexpr auto kMaxNameLines = 3;

class VerticalSlider final : public Ui::RpWidget {
public:
	explicit VerticalSlider(not_null<QWidget*> parent);

	struct Section {
		std::shared_ptr<Ui::DynamicImage> userpic;
		QString text;
	};

	void setSections(std::vector<Section> sections, Fn<bool()> paused);
	void setActiveSectionFast(int active);

	void fitHeightToSections();

	[[nodiscard]] rpl::producer<int> sectionActivated() const {
		return _sectionActivated.events();
	}

	[[nodiscard]] int sectionsCount() const;
	[[nodiscard]] int lookupSectionTop(int index) const;

private:
	struct Tab {
		std::shared_ptr<Ui::DynamicImage> userpic;
		Ui::Text::String text;
		std::unique_ptr<Ui::RippleAnimation> ripple;
		int top = 0;
		int height = 0;
		bool subscribed = false;
	};
	struct Range {
		int top = 0;
		int height = 0;
	};

	void paintEvent(QPaintEvent *e) override;
	void timerEvent(QTimerEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

	void startRipple(int index);
	[[nodiscard]] int getIndexFromPosition(QPoint position) const;
	[[nodiscard]] QImage prepareRippleMask(int index, const Tab &tab);

	void activateCallback();
	[[nodiscard]] Range getFinalActiveRange() const;

	const style::ChatTabsVertical &_st;
	Ui::RoundRect _bar;
	std::vector<Tab> _tabs;
	int _active = -1;
	int _pressed = -1;
	Ui::Animations::Simple _activeTop;
	Ui::Animations::Simple _activeHeight;

	int _timerId = -1;
	crl::time _callbackAfterMs = 0;

	rpl::event_stream<int> _sectionActivated;
	Fn<bool()> _paused;

};

VerticalSlider::VerticalSlider(not_null<QWidget*> parent)
: RpWidget(parent)
, _st(st::chatTabsVertical)
, _bar(_st.barRadius, _st.barFg) {
	setCursor(style::cur_pointer);
}

void VerticalSlider::setSections(
		std::vector<Section> sections,
		Fn<bool()> paused) {
	auto old = base::take(_tabs);
	_tabs.reserve(sections.size());

	for (auto &section : sections) {
		const auto i = ranges::find(old, section.userpic, &Tab::userpic);
		if (i != end(old)) {
			_tabs.push_back(std::move(*i));
			old.erase(i);
		} else {
			_tabs.push_back({ .userpic = std::move(section.userpic), });
		}
		_tabs.back().text = Ui::Text::String(
			_st.nameStyle,
			section.text,
			kDefaultTextOptions,
			_st.nameWidth);
	}
	for (const auto &was : old) {
		if (was.subscribed) {
			was.userpic->subscribeToUpdates(nullptr);
		}
	}
}

void VerticalSlider::setActiveSectionFast(int active) {
	_active = active;
	_activeTop.stop();
	_activeHeight.stop();
}

void VerticalSlider::fitHeightToSections() {
	auto top = 0;
	for (auto &tab : _tabs) {
		tab.top = top;
		tab.height = _st.baseHeight + std::min(
			_st.nameStyle.font->height * kMaxNameLines,
			tab.text.countHeight(_st.nameWidth, true));
		top += tab.height;
	}
	resize(_st.width, top);
}

int VerticalSlider::sectionsCount() const {
	return int(_tabs.size());
}

int VerticalSlider::lookupSectionTop(int index) const {
	Expects(index >= 0 && index < _tabs.size());

	return _tabs[index].top;
}

VerticalSlider::Range VerticalSlider::getFinalActiveRange() const {
	return (_active >= 0)
		? Range{ _tabs[_active].top, _tabs[_active].height }
		: Range();
}

void VerticalSlider::paintEvent(QPaintEvent *e) {
	const auto finalRange = getFinalActiveRange();
	const auto range = Range{
		int(base::SafeRound(_activeTop.value(finalRange.top))),
		int(base::SafeRound(_activeHeight.value(finalRange.height))),
	};

	auto p = QPainter(this);
	auto clip = e->rect();
	const auto drawRect = [&](QRect rect) {
		_bar.paint(p, rect);
	};
	const auto nameLeft = (_st.width - _st.nameWidth) / 2;
	for (auto &tab : _tabs) {
		if (!clip.intersects(QRect(0, tab.top, width(), tab.height))) {
			continue;
		}
		const auto divider = std::max(std::min(tab.height, range.height), 1);
		const auto active = 1.
			- std::clamp(
				std::abs(range.top - tab.top) / float64(divider),
				0.,
				1.);
		if (tab.ripple) {
			const auto color = anim::color(
				_st.rippleBg,
				_st.rippleBgActive,
				active);
			tab.ripple->paint(p, 0, tab.top, width(), &color);
			if (tab.ripple->empty()) {
				tab.ripple.reset();
			}
		}

		if (!tab.subscribed) {
			tab.subscribed = true;
			tab.userpic->subscribeToUpdates([=] { update(); });
		}
		const auto &image = tab.userpic->image(_st.userpicSize);
		const auto userpicLeft = (width() - _st.userpicSize) / 2;
		p.drawImage(userpicLeft, tab.top + _st.userpicTop, image);
		p.setPen(anim::pen(_st.nameFg, _st.nameFgActive, active));
		tab.text.draw(p, {
			.position = QPoint(nameLeft, tab.top + _st.nameTop),
			.outerWidth = width(),
			.availableWidth = _st.nameWidth,
			.align = style::al_top,
			.paused = _paused && _paused(),
		});
	}
	if (range.height > 0) {
		const auto add = _st.barStroke / 2;
		drawRect(myrtlrect(-add, range.top, _st.barStroke, range.height));
	}
}

void VerticalSlider::timerEvent(QTimerEvent *e) {
	activateCallback();
}

void VerticalSlider::startRipple(int index) {
	if (!_st.ripple.showDuration) {
		return;
	}
	auto &tab = _tabs[index];
	if (!tab.ripple) {
		auto mask = prepareRippleMask(index, tab);
		tab.ripple = std::make_unique<Ui::RippleAnimation>(
			_st.ripple,
			std::move(mask),
			[this] { update(); });
	}
	const auto point = mapFromGlobal(QCursor::pos());
	tab.ripple->add(point - QPoint(0, tab.top));
}

QImage VerticalSlider::prepareRippleMask(int index, const Tab &tab) {
	return Ui::RippleAnimation::RectMask(QSize(width(), tab.height));
}

int VerticalSlider::getIndexFromPosition(QPoint position) const {
	const auto count = int(_tabs.size());
	for (auto i = 0; i != count; ++i) {
		const auto &tab = _tabs[i];
		if (position.y() < tab.top + tab.height) {
			return i;
		}
	}
	return count - 1;
}

void VerticalSlider::mousePressEvent(QMouseEvent *e) {
	for (auto i = 0, count = int(_tabs.size()); i != count; ++i) {
		auto &tab = _tabs[i];
		if (tab.top <= e->y() && e->y() < tab.top + tab.height) {
			startRipple(i);
			_pressed = i;
			break;
		}
	}
}

void VerticalSlider::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed < 0) {
		return;
	}

	const auto index = getIndexFromPosition(e->pos());
	if (pressed < _tabs.size()) {
		if (_tabs[pressed].ripple) {
			_tabs[pressed].ripple->lastStop();
		}
	}
	if (index == pressed) {
		if (_active != index) {
			_callbackAfterMs = crl::now() + _st.duration;
			activateCallback();

			const auto from = getFinalActiveRange();
			_active = index;
			const auto to = getFinalActiveRange();
			const auto updater = [this] { update(); };
			_activeTop.start(updater, from.top, to.top, _st.duration);
			_activeHeight.start(
				updater,
				from.height,
				to.height,
				_st.duration);
		}
	}
}

void VerticalSlider::activateCallback() {
	if (_timerId >= 0) {
		killTimer(_timerId);
		_timerId = -1;
	}
	auto ms = crl::now();
	if (ms >= _callbackAfterMs) {
		_sectionActivated.fire_copy(_active);
	} else {
		_timerId = startTimer(_callbackAfterMs - ms, Qt::PreciseTimer);
	}
}

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

	_horizontal->sizeValue(
	) | rpl::start_with_next([=](QSize size) {
		const auto togglew = toggle->width();
		const auto height = size.height();
		scroll->setGeometry(togglew, 0, size.width() - togglew, height);
	}, scroll->lifetime());

	_horizontal->paintRequest() | rpl::start_with_next([=](QRect clip) {
		QPainter(_horizontal).fillRect(
			clip.intersected(
				_horizontal->rect().marginsRemoved(
					{ 0, 0, 0, st::lineWidth })),
			st::windowBg);
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
	const auto scroll = Ui::CreateChild<Ui::ScrollArea>(
		_vertical,
		st::chatTabsScroll);
	scroll->show();
	const auto tabs = scroll->setOwnedWidget(
		object_ptr<VerticalSlider>(scroll));
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

	rpl::merge(
		scroll->scrolls(),
		_scrollCheckRequests.events(),
		scroll->heightValue() | rpl::skip(1) | rpl::map_to(rpl::empty)
	) | rpl::start_with_next([=] {
		const auto height = scroll->height();
		const auto top = scroll->scrollTop();
		const auto max = scroll->scrollTopMax();
		const auto availableTop = top;
		const auto availableBottom = (max - top);
		if (max <= 2 * height && _afterAvailable > 0) {
			_beforeLimit *= 2;
			_afterLimit *= 2;
		}
		if (availableTop < height
			&& _beforeSkipped.value_or(0) > 0
			&& !_slice.empty()) {
			_around = _slice.front();
			refreshSlice();
		} else if (availableBottom < height) {
			if (_afterAvailable > 0) {
				_around = _slice.back();
				refreshSlice();
			} else if (!_afterSkipped.has_value()) {
				_loading = true;
				loadMore();
			}
		}
	}, _vertical->lifetime());

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
		auto sections = std::vector<VerticalSlider::Section>();
		auto activeIndex = -1;
		for (const auto &thread : _slice) {
			if (thread == _active) {
				activeIndex = int(sections.size());
			}
			if (const auto topic = thread->asTopic()) {
				sections.push_back({
					.userpic = (topic->iconId()
						? Ui::MakeEmojiThumbnail(
							&topic->owner(),
							Data::SerializeCustomEmojiId(topic->iconId()))
						: Ui::MakeUserpicThumbnail(
						_controller->session().user())),
					.text = topic->title(),
				});
			} else if (const auto sublist = thread->asSublist()) {
				const auto peer = sublist->sublistPeer();
				sections.push_back({
					.userpic = Ui::MakeUserpicThumbnail(peer),
					.text = peer->shortName(),
				});
			} else {
				sections.push_back({
					.userpic = Ui::MakeUserpicThumbnail(
						_controller->session().user()),
					.text = tr::lng_filters_all_short(tr::now),
				});
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
			const auto scrollTop = scroll->scrollTop();
			auto indexTop = tabs->lookupSectionTop(0);
			for (auto index = 0; index != count; ++index) {
				const auto nextTop = (index + 1 != count)
					? tabs->lookupSectionTop(index + 1)
					: (indexTop + scrollTop + 1);
				if (indexTop <= scrollTop && nextTop > scrollTop) {
					scrollSavingThread = _sectionsSlice[index];
					scrollSavingShift = scrollTop - indexTop;
					break;
				}
				indexTop = nextTop;
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
						scrollSavingShift = scrollTop
							- tabs->lookupSectionTop(index);
						scrollSavingIndex = index;
						break;
					}
				}
			}
		}

		tabs->setSections(sections, paused);
		tabs->fitHeightToSections();
		tabs->setActiveSectionFast(activeIndex);
		_sectionsSlice = _slice;
		_vertical->resize(
			std::max(toggle->width(), tabs->width()),
			_vertical->height());
		if (scrollSavingIndex >= 0) {
			scroll->scrollToY(tabs->lookupSectionTop(scrollSavingIndex)
				+ scrollSavingShift);
		}

		_scrollCheckRequests.fire({});
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
