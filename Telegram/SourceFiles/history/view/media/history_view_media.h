/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"
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
class SinglePlayer;
struct ColorReplacements;
} // namespace Lottie

namespace HistoryView {

enum class PointState : char;
enum class CursorState : char;
enum class InfoDisplayType : char;
struct TextState;
struct StateRequest;
class Element;

enum class MediaInBubbleState {
	None,
	Top,
	Middle,
	Bottom,
};

struct BubbleSelectionInterval {
	int top = 0;
	int height = 0;
};

[[nodiscard]] QString DocumentTimestampLinkBase(
	not_null<DocumentData*> document,
	FullMsgId context);
[[nodiscard]] TextWithEntities AddTimestampLinks(
	TextWithEntities text,
	TimeId duration,
	const QString &base);

class Media : public Object {
public:
	explicit Media(not_null<Element*> parent) : _parent(parent) {
	}

	[[nodiscard]] not_null<History*> history() const;

	[[nodiscard]] virtual TextForMimeData selectedText(
			TextSelection selection) const {
		return TextForMimeData();
	}

	[[nodiscard]] virtual bool isDisplayed() const;
	virtual void updateNeedBubbleState() {
	}
	[[nodiscard]] virtual bool hasTextForCopy() const {
		return false;
	}
	[[nodiscard]] virtual bool hideMessageText() const {
		return true;
	}
	[[nodiscard]] virtual bool allowsFastShare() const {
		return false;
	}
	virtual void refreshParentId(not_null<HistoryItem*> realParent) {
	}
	virtual void drawHighlight(Painter &p, int top) const {
	}
	virtual void draw(
		Painter &p,
		const QRect &r,
		TextSelection selection,
		crl::time ms) const = 0;
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

	// if we press and drag on this media should we drag the item
	[[nodiscard]] virtual bool dragItem() const {
		return false;
	}

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
	-> std::vector<BubbleSelectionInterval> {
		return {};
	}

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
	virtual std::unique_ptr<Lottie::SinglePlayer> stickerTakeLottie(
		not_null<DocumentData*> data,
		const Lottie::ColorReplacements *replacements);
	virtual void checkAnimation() {
	}

	[[nodiscard]] virtual QSize sizeForGroupingOptimal(int maxWidth) const {
		Unexpected("Grouping method call.");
	}
	[[nodiscard]] virtual QSize sizeForGrouping(int width) const {
		Unexpected("Grouping method call.");
	}
	virtual void drawGrouped(
			Painter &p,
			const QRect &clip,
			TextSelection selection,
			crl::time ms,
			const QRect &geometry,
			RectParts sides,
			RectParts corners,
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

	[[nodiscard]] virtual bool animating() const {
		return false;
	}

	[[nodiscard]] virtual TextWithEntities getCaption() const {
		return TextWithEntities();
	}
	[[nodiscard]] virtual bool needsBubble() const = 0;
	[[nodiscard]] virtual bool customInfoLayout() const = 0;
	[[nodiscard]] virtual QMargins bubbleMargins() const {
		return QMargins();
	}
	[[nodiscard]] virtual bool hideForwardedFrom() const {
		return false;
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

	[[nodiscard]] virtual bool hidesForwardedInfo() const {
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

	virtual ~Media() = default;

protected:
	[[nodiscard]] QSize countCurrentSize(int newWidth) override;
	[[nodiscard]] Ui::Text::String createCaption(
		not_null<HistoryItem*> item,
		TimeId timestampLinksDuration = 0,
		const QString &timestampLinkBase = QString()) const;

	virtual void playAnimation(bool autoplay) {
	}

	const not_null<Element*> _parent;
	MediaInBubbleState _inBubbleState = MediaInBubbleState::None;

};

} // namespace HistoryView
