/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_controller.h"

#include "base/timer.h"
#include "base/power_save_blocker.h"
#include "base/qt_signal_producer.h"
#include "chat_helpers/compose/compose_show.h"
#include "data/data_changes.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "media/stories/media_stories_caption_full_view.h"
#include "media/stories/media_stories_delegate.h"
#include "media/stories/media_stories_header.h"
#include "media/stories/media_stories_sibling.h"
#include "media/stories/media_stories_slider.h"
#include "media/stories/media_stories_reactions.h"
#include "media/stories/media_stories_recent_views.h"
#include "media/stories/media_stories_reply.h"
#include "media/stories/media_stories_view.h"
#include "media/audio/media_audio.h"
#include "ui/rp_widget.h"
#include "styles/style_media_view.h"
#include "styles/style_widgets.h"
#include "styles/style_boxes.h" // UserpicButton

#include <QtGui/QWindow>

namespace Media::Stories {
namespace {

constexpr auto kPhotoProgressInterval = crl::time(100);
constexpr auto kPhotoDuration = 5 * crl::time(1000);
constexpr auto kFullContentFade = 0.35;
constexpr auto kSiblingMultiplierDefault = 0.448;
constexpr auto kSiblingMultiplierMax = 0.72;
constexpr auto kSiblingOutsidePart = 0.24;
constexpr auto kSiblingUserpicSize = 0.3;
constexpr auto kInnerHeightMultiplier = 1.6;
constexpr auto kPreloadUsersCount = 3;
constexpr auto kMarkAsReadAfterSeconds = 1;
constexpr auto kMarkAsReadAfterProgress = 0.2;

} // namespace

class Controller::PhotoPlayback final {
public:
	explicit PhotoPlayback(not_null<Controller*> controller);

	[[nodiscard]] bool paused() const;
	void togglePaused(bool paused);

private:
	void callback();

	const not_null<Controller*> _controller;

	base::Timer _timer;
	crl::time _started = 0;
	crl::time _paused = 0;

};

Controller::PhotoPlayback::PhotoPlayback(not_null<Controller*> controller)
: _controller(controller)
, _timer([=] { callback(); })
, _started(crl::now())
, _paused(_started) {
}

bool Controller::PhotoPlayback::paused() const {
	return _paused != 0;
}

void Controller::PhotoPlayback::togglePaused(bool paused) {
	if (!_paused == !paused) {
		return;
	} else if (paused) {
		const auto now = crl::now();
		if (now - _started >= kPhotoDuration) {
			return;
		}
		_paused = now;
		_timer.cancel();
	} else {
		_started += crl::now() - _paused;
		_paused = 0;
		_timer.callEach(kPhotoProgressInterval);
	}
	callback();
}

void Controller::PhotoPlayback::callback() {
	const auto now = crl::now();
	const auto elapsed = now - _started;
	const auto finished = (now - _started >= kPhotoDuration);
	if (finished) {
		_timer.cancel();
	}
	using State = Player::State;
	const auto state = finished
		? State::StoppedAtEnd
		: _paused
		? State::Paused
		: State::Playing;
	_controller->updatePhotoPlayback({
		.state = state,
		.position = elapsed,
		.receivedTill = kPhotoDuration,
		.length = kPhotoDuration,
		.frequency = 1000,
	});
}

Controller::Controller(not_null<Delegate*> delegate)
: _delegate(delegate)
, _wrap(_delegate->storiesWrap())
, _header(std::make_unique<Header>(this))
, _slider(std::make_unique<Slider>(this))
, _replyArea(std::make_unique<ReplyArea>(this))
, _reactions(std::make_unique<Reactions>(this))
, _recentViews(std::make_unique<RecentViews>(this)) {
	initLayout();

	using namespace rpl::mappers;

	rpl::combine(
		_replyArea->activeValue(),
		_reactions->expandedValue(),
		_1 || _2
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool active) {
		if (active) {
			_captionFullView = nullptr;
		}
		_replyActive = active;
		updateContentFaded();
	}, _lifetime);

	_replyArea->focusedValue(
	) | rpl::start_with_next([=](bool focused) {
		_replyFocused = focused;
		if (!_replyFocused) {
			_reactions->hideIfCollapsed();
		} else if (!_hasSendText) {
			_reactions->show();
		}
	}, _lifetime);

	_replyArea->hasSendTextValue(
	) | rpl::start_with_next([=](bool has) {
		_hasSendText = has;
		if (_replyFocused) {
			if (_hasSendText) {
				_reactions->hide();
			} else {
				_reactions->show();
			}
		}
	}, _lifetime);

	_reactions->chosen(
	) | rpl::start_with_next([=](const Data::ReactionId &id) {
		_replyArea->sendReaction(id);
	}, _lifetime);

	_delegate->storiesLayerShown(
	) | rpl::start_with_next([=](bool shown) {
		_layerShown = shown;
		updatePlayingAllowed();
	}, _lifetime);

	const auto window = _wrap->window()->windowHandle();
	Assert(window != nullptr);
	base::qt_signal_producer(
		window,
		&QWindow::activeChanged
	) | rpl::start_with_next([=] {
		_windowActive = window->isActive();
		updatePlayingAllowed();
	}, _lifetime);
	_windowActive = window->isActive();

	_contentFadeAnimation.stop();
}

Controller::~Controller() = default;

void Controller::updateContentFaded() {
	if (_contentFaded == _replyActive) {
		return;
	}
	_contentFaded = _replyActive;
	_contentFadeAnimation.start(
		[=] { _delegate->storiesRepaint(); },
		_contentFaded ? 0. : 1.,
		_contentFaded ? 1. : 0.,
		st::fadeWrapDuration);
	updatePlayingAllowed();
}

void Controller::initLayout() {
	const auto headerHeight = st::storiesHeaderMargin.top()
		+ st::storiesHeaderPhoto.photoSize
		+ st::storiesHeaderMargin.bottom();
	const auto sliderHeight = st::storiesSliderMargin.top()
		+ st::storiesSliderWidth
		+ st::storiesSliderMargin.bottom();
	const auto outsideHeaderHeight = headerHeight
		+ sliderHeight
		+ st::storiesSliderOutsideSkip;
	const auto fieldMinHeight = st::storiesFieldMargin.top()
		+ st::storiesAttach.height
		+ st::storiesFieldMargin.bottom();
	const auto minHeightForOutsideHeader = st::storiesFieldMargin.bottom()
		+ outsideHeaderHeight
		+ st::storiesMaxSize.height()
		+ fieldMinHeight;

	_layout = _wrap->sizeValue(
	) | rpl::map([=](QSize size) {
		size = QSize(
			std::max(size.width(), st::mediaviewMinWidth),
			std::max(size.height(), st::mediaviewMinHeight));

		auto layout = Layout();
		layout.headerLayout = (size.height() >= minHeightForOutsideHeader)
			? HeaderLayout::Outside
			: HeaderLayout::Normal;

		const auto topSkip = st::storiesFieldMargin.bottom()
			+ (layout.headerLayout == HeaderLayout::Outside
				? outsideHeaderHeight
				: 0);
		const auto bottomSkip = fieldMinHeight;
		const auto maxWidth = size.width() - 2 * st::storiesSideSkip;
		const auto availableHeight = size.height() - topSkip - bottomSkip;
		const auto maxContentHeight = std::min(
			availableHeight,
			st::storiesMaxSize.height());
		const auto nowWidth = maxContentHeight * st::storiesMaxSize.width()
			/ st::storiesMaxSize.height();
		const auto contentWidth = std::min(nowWidth, maxWidth);
		const auto contentHeight = (contentWidth < nowWidth)
			? (contentWidth * st::storiesMaxSize.height()
				/ st::storiesMaxSize.width())
			: maxContentHeight;
		const auto addedTopSkip = (availableHeight - contentHeight) / 2;
		layout.content = QRect(
			(size.width() - contentWidth) / 2,
			addedTopSkip + topSkip,
			contentWidth,
			contentHeight);

		const auto reactionsWidth = st::storiesReactionsWidth;
		layout.reactions = QRect(
			(size.width() - reactionsWidth) / 2,
			layout.content.y(),
			reactionsWidth,
			contentHeight);

		if (layout.headerLayout == HeaderLayout::Outside) {
			layout.header = QRect(
				layout.content.topLeft() - QPoint(0, outsideHeaderHeight),
				QSize(contentWidth, outsideHeaderHeight));
			layout.slider = QRect(
				layout.header.topLeft() + QPoint(0, headerHeight),
				QSize(contentWidth, sliderHeight));
		} else {
			layout.slider = QRect(
				layout.content.topLeft(),
				QSize(contentWidth, sliderHeight));
			layout.header = QRect(
				layout.slider.topLeft() + QPoint(0, sliderHeight),
				QSize(contentWidth, headerHeight));
		}
		layout.controlsWidth = std::max(
			layout.content.width(),
			st::storiesControlsMinWidth);
		layout.controlsBottomPosition = QPoint(
			(size.width() - layout.controlsWidth) / 2,
			(layout.content.y()
				+ layout.content.height()
				+ fieldMinHeight
				- st::storiesFieldMargin.bottom()));
		layout.views = QRect(
			layout.controlsBottomPosition - QPoint(0, fieldMinHeight),
			QSize(layout.controlsWidth, fieldMinHeight));
		layout.autocompleteRect = QRect(
			layout.controlsBottomPosition.x(),
			0,
			layout.controlsWidth,
			layout.controlsBottomPosition.y());

		const auto sidesAvailable = size.width() - layout.content.width();
		const auto widthForSiblings = sidesAvailable
			- 2 * st::storiesFieldMargin.bottom();
		const auto siblingWidthMax = widthForSiblings
			/ (2 * (1. - kSiblingOutsidePart));
		const auto siblingMultiplierMax = std::max(
			kSiblingMultiplierDefault,
			st::storiesSiblingWidthMin / float64(layout.content.width()));
		const auto siblingMultiplier = std::min({
			siblingMultiplierMax,
			kSiblingMultiplierMax,
			siblingWidthMax / layout.content.width(),
		});
		const auto siblingSize = layout.content.size() * siblingMultiplier;
		const auto siblingTop = (size.height() - siblingSize.height()) / 2;
		const auto outsideMax = int(base::SafeRound(
			siblingSize.width() * kSiblingOutsidePart));
		const auto leftAvailable = layout.content.x() - siblingSize.width();
		const auto xDesired = leftAvailable / 3;
		const auto xPossible = std::min(
			xDesired,
			(leftAvailable - st::storiesControlSize));
		const auto xLeft = std::max(xPossible, -outsideMax);
		const auto xRight = size.width() - siblingSize.width() - xLeft;
		const auto userpicSize = int(base::SafeRound(
			siblingSize.width() * kSiblingUserpicSize));
		const auto innerHeight = userpicSize * kInnerHeightMultiplier;
		const auto userpic = [&](QRect geometry) {
			return QRect(
				(geometry.width() - userpicSize) / 2,
				(geometry.height() - innerHeight) / 2,
				userpicSize,
				userpicSize
			).translated(geometry.topLeft());
		};
		const auto nameFontSize = std::max(
			(st::storiesMaxNameFontSize * contentHeight
				/ st::storiesMaxSize.height()),
			st::fsize);
		const auto nameBoundingRect = [&](QRect geometry, bool left) {
			const auto skipSmall = nameFontSize;
			const auto skipBig = skipSmall - std::min(xLeft, 0);
			const auto top = userpic(geometry).y() + innerHeight;
			return QRect(
				left ? skipBig : skipSmall,
				(geometry.height() - innerHeight) / 2,
				geometry.width() - skipSmall - skipBig,
				innerHeight
			).translated(geometry.topLeft());
		};
		const auto left = QRect({ xLeft, siblingTop }, siblingSize);
		const auto right = QRect({ xRight, siblingTop }, siblingSize);
		layout.siblingLeft = {
			.geometry = left,
			.userpic = userpic(left),
			.nameBoundingRect = nameBoundingRect(left, true),
			.nameFontSize = nameFontSize,
		};
		layout.siblingRight = {
			.geometry = right,
			.userpic = userpic(right),
			.nameBoundingRect = nameBoundingRect(right, false),
			.nameFontSize = nameFontSize,
		};

		return layout;
	});
}

not_null<Ui::RpWidget*> Controller::wrap() const {
	return _wrap;
}

Layout Controller::layout() const {
	Expects(_layout.current().has_value());

	return *_layout.current();
}

rpl::producer<Layout> Controller::layoutValue() const {
	return _layout.value() | rpl::filter_optional();
}

ContentLayout Controller::contentLayout() const {
	const auto &current = _layout.current();
	Assert(current.has_value());

	return {
		.geometry = current->content,
		.fade = (_contentFadeAnimation.value(_contentFaded ? 1. : 0.)
			* kFullContentFade),
		.radius = st::storiesRadius,
		.headerOutside = (current->headerLayout == HeaderLayout::Outside),
	};
}

Data::FileOrigin Controller::fileOrigin() const {
	return Data::FileOriginStory(_shown.peer, _shown.story);
}

TextWithEntities Controller::captionText() const {
	return _captionText;
}

void Controller::showFullCaption() {
	if (_captionText.empty()) {
		return;
	}
	togglePaused(true);
	_captionFullView = std::make_unique<CaptionFullView>(
		wrap(),
		&_delegate->storiesShow()->session(),
		_captionText,
		[=] { togglePaused(false); });
}

std::shared_ptr<ChatHelpers::Show> Controller::uiShow() const {
	return _delegate->storiesShow();
}

auto Controller::stickerOrEmojiChosen() const
-> rpl::producer<ChatHelpers::FileChosen> {
	return _delegate->storiesStickerOrEmojiChosen();
}

auto Controller::cachedReactionIconFactory() const
-> HistoryView::Reactions::CachedIconFactory & {
	return _delegate->storiesCachedReactionIconFactory();
}

void Controller::show(
		not_null<Data::Story*> story,
		Data::StoriesContext context) {
	using namespace Data;

	auto &stories = story->owner().stories();
	const auto storyId = story->fullId();
	const auto id = storyId.story;
	auto source = stories.source(storyId.peer);
	auto single = StoriesSource{ story->peer()->asUser() };
	v::match(context.data, [&](StoriesContextSingle) {
		source = nullptr;
		hideSiblings();
	}, [&](StoriesContextPeer) {
		hideSiblings();
	}, [&](StoriesContextSaved) {
		hideSiblings();
	}, [&](StoriesContextArchive) {
		hideSiblings();
	}, [&](StorySourcesList list) {
		const auto &sources = stories.sources(list);
		const auto i = ranges::find(
			sources,
			storyId.peer,
			&StoriesSourceInfo::id);
		if (i == end(sources)) {
			source = nullptr;
			return;
		}
		showSiblings(&story->session(), sources, (i - begin(sources)));

		if (int(sources.end() - i) < kPreloadUsersCount) {
			stories.loadMore(list);
		}
	});
	const auto idDates = story->idDates();
	_index = source ? (source->ids.find(idDates) - begin(source->ids)) : 0;
	if (!source || _index == source->ids.size()) {
		source = &single;
		single.ids.emplace(idDates);
		_index = 0;
	}
	const auto guard = gsl::finally([&] {
		_paused = false;
		_started = false;
		if (story->photo()) {
			_photoPlayback = std::make_unique<PhotoPlayback>(this);
		} else {
			_photoPlayback = nullptr;
		}
	});
	if (_source != *source) {
		_source = *source;
	}
	_context = context;
	_waitingForId = {};
	if (_shown == storyId) {
		return;
	}
	_shown = storyId;
	_viewed = false;
	_captionText = story->caption();
	_captionFullView = nullptr;
	invalidate_weak_ptrs(&_viewsLoadGuard);
	_reactions->hide();
	if (_replyFocused) {
		unfocusReply();
	}

	_header->show({ .user = source->user, .date = story->date() });
	_slider->show({ .index = _index, .total = int(source->ids.size()) });
	_replyArea->show({ .user = source->user, .id = id });
	_recentViews->show({
		.list = story->recentViewers(),
		.total = story->views(),
		.valid = source->user->isSelf(),
	});

	const auto session = &story->session();
	if (_session != session) {
		_session = session;
		_sessionLifetime = session->changes().storyUpdates(
			Data::StoryUpdate::Flag::Destroyed
		) | rpl::start_with_next([=](Data::StoryUpdate update) {
			if (update.story->fullId() == _shown) {
				_delegate->storiesClose();
			}
		});
		session->data().stories().itemsChanged(
		) | rpl::start_with_next([=](PeerId peerId) {
			if (_waitingForId.peer == peerId) {
				checkWaitingFor();
			}
		}, _sessionLifetime);
	}

	stories.loadAround(storyId, context);

	updatePlayingAllowed();
	source->user->updateFull();
}

void Controller::updatePlayingAllowed() {
	if (!_shown) {
		return;
	}
	setPlayingAllowed(_started
		&& _windowActive
		&& !_paused
		&& !_replyActive
		&& !_layerShown
		&& !_menuShown);
}

void Controller::setPlayingAllowed(bool allowed) {
	if (allowed) {
		_captionFullView = nullptr;
	}
	if (_photoPlayback) {
		_photoPlayback->togglePaused(!allowed);
	} else {
		_delegate->storiesTogglePaused(!allowed);
	}
}

void Controller::showSiblings(
		not_null<Main::Session*> session,
		const std::vector<Data::StoriesSourceInfo> &sources,
		int index) {
	showSibling(
		_siblingLeft,
		session,
		(index > 0) ? sources[index - 1].id : PeerId());
	showSibling(
		_siblingRight,
		session,
		(index + 1 < sources.size()) ? sources[index + 1].id : PeerId());
}

void Controller::hideSiblings() {
	_siblingLeft = nullptr;
	_siblingRight = nullptr;
}

void Controller::showSibling(
		std::unique_ptr<Sibling> &sibling,
		not_null<Main::Session*> session,
		PeerId peerId) {
	if (!peerId) {
		sibling = nullptr;
		return;
	}
	const auto source = session->data().stories().source(peerId);
	if (!source) {
		sibling = nullptr;
	} else if (!sibling || !sibling->shows(*source)) {
		sibling = std::make_unique<Sibling>(this, *source);
	}
}

void Controller::ready() {
	if (_started) {
		return;
	}
	_started = true;
	updatePlayingAllowed();
}

void Controller::updateVideoPlayback(const Player::TrackState &state) {
	updatePlayback(state);
}

void Controller::updatePhotoPlayback(const Player::TrackState &state) {
	updatePlayback(state);
}

void Controller::updatePlayback(const Player::TrackState &state) {
	_slider->updatePlayback(state);
	updatePowerSaveBlocker(state);
	maybeMarkAsRead(state);
	if (Player::IsStoppedAtEnd(state.state)) {
		if (!subjumpFor(1)) {
			_delegate->storiesClose();
		}
	}
}

void Controller::maybeMarkAsRead(const Player::TrackState &state) {
	const auto length = state.length;
	const auto position = Player::IsStoppedAtEnd(state.state)
		? state.length
		: Player::IsStoppedOrStopping(state.state)
		? 0
		: state.position;
	if (position > state.frequency * kMarkAsReadAfterSeconds) {
		if (position > kMarkAsReadAfterProgress * length) {
			markAsRead();
		}
	}
}

void Controller::markAsRead() {
	Expects(_source.has_value());

	if (_viewed) {
		return;
	}
	_viewed = true;
	_source->user->owner().stories().markAsRead(_shown, _started);
}

bool Controller::subjumpAvailable(int delta) const {
	const auto index = _index + delta;
	if (index < 0) {
		return _siblingLeft && _siblingLeft->shownId().valid();
	} else if (index >= int(_source->ids.size())) {
		return _siblingRight && _siblingRight->shownId().valid();
	}
	return index >= 0 && index < int(_source->ids.size());
}

bool Controller::subjumpFor(int delta) {
	if (delta > 0) {
		markAsRead();
	}
	const auto index = _index + delta;
	if (index < 0) {
		if (_siblingLeft && _siblingLeft->shownId().valid()) {
			return jumpFor(-1);
		} else if (!_source || _source->ids.empty()) {
			return false;
		}
		subjumpTo(0);
		return true;
	} else if (index >= int(_source->ids.size())) {
		return _siblingRight
			&& _siblingRight->shownId().valid()
			&& jumpFor(1);
	} else {
		subjumpTo(index);
	}
	return true;
}

void Controller::subjumpTo(int index) {
	Expects(_source.has_value());
	Expects(index >= 0 && index < _source->ids.size());

	const auto id = FullStoryId{
		.peer = _source->user->id,
		.story = (begin(_source->ids) + index)->id,
	};
	auto &stories = _source->user->owner().stories();
	if (stories.lookup(id)) {
		_delegate->storiesJumpTo(&_source->user->session(), id, _context);
	} else if (_waitingForId != id) {
		_waitingForId = id;
		stories.loadAround(id, _context);
	}
}

void Controller::checkWaitingFor() {
	Expects(_waitingForId.valid());
	Expects(_source.has_value());

	auto &stories = _source->user->owner().stories();
	const auto maybe = stories.lookup(_waitingForId);
	if (!maybe) {
		if (maybe.error() == Data::NoStory::Deleted) {
			_waitingForId = {};
		}
		return;
	}
	_delegate->storiesJumpTo(
		&_source->user->session(),
		base::take(_waitingForId),
		_context);
}

bool Controller::jumpFor(int delta) {
	if (delta == -1) {
		if (const auto left = _siblingLeft.get()) {
			_delegate->storiesJumpTo(
				&left->peer()->session(),
				left->shownId(),
				_context);
			return true;
		}
	} else if (delta == 1) {
		if (_source && _index + 1 >= int(_source->ids.size())) {
			markAsRead();
		}
		if (const auto right = _siblingRight.get()) {
			_delegate->storiesJumpTo(
				&right->peer()->session(),
				right->shownId(),
				_context);
			return true;
		}
	}
	return false;
}

bool Controller::paused() const {
	return _paused;
}

void Controller::togglePaused(bool paused) {
	if (_paused != paused) {
		_paused = paused;
		updatePlayingAllowed();
	}
}

void Controller::contentPressed(bool pressed) {
	togglePaused(pressed);
	if (pressed) {
		_reactions->collapse();
	}
}

void Controller::setMenuShown(bool shown) {
	if (_menuShown != shown) {
		_menuShown = shown;
		updatePlayingAllowed();
	}
}

bool Controller::canDownload() const {
	return _source && _source->user->isSelf();
}

void Controller::repaintSibling(not_null<Sibling*> sibling) {
	if (sibling == _siblingLeft.get() || sibling == _siblingRight.get()) {
		_delegate->storiesRepaint();
	}
}

SiblingView Controller::sibling(SiblingType type) const {
	const auto &pointer = (type == SiblingType::Left)
		? _siblingLeft
		: _siblingRight;
	if (const auto value = pointer.get()) {
		const auto over = _delegate->storiesSiblingOver(type);
		const auto layout = (type == SiblingType::Left)
			? _layout.current()->siblingLeft
			: _layout.current()->siblingRight;
		return value->view(layout, over);
	}
	return {};
}

ViewsSlice Controller::views(PeerId offset) {
	invalidate_weak_ptrs(&_viewsLoadGuard);
	if (!offset) {
		refreshViewsFromData();
	} else if (!sliceViewsTo(offset)) {
		return { .left = _viewsSlice.left };
	}
	return _viewsSlice;
}

rpl::producer<> Controller::moreViewsLoaded() const {
	return _moreViewsLoaded.events();
}

Fn<void(std::vector<Data::StoryView>)> Controller::viewsGotMoreCallback() {
	return crl::guard(&_viewsLoadGuard, [=](
			const std::vector<Data::StoryView> &result) {
		if (_viewsSlice.list.empty()) {
			auto &stories = _source->user->owner().stories();
			if (const auto maybeStory = stories.lookup(_shown)) {
				_viewsSlice = {
					.list = result,
					.left = (*maybeStory)->views() - int(result.size()),
				};
			} else {
				_viewsSlice = {};
			}
		} else {
			_viewsSlice.list.insert(
				end(_viewsSlice.list),
				begin(result),
				end(result));
			_viewsSlice.left
				= std::max(_viewsSlice.left - int(result.size()), 0);
		}
		_moreViewsLoaded.fire({});
	});
}

void Controller::refreshViewsFromData() {
	Expects(_source.has_value());

	auto &stories = _source->user->owner().stories();
	const auto maybeStory = stories.lookup(_shown);
	if (!maybeStory || !_source->user->isSelf()) {
		_viewsSlice = {};
		return;
	}
	const auto story = *maybeStory;
	const auto &list = story->viewsList();
	const auto total = story->views();
	_viewsSlice.list = list
		| ranges::views::take(Data::Stories::kViewsPerPage)
		| ranges::to_vector;
	_viewsSlice.left = total - int(_viewsSlice.list.size());
	if (_viewsSlice.list.empty() && _viewsSlice.left > 0) {
		const auto done = viewsGotMoreCallback();
		stories.loadViewsSlice(_shown.story, std::nullopt, done);
	}
}

bool Controller::sliceViewsTo(PeerId offset) {
	Expects(_source.has_value());

	auto &stories = _source->user->owner().stories();
	const auto maybeStory = stories.lookup(_shown);
	if (!maybeStory || !_source->user->isSelf()) {
		_viewsSlice = {};
		return true;
	}
	const auto story = *maybeStory;
	const auto &list = story->viewsList();
	const auto proj = [&](const Data::StoryView &single) {
		return single.peer->id;
	};
	const auto i = ranges::find(list, _viewsSlice.list.back());
	const auto add = (i != end(list)) ? int(end(list) - i - 1) : 0;
	const auto j = ranges::find(_viewsSlice.list, offset, proj);
	Assert(j != end(_viewsSlice.list));
	if (!add && (j + 1) == end(_viewsSlice.list)) {
		const auto done = viewsGotMoreCallback();
		stories.loadViewsSlice(_shown.story, _viewsSlice.list.back(), done);
		return false;
	}
	_viewsSlice.list.erase(begin(_viewsSlice.list), j + 1);
	_viewsSlice.list.insert(end(_viewsSlice.list), i + 1, end(list));
	_viewsSlice.left -= add;
	return true;
}

void Controller::unfocusReply() {
	_wrap->setFocus();
}

rpl::lifetime &Controller::lifetime() {
	return _lifetime;
}

void Controller::updatePowerSaveBlocker(const Player::TrackState &state) {
	const auto block = !Player::IsPausedOrPausing(state.state)
		&& !Player::IsStoppedOrStopping(state.state);
	base::UpdatePowerSaveBlocker(
		_powerSaveBlocker,
		block,
		base::PowerSaveBlockType::PreventDisplaySleep,
		[] { return u"Stories playback is active"_q; },
		[=] { return _wrap->window()->windowHandle(); });
}

} // namespace Media::Stories
