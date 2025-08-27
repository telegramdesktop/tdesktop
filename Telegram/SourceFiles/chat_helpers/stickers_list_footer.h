/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "chat_helpers/compose/compose_features.h"
#include "chat_helpers/tabbed_selector.h"
#include "media/clip/media_clip_reader.h"
#include "mtproto/sender.h"
#include "ui/dpr/dpr_image.h"
#include "ui/round_rect.h"
#include "ui/userpic_view.h"

namespace Ui {
class InputField;
class CrossButton;
} // namespace Ui

namespace Ui::Text {
class CustomEmoji;
} // namespace Ui::Text

namespace Data {
class StickersSet;
class StickersSetThumbnailView;
class DocumentMedia;
} // namespace Data

namespace Lottie {
class SinglePlayer;
class FrameRenderer;
} // namespace Lottie

namespace Window {
class SessionController;
} // namespace Window

namespace style {
struct EmojiPan;
} // namespace style

namespace ChatHelpers {

enum class ValidateIconAnimations {
	Full,
	Scroll,
	None,
};

[[nodiscard]] uint64 EmojiSectionSetId(Ui::Emoji::Section section);
[[nodiscard]] uint64 RecentEmojiSectionSetId();
[[nodiscard]] uint64 AllEmojiSectionSetId();
[[nodiscard]] uint64 SearchEmojiSectionSetId();
[[nodiscard]] std::optional<Ui::Emoji::Section> SetIdEmojiSection(uint64 id);

struct GifSection {
	DocumentData *document = nullptr;
	EmojiPtr emoji;

	friend inline constexpr auto operator<=>(
		GifSection,
		GifSection) = default;
};
[[nodiscard]] rpl::producer<std::vector<GifSection>> GifSectionsValue(
	not_null<Main::Session*> session);

[[nodiscard]] std::vector<EmojiPtr> SearchEmoji(
	const std::vector<QString> &query,
	base::flat_set<EmojiPtr> &outResultSet);

struct StickerIcon {
	explicit StickerIcon(uint64 setId);
	StickerIcon(
		not_null<Data::StickersSet*> set,
		DocumentData *sticker,
		int pixw,
		int pixh);
	StickerIcon(StickerIcon&&);
	StickerIcon &operator=(StickerIcon&&);
	~StickerIcon();

	void ensureMediaCreated() const;

	uint64 setId = 0;
	Data::StickersSet *set = nullptr;
	mutable std::unique_ptr<Lottie::SinglePlayer> lottie;
	mutable std::unique_ptr<Ui::Text::CustomEmoji> custom;
	mutable Media::Clip::ReaderPointer webm;
	mutable QImage savedFrame;
	DocumentData *sticker = nullptr;
	ChannelData *megagroup = nullptr;
	mutable std::shared_ptr<Data::StickersSetThumbnailView> thumbnailMedia;
	mutable std::shared_ptr<Data::DocumentMedia> stickerMedia;
	mutable Ui::PeerUserpicView megagroupUserpic;
	int pixw = 0;
	int pixh = 0;
	mutable rpl::lifetime lifetime;
};

class GradientPremiumStar {
public:
	GradientPremiumStar();

	[[nodiscard]] QImage image() const;

private:
	void renderOnDemand() const;

	mutable QImage _image;
	rpl::lifetime _lifetime;

};

class StickersListFooter final : public TabbedSelector::InnerFooter {
public:
	struct Descriptor {
		not_null<Main::Session*> session;
		Fn<QColor()> customTextColor;
		Fn<bool()> paused;
		not_null<RpWidget*> parent;
		const style::EmojiPan *st = nullptr;
		ComposeFeatures features;
		bool forceFirstFrame = false;
	};
	explicit StickersListFooter(Descriptor &&descriptor);

	void preloadImages();
	void validateSelectedIcon(
		uint64 setId,
		ValidateIconAnimations animations);
	void refreshIcons(
		std::vector<StickerIcon> icons,
		uint64 activeSetId,
		Fn<std::shared_ptr<Lottie::FrameRenderer>()> renderer,
		ValidateIconAnimations animations);

	void leaveToChildEvent(QEvent *e, QWidget *child) override;

	void clearHeavyData();

	[[nodiscard]] rpl::producer<uint64> setChosen() const;
	[[nodiscard]] rpl::producer<> openSettingsRequests() const;

	void paintExpanding(
		Painter &p,
		QRect clip,
		float64 radius,
		RectPart origin);

	[[nodiscard]] static int IconFrameSize();

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	bool eventHook(QEvent *e) override;

	void processHideFinished() override;

private:
	enum class SpecialOver {
		None,
		Settings,
	};
	struct IconId {
		int index = 0;
		int subindex = 0;

		friend inline bool operator==(IconId a, IconId b) {
			return (a.index == b.index) && (a.subindex == b.subindex);
		}
	};
	using OverState = std::variant<SpecialOver, IconId>;
	struct IconInfo {
		int index = 0;
		int left = 0;
		int adjustedLeft = 0;
		int width = 0;
		bool visible = false;
	};
	struct ScrollState {
		template <typename UpdateCallback>
		explicit ScrollState(UpdateCallback &&callback);

		bool animationCallback(crl::time now);

		int selected = 0;
		int max = 0;
		int draggingStartX = 0;
		bool dragging = false;
		anim::value x;
		anim::value selectionX;
		anim::value selectionWidth;
		crl::time animationStart = 0;
		Ui::Animations::Basic animation;
	};
	struct ExpandingContext {
		QRect clip;
		float64 progress = 0.;
		int radius = 0;
		bool expanding = false;
	};

	void enumerateVisibleIcons(Fn<void(const IconInfo &)> callback) const;
	void enumerateIcons(Fn<bool(const IconInfo &)> callback) const;
	void enumerateSubicons(Fn<bool(const IconInfo &)> callback) const;
	[[nodiscard]] IconInfo iconInfo(int index) const;
	[[nodiscard]] IconInfo subiconInfo(int index) const;

	[[nodiscard]] std::shared_ptr<Lottie::FrameRenderer> getLottieRenderer();
	void setSelectedIcon(
		int newSelected,
		ValidateIconAnimations animations);
	void setSelectedSubicon(
		int newSelected,
		ValidateIconAnimations animations);
	void validateIconLottieAnimation(const StickerIcon &icon);
	void validateIconWebmAnimation(const StickerIcon &icon);
	void validateIconAnimation(const StickerIcon &icon);
	void customEmojiRepaint();

	void refreshIconsGeometry(
		uint64 activeSetId,
		ValidateIconAnimations animations);
	void refreshSubiconsGeometry();
	void refreshScrollableDimensions();
	void updateSelected();
	void updateSetIcon(uint64 setId);
	void updateSetIconAt(int left);
	void checkDragging(ScrollState &state);
	bool finishDragging(ScrollState &state);
	bool finishDragging();

	void paint(Painter &p, const ExpandingContext &context) const;
	void paintStickerSettingsIcon(QPainter &p) const;
	void paintSetIcon(
		Painter &p,
		const ExpandingContext &context,
		const IconInfo &info,
		crl::time now,
		bool paused) const;
	void prepareSetIcon(
		const ExpandingContext &context,
		const IconInfo &info,
		crl::time now,
		bool paused) const;
	void paintSetIconToCache(
		Painter &p,
		const ExpandingContext &context,
		const IconInfo &info,
		crl::time now,
		bool paused) const;
	void paintSelectionBg(
		QPainter &p,
		const ExpandingContext &context) const;
	void paintLeftRightFading(
		QPainter &p,
		const ExpandingContext &context) const;

	void updateEmojiSectionWidth();
	void updateEmojiWidthCallback();

	void scrollByWheelEvent(not_null<QWheelEvent*> e);

	void validateFadeLeft(int leftWidth) const;
	void validateFadeRight(int rightWidth) const;
	void validateFadeMask() const;

	void clipCallback(Media::Clip::Notification notification, uint64 setId);

	const not_null<Main::Session*> _session;
	const Fn<QColor()> _customTextColor;
	const Fn<bool()> _paused;
	const ComposeFeatures _features;

	static constexpr auto kVisibleIconsCount = 8;

	std::weak_ptr<Lottie::FrameRenderer> _lottieRenderer;
	std::vector<StickerIcon> _icons;
	Fn<std::shared_ptr<Lottie::FrameRenderer>()> _renderer;
	uint64 _activeByScrollId = 0;
	OverState _selected = SpecialOver::None;
	OverState _pressed = SpecialOver::None;

	QPoint _iconsMousePos, _iconsMouseDown;
	int _iconsLeft = 0;
	int _iconsRight = 0;
	int _iconsTop = 0;
	int _singleWidth = 0;
	QPoint _areaPosition;

	mutable QImage _fadeLeftCache;
	mutable QColor _fadeLeftColor;
	mutable QImage _fadeRightCache;
	mutable QColor _fadeRightColor;
	mutable QImage _fadeMask;
	mutable QImage _setIconCache;

	ScrollState _iconState;
	ScrollState _subiconState;

	Ui::RoundRect _selectionBg, _subselectionBg;
	Ui::Animations::Simple _subiconsWidthAnimation;
	int _subiconsWidth = 0;
	bool _subiconsExpanded = false;
	bool _repaintScheduled = false;
	bool _forceFirstFrame = false;

	rpl::event_stream<> _openSettingsRequests;
	rpl::event_stream<uint64> _setChosen;

};

class LocalStickersManager final {
public:
	explicit LocalStickersManager(not_null<Main::Session*> session);

	void install(uint64 setId);
	[[nodiscard]] bool isInstalledLocally(uint64 setId) const;
	void removeInstalledLocally(uint64 setId);
	bool clearInstalledLocally();

private:
	void sendInstallRequest(
		uint64 setId,
		const MTPInputStickerSet &input);
	void installedLocally(uint64 setId);
	void notInstalledLocally(uint64 setId);

	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	base::flat_set<uint64> _installedLocallySets;

};

} // namespace ChatHelpers
