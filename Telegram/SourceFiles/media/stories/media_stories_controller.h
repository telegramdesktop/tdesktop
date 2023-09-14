/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "data/data_stories.h"
#include "ui/effects/animations.h"

namespace style {
struct ReportBox;
} // namespace style

namespace base {
class PowerSaveBlocker;
} // namespace base

namespace ChatHelpers {
class Show;
struct FileChosen;
} // namespace ChatHelpers

namespace Data {
struct FileOrigin;
class DocumentMedia;
} // namespace Data

namespace HistoryView::Reactions {
class CachedIconFactory;
struct ChosenReaction;
enum class AttachSelectorResult;
} // namespace HistoryView::Reactions

namespace Ui {
class RpWidget;
class BoxContent;
class PopupMenu;
} // namespace Ui

namespace Ui::Toast {
struct Config;
} // namespace Ui::Toast

namespace Main {
class Session;
class SessionShow;
} // namespace Main

namespace Media::Player {
struct TrackState;
} // namespace Media::Player

namespace Media::Stories {

class Header;
class Slider;
class ReplyArea;
class Reactions;
class RecentViews;
class Sibling;
class Delegate;
struct SiblingView;
enum class SiblingType;
struct ContentLayout;
class CaptionFullView;
enum class ReactionsMode;
class SuggestedReactionView;

enum class HeaderLayout {
	Normal,
	Outside,
};

enum class PauseState {
	Playing,
	Paused,
	Inactive,
	InactivePaused,
};

struct SiblingLayout {
	QRect geometry;
	QRect userpic;
	QRect nameBoundingRect;
	int nameFontSize = 0;

	friend inline bool operator==(SiblingLayout, SiblingLayout) = default;
};

struct Layout {
	QRect content;
	QRect header;
	QRect slider;
	QRect reactions;
	int controlsWidth = 0;
	QPoint controlsBottomPosition;
	QRect views;
	QRect autocompleteRect;
	HeaderLayout headerLayout = HeaderLayout::Normal;
	SiblingLayout siblingLeft;
	SiblingLayout siblingRight;

	friend inline bool operator==(Layout, Layout) = default;
};

class Controller final : public base::has_weak_ptr {
public:
	explicit Controller(not_null<Delegate*> delegate);
	~Controller();

	[[nodiscard]] Data::Story *story() const;
	[[nodiscard]] not_null<Ui::RpWidget*> wrap() const;
	[[nodiscard]] Layout layout() const;
	[[nodiscard]] rpl::producer<Layout> layoutValue() const;
	[[nodiscard]] ContentLayout contentLayout() const;
	[[nodiscard]] bool closeByClickAt(QPoint position) const;
	[[nodiscard]] Data::FileOrigin fileOrigin() const;
	[[nodiscard]] TextWithEntities captionText() const;
	[[nodiscard]] bool skipCaption() const;
	void toggleLiked();
	void showFullCaption();
	void captionClosing();
	void captionClosed();

	[[nodiscard]] std::shared_ptr<ChatHelpers::Show> uiShow() const;
	[[nodiscard]] auto stickerOrEmojiChosen() const
	-> rpl::producer<ChatHelpers::FileChosen>;
	[[nodiscard]] auto cachedReactionIconFactory() const
		-> HistoryView::Reactions::CachedIconFactory &;

	void show(not_null<Data::Story*> story, Data::StoriesContext context);
	void ready();

	void updateVideoPlayback(const Player::TrackState &state);
	[[nodiscard]] ClickHandlerPtr lookupAreaHandler(QPoint point) const;

	[[nodiscard]] bool subjumpAvailable(int delta) const;
	[[nodiscard]] bool subjumpFor(int delta);
	[[nodiscard]] bool jumpFor(int delta);
	[[nodiscard]] bool paused() const;
	void togglePaused(bool paused);
	void contentPressed(bool pressed);
	void setMenuShown(bool shown);

	[[nodiscard]] PauseState pauseState() const;
	[[nodiscard]] float64 currentVolume() const;
	void toggleVolume();
	void changeVolume(float64 volume);
	void volumeChangeFinished();

	void repaintSibling(not_null<Sibling*> sibling);
	[[nodiscard]] SiblingView sibling(SiblingType type) const;

	[[nodiscard]] const Data::StoryViews &views(int limit, bool initial);
	[[nodiscard]] rpl::producer<> moreViewsLoaded() const;

	void unfocusReply();
	void shareRequested();
	void deleteRequested();
	void reportRequested();
	void togglePinnedRequested(bool pinned);

	[[nodiscard]] bool ignoreWindowMove(QPoint position) const;
	void tryProcessKeyInput(not_null<QKeyEvent*> e);

	[[nodiscard]] bool allowStealthMode() const;
	void setupStealthMode();

	using AttachStripResult = HistoryView::Reactions::AttachSelectorResult;
	[[nodiscard]] AttachStripResult attachReactionsToMenu(
		not_null<Ui::PopupMenu*> menu,
		QPoint desiredPosition);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	class PhotoPlayback;
	class Unsupported;
	using ChosenReaction = HistoryView::Reactions::ChosenReaction;
	struct StoriesList {
		not_null<PeerData*> peer;
		Data::StoriesIds ids;
		int total = 0;

		friend inline bool operator==(
			const StoriesList &,
			const StoriesList &) = default;
	};
	struct CachedSource {
		PeerId peerId = 0;
		StoryId shownId = 0;

		explicit operator bool() const {
			return peerId != 0;
		}
	};
	struct ActiveArea {
		QRectF original;
		QRect geometry;
		float64 rotation = 0.;
		ClickHandlerPtr handler;
		std::unique_ptr<SuggestedReactionView> reaction;
	};

	void initLayout();
	bool changeShown(Data::Story *story);
	void subscribeToSession();
	void updatePhotoPlayback(const Player::TrackState &state);
	void updatePlayback(const Player::TrackState &state);
	void updatePowerSaveBlocker(const Player::TrackState &state);
	void maybeMarkAsRead(const Player::TrackState &state);
	void markAsRead();

	void updateContentFaded();
	void updatePlayingAllowed();
	void setPlayingAllowed(bool allowed);
	void rebuildActiveAreas(const Layout &layout) const;

	void hideSiblings();
	void showSiblings(not_null<Main::Session*> session);
	void showSibling(
		std::unique_ptr<Sibling> &sibling,
		not_null<Main::Session*> session,
		CachedSource cached);

	void subjumpTo(int index);
	void checkWaitingFor();
	void moveFromShown();

	void refreshViewsFromData();
	[[nodiscard]] auto viewsGotMoreCallback()
		-> Fn<void(Data::StoryViews)>;

	[[nodiscard]] bool shown() const;
	[[nodiscard]] PeerData *shownPeer() const;
	[[nodiscard]] int shownCount() const;
	[[nodiscard]] StoryId shownId(int index) const;
	void rebuildFromContext(not_null<PeerData*> peer, FullStoryId storyId);
	void checkMoveByDelta();
	void loadMoreToList();
	void preloadNext();
	void rebuildCachedSourcesList(
		const std::vector<Data::StoriesSourceInfo> &lists,
		int index);

	void updateAreas(Data::Story *story);
	void reactionChosen(ReactionsMode mode, ChosenReaction chosen);

	const not_null<Delegate*> _delegate;

	rpl::variable<std::optional<Layout>> _layout;

	const not_null<Ui::RpWidget*> _wrap;
	const std::unique_ptr<Header> _header;
	const std::unique_ptr<Slider> _slider;
	const std::unique_ptr<ReplyArea> _replyArea;
	const std::unique_ptr<Reactions> _reactions;
	const std::unique_ptr<RecentViews> _recentViews;
	std::unique_ptr<Unsupported> _unsupported;
	std::unique_ptr<PhotoPlayback> _photoPlayback;
	std::unique_ptr<CaptionFullView> _captionFullView;

	Ui::Animations::Simple _contentFadeAnimation;
	bool _contentFaded = false;

	bool _windowActive = false;
	bool _replyActive = false;
	bool _layerShown = false;
	bool _menuShown = false;
	bool _tooltipShown = false;
	bool _paused = false;

	FullStoryId _shown;
	TextWithEntities _captionText;
	Data::StoriesContext _context;
	std::optional<Data::StoriesSource> _source;
	std::optional<StoriesList> _list;
	FullStoryId _waitingForId;
	int _waitingForDelta = 0;
	int _index = 0;
	int _sliderIndex = 0;
	int _sliderCount = 0;
	bool _started = false;
	bool _viewed = false;

	std::vector<Data::StoryLocation> _locations;
	std::vector<Data::SuggestedReaction> _suggestedReactions;
	mutable std::vector<ActiveArea> _areas;

	std::vector<CachedSource> _cachedSourcesList;
	int _cachedSourceIndex = -1;
	bool _showingUnreadSources = false;

	Data::StoryViews _viewsSlice;
	rpl::event_stream<> _moreViewsLoaded;
	base::has_weak_ptr _viewsLoadGuard;

	std::unique_ptr<Sibling> _siblingLeft;
	std::unique_ptr<Sibling> _siblingRight;

	std::unique_ptr<base::PowerSaveBlocker> _powerSaveBlocker;

	Main::Session *_session = nullptr;
	rpl::lifetime _sessionLifetime;

	rpl::lifetime _contextLifetime;

	rpl::lifetime _lifetime;

};

[[nodiscard]] Ui::Toast::Config PrepareTogglePinnedToast(
	bool channel,
	int count,
	bool pinned);
void ReportRequested(
	std::shared_ptr<Main::SessionShow> show,
	FullStoryId id,
	const style::ReportBox *stOverride = nullptr);
[[nodiscard]] object_ptr<Ui::BoxContent> PrepareShortInfoBox(
	not_null<PeerData*> peer);

} // namespace Media::Stories
