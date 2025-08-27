/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"
#include "ui/chat/message_bubble.h"
#include "ui/rect_part.h"

class History;
struct HistoryMessageEdited;
struct TextSelection;

namespace base {
template <typename Enum>
class enum_mask;
} // namespace base

namespace Storage {
enum class SharedMediaType : signed char;
using SharedMediaTypesMask = base::enum_mask<SharedMediaType>;
} // namespace Storage

namespace Lottie {
struct ColorReplacements;
} // namespace Lottie

namespace Ui {
struct BubbleSelectionInterval;
struct ChatPaintContext;
class SpoilerAnimation;
} // namespace Ui

namespace Images {
struct CornersMaskRef;
} // namespace Images

namespace HistoryView {

enum class PointState : char;
enum class CursorState : char;
enum class InfoDisplayType : char;
struct TextState;
struct StateRequest;
struct MediaSpoiler;
struct MediaSpoilerTag;
class StickerPlayer;
class Element;
struct SelectedQuote;

using PaintContext = Ui::ChatPaintContext;

enum class MediaInBubbleState : uchar {
	None,
	Top,
	Middle,
	Bottom,
};

[[nodiscard]] TimeId DurationForTimestampLinks(
	not_null<DocumentData*> document);
[[nodiscard]] QString TimestampLinkBase(
	not_null<DocumentData*> document,
	FullMsgId context);

[[nodiscard]] TimeId DurationForTimestampLinks(
	not_null<WebPageData*> webpage);
[[nodiscard]] QString TimestampLinkBase(
	not_null<WebPageData*> webpage,
	FullMsgId context);

[[nodiscard]] TextWithEntities AddTimestampLinks(
	TextWithEntities text,
	TimeId duration,
	const QString &base);

struct PaidInformation {
	int messages = 0;
	int stars = 0;

	explicit operator bool() const {
		return stars != 0;
	}
};

class Media : public Object, public base::has_weak_ptr {
public:
	explicit Media(not_null<Element*> parent) : _parent(parent) {
	}

	[[nodiscard]] not_null<Element*> parent() const;
	[[nodiscard]] not_null<History*> history() const;

	[[nodiscard]] virtual TextForMimeData selectedText(
			TextSelection selection) const {
		return {};
	}
	[[nodiscard]] virtual SelectedQuote selectedQuote(
		TextSelection selection) const;
	[[nodiscard]] virtual TextSelection selectionFromQuote(
			const SelectedQuote &quote) const {
		return {};
	}

	[[nodiscard]] virtual bool isDisplayed() const {
		return true;
	}
	virtual void updateNeedBubbleState() {
	}
	[[nodiscard]] virtual bool hasTextForCopy() const {
		return false;
	}
	[[nodiscard]] virtual bool aboveTextByDefault() const {
		return true;
	}
	[[nodiscard]] virtual HistoryItem *itemForText() const;
	[[nodiscard]] virtual bool hideMessageText() const {
		return true;
	}
	[[nodiscard]] virtual bool hideServiceText() const {
		return false;
	}
	[[nodiscard]] virtual bool hideFromName() const {
		return false;
	}
	[[nodiscard]] virtual bool allowsFastShare() const {
		return false;
	}
	[[nodiscard]] virtual auto paidInformation() const
	-> std::optional<PaidInformation> {
		return {};
	}
	virtual void refreshParentId(not_null<HistoryItem*> realParent) {
	}
	virtual void drawHighlight(
		Painter &p,
		const PaintContext &context,
		int top) const {
	}
	virtual void draw(Painter &p, const PaintContext &context) const = 0;
	[[nodiscard]] virtual PointState pointState(QPoint point) const;
	[[nodiscard]] virtual TextState textState(
		QPoint point,
		StateRequest request) const = 0;
	virtual void updatePressed(QPoint point) {
	}

	[[nodiscard]] virtual Storage::SharedMediaTypesMask sharedMediaTypes() const;

	// if we are in selecting items mode perhaps we want to
	// toggle selection instead of activating the pressed link
	[[nodiscard]] virtual bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &p) const = 0;
	[[nodiscard]] virtual bool allowTextSelectionByHandler(
		const ClickHandlerPtr &p) const;

	[[nodiscard]] virtual TextSelection adjustSelection(
			TextSelection selection,
			TextSelectType type) const {
		return selection;
	}
	[[nodiscard]] virtual uint16 fullSelectionLength() const {
		return 0;
	}
	[[nodiscard]] TextSelection skipSelection(
		TextSelection selection) const;
	[[nodiscard]] TextSelection unskipSelection(
		TextSelection selection) const;

	[[nodiscard]] virtual auto getBubbleSelectionIntervals(
		TextSelection selection) const
	-> std::vector<Ui::BubbleSelectionInterval>;

	// if we press and drag this link should we drag the item
	[[nodiscard]] virtual bool dragItemByHandler(
		const ClickHandlerPtr &p) const = 0;

	virtual void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	}
	virtual void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	}

	[[nodiscard]] virtual bool uploading() const {
		return false;
	}

	[[nodiscard]] virtual PhotoData *getPhoto() const {
		return nullptr;
	}
	[[nodiscard]] virtual DocumentData *getDocument() const {
		return nullptr;
	}

	void playAnimation() {
		playAnimation(false);
	}
	void autoplayAnimation() {
		playAnimation(true);
	}
	virtual void stopAnimation() {
	}
	virtual void stickerClearLoopPlayed() {
	}
	virtual std::unique_ptr<StickerPlayer> stickerTakePlayer(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements);
	virtual QImage locationTakeImage();

	struct TodoTaskInfo {
		int id = 0;
		PeerData *completedBy = nullptr;
		TimeId completionDate = TimeId();
	};
	virtual std::vector<TodoTaskInfo> takeTasksInfo();

	virtual void checkAnimation() {
	}

	[[nodiscard]] virtual QSize sizeForGroupingOptimal(
			int maxWidth,
			bool last) const {
		Unexpected("Grouping method call.");
	}
	[[nodiscard]] virtual QSize sizeForGrouping(int width) const {
		Unexpected("Grouping method call.");
	}
	virtual void drawGrouped(
			Painter &p,
			const PaintContext &context,
			const QRect &geometry,
			RectParts sides,
			Ui::BubbleRounding rounding,
			float64 highlightOpacity,
			not_null<uint64*> cacheKey,
			not_null<QPixmap*> cache) const {
		Unexpected("Grouping method call.");
	}
	[[nodiscard]] virtual TextState getStateGrouped(
		const QRect &geometry,
		RectParts sides,
		QPoint point,
		StateRequest request) const;

	virtual void drawSpoilerTag(
			Painter &p,
			QRect rthumb,
			const PaintContext &context,
			Fn<QImage()> generateBackground) const {
		Unexpected("Spoiler tag method call.");
	}
	[[nodiscard]] virtual ClickHandlerPtr spoilerTagLink() const {
		Unexpected("Spoiler tag method call.");
	}
	[[nodiscard]] virtual QImage spoilerTagBackground() const {
		Unexpected("Spoiler tag method call.");
	}

	[[nodiscard]] virtual bool animating() const {
		return false;
	}

	virtual void hideSpoilers() {
	}
	[[nodiscard]] virtual bool needsBubble() const = 0;
	[[nodiscard]] virtual bool unwrapped() const {
		return false;
	}
	[[nodiscard]] virtual bool customInfoLayout() const = 0;
	[[nodiscard]] virtual QRect contentRectForReactions() const {
		return QRect(0, 0, width(), height());
	}
	[[nodiscard]] virtual auto reactionButtonCenterOverride() const
	-> std::optional<int> {
		return std::nullopt;
	}
	[[nodiscard]] virtual QPoint resolveCustomInfoRightBottom() const {
		return QPoint();
	}
	[[nodiscard]] virtual QMargins bubbleMargins() const {
		return QMargins();
	}

	[[nodiscard]] virtual bool overrideEditedDate() const {
		return false;
	}
	[[nodiscard]] virtual HistoryMessageEdited *displayedEditBadge() const {
		Unexpected("displayedEditBadge() on non-grouped media.");
	}

	// An attach media in a web page can provide an
	// additional text to be displayed below the attach.
	// For example duration / progress for video messages.
	[[nodiscard]] virtual QString additionalInfoString() const {
		return QString();
	}

	void setInBubbleState(MediaInBubbleState state) {
		_inBubbleState = state;
	}
	[[nodiscard]] MediaInBubbleState inBubbleState() const {
		return _inBubbleState;
	}
	void setBubbleRounding(Ui::BubbleRounding rounding) {
		_bubbleRounding = rounding;
	}
	[[nodiscard]] Ui::BubbleRounding bubbleRounding() const {
		return _bubbleRounding;
	}
	[[nodiscard]] Ui::BubbleRounding adjustedBubbleRounding(
		RectParts square = {}) const;
	[[nodiscard]] bool isBubbleTop() const {
		return (_inBubbleState == MediaInBubbleState::Top)
			|| (_inBubbleState == MediaInBubbleState::None);
	}
	[[nodiscard]] bool isBubbleBottom() const {
		return (_inBubbleState == MediaInBubbleState::Bottom)
			|| (_inBubbleState == MediaInBubbleState::None);
	}
	[[nodiscard]] bool isRoundedInBubbleBottom() const;
	[[nodiscard]] virtual bool skipBubbleTail() const {
		return false;
	}

	// Sometimes webpages can force the bubble to fit their size instead of
	// allowing message text to be as wide as possible (like wallpapers).
	[[nodiscard]] virtual bool enforceBubbleWidth() const {
		return false;
	}

	// Sometimes click on media in message is overloaded by the message:
	// (for example it can open a link or a game instead of opening media)
	// But the overloading click handler should be used only when media
	// is already loaded (not a photo or GIF waiting for load with auto
	// load being disabled - in such case media should handle the click).
	[[nodiscard]] virtual bool isReadyForOpen() const {
		return true;
	}

	struct BubbleRoll {
		float64 rotate = 0.;
		float64 scale = 1.;

		explicit operator bool() const {
			return (rotate != 0.) || (scale != 1.);
		}
	};
	[[nodiscard]] virtual BubbleRoll bubbleRoll() const {
		return BubbleRoll();
	}
	[[nodiscard]] virtual QMargins bubbleRollRepaintMargins() const {
		return QMargins();
	}
	virtual void paintBubbleFireworks(
		Painter &p,
		const QRect &bubble,
		crl::time ms) const {
	}
	[[nodiscard]] virtual bool customHighlight() const {
		return false;
	}

	virtual bool hasHeavyPart() const {
		return false;
	}
	virtual void unloadHeavyPart() {
	}

	// Should be called only by Data::Session.
	virtual void updateSharedContactUserId(UserId userId) {
	}
	virtual void parentTextUpdated() {
	}

	virtual bool consumeHorizontalScroll(QPoint position, int delta) {
		return false;
	}

	[[nodiscard]] bool hasPurchasedTag() const;
	void drawPurchasedTag(
		Painter &p,
		QRect outer,
		const PaintContext &context) const;

	virtual ~Media() = default;

protected:
	[[nodiscard]] QSize countCurrentSize(int newWidth) override;
	[[nodiscard]] Ui::Text::String createCaption(
		not_null<HistoryItem*> item) const;

	virtual void playAnimation(bool autoplay) {
	}

	[[nodiscard]] bool usesBubblePattern(const PaintContext &context) const;

	void fillImageShadow(
		QPainter &p,
		QRect rect,
		Ui::BubbleRounding rounding,
		const PaintContext &context) const;
	void fillImageOverlay(
		QPainter &p,
		QRect rect,
		std::optional<Ui::BubbleRounding> rounding, // nullopt if in WebPage.
		const PaintContext &context) const;
	void fillImageSpoiler(
		QPainter &p,
		not_null<MediaSpoiler*> spoiler,
		QRect rect,
		const PaintContext &context) const;
	void drawSpoilerTag(
		Painter &p,
		not_null<MediaSpoiler*> spoiler,
		std::unique_ptr<MediaSpoilerTag> &tag,
		QRect rthumb,
		const PaintContext &context,
		Fn<QImage()> generateBackground) const;
	void setupSpoilerTag(std::unique_ptr<MediaSpoilerTag> &tag) const;
	[[nodiscard]] ClickHandlerPtr spoilerTagLink(
		not_null<MediaSpoiler*> spoiler,
		std::unique_ptr<MediaSpoilerTag> &tag) const;
	void createSpoilerLink(not_null<MediaSpoiler*> spoiler);

	void repaint() const;

	const not_null<Element*> _parent;
	MediaInBubbleState _inBubbleState = MediaInBubbleState::None;
	Ui::BubbleRounding _bubbleRounding;

};

[[nodiscard]] Images::CornersMaskRef MediaRoundingMask(
	std::optional<Ui::BubbleRounding> rounding);

} // namespace HistoryView
