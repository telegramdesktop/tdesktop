/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "info/info_controller.h" // Key
#include "info/profile/info_profile_badge.h"
#include "ui/rp_widget.h"
#include "ui/userpic_view.h"

namespace Data {
class ForumTopic;
class DocumentMedia;
struct SavedStarGift;
struct ColorProfileSet;
class SavedStarGiftId;
} // namespace Data

namespace Info::Profile {
class BadgeTooltip;
class TopicIconView;
} // namespace Info::Profile

namespace Lottie {
class Animation;
class MultiPlayer;
} // namespace Lottie

namespace Ui {
class VideoUserpicPlayer;
struct OutlineSegment;
namespace Text {
class CustomEmoji;
} // namespace Text
} // namespace Ui

class PeerData;

namespace base {
class Timer;
} // namespace base

namespace style {
struct InfoTopBar;
struct InfoPeerBadge;
struct FlatLabel;
} //namespace style

class QGraphicsOpacityEffect;

namespace Ui {
class FlatLabel;
class IconButton;
class PopupMenu;
class RoundButton;
class StarsRating;
template <typename Widget>
class FadeWrap;
class HorizontalFitContainer;
namespace Animations {
class Simple;
} // namespace Animations
} //namespace Ui

namespace Info {
class Controller;
class Key;
enum class Wrap;
} //namespace Info

namespace Ui::Menu {
struct MenuCallback;
} //namespace Ui::Menu

namespace Info::Profile {

class Badge;
class StatusLabel;

struct TopBarActionButtonStyle;

class TopBar final : public Ui::RpWidget {
public:
	enum class Source {
		Profile,
		Stories,
		Preview,
	};

	struct Descriptor {
		not_null<Window::SessionController*> controller;
		Key key;
		rpl::producer<Wrap> wrap;
		Source source = Source::Profile;
		PeerData *peer = nullptr;
		rpl::producer<bool> backToggles;
		rpl::producer<> showFinished;
	};

	struct AnimatedPatternPoint {
		QPointF basePosition;
		float64 size;
		float64 startTime;
		float64 endTime;
	};

	TopBar(not_null<Ui::RpWidget*> parent, Descriptor descriptor);
	~TopBar();

	[[nodiscard]] rpl::producer<> backRequest() const;

	void setOnlineCount(rpl::producer<int> &&count);

	void setRoundEdges(bool value);
	void setLottieSingleLoop(bool value);
	void setColorProfileIndex(std::optional<uint8> index);
	void setPatternEmojiId(std::optional<DocumentId> patternEmojiId);
	void setLocalEmojiStatusId(EmojiStatusId emojiStatusId);
	void addTopBarEditButton(
		not_null<Window::SessionController*> controller,
		Wrap wrap,
		bool shouldUseColored);

	rpl::producer<std::optional<QColor>> edgeColor() const;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void paintEdges(QPainter &p, const QBrush &brush) const;
	void paintEdges(QPainter &p) const;
	void updateLabelsPosition();
	[[nodiscard]] int titleMostLeft() const;
	[[nodiscard]] int statusMostLeft() const;
	[[nodiscard]] QRect userpicGeometry() const;
	void updateGiftButtonsGeometry(
		float64 progressCurrent,
		const QRect &userpicRect);
	void paintUserpic(QPainter &p, const QRect &geometry);
	void updateVideoUserpic();
	void showTopBarMenu(
		not_null<Window::SessionController*> controller,
		bool check);
	void fillTopBarMenu(
		not_null<Window::SessionController*> controller,
		const Ui::Menu::MenuCallback &addAction);
	void setupUserpicButton(not_null<Window::SessionController*> controller);
	void setupActions(not_null<Window::SessionController*> controller);
	void setupButtons(
		not_null<Window::SessionController*> controller,
		Source source);
	void setupShowLastSeen(not_null<Window::SessionController*> controller);
	void setupUniqueBadgeTooltip();
	void hideBadgeTooltip();
	void setupAnimatedPattern(const QRect &userpicGeometry = QRect());
	void paintAnimatedPattern(
		QPainter &p,
		const QRect &rect,
		const QRect &userpicGeometry);
	void setupPinnedToTopGifts(
		not_null<Window::SessionController*> controller);
	void setupNewGifts(
		not_null<Window::SessionController*> controller,
		const std::vector<Data::SavedStarGift> &gifts);
	void paintPinnedToTopGifts(
		QPainter &p,
		const QRect &rect,
		const QRect &userpicGeometry);
	[[nodiscard]] QPointF calculateGiftPosition(
		int position,
		float64 progress,
		const QRect &userpicRect) const;
	void adjustColors(const std::optional<QColor> &edgeColor);
	void updateCollectibleStatus();
	void setupStoryOutline(const QRect &geometry = QRect());
	void updateStoryOutline(std::optional<QColor> edgeColor);
	void paintStoryOutline(QPainter &p, const QRect &geometry);
	void updateStatusPosition(float64 progressCurrent);
	[[nodiscard]] int calculateRightButtonsWidth() const;
	[[nodiscard]] const style::FlatLabel &statusStyle() const;
	void setupStatusWithRating();
	[[nodiscard]] TopBarActionButtonStyle mapActionStyle(
		std::optional<QColor> c) const;

	[[nodiscard]] rpl::producer<QString> nameValue() const;

	[[nodiscard]] auto effectiveColorProfile()
	const -> std::optional<Data::ColorProfileSet>;
	[[nodiscard]] auto effectiveCollectible()
	const -> std::shared_ptr<Data::EmojiStatusCollectible>;

	const not_null<PeerData*> _peer;
	Data::ForumTopic *_topic = nullptr;
	const Key _key;
	rpl::variable<Wrap> _wrap;
	const style::InfoTopBar &_st;
	const Source _source;

	std::unique_ptr<base::Timer> _badgeTooltipHide;
	const std::unique_ptr<Badge> _botVerify;
	rpl::variable<Badge::Content> _badgeContent;
	const Fn<bool()> _gifPausedChecker;
	const std::unique_ptr<Badge> _badge;
	const std::unique_ptr<Badge> _verified;

	const bool _hasActions;
	const int _minForProgress;

	std::unique_ptr<BadgeTooltip> _badgeTooltip;
	std::vector<std::unique_ptr<BadgeTooltip>> _badgeOldTooltips;
	uint64 _badgeCollectibleId = 0;

	object_ptr<Ui::FlatLabel> _title;
	std::unique_ptr<Ui::StarsRating> _starsRating;
	object_ptr<Ui::FlatLabel> _status;
	std::unique_ptr<StatusLabel> _statusLabel;
	rpl::variable<int> _statusShift = 0;
	object_ptr<Ui::RoundButton> _showLastSeen = { nullptr };
	object_ptr<Ui::RoundButton> _forumButton = { nullptr };
	QGraphicsOpacityEffect *_showLastSeenOpacity = nullptr;

	std::shared_ptr<style::FlatLabel> _statusSt;
	std::shared_ptr<style::InfoPeerBadge> _botVerifySt;
	std::shared_ptr<style::InfoPeerBadge> _badgeSt;
	std::shared_ptr<style::InfoPeerBadge> _verifiedSt;

	rpl::variable<float64> _progress = 0.;
	bool _roundEdges = true;

	rpl::variable<std::optional<QColor>> _edgeColor;
	bool _hasGradientBg = false;
	std::optional<QColor> _solidBg;
	QImage _cachedGradient;
	QPainterPath _cachedClipPath;
	std::unique_ptr<Ui::Text::CustomEmoji> _patternEmoji;
	QImage _basePatternImage;

	std::vector<AnimatedPatternPoint> _animatedPoints;
	QRect _lastUserpicRect;

	base::unique_qptr<Ui::AbstractButton> _userpicButton;

	Ui::PeerUserpicView _userpicView;
	InMemoryKey _userpicUniqueKey;
	QImage _cachedUserpic;
	QImage _monoforumMask;
	std::unique_ptr<Ui::VideoUserpicPlayer> _videoUserpicPlayer;
	std::unique_ptr<TopicIconView> _topicIconView;
	rpl::lifetime _userpicLoadingLifetime;

	base::unique_qptr<Ui::IconButton> _close;
	base::unique_qptr<Ui::FadeWrap<Ui::IconButton>> _back;
	rpl::variable<bool> _backToggles;

	rpl::event_stream<> _backClicks;

	base::unique_qptr<Ui::IconButton> _topBarButton;
	base::unique_qptr<Ui::PopupMenu> _peerMenu;

	Ui::RpWidget *_actionMore = nullptr;

	base::unique_qptr<Ui::HorizontalFitContainer> _actions;

	std::unique_ptr<Lottie::MultiPlayer> _lottiePlayer;
	bool _lottieSingleLoop = false;
	struct PinnedToTopGiftEntry {
		Data::SavedStarGiftId manageId;
		// QString slug;
		Lottie::Animation *animation = nullptr;
		std::shared_ptr<Data::DocumentMedia> media;
		QImage bg;
		QImage lastFrame;
		int position = 0;
		base::unique_qptr<Ui::AbstractButton> button;
	};
	bool _pinnedToTopGiftsFirstTimeShowed = false;
	std::vector<PinnedToTopGiftEntry> _pinnedToTopGifts;
	std::unique_ptr<Ui::Animations::Simple> _giftsAppearing;
	std::unique_ptr<Ui::Animations::Simple> _giftsHiding;
	rpl::lifetime _giftsLoadingLifetime;

	QBrush _storyOutlineBrush;
	std::vector<Ui::OutlineSegment> _storySegments;
	bool _hasStories = false;
	bool _hasLiveStories = false;

	std::optional<uint8> _localColorProfileIndex;
	std::optional<DocumentId> _localPatternEmojiId;
	std::shared_ptr<Data::EmojiStatusCollectible> _localCollectible;

};

} // namespace Info::Profile
