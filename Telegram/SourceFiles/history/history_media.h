/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"

struct HistoryMessageEdited;
struct TextSelection;

namespace base {
template <typename Enum>
class enum_mask;
} // namespace base

namespace Storage {
enum class SharedMediaType : char;
using SharedMediaTypesMask = base::enum_mask<SharedMediaType>;
} // namespace Storage

namespace HistoryView {
enum class PointState : char;
enum class CursorState : char;
enum class InfoDisplayType : char;
struct TextState;
struct StateRequest;
} // namespace HistoryView

enum class MediaInBubbleState {
	None,
	Top,
	Middle,
	Bottom,
};

enum HistoryMediaType : char {
	MediaTypePhoto,
	MediaTypeVideo,
	MediaTypeContact,
	MediaTypeCall,
	MediaTypeFile,
	MediaTypeGif,
	MediaTypeSticker,
	MediaTypeLocation,
	MediaTypeWebPage,
	MediaTypeMusicFile,
	MediaTypeVoiceFile,
	MediaTypeGame,
	MediaTypeInvoice,
	MediaTypeGrouped,

	MediaTypeCount
};

class HistoryMedia : public HistoryView::Object {
public:
	using Element = HistoryView::Element;
	using PointState = HistoryView::PointState;
	using TextState = HistoryView::TextState;
	using StateRequest = HistoryView::StateRequest;

	HistoryMedia(not_null<Element*> parent) : _parent(parent) {
	}

	virtual HistoryMediaType type() const = 0;

	virtual TextWithEntities selectedText(TextSelection selection) const {
		return TextWithEntities();
	}

	virtual bool isDisplayed() const;
	virtual void updateNeedBubbleState() {
	}
	virtual bool hasTextForCopy() const {
		return false;
	}
	virtual bool hideMessageText() const {
		return true;
	}
	virtual bool allowsFastShare() const {
		return false;
	}
	virtual void refreshParentId(not_null<HistoryItem*> realParent) {
	}
	virtual void draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const = 0;
	virtual PointState pointState(QPoint point) const;
	virtual TextState textState(QPoint point, StateRequest request) const = 0;
	virtual void updatePressed(QPoint point) {
	}

	virtual Storage::SharedMediaTypesMask sharedMediaTypes() const;

	// if we are in selecting items mode perhaps we want to
	// toggle selection instead of activating the pressed link
	virtual bool toggleSelectionByHandlerClick(
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

	// if we press and drag this link should we drag the item
	virtual bool dragItemByHandler(const ClickHandlerPtr &p) const = 0;

	virtual void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	}
	virtual void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	}

	virtual bool uploading() const {
		return false;
	}

	virtual PhotoData *getPhoto() const {
		return nullptr;
	}
	virtual DocumentData *getDocument() const {
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

	virtual QSize sizeForGrouping() const {
		Unexpected("Grouping method call.");
	}
	virtual void drawGrouped(
			Painter &p,
			const QRect &clip,
			TextSelection selection,
			TimeMs ms,
			const QRect &geometry,
			RectParts corners,
			not_null<uint64*> cacheKey,
			not_null<QPixmap*> cache) const {
		Unexpected("Grouping method call.");
	}
	virtual TextState getStateGrouped(
		const QRect &geometry,
		QPoint point,
		StateRequest request) const;

	virtual bool animating() const {
		return false;
	}

	virtual TextWithEntities getCaption() const {
		return TextWithEntities();
	}
	virtual bool needsBubble() const = 0;
	virtual bool customInfoLayout() const = 0;
	virtual QMargins bubbleMargins() const {
		return QMargins();
	}
	virtual bool hideForwardedFrom() const {
		return false;
	}

	virtual bool overrideEditedDate() const {
		return false;
	}
	virtual HistoryMessageEdited *displayedEditBadge() const {
		Unexpected("displayedEditBadge() on non-grouped media.");
	}

	// An attach media in a web page can provide an
	// additional text to be displayed below the attach.
	// For example duration / progress for video messages.
	virtual QString additionalInfoString() const {
		return QString();
	}

	void setInBubbleState(MediaInBubbleState state) {
		_inBubbleState = state;
	}
	MediaInBubbleState inBubbleState() const {
		return _inBubbleState;
	}
	bool isBubbleTop() const {
		return (_inBubbleState == MediaInBubbleState::Top) || (_inBubbleState == MediaInBubbleState::None);
	}
	bool isBubbleBottom() const {
		return (_inBubbleState == MediaInBubbleState::Bottom) || (_inBubbleState == MediaInBubbleState::None);
	}
	virtual bool skipBubbleTail() const {
		return false;
	}

	// Sometimes click on media in message is overloaded by the message:
	// (for example it can open a link or a game instead of opening media)
	// But the overloading click handler should be used only when media
	// is already loaded (not a photo or GIF waiting for load with auto
	// load being disabled - in such case media should handle the click).
	virtual bool isReadyForOpen() const {
		return true;
	}

	// Should be called only by Data::Session.
	virtual void updateSharedContactUserId(UserId userId) {
	}

	virtual ~HistoryMedia() = default;

protected:
	using CursorState = HistoryView::CursorState;
	using InfoDisplayType = HistoryView::InfoDisplayType;

	QSize countCurrentSize(int newWidth) override;
	Text createCaption(not_null<HistoryItem*> item) const;

	virtual void playAnimation(bool autoplay) {
	}

	not_null<Element*> _parent;
	MediaInBubbleState _inBubbleState = MediaInBubbleState::None;

};
