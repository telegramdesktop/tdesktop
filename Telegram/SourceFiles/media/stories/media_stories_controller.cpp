/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_controller.h"

#include "base/timer.h"
#include "base/power_save_blocker.h"
#include "data/data_stories.h"
#include "data/data_user.h"
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
constexpr auto kSiblingMultiplier = 0.448;
constexpr auto kFullContentFade = 0.2;

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
		_contentFaded = focused;
		_contentFadeAnimation.start(
			[=] { _delegate->storiesRepaint(); },
			focused ? 0. : 1.,
			focused ? 1. : 0.,
			st::fadeWrapDuration);
		togglePaused(focused);
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

		const auto siblingSize = layout.content.size() * kSiblingMultiplier;
		const auto siblingTop = layout.content.y()
			+ (layout.content.height() - siblingSize.height()) / 2;
		layout.siblingLeft = QRect(
			{ -siblingSize.width() / 3, siblingTop },
			siblingSize);
		layout.siblingRight = QRect(
			{ size.width() - (2 * siblingSize.width() / 3), siblingTop },
			siblingSize);

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

float64 Controller::contentFade() const {
	return _contentFadeAnimation.value(_contentFaded ? 1. : 0.)
		* kFullContentFade;
}

std::shared_ptr<ChatHelpers::Show> Controller::uiShow() const {
	return _delegate->storiesShow();
}

auto Controller::stickerOrEmojiChosen() const
->rpl::producer<ChatHelpers::FileChosen> {
	return _delegate->storiesStickerOrEmojiChosen();
}

void Controller::show(
		const std::vector<Data::StoriesList> &lists,
		int index,
		int subindex) {
	Expects(index >= 0 && index < lists.size());
	Expects(subindex >= 0 && subindex < lists[index].items.size());

	showSiblings(lists, index);

	const auto &list = lists[index];
	const auto &item = list.items[subindex];
	const auto guard = gsl::finally([&] {
		_started = false;
		if (v::is<not_null<PhotoData*>>(item.media.data)) {
			_photoPlayback = std::make_unique<PhotoPlayback>(this);
		} else {
			_photoPlayback = nullptr;
		}
	});
	if (_list != list) {
		_list = list;
	}
	_index = subindex;

	const auto id = Data::FullStoryId{
		.user = list.user,
		.id = item.id,
	};
	if (_shown == id) {
		return;
	}
	_shown = id;

	_header->show({ .user = list.user, .date = item.date });
	_slider->show({ .index = _index, .total = list.total });
	_replyArea->show({ .user = list.user });
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
	if (!list || list->items.empty()) {
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
	if (_photoPlayback) {
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
			_delegate->storiesJumpTo({});
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
		if (_siblingLeft->shownId().valid()) {
			return jumpFor(-1);
		} else if (!_list || _list->items.empty()) {
			return false;
		}
		_delegate->storiesJumpTo({
			.user = _list->user,
			.id = _list->items.front().id
		});
		return true;
	} else if (index >= _list->total) {
		return _siblingRight->shownId().valid() && jumpFor(1);
	} else if (index < _list->items.size()) {
		// #TODO stories load more
		_delegate->storiesJumpTo({
			.user = _list->user,
			.id = _list->items[index].id
		});
	}
	return true;
}


bool Controller::jumpFor(int delta) {
	if (delta == -1) {
		if (const auto left = _siblingLeft.get()) {
			_delegate->storiesJumpTo(left->shownId());
			return true;
		}
	} else if (delta == 1) {
		if (const auto right = _siblingRight.get()) {
			_delegate->storiesJumpTo(right->shownId());
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

SiblingView Controller::siblingLeft() const {
	if (const auto value = _siblingLeft.get()) {
		return { value->image(), _layout.current()->siblingLeft };
	}
	return {};
}

SiblingView Controller::siblingRight() const {
	if (const auto value = _siblingRight.get()) {
		return { value->image(), _layout.current()->siblingRight };
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
