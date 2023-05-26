/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_controller.h"

#include "base/timer.h"
#include "base/power_save_blocker.h"
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
#include "media/stories/media_stories_reply.h"
#include "media/stories/media_stories_view.h"
#include "media/audio/media_audio.h"
#include "ui/rp_widget.h"
#include "styles/style_media_view.h"
#include "styles/style_widgets.h"
#include "styles/style_boxes.h" // UserpicButton

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
, _replyArea(std::make_unique<ReplyArea>(this)) {
	initLayout();

	_replyArea->focusedValue(
	) | rpl::start_with_next([=](bool focused) {
		if (focused) {
			_captionFullView = nullptr;
		}
		_contentFaded = focused;
		_contentFadeAnimation.start(
			[=] { _delegate->storiesRepaint(); },
			focused ? 0. : 1.,
			focused ? 1. : 0.,
			st::fadeWrapDuration);
		if (_started) {
			togglePaused(focused);
		}
	}, _lifetime);

	_contentFadeAnimation.stop();
}

Controller::~Controller() = default;

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

void Controller::show(
		const std::vector<Data::StoriesList> &lists,
		int index,
		int subindex) {
	Expects(index >= 0 && index < lists.size());
	Expects(subindex >= 0 && subindex < lists[index].ids.size());

	showSiblings(lists, index);

	const auto &list = lists[index];
	const auto id = list.ids[subindex];
	const auto maybeStory = list.user->owner().stories().lookup({
		.peer = list.user->id,
		.story = id,
	});
	if (!maybeStory) {
		return;
	}
	const auto story = *maybeStory;
	const auto guard = gsl::finally([&] {
		_started = false;
		if (story->photo()) {
			_photoPlayback = std::make_unique<PhotoPlayback>(this);
		} else {
			_photoPlayback = nullptr;
		}
	});
	if (_list != list) {
		_list = list;
	}
	_index = subindex;

	const auto storyId = FullStoryId{
		.peer = list.user->id,
		.story = id,
	};
	if (_shown == storyId) {
		return;
	}
	_shown = storyId;
	_captionText = story->caption();
	_captionFullView = nullptr;

	_header->show({ .user = list.user, .date = story->date() });
	_slider->show({ .index = _index, .total = list.total });
	_replyArea->show({ .user = list.user, .id = id });

	const auto session = &list.user->session();
	if (_session != session) {
		_session = session;
		_sessionLifetime = session->changes().storyUpdates(
			Data::StoryUpdate::Flag::Destroyed
		) | rpl::start_with_next([=](Data::StoryUpdate update) {
			if (update.story->fullId() == _shown) {
				_delegate->storiesClose();
			}
		});
	}

	if (_contentFaded) {
		togglePaused(true);
	}
}

void Controller::showSiblings(
		const std::vector<Data::StoriesList> &lists,
		int index) {
	showSibling(_siblingLeft, (index > 0) ? &lists[index - 1] : nullptr);
	showSibling(
		_siblingRight,
		(index + 1 < lists.size()) ? &lists[index + 1] : nullptr);
}

void Controller::showSibling(
		std::unique_ptr<Sibling> &sibling,
		const Data::StoriesList *list) {
	if (!list || list->ids.empty()) {
		sibling = nullptr;
	} else if (!sibling || !sibling->shows(*list)) {
		sibling = std::make_unique<Sibling>(this, *list);
	}
}

void Controller::ready() {
	if (_started) {
		return;
	}
	_started = true;
	if (!_contentFaded && _photoPlayback) {
		_photoPlayback->togglePaused(false);
	}
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
	if (Player::IsStoppedAtEnd(state.state)) {
		if (!subjumpFor(1)) {
			_delegate->storiesClose();
		}
	}
}

bool Controller::subjumpAvailable(int delta) const {
	const auto index = _index + delta;
	if (index < 0) {
		return _siblingLeft && _siblingLeft->shownId().valid();
	} else if (index >= _list->total) {
		return _siblingRight && _siblingRight->shownId().valid();
	}
	return index >= 0 && index < _list->total;
}

bool Controller::subjumpFor(int delta) {
	const auto index = _index + delta;
	if (index < 0) {
		if (_siblingLeft && _siblingLeft->shownId().valid()) {
			return jumpFor(-1);
		} else if (!_list || _list->ids.empty()) {
			return false;
		}
		_delegate->storiesJumpTo(&_list->user->session(), {
			.peer = _list->user->id,
			.story = _list->ids.front()
		});
		return true;
	} else if (index >= _list->total) {
		return _siblingRight
			&& _siblingRight->shownId().valid()
			&& jumpFor(1);
	} else if (index < _list->ids.size()) {
		// #TODO stories load more
		_delegate->storiesJumpTo(&_list->user->session(), {
			.peer = _list->user->id,
			.story = _list->ids[index]
		});
	}
	return true;
}


bool Controller::jumpFor(int delta) {
	if (delta == -1) {
		if (const auto left = _siblingLeft.get()) {
			_delegate->storiesJumpTo(
				&left->peer()->session(),
				left->shownId());
			return true;
		}
	} else if (delta == 1) {
		if (const auto right = _siblingRight.get()) {
			_delegate->storiesJumpTo(
				&right->peer()->session(),
				right->shownId());
			return true;
		}
	}
	return false;
}

bool Controller::paused() const {
	return _photoPlayback
		? _photoPlayback->paused()
		: _delegate->storiesPaused();
}

void Controller::togglePaused(bool paused) {
	if (!paused) {
		_captionFullView = nullptr;
	}
	if (_photoPlayback) {
		_photoPlayback->togglePaused(paused);
	} else {
		_delegate->storiesTogglePaused(paused);
	}
}

bool Controller::canDownload() const {
	return _list && _list->user->isSelf();
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
