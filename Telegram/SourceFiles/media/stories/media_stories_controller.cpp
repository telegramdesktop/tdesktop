/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/stories/media_stories_controller.h"

#include "base/platform/base_platform_info.h"
#include "base/power_save_blocker.h"
#include "base/qt_signal_producer.h"
#include "base/unixtime.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "boxes/report_messages_box.h"
#include "chat_helpers/compose/compose_show.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/core_settings.h"
#include "core/local_url_handlers.h"
#include "core/update_checker.h"
#include "data/data_changes.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "history/view/reactions/history_view_reactions_strip.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "media/stories/media_stories_caption_full_view.h"
#include "media/stories/media_stories_delegate.h"
#include "media/stories/media_stories_header.h"
#include "media/stories/media_stories_sibling.h"
#include "media/stories/media_stories_slider.h"
#include "media/stories/media_stories_reactions.h"
#include "media/stories/media_stories_recent_views.h"
#include "media/stories/media_stories_reply.h"
#include "media/stories/media_stories_repost_view.h"
#include "media/stories/media_stories_share.h"
#include "media/stories/media_stories_stealth.h"
#include "media/stories/media_stories_view.h"
#include "media/audio/media_audio.h"
#include "info/stories/info_stories_common.h"
#include "settings/settings_credits_graphics.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/report_box_graphics.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/round_rect.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_chat_helpers.h" // defaultReportBox
#include "styles/style_media_view.h"
#include "styles/style_boxes.h" // UserpicButton

#include <QtGui/QWindow>

namespace Media::Stories {
namespace {

constexpr auto kPhotoProgressInterval = crl::time(100);
constexpr auto kPhotoDuration = 5 * crl::time(1000);
constexpr auto kFullContentFade = 0.6;
constexpr auto kSiblingMultiplierDefault = 0.448;
constexpr auto kSiblingMultiplierMax = 0.72;
constexpr auto kSiblingOutsidePart = 0.24;
constexpr auto kSiblingUserpicSize = 0.3;
constexpr auto kInnerHeightMultiplier = 1.6;
constexpr auto kPreloadPeersCount = 3;
constexpr auto kPreloadStoriesCount = 5;
constexpr auto kPreloadNextMediaCount = 3;
constexpr auto kPreloadPreviousMediaCount = 1;
constexpr auto kMarkAsReadAfterSeconds = 0.2;
constexpr auto kMarkAsReadAfterProgress = 0.;

struct SameDayRange {
	int from = 0;
	int till = 0;
};
[[nodiscard]] SameDayRange ComputeSameDayRange(
		not_null<Data::Story*> story,
		const Data::StoriesIds &ids,
		const std::vector<StoryId> &sorted,
		int index) {
	Expects(index >= 0 && index < ids.list.size());
	Expects(index >= 0 && index < sorted.size());

	const auto pinned = int(ids.pinnedToTop.size());
	if (index < pinned) {
		return SameDayRange{ .from = 0, .till = pinned - 1 };
	}

	auto result = SameDayRange{ .from = index, .till = index };
	const auto peerId = story->peer()->id;
	const auto stories = &story->owner().stories();
	const auto now = base::unixtime::parse(story->date());
	for (auto i = index; i != 0;) {
		const auto storyId = sorted[--i];
		if (const auto maybeStory = stories->lookup({ peerId, storyId })) {
			const auto day = base::unixtime::parse((*maybeStory)->date());
			if (day.date() != now.date()) {
				break;
			}
		}
		--result.from;
	}
	for (auto i = index + 1, c = int(sorted.size()); i != c; ++i) {
		const auto storyId = sorted[i];
		if (const auto maybeStory = stories->lookup({ peerId, storyId })) {
			const auto day = base::unixtime::parse((*maybeStory)->date());
			if (day.date() != now.date()) {
				break;
			}
		}
		++result.till;
	}
	return result;
}

[[nodiscard]] QPoint Rotated(QPoint point, QPoint origin, float64 angle) {
	if (std::abs(angle) < 1.) {
		return point;
	}
	const auto alpha = angle / 180. * M_PI;
	const auto acos = cos(alpha);
	const auto asin = sin(alpha);
	point -= origin;
	return origin + QPoint(
		int(base::SafeRound(acos * point.x() - asin * point.y())),
		int(base::SafeRound(asin * point.x() + acos * point.y())));
}

[[nodiscard]] bool ResolveWeatherInCelsius() {
	const auto saved = Core::App().settings().weatherInCelsius();
	return saved.value_or(!ranges::contains(
		std::array{ u"US"_q, u"BS"_q, u"KY"_q, u"LR"_q, u"BZ"_q },
		Platform::SystemCountry().toUpper()));
}

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

class Controller::Unsupported final {
public:
	Unsupported(not_null<Controller*> controller, not_null<PeerData*> peer);

private:
	void setup(not_null<PeerData*> peer);

	const not_null<Controller*> _controller;
	std::unique_ptr<Ui::RpWidget> _bg;
	std::unique_ptr<Ui::FlatLabel> _text;
	std::unique_ptr<Ui::RoundButton> _button;
	Ui::RoundRect _bgRound;

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

Controller::Unsupported::Unsupported(
	not_null<Controller*> controller,
	not_null<PeerData*> peer)
: _controller(controller)
, _bgRound(st::storiesRadius, st::storiesComposeBg) {
	setup(peer);
}

void Controller::Unsupported::setup(not_null<PeerData*> peer) {
	const auto wrap = _controller->wrap();

	_bg = std::make_unique<Ui::RpWidget>(wrap);
	_bg->show();
	_bg->paintRequest() | rpl::start_with_next([=] {
		auto p = QPainter(_bg.get());
		_bgRound.paint(p, _bg->rect());
	}, _bg->lifetime());

	_controller->layoutValue(
	) | rpl::start_with_next([=](const Layout &layout) {
		_bg->setGeometry(layout.content);
	}, _bg->lifetime());

	_text = std::make_unique<Ui::FlatLabel>(
		wrap,
		tr::lng_stories_unsupported(),
		st::storiesUnsupportedLabel);
	_text->show();

	_button = std::make_unique<Ui::RoundButton>(
		wrap,
		tr::lng_update_telegram(),
		st::storiesUnsupportedUpdate);
	_button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
	_button->show();

	rpl::combine(
		_controller->layoutValue(),
		_text->sizeValue(),
		_button->sizeValue()
	) | rpl::start_with_next([=](
			const Layout &layout,
			QSize text,
			QSize button) {
		const auto wrap = layout.content;
		const auto totalHeight = st::storiesUnsupportedTop
			+ text.height()
			+ st::storiesUnsupportedSkip
			+ button.height();
		const auto top = (wrap.height() - totalHeight) / 2;
		_text->move(
			wrap.x() + (wrap.width() - text.width()) / 2,
			wrap.y() + top + st::storiesUnsupportedTop);
		_button->move(
			wrap.x() + (wrap.width() - button.width()) / 2,
			wrap.y() + top + totalHeight - button.height());
	}, _button->lifetime());

	_button->setClickedCallback([=] {
		Core::UpdateApplication();
	});
}

Controller::Controller(not_null<Delegate*> delegate)
: _delegate(delegate)
, _wrap(_delegate->storiesWrap())
, _header(std::make_unique<Header>(this))
, _slider(std::make_unique<Slider>(this))
, _replyArea(std::make_unique<ReplyArea>(this))
, _reactions(std::make_unique<Reactions>(this))
, _recentViews(std::make_unique<RecentViews>(this))
, _weatherInCelsius(ResolveWeatherInCelsius()){
	initLayout();

	using namespace rpl::mappers;

	rpl::combine(
		_replyArea->activeValue(),
		_reactions->activeValue(),
		_1 || _2
	) | rpl::distinct_until_changed(
	) | rpl::start_with_next([=](bool active) {
		_replyActive = active;
		updateContentFaded();
	}, _lifetime);

	_reactions->setReplyFieldState(
		_replyArea->focusedValue(),
		_replyArea->hasSendTextValue());
	if (const auto like = _replyArea->likeAnimationTarget()) {
		_reactions->attachToReactionButton(like);
	}

	_reactions->chosen(
	) | rpl::start_with_next([=](Reactions::Chosen chosen) {
		if (reactionChosen(chosen.mode, chosen.reaction)) {
			_reactions->animateAndProcess(std::move(chosen));
		}
	}, _lifetime);

	_delegate->storiesLayerShown(
	) | rpl::start_with_next([=](bool shown) {
		if (_layerShown != shown) {
			_layerShown = shown;
			updatePlayingAllowed();
		}
	}, _lifetime);

	_header->tooltipShownValue(
	) | rpl::start_with_next([=](bool shown) {
		if (_tooltipShown != shown) {
			_tooltipShown = shown;
			updatePlayingAllowed();
		}
	}, _lifetime);

	_wrap->windowActiveValue(
	) | rpl::start_with_next([=](bool active) {
		_windowActive = active;
		updatePlayingAllowed();
	}, _lifetime);

	_contentFadeAnimation.stop();
}

Controller::~Controller() {
	_captionFullView = nullptr;
	_repostView = nullptr;
	changeShown(nullptr);
}

void Controller::updateContentFaded() {
	const auto faded = _replyActive
		|| (_captionFullView && !_captionFullView->closing());
	if (_contentFaded == faded) {
		return;
	}
	_contentFaded = faded;
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
		const auto topNotchSkip = _delegate->storiesTopNotchSkip();

		size = QSize(
			std::max(size.width(), st::mediaviewMinWidth),
			std::max(size.height(), st::mediaviewMinHeight));

		auto layout = Layout();
		layout.headerLayout = (size.height() >= minHeightForOutsideHeader)
			? HeaderLayout::Outside
			: HeaderLayout::Normal;

		const auto topSkip = topNotchSkip
			+ st::storiesFieldMargin.bottom()
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
		if (!_areas.empty()) {
			rebuildActiveAreas(layout);
		}
		return layout;
	});
}

void Controller::rebuildActiveAreas(const Layout &layout) const {
	const auto origin = layout.content.topLeft();
	const auto scale = layout.content.size();
	for (auto &area : _areas) {
		const auto &general = area.original;
		area.geometry = QRect(
			int(base::SafeRound(general.x() * scale.width())),
			int(base::SafeRound(general.y() * scale.height())),
			int(base::SafeRound(general.width() * scale.width())),
			int(base::SafeRound(general.height() * scale.height()))
		).translated(origin);
		area.radius = scale.width() * area.radiusOriginal / 100.;
		if (const auto view = area.view.get()) {
			view->setAreaGeometry(area.geometry, area.radius);
		}
	}
}

Data::Story *Controller::story() const {
	if (!_session) {
		return nullptr;
	}
	const auto maybeStory = _session->data().stories().lookup(_shown);
	return maybeStory ? maybeStory->get() : nullptr;
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

bool Controller::closeByClickAt(QPoint position) const {
	const auto &current = _layout.current();
	Assert(current.has_value());

	return (position.x() < current->content.x() - st::storiesControlSize)
		|| (position.x() > current->content.x() + current->content.width()
			+ st::storiesControlSize);
}

Data::FileOrigin Controller::fileOrigin() const {
	return _shown;
}

TextWithEntities Controller::captionText() const {
	return _captionText;
}

bool Controller::skipCaption() const {
	return (_captionFullView != nullptr)
		|| (_captionText.empty() && !repost());
}

bool Controller::repost() const {
	return _repostView != nullptr;
}

int Controller::repostSkipTop() const {
	return _repostView
		? (_repostView->height()
			+ (_captionText.empty() ? 0 : st::mediaviewTextSkip))
		: 0;
}

QMargins Controller::repostCaptionPadding() const {
	return { 0, repostSkipTop(), 0, 0 };
}

void Controller::drawRepostInfo(
		Painter &p,
		int x,
		int y,
		int availableWidth) const {
	Expects(_repostView != nullptr);

	_repostView->draw(p, x, y, availableWidth);
}

RepostClickHandler Controller::lookupRepostHandler(QPoint position) const {
	return _repostView
		? _repostView->lookupHandler(position)
		: RepostClickHandler();
}

void Controller::toggleLiked() {
	_reactions->toggleLiked();
}

bool Controller::reactionChosen(ReactionsMode mode, ChosenReaction chosen) {
	auto result = true;
	if (mode == ReactionsMode::Message) {
		result = _replyArea->sendReaction(chosen.id);
	} else if (const auto peer = shownPeer()) {
		peer->owner().stories().sendReaction(_shown, chosen.id);
	}
	unfocusReply();
	return result;
}

void Controller::showFullCaption() {
	if (_captionText.empty()) {
		return;
	}
	_captionFullView = std::make_unique<CaptionFullView>(this);
	updateContentFaded();
}

void Controller::captionClosing() {
	updateContentFaded();
}

void Controller::captionClosed() {
	if (!_captionFullView) {
		return;
	} else if (_captionFullView->focused()) {
		_wrap->setFocus();
	}
	_captionFullView = nullptr;
}

std::shared_ptr<ChatHelpers::Show> Controller::uiShow() const {
	return _delegate->storiesShow();
}

auto Controller::stickerOrEmojiChosen() const
-> rpl::producer<ChatHelpers::FileChosen> {
	return _delegate->storiesStickerOrEmojiChosen();
}

void Controller::rebuildFromContext(
		not_null<PeerData*> peer,
		FullStoryId storyId) {
	using namespace Data;

	auto &stories = peer->owner().stories();
	auto list = std::optional<StoriesList>();
	auto source = (const StoriesSource*)nullptr;
	const auto peerId = storyId.peer;
	const auto id = storyId.story;
	v::match(_context.data, [&](StoriesContextSingle) {
		hideSiblings();
	}, [&](StoriesContextPeer) {
		source = stories.source(peerId);
		hideSiblings();
	}, [&](StoriesContextAlbum album) {
		const auto known = stories.albumIdsCountKnown(peerId, album.id);
		if (known) {
			const auto &ids = stories.albumIds(peerId, album.id);
			auto sorted = RespectingPinned(ids);
			const auto i = ranges::find(sorted, id);
			const auto tillEnd = int(end(sorted) - i);
			if (tillEnd > 0) {
				_index = int(i - begin(sorted));
				const auto total = stories.albumIdsCount(peerId, album.id);
				list = StoriesList{
					.peer = peer,
					.ids = ids,
					.sorted = std::move(sorted),
					.total = total,
				};
				if (ids.list.size() < list->total
					&& tillEnd < kPreloadStoriesCount) {
					stories.albumIdsLoadMore(peerId, album.id);
				}
			}
		}
		hideSiblings();
	}, [&](StorySourcesList list) {
		source = stories.source(peerId);
		const auto &sources = stories.sources(list);
		const auto i = ranges::find(
			sources,
			storyId.peer,
			&StoriesSourceInfo::id);
		if (i != end(sources)) {
			if (_cachedSourcesList.empty()) {
				_showingUnreadSources = source && (source->readTill < id);
			}
			rebuildCachedSourcesList(sources, (i - begin(sources)));
			_cachedSourcesList[_cachedSourceIndex].shownId = storyId.story;
			showSiblings(&peer->session());
			if (int(sources.end() - i) < kPreloadPeersCount) {
				stories.loadMore(list);
			}
		}
	});
	_sliderIndex = 0;
	_sliderCount = 0;
	if (list) {
		_source = std::nullopt;
		if (_list != list) {
			_list = std::move(list);
		}
		if (const auto maybe = peer->owner().stories().lookup(storyId)) {
			const auto now = *maybe;
			const auto range = ComputeSameDayRange(
				now,
				_list->ids,
				_list->sorted,
				_index);
			_sliderCount = range.till - range.from + 1;
			_sliderIndex = _index - range.from;
		}
	} else {
		if (source) {
			const auto i = source->ids.lower_bound(StoryIdDates{ id });
			if (i != end(source->ids) && i->id == id) {
				_index = int(i - begin(source->ids));
			} else {
				source = nullptr;
			}
		}
		if (!source) {
			_source = std::nullopt;
			_list = StoriesList{
				.peer = peer,
				.ids = { { id } },
				.sorted = { id },
				.total = 1,
			};
			_index = 0;
		} else {
			_list = std::nullopt;
			if (_source != *source) {
				_source = *source;
			}
		}
	}
	preloadNext();
	_slider->show({
		.index = _sliderCount ? _sliderIndex : _index,
		.total = _sliderCount ? _sliderCount : shownCount(),
	});
}

void Controller::preloadNext() {
	Expects(shown());

	auto ids = std::vector<FullStoryId>();
	ids.reserve(kPreloadPreviousMediaCount + kPreloadNextMediaCount);
	const auto peer = shownPeer();
	const auto count = shownCount();
	const auto till = std::min(_index + kPreloadNextMediaCount, count);
	for (auto i = _index + 1; i != till; ++i) {
		ids.push_back({ .peer = peer->id, .story = shownId(i) });
	}
	const auto from = std::max(_index - kPreloadPreviousMediaCount, 0);
	for (auto i = _index; i != from;) {
		ids.push_back({ .peer = peer->id, .story = shownId(--i) });
	}
	peer->owner().stories().setPreloadingInViewer(std::move(ids));
}

void Controller::checkMoveByDelta() {
	const auto index = _index + _waitingForDelta;
	if (_waitingForDelta && shown() && index >= 0 && index < shownCount()) {
		subjumpTo(index);
	}
}

void Controller::show(
		not_null<Data::Story*> story,
		Data::StoriesContext context) {
	auto &stories = story->owner().stories();
	const auto storyId = story->fullId();
	const auto peer = story->peer();
	_context = context;
	_waitingForId = {};
	_waitingForDelta = 0;

	rebuildFromContext(peer, storyId);
	_contextLifetime.destroy();
	const auto subscribeToSource = [&] {
		stories.sourceChanged() | rpl::filter(
			rpl::mappers::_1 == storyId.peer
		) | rpl::start_with_next([=] {
			rebuildFromContext(peer, storyId);
		}, _contextLifetime);
	};
	v::match(_context.data, [&](Data::StoriesContextSingle) {
	}, [&](Data::StoriesContextPeer) {
		subscribeToSource();
	}, [&](Data::StoriesContextAlbum album) {
		const auto key = Data::StoryAlbumIdsKey{ storyId.peer, album.id };
		stories.albumIdsChanged() | rpl::filter(
			rpl::mappers::_1 == key
		) | rpl::start_with_next([=] {
			rebuildFromContext(peer, storyId);
			checkMoveByDelta();
		}, _contextLifetime);
	}, [&](Data::StorySourcesList) {
		subscribeToSource();
	});

	const auto guard = gsl::finally([&] {
		_paused = false;
		_started = false;
		if (!story->document()) {
			_photoPlayback = std::make_unique<PhotoPlayback>(this);
		} else {
			_photoPlayback = nullptr;
		}
	});

	const auto unsupported = story->unsupported();
	if (!unsupported) {
		_unsupported = nullptr;
	} else {
		_unsupported = std::make_unique<Unsupported>(this, peer);
		_header->raise();
		_slider->raise();
	}

	captionClosed();
	_repostView = validateRepostView(story);
	_captionText = story->caption();
	_contentFaded = false;
	_contentFadeAnimation.stop();
	const auto document = story->document();
	_header->show({
		.peer = peer,
		.fromPeer = story->fromPeer(),
		.repostPeer = _repostView ? _repostView->fromPeer() : nullptr,
		.repostFrom = _repostView ? _repostView->fromName() : nullptr,
		.date = story->date(),
		.fullIndex = _sliderCount ? _index : 0,
		.fullCount = _sliderCount ? shownCount() : 0,
		.privacy = story->privacy(),
		.edited = story->edited(),
		.video = (document != nullptr),
		.silent = (document && document->isSilentVideo()),
	});
	uiShow()->hideLayer(anim::type::instant);
	if (!changeShown(story)) {
		return;
	}

	_replyArea->show({
		.peer = unsupported ? nullptr : peer.get(),
		.id = story->id(),
	}, _reactions->likedValue());

	const auto wasLikeButton = QPointer(_recentViews->likeButton());
	_recentViews->show({
		.list = story->recentViewers(),
		.reactions = story->reactions(),
		.forwards = story->forwards(),
		.views = story->views(),
		.total = story->interactions(),
		.type = RecentViewsTypeFor(peer),
		.canViewReactions = CanViewReactionsFor(peer) && !peer->isMegagroup(),
	}, _reactions->likedValue());
	if (const auto nowLikeButton = _recentViews->likeButton()) {
		if (wasLikeButton != nowLikeButton) {
			_reactions->attachToReactionButton(nowLikeButton);
		}
	}

	if (peer->isSelf() || peer->isBroadcast() || peer->isServiceUser()) {
		_reactions->setReactionIconWidget(_recentViews->likeIconWidget());
	} else if (const auto like = _replyArea->likeAnimationTarget()) {
		_reactions->setReactionIconWidget(like);
	}
	_reactions->showLikeFrom(story);

	stories.loadAround(storyId, context);

	updatePlayingAllowed();
	peer->updateFull();
}

void Controller::jumpTo(
		not_null<Data::Story*> story,
		Data::StoriesContext context) {
	show(story, std::move(context));
	_delegate->storiesRedisplay(story);
}

bool Controller::changeShown(Data::Story *story) {
	const auto id = story ? story->fullId() : FullStoryId();
	const auto session = story ? &story->session() : nullptr;
	const auto sessionChanged = (_session != session);

	updateAreas(story);

	if (_shown == id && !sessionChanged) {
		return false;
	}
	if (_shown) {
		Assert(_session != nullptr);
		_session->data().stories().unregisterPolling(
			_shown,
			Data::Stories::Polling::Viewer);
	}
	if (sessionChanged) {
		_sessionLifetime.destroy();
	}
	_shown = id;
	_session = session;
	if (sessionChanged) {
		subscribeToSession();
	}
	if (story) {
		story->owner().stories().registerPolling(
			story,
			Data::Stories::Polling::Viewer);
	}

	_viewed = false;
	invalidate_weak_ptrs(&_viewsLoadGuard);
	_reactions->hide();
	_reactions->setReactionIconWidget(nullptr);
	if (_replyArea->focused()) {
		unfocusReply();
	}

	return true;
}

void Controller::subscribeToSession() {
	Expects(!_sessionLifetime);

	if (!_session) {
		return;
	}
	_session->changes().storyUpdates(
		Data::StoryUpdate::Flag::Destroyed
	) | rpl::start_with_next([=](Data::StoryUpdate update) {
		if (update.story->fullId() == _shown) {
			_delegate->storiesClose();
		}
	}, _sessionLifetime);
	_session->data().stories().itemsChanged(
	) | rpl::start_with_next([=](PeerId peerId) {
		if (_waitingForId.peer == peerId) {
			checkWaitingFor();
		}
	}, _sessionLifetime);
	_session->changes().storyUpdates(
		Data::StoryUpdate::Flag::Edited
		| Data::StoryUpdate::Flag::ViewsChanged
		| Data::StoryUpdate::Flag::Reaction
	) | rpl::filter([=](const Data::StoryUpdate &update) {
		return (update.story == this->story());
	}) | rpl::start_with_next([=](const Data::StoryUpdate &update) {
		if (update.flags & Data::StoryUpdate::Flag::Edited) {
			show(update.story, _context);
			_delegate->storiesRedisplay(update.story);
		} else {
			const auto peer = update.story->peer();
			_recentViews->show({
				.list = update.story->recentViewers(),
				.reactions = update.story->reactions(),
				.forwards = update.story->forwards(),
				.views = update.story->views(),
				.total = update.story->interactions(),
				.type = RecentViewsTypeFor(peer),
				.canViewReactions = CanViewReactionsFor(peer) && !peer->isMegagroup(),
			});
			updateAreas(update.story);
		}
	}, _sessionLifetime);
	_sessionLifetime.add([=] {
		_session->data().stories().setPreloadingInViewer({});
	});
}

void Controller::updateAreas(Data::Story *story) {
	const auto &locations = story
		? story->locations()
		: std::vector<Data::StoryLocation>();
	const auto &suggestedReactions = story
		? story->suggestedReactions()
		: std::vector<Data::SuggestedReaction>();
	const auto &channelPosts = story
		? story->channelPosts()
		: std::vector<Data::ChannelPost>();
	const auto &urlAreas = story
		? story->urlAreas()
		: std::vector<Data::UrlArea>();
	const auto &weatherAreas = story
		? story->weatherAreas()
		: std::vector<Data::WeatherArea>();
	if (_locations != locations) {
		_locations = locations;
		_areas.clear();
	}
	if (_channelPosts != channelPosts) {
		_channelPosts = channelPosts;
		_areas.clear();
	}
	if (_urlAreas != urlAreas) {
		_urlAreas = urlAreas;
		_areas.clear();
	}
	if (_weatherAreas != weatherAreas) {
		_weatherAreas = weatherAreas;
		_areas.clear();
	}
	const auto reactionsCount = int(suggestedReactions.size());
	if (_suggestedReactions.size() == reactionsCount && !_areas.empty()) {
		for (auto i = 0; i != reactionsCount; ++i) {
			const auto count = suggestedReactions[i].count;
			if (_suggestedReactions[i].count != count) {
				_suggestedReactions[i].count = count;
				const auto view = _areas[i + _locations.size()].view.get();
				view->updateReactionsCount(count);
			}
			if (_suggestedReactions[i] != suggestedReactions[i]) {
				_suggestedReactions = suggestedReactions;
				_areas.clear();
				break;
			}
		}
	} else if (_suggestedReactions != suggestedReactions) {
		_suggestedReactions = suggestedReactions;
		_areas.clear();
	}
}

PauseState Controller::pauseState() const {
	const auto inactive = !_windowActive
		|| _replyActive
		|| _layerShown
		|| _menuShown;
	const auto playing = !inactive && !_paused;
	return playing
		? PauseState::Playing
		: !inactive
		? PauseState::Paused
		: _paused
		? PauseState::InactivePaused
		: PauseState::Inactive;
}

float64 Controller::currentVolume() const {
	return Core::App().settings().videoVolume();
}

void Controller::toggleVolume() {
	_delegate->storiesVolumeToggle();
}

void Controller::changeVolume(float64 volume) {
	_delegate->storiesVolumeChanged(volume);
}

void Controller::volumeChangeFinished() {
	_delegate->storiesVolumeChangeFinished();
}

void Controller::updatePlayingAllowed() {
	if (!_shown) {
		return;
	}
	_header->updatePauseState();
	setPlayingAllowed(_started
		&& _windowActive
		&& !_paused
		&& !_replyActive
		&& (!_captionFullView || _captionFullView->closing())
		&& !_layerShown
		&& !_menuShown
		&& !_tooltipShown);
}

void Controller::setPlayingAllowed(bool allowed) {
	if (_photoPlayback) {
		_photoPlayback->togglePaused(!allowed);
	} else {
		_delegate->storiesTogglePaused(!allowed);
	}
}

void Controller::showSiblings(not_null<Main::Session*> session) {
	showSibling(
		_siblingLeft,
		session,
		(_cachedSourceIndex > 0
			? _cachedSourcesList[_cachedSourceIndex - 1]
			: CachedSource()));
	showSibling(
		_siblingRight,
		session,
		(_cachedSourceIndex + 1 < _cachedSourcesList.size()
			? _cachedSourcesList[_cachedSourceIndex + 1]
			: CachedSource()));
}

void Controller::hideSiblings() {
	_siblingLeft = nullptr;
	_siblingRight = nullptr;
}

void Controller::showSibling(
		std::unique_ptr<Sibling> &sibling,
		not_null<Main::Session*> session,
		CachedSource cached) {
	if (!cached) {
		sibling = nullptr;
		return;
	}
	const auto source = session->data().stories().source(cached.peerId);
	if (!source) {
		sibling = nullptr;
	} else if (!sibling || !sibling->shows(*source, cached.shownId)) {
		sibling = std::make_unique<Sibling>(this, *source, cached.shownId);
	}
}

void Controller::ready() {
	if (_started) {
		return;
	}
	_started = true;
	updatePlayingAllowed();
	_reactions->ready();
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

ClickHandlerPtr Controller::lookupAreaHandler(QPoint point) const {
	const auto &layout = _layout.current();
	if (!layout
		|| (_locations.empty()
			&& _suggestedReactions.empty()
			&& _channelPosts.empty()
			&& _urlAreas.empty()
			&& _weatherAreas.empty())) {
		return nullptr;
	} else if (_areas.empty()) {
		const auto now = story();
		_areas.reserve(_locations.size()
			+ _suggestedReactions.size()
			+ _channelPosts.size()
			+ _urlAreas.size());
		for (const auto &location : _locations) {
			_areas.push_back({
				.original = location.area.geometry,
				.rotation = location.area.rotation,
				.handler = std::make_shared<LocationClickHandler>(
					location.point),
			});
		}
		for (const auto &suggestedReaction : _suggestedReactions) {
			const auto id = suggestedReaction.reaction;
			auto widget = _reactions->makeSuggestedReactionWidget(
				suggestedReaction);
			const auto raw = widget.get();
			_areas.push_back({
				.original = suggestedReaction.area.geometry,
				.rotation = suggestedReaction.area.rotation,
				.handler = std::make_shared<LambdaClickHandler>([=] {
					raw->playEffect();
					if (const auto now = story()) {
						if (now->sentReactionId() != id) {
							now->owner().stories().sendReaction(
								now->fullId(),
								id);
						}
					}
				}),
				.view = std::move(widget),
			});
		}
		if (const auto session = now ? &now->session() : nullptr) {
			for (const auto &channelPost : _channelPosts) {
				_areas.push_back({
					.original = channelPost.area.geometry,
					.rotation = channelPost.area.rotation,
					.handler = MakeChannelPostHandler(
						session,
						channelPost.itemId),
				});
			}
		}
		const auto weak = base::make_weak(this);
		for (const auto &url : _urlAreas) {
			_areas.push_back({
				.original = url.area.geometry,
				.rotation = url.area.rotation,
				.handler = MakeUrlAreaHandler(weak, url.url),
			});
		}
		for (const auto &weather : _weatherAreas) {
			_areas.push_back({
				.original = weather.area.geometry,
				.radiusOriginal = weather.area.radius,
				.rotation = weather.area.rotation,
				.handler = std::make_shared<LambdaClickHandler>([=] {
					toggleWeatherMode();
				}),
				.view = _reactions->makeWeatherAreaWidget(
					weather,
					_weatherInCelsius.value()),
			});
		}
		rebuildActiveAreas(*layout);
	}

	for (const auto &area : _areas) {
		const auto center = area.geometry.center();
		const auto angle = -area.rotation;
		const auto contains = area.view
			? area.view->contains(point)
			: area.geometry.contains(Rotated(point, center, angle));
		if (contains) {
			return area.handler;
		}
	}
	return nullptr;
}

void Controller::toggleWeatherMode() const {
	const auto now = !_weatherInCelsius.current();
	Core::App().settings().setWeatherInCelsius(now);
	Core::App().saveSettingsDelayed();
	_weatherInCelsius = now;
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
	Expects(shown());

	if (_viewed) {
		return;
	}
	_viewed = true;
	shownPeer()->owner().stories().markAsRead(_shown, _started);
}

bool Controller::subjumpAvailable(int delta) const {
	const auto index = _index + delta;
	if (index < 0) {
		return _siblingLeft && _siblingLeft->shownId().valid();
	} else if (index >= shownCount()) {
		return _siblingRight && _siblingRight->shownId().valid();
	}
	return index >= 0 && index < shownCount();
}

bool Controller::subjumpFor(int delta) {
	if (delta > 0) {
		markAsRead();
	}
	const auto index = _index + delta;
	if (index < 0) {
		if (_siblingLeft && _siblingLeft->shownId().valid()) {
			return jumpFor(-1);
		} else if (!shown() || !shownCount()) {
			return false;
		}
		subjumpTo(0);
		return true;
	} else if (index >= shownCount()) {
		return _siblingRight
			&& _siblingRight->shownId().valid()
			&& jumpFor(1);
	} else {
		subjumpTo(index);
	}
	return true;
}

void Controller::subjumpTo(int index) {
	Expects(shown());
	Expects(index >= 0 && index < shownCount());

	const auto peer = shownPeer();
	const auto id = FullStoryId{
		.peer = peer->id,
		.story = shownId(index),
	};
	auto &stories = peer->owner().stories();
	if (!id.story) {
		const auto delta = index - _index;
		if (_waitingForDelta != delta) {
			_waitingForDelta = delta;
			_waitingForId = {};
			loadMoreToList();
		}
	} else if (stories.lookup(id)) {
		_delegate->storiesJumpTo(&peer->session(), id, _context);
	} else if (_waitingForId != id) {
		_waitingForId = id;
		_waitingForDelta = 0;
		stories.loadAround(id, _context);
	}
}

void Controller::checkWaitingFor() {
	Expects(_waitingForId.valid());
	Expects(shown());

	const auto peer = shownPeer();
	auto &stories = peer->owner().stories();
	const auto maybe = stories.lookup(_waitingForId);
	if (!maybe) {
		if (maybe.error() == Data::NoStory::Deleted) {
			_waitingForId = {};
		}
		return;
	}
	_delegate->storiesJumpTo(
		&peer->session(),
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
		if (shown() && _index + 1 >= shownCount()) {
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
	if (_captionFullView) {
		_captionFullView->close();
	}
	if (pressed) {
		_reactions->outsidePressed();
	}
}

void Controller::setMenuShown(bool shown) {
	if (_menuShown != shown) {
		_menuShown = shown;
		updatePlayingAllowed();
	}
}

void Controller::repaintSibling(not_null<Sibling*> sibling) {
	if (sibling == _siblingLeft.get() || sibling == _siblingRight.get()) {
		_delegate->storiesRepaint();
	}
}

void Controller::repaint() {
	if (_captionFullView) {
		_captionFullView->repaint();
	}
	_delegate->storiesRepaint();
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

const Data::StoryViews &Controller::views(int limit, bool initial) {
	invalidate_weak_ptrs(&_viewsLoadGuard);
	if (initial) {
		refreshViewsFromData();
	}
	if (_viewsSlice.total > _viewsSlice.list.size()
		&& _viewsSlice.list.size() < limit) {
		const auto done = viewsGotMoreCallback();
		const auto peer = shownPeer();
		auto &stories = peer->owner().stories();
		if (peer->isChannel()) {
			stories.loadReactionsSlice(
				peer,
				_shown.story,
				_viewsSlice.nextOffset,
				done);
		} else {
			stories.loadViewsSlice(
				peer,
				_shown.story,
				_viewsSlice.nextOffset,
				done);
		}
	}
	return _viewsSlice;
}

rpl::producer<> Controller::moreViewsLoaded() const {
	return _moreViewsLoaded.events();
}

Fn<void(Data::StoryViews)> Controller::viewsGotMoreCallback() {
	return crl::guard(&_viewsLoadGuard, [=](Data::StoryViews result) {
		if (_viewsSlice.list.empty()) {
			const auto peer = shownPeer();
			auto &stories = peer->owner().stories();
			if (const auto maybeStory = stories.lookup(_shown)) {
				if (peer->isChannel()) {
					_viewsSlice = (*maybeStory)->channelReactionsList();
				} else {
					_viewsSlice = (*maybeStory)->viewsList();
				}
			} else {
				_viewsSlice = {};
			}
		} else {
			_viewsSlice.list.insert(
				end(_viewsSlice.list),
				begin(result.list),
				end(result.list));
			_viewsSlice.total = result.nextOffset.isEmpty()
				? int(_viewsSlice.list.size())
				: std::max(result.total, int(_viewsSlice.list.size()));
			_viewsSlice.nextOffset = result.nextOffset;
		}
		_moreViewsLoaded.fire({});
	});
}

bool Controller::shown() const {
	return _source || _list;
}

PeerData *Controller::shownPeer() const {
	return _source
		? _source->peer.get()
		: _list
		? _list->peer.get()
		: nullptr;
}

int Controller::shownCount() const {
	return _source ? int(_source->ids.size()) : _list ? _list->total : 0;
}

StoryId Controller::shownId(int index) const {
	Expects(index >= 0 && index < shownCount());

	return _source
		? (_source->ids.begin() + index)->id
		: (index < int(_list->sorted.size()))
		? _list->sorted[index]
		: StoryId();
}

std::unique_ptr<RepostView> Controller::validateRepostView(
		not_null<Data::Story*> story) {
	return (story->repost() || !story->channelPosts().empty())
		? std::make_unique<RepostView>(this, story)
		: nullptr;
}

void Controller::loadMoreToList() {
	Expects(shown());

	using namespace Data;

	const auto peer = shownPeer();
	const auto peerId = _shown.peer;
	auto &stories = peer->owner().stories();
	v::match(_context.data, [&](StoriesContextAlbum album) {
		stories.albumIdsLoadMore(peerId, album.id);
	}, [](const auto &) {
	});
}

void Controller::rebuildCachedSourcesList(
		const std::vector<Data::StoriesSourceInfo> &lists,
		int index) {
	Expects(index >= 0 && index < lists.size());

	const auto currentPeerId = lists[index].id;

	// Remove removed.
	_cachedSourcesList.erase(ranges::remove_if(_cachedSourcesList, [&](
			CachedSource source) {
		return !ranges::contains(
			lists,
			source.peerId,
			&Data::StoriesSourceInfo::id);
	}), end(_cachedSourcesList));

	// Find current, full rebuild if can't find.
	const auto i = ranges::find(
		_cachedSourcesList,
		currentPeerId,
		&CachedSource::peerId);
	if (i == end(_cachedSourcesList)) {
		_cachedSourcesList.clear();
	} else {
		_cachedSourceIndex = int(i - begin(_cachedSourcesList));
	}

	if (_cachedSourcesList.empty()) {
		// Full rebuild.
		const auto predicate = [&](const Data::StoriesSourceInfo &info) {
			return !_showingUnreadSources
				|| (info.unreadCount > 0)
				|| (info.id == currentPeerId);
		};
		const auto mapper = [](const Data::StoriesSourceInfo &info) {
			return CachedSource{ info.id };
		};
		_cachedSourcesList = lists
			| ranges::views::filter(predicate)
			| ranges::views::transform(mapper)
			| ranges::to_vector;
		_cachedSourceIndex = ranges::find(
			_cachedSourcesList,
			currentPeerId,
			&CachedSource::peerId
		) - begin(_cachedSourcesList);
	} else if (ranges::equal(
			lists,
			_cachedSourcesList,
			ranges::equal_to(),
			&Data::StoriesSourceInfo::id,
			&CachedSource::peerId)) {
		// No rebuild needed.
	} else {
		// All that go before the current push to front.
		for (auto before = index; before > 0;) {
			const auto &info = lists[--before];
			if (_showingUnreadSources && !info.unreadCount) {
				continue;
			} else if (!ranges::contains(
					_cachedSourcesList,
					info.id,
					&CachedSource::peerId)) {
				_cachedSourcesList.insert(
					begin(_cachedSourcesList),
					{ info.id });
				++_cachedSourceIndex;
			}
		}
		// All that go after the current push to back.
		for (auto after = index + 1, count = int(lists.size())
			; after != count
			; ++after) {
			const auto &info = lists[after];
			if (_showingUnreadSources && !info.unreadCount) {
				continue;
			} else if (!ranges::contains(
					_cachedSourcesList,
					info.id,
					&CachedSource::peerId)) {
				_cachedSourcesList.push_back({ info.id });
			}
		}
	}

	Ensures(_cachedSourcesList.size() <= lists.size());
	Ensures(_cachedSourceIndex >= 0
		&& _cachedSourceIndex < _cachedSourcesList.size());
}

void Controller::refreshViewsFromData() {
	Expects(shown());

	const auto peer = shownPeer();
	auto &stories = peer->owner().stories();
	const auto maybeStory = stories.lookup(_shown);
	const auto check = peer->isSelf()
		|| CanViewReactionsFor(peer);
	if (!maybeStory || !check) {
		_viewsSlice = {};
	} else if (peer->isChannel()) {
		_viewsSlice = (*maybeStory)->channelReactionsList();
	} else {
		_viewsSlice = (*maybeStory)->viewsList();
	}
}

void Controller::unfocusReply() {
	_wrap->setFocus();
}

void Controller::shareRequested() {
	const auto show = _delegate->storiesShow();
	if (auto box = PrepareShareBox(show, _shown, true)) {
		show->show(std::move(box));
	}
}

void Controller::deleteRequested() {
	const auto story = this->story();
	if (!story) {
		return;
	}
	const auto id = story->fullId();
	const auto weak = base::make_weak(this);
	const auto owner = &story->owner();
	const auto confirmed = [=](Fn<void()> close) {
		if (const auto strong = weak.get()) {
			if (const auto story = strong->story()) {
				if (story->fullId() == id) {
					moveFromShown();
				}
			}
		}
		owner->stories().deleteList({ id });
		close();
	};
	uiShow()->show(Ui::MakeConfirmBox({
		.text = tr::lng_stories_delete_one_sure(),
		.confirmed = confirmed,
		.confirmText = tr::lng_selected_delete(),
		.labelStyle = &st::storiesBoxLabel,
	}));
}

void Controller::reportRequested() {
	ReportRequested(uiShow(), _shown, &st::storiesReportBox);
}

void Controller::toggleInProfileRequested(bool inProfile) {
	const auto story = this->story();
	if (!story || !story->peer()->isSelf()) {
		return;
	}
	if (!inProfile
		&& v::is<Data::StoriesContextAlbum>(_context.data)
		&& (v::get<Data::StoriesContextAlbum>(_context.data).id
			!= Data::kStoriesAlbumIdArchive)) {
		moveFromShown();
	}
	story->owner().stories().toggleInProfileList(
		{ story->fullId() },
		inProfile);
	const auto channel = story->peer()->isChannel();
	uiShow()->showToast(PrepareToggleInProfileToast(channel, 1, inProfile));
}

void Controller::moveFromShown() {
	if (!subjumpFor(1)) {
		[[maybe_unused]] const auto jumped = subjumpFor(-1);
	}
}

bool Controller::ignoreWindowMove(QPoint position) const {
	return _replyArea->ignoreWindowMove(position)
		|| _header->ignoreWindowMove(position);
}

void Controller::tryProcessKeyInput(not_null<QKeyEvent*> e) {
	_replyArea->tryProcessKeyInput(e);
}

bool Controller::allowStealthMode() const {
	const auto story = this->story();
	return story
		&& !story->peer()->isSelf()
		&& story->peer()->session().premiumPossible();
}

void Controller::setupStealthMode() {
	SetupStealthMode(uiShow());
}

auto Controller::attachReactionsToMenu(
	not_null<Ui::PopupMenu*> menu,
	QPoint desiredPosition)
-> AttachStripResult {
	return _reactions->attachToMenu(menu, desiredPosition);
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

Ui::Toast::Config PrepareToggleInProfileToast(
		bool channel,
		int count,
		bool inProfile) {
	return {
		.text = (inProfile
			? (count == 1
				? (channel
					? tr::lng_stories_channel_save_done
					: tr::lng_stories_save_done)(
						tr::now,
						Ui::Text::Bold)
				: (channel
					? tr::lng_stories_channel_save_done_many
					: tr::lng_stories_save_done_many)(
						tr::now,
						lt_count,
						count,
						Ui::Text::Bold)).append(
							'\n').append((channel
								? tr::lng_stories_channel_save_done_about
								: tr::lng_stories_save_done_about)(tr::now))
			: (count == 1
				? (channel
					? tr::lng_stories_channel_archive_done
					: tr::lng_stories_archive_done)(
						tr::now,
						Ui::Text::WithEntities)
				: (channel
					? tr::lng_stories_channel_archive_done_many
					: tr::lng_stories_archive_done_many)(
						tr::now,
						lt_count,
						count,
						Ui::Text::WithEntities))),
		.st = &st::storiesActionToast,
		.duration = (inProfile
			? Data::Stories::kInProfileToastDuration
			: Ui::Toast::kDefaultDuration),
	};
}

Ui::Toast::Config PrepareTogglePinToast(
		bool channel,
		int count,
		bool pin) {
	return {
		.title = (pin
			? (count == 1
				? tr::lng_mediaview_pin_story_done(tr::now)
				: tr::lng_mediaview_pin_stories_done(
					tr::now,
					lt_count,
					count))
			: QString()),
		.text = { (pin
			? (count == 1
				? tr::lng_mediaview_pin_story_about(tr::now)
				: tr::lng_mediaview_pin_stories_about(
					tr::now,
					lt_count,
					count))
			: (count == 1
				? tr::lng_mediaview_unpin_story_done(tr::now)
				: tr::lng_mediaview_unpin_stories_done(
					tr::now,
					lt_count,
					count))) },
		.st = &st::storiesActionToast,
		.duration = (pin
			? Data::Stories::kInProfileToastDuration
			: Ui::Toast::kDefaultDuration),
	};
}

void ReportRequested(
		std::shared_ptr<Main::SessionShow> show,
		FullStoryId id,
		const style::ReportBox *stOverride) {
	if (const auto maybeStory = show->session().data().stories().lookup(id)) {
		const auto story = *maybeStory;
		const auto st = stOverride ? stOverride : &st::defaultReportBox;
		// show->hideLayer();
		ShowReportMessageBox(show, story->peer(), {}, { story->id() }, st);
	}
}

object_ptr<Ui::BoxContent> PrepareShortInfoBox(not_null<PeerData*> peer) {
	const auto open = [=] {
		if (const auto window = Core::App().windowFor(peer)) {
			window->invokeForSessionController(
				&peer->session().account(),
				peer,
				[&](not_null<Window::SessionController*> controller) {
					Core::App().hideMediaView();
					controller->showPeerHistory(peer);
				});
		}
	};
	return ::PrepareShortInfoBox(
		peer,
		open,
		[] { return false; },
		nullptr,
		&st::storiesShortInfoBox);
}

ClickHandlerPtr MakeChannelPostHandler(
		not_null<Main::Session*> session,
		FullMsgId item) {
	return std::make_shared<LambdaClickHandler>(crl::guard(session, [=] {
		const auto peer = session->data().peer(item.peer);
		if (const auto controller = session->tryResolveWindow(peer)) {
			Core::App().hideMediaView();
			controller->showPeerHistory(
				peer,
				Window::SectionShow::Way::ClearStack,
				item.msg);
		}
	}));
}

ClickHandlerPtr MakeUrlAreaHandler(
		base::weak_ptr<Controller> weak,
		const QString &url) {
	class Handler final : public HiddenUrlClickHandler {
	public:
		Handler(const QString &url, base::weak_ptr<Controller> weak)
		: HiddenUrlClickHandler(url), _weak(weak) {
		}

		void onClick(ClickContext context) const override {
			const auto raw = url();
			const auto strong = _weak.get();
			const auto prefix = u"tg://nft?slug="_q;
			if (raw.startsWith(prefix) && strong) {
				const auto slug = raw.mid(
					prefix.size()
				).split('&').front().split('#').front();
				Core::ResolveAndShowUniqueGift(
					strong->uiShow(),
					slug,
					::Settings::DarkCreditsEntryBoxStyle());
			} else {
				HiddenUrlClickHandler::onClick(context);
			}
		}

	private:
		base::weak_ptr<Controller> _weak;

	};
	return std::make_shared<Handler>(url, weak);
}

} // namespace Media::Stories
