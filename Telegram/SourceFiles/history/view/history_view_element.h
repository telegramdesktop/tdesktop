/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"
#include "base/runtime_composer.h"
#include "base/flags.h"

class History;
class HistoryBlock;
class HistoryItem;
class HistoryMessage;
class HistoryService;
struct HistoryMessageReply;

namespace Data {
struct Reaction;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class PathShiftGradient;
struct BubblePattern;
struct ChatPaintContext;
class ChatStyle;
} // namespace Ui

namespace Lottie {
class Icon;
} // namespace Lottie

namespace HistoryView {

enum class PointState : char;
enum class InfoDisplayType : char;
struct StateRequest;
struct TextState;
class Media;

using PaintContext = Ui::ChatPaintContext;

namespace Reactions {
struct ButtonParameters;
class Animation;
} // namespace Reactions

enum class Context : char {
	History,
	Replies,
	Pinned,
	AdminLog,
	ContactPreview
};

class Element;
class ElementDelegate {
public:
	virtual Context elementContext() = 0;
	virtual std::unique_ptr<Element> elementCreate(
		not_null<HistoryMessage*> message,
		Element *replacing = nullptr) = 0;
	virtual std::unique_ptr<Element> elementCreate(
		not_null<HistoryService*> message,
		Element *replacing = nullptr) = 0;
	virtual bool elementUnderCursor(not_null<const Element*> view) = 0;
	virtual crl::time elementHighlightTime(
		not_null<const HistoryItem*> item) = 0;
	virtual bool elementInSelectionMode() = 0;
	virtual bool elementIntersectsRange(
		not_null<const Element*> view,
		int from,
		int till) = 0;
	virtual void elementStartStickerLoop(not_null<const Element*> view) = 0;
	virtual void elementShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) = 0;
	virtual void elementOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) = 0;
	virtual void elementOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView = false) = 0;
	virtual void elementCancelUpload(const FullMsgId &context) = 0;
	virtual void elementShowTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) = 0;
	virtual bool elementIsGifPaused() = 0;
	virtual bool elementHideReply(not_null<const Element*> view) = 0;
	virtual bool elementShownUnread(not_null<const Element*> view) = 0;
	virtual void elementSendBotCommand(
		const QString &command,
		const FullMsgId &context) = 0;
	virtual void elementHandleViaClick(not_null<UserData*> bot) = 0;
	virtual bool elementIsChatWide() = 0;
	virtual not_null<Ui::PathShiftGradient*> elementPathShiftGradient() = 0;
	virtual void elementReplyTo(const FullMsgId &to) = 0;
	virtual void elementStartInteraction(not_null<const Element*> view) = 0;
	virtual void elementShowSpoilerAnimation() = 0;

	virtual ~ElementDelegate() {
	}

};

[[nodiscard]] std::unique_ptr<Ui::PathShiftGradient> MakePathShiftGradient(
	not_null<const Ui::ChatStyle*> st,
	Fn<void()> update);

class SimpleElementDelegate : public ElementDelegate {
public:
	SimpleElementDelegate(
		not_null<Window::SessionController*> controller,
		Fn<void()> update);
	~SimpleElementDelegate();

	std::unique_ptr<Element> elementCreate(
		not_null<HistoryMessage*> message,
		Element *replacing = nullptr) override;
	std::unique_ptr<Element> elementCreate(
		not_null<HistoryService*> message,
		Element *replacing = nullptr) override;
	bool elementUnderCursor(not_null<const Element*> view) override;
	crl::time elementHighlightTime(
		not_null<const HistoryItem*> item) override;
	bool elementInSelectionMode() override;
	bool elementIntersectsRange(
		not_null<const Element*> view,
		int from,
		int till) override;
	void elementStartStickerLoop(not_null<const Element*> view) override;
	void elementShowPollResults(
		not_null<PollData*> poll,
		FullMsgId context) override;
	void elementOpenPhoto(
		not_null<PhotoData*> photo,
		FullMsgId context) override;
	void elementOpenDocument(
		not_null<DocumentData*> document,
		FullMsgId context,
		bool showInMediaView = false) override;
	void elementCancelUpload(const FullMsgId &context) override;
	void elementShowTooltip(
		const TextWithEntities &text,
		Fn<void()> hiddenCallback) override;
	bool elementIsGifPaused() override;
	bool elementHideReply(not_null<const Element*> view) override;
	bool elementShownUnread(not_null<const Element*> view) override;
	void elementSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void elementHandleViaClick(not_null<UserData*> bot) override;
	bool elementIsChatWide() override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;
	void elementReplyTo(const FullMsgId &to) override;
	void elementStartInteraction(not_null<const Element*> view) override;
	void elementShowSpoilerAnimation() override;

protected:
	[[nodiscard]] not_null<Window::SessionController*> controller() const {
		return _controller;
	}

private:
	const not_null<Window::SessionController*> _controller;
	const std::unique_ptr<Ui::PathShiftGradient> _pathGradient;

};

TextSelection UnshiftItemSelection(
	TextSelection selection,
	uint16 byLength);
TextSelection ShiftItemSelection(
	TextSelection selection,
	uint16 byLength);
TextSelection UnshiftItemSelection(
	TextSelection selection,
	const Ui::Text::String &byText);
TextSelection ShiftItemSelection(
	TextSelection selection,
	const Ui::Text::String &byText);

QString DateTooltipText(not_null<Element*> view);

// Any HistoryView::Element can have this Component for
// displaying the unread messages bar above the message.
struct UnreadBar : public RuntimeComponent<UnreadBar, Element> {
	void init(const QString &string);

	static int height();
	static int marginTop();

	void paint(
		Painter &p,
		const PaintContext &context,
		int y,
		int w,
		bool chatWide) const;

	QString text;
	int width = 0;
	rpl::lifetime lifetime;

};

// Any HistoryView::Element can have this Component for
// displaying the day mark above the message.
struct DateBadge : public RuntimeComponent<DateBadge, Element> {
	void init(const QString &date);

	int height() const;
	void paint(
		Painter &p,
		not_null<const Ui::ChatStyle*> st,
		int y,
		int w,
		bool chatWide) const;

	QString text;
	int width = 0;

};

struct ReactionAnimationArgs {
	QString emoji;
	std::shared_ptr<Lottie::Icon> flyIcon;
	QRect flyFrom;

	[[nodiscard]] ReactionAnimationArgs translated(QPoint point) const;
};

class Element
	: public Object
	, public RuntimeComposer<Element>
	, public ClickHandlerHost {
public:
	Element(
		not_null<ElementDelegate*> delegate,
		not_null<HistoryItem*> data,
		Element *replacing);

	enum class Flag : uchar {
		NeedsResize        = 0x01,
		AttachedToPrevious = 0x02,
		AttachedToNext     = 0x04,
		HiddenByGroup      = 0x08,
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	not_null<ElementDelegate*> delegate() const;
	not_null<HistoryItem*> data() const;
	not_null<History*> history() const;
	Media *media() const;
	Context context() const;
	void refreshDataId();

	QDateTime dateTime() const;

	int y() const;
	void setY(int y);

	virtual int marginTop() const = 0;
	virtual int marginBottom() const = 0;

	void setPendingResize();
	bool pendingResize() const;
	bool isUnderCursor() const;

	bool isLastAndSelfMessage() const;

	bool isAttachedToPrevious() const;
	bool isAttachedToNext() const;

	int skipBlockWidth() const;
	int skipBlockHeight() const;
	virtual int infoWidth() const;
	virtual int bottomInfoFirstLineWidth() const;
	virtual bool bottomInfoIsWide() const;

	bool isHiddenByGroup() const;
	virtual bool isHidden() const;

	// For blocks context this should be called only from recountAttachToPreviousInBlocks().
	void setAttachToPrevious(bool attachToNext);

	// For blocks context this should be called only from recountAttachToPreviousInBlocks()
	// of the next item or when the next item is removed through nextInBlocksRemoved() call.
	void setAttachToNext(bool attachToNext);

	// For blocks context this should be called only from recountDisplayDate().
	void setDisplayDate(bool displayDate);

	bool computeIsAttachToPrevious(not_null<Element*> previous);

	void createUnreadBar(rpl::producer<QString> text);
	void destroyUnreadBar();

	int displayedDateHeight() const;
	bool displayDate() const;
	bool isInOneDayWithPrevious() const;

	virtual void draw(Painter &p, const PaintContext &context) const = 0;
	[[nodiscard]] virtual PointState pointState(QPoint point) const = 0;
	[[nodiscard]] virtual TextState textState(
		QPoint point,
		StateRequest request) const = 0;
	virtual void updatePressed(QPoint point) = 0;
	virtual void drawInfo(
		Painter &p,
		const PaintContext &context,
		int right,
		int bottom,
		int width,
		InfoDisplayType type) const;
	virtual TextState bottomInfoTextState(
		int right,
		int bottom,
		QPoint point,
		InfoDisplayType type) const;
	virtual TextForMimeData selectedText(
		TextSelection selection) const = 0;
	[[nodiscard]] virtual TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const;

	[[nodiscard]] virtual auto reactionButtonParameters(
		QPoint position,
		const TextState &reactionState) const -> Reactions::ButtonParameters;
	[[nodiscard]] virtual int reactionsOptimalWidth() const;

	// ClickHandlerHost interface.
	void clickHandlerActiveChanged(
		const ClickHandlerPtr &handler,
		bool active) override;
	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

	// hasFromPhoto() returns true even if we don't display the photo
	// but we need to skip a place at the left side for this photo
	[[nodiscard]] virtual bool hasFromPhoto() const;
	[[nodiscard]] virtual bool displayFromPhoto() const;
	[[nodiscard]] virtual bool hasFromName() const;
	[[nodiscard]] virtual bool displayFromName() const;
	[[nodiscard]] virtual bool displayForwardedFrom() const;
	[[nodiscard]] virtual bool hasOutLayout() const;
	[[nodiscard]] virtual bool drawBubble() const;
	[[nodiscard]] virtual bool hasBubble() const;
	[[nodiscard]] virtual int minWidthForMedia() const {
		return 0;
	}
	[[nodiscard]] virtual bool hasFastReply() const;
	[[nodiscard]] virtual bool displayFastReply() const;
	[[nodiscard]] virtual std::optional<QSize> rightActionSize() const;
	virtual void drawRightAction(
		Painter &p,
		const PaintContext &context,
		int left,
		int top,
		int outerWidth) const;
	[[nodiscard]] virtual ClickHandlerPtr rightActionLink() const;
	[[nodiscard]] virtual bool displayEditedBadge() const;
	[[nodiscard]] virtual TimeId displayedEditDate() const;
	[[nodiscard]] virtual bool hasVisibleText() const;
	[[nodiscard]] virtual HistoryMessageReply *displayedReply() const;
	virtual void applyGroupAdminChanges(
		const base::flat_set<UserId> &changes) {
	}
	[[nodiscard]] virtual bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &handler) const;

	struct VerticalRepaintRange {
		int top = 0;
		int height = 0;
	};
	[[nodiscard]] virtual VerticalRepaintRange verticalRepaintRange() const;

	[[nodiscard]] virtual bool isSignedAuthorElided() const;

	virtual void itemDataChanged();

	virtual bool hasHeavyPart() const;
	virtual void unloadHeavyPart();
	void checkHeavyPart();

	void paintCustomHighlight(
		Painter &p,
		const PaintContext &context,
		int y,
		int height,
		not_null<const HistoryItem*> item) const;
	float64 highlightOpacity(not_null<const HistoryItem*> item) const;

	// Legacy blocks structure.
	HistoryBlock *block();
	const HistoryBlock *block() const;
	void attachToBlock(not_null<HistoryBlock*> block, int index);
	void removeFromBlock();
	void refreshInBlock();
	void setIndexInBlock(int index);
	int indexInBlock() const;
	Element *previousInBlocks() const;
	Element *previousDisplayedInBlocks() const;
	Element *nextInBlocks() const;
	Element *nextDisplayedInBlocks() const;
	void previousInBlocksChanged();
	void nextInBlocksRemoved();

	[[nodiscard]] ClickHandlerPtr fromPhotoLink() const {
		return fromLink();
	}

	[[nodiscard]] bool markSponsoredViewed(int shownFromTop) const;

	virtual void animateReaction(ReactionAnimationArgs &&args);
	void animateUnreadReactions();
	[[nodiscard]] virtual auto takeReactionAnimations()
		-> base::flat_map<QString, std::unique_ptr<Reactions::Animation>>;

	virtual ~Element();

	static void Hovered(Element *view);
	[[nodiscard]] static Element *Hovered();
	static void Pressed(Element *view);
	[[nodiscard]] static Element *Pressed();
	static void HoveredLink(Element *view);
	[[nodiscard]] static Element *HoveredLink();
	static void PressedLink(Element *view);
	[[nodiscard]] static Element *PressedLink();
	static void Moused(Element *view);
	[[nodiscard]] static Element *Moused();
	static void ClearGlobal();

protected:
	void repaint() const;

	void paintHighlight(
		Painter &p,
		const PaintContext &context,
		int geometryHeight) const;

	[[nodiscard]] ClickHandlerPtr fromLink() const;

	virtual void refreshDataIdHook();

private:
	// This should be called only from previousInBlocksChanged()
	// to add required bits to the Composer mask
	// after that always use Has<DateBadge>().
	void recountDisplayDateInBlocks();

	// This should be called only from previousInBlocksChanged() or when
	// DateBadge or UnreadBar bit is changed in the Composer mask
	// then the result should be cached in a client side flag
	// HistoryView::Element::Flag::AttachedToPrevious.
	void recountAttachToPreviousInBlocks();

	QSize countOptimalSize() final override;
	QSize countCurrentSize(int newWidth) final override;

	virtual QSize performCountOptimalSize() = 0;
	virtual QSize performCountCurrentSize(int newWidth) = 0;

	void refreshMedia(Element *replacing);

	const not_null<ElementDelegate*> _delegate;
	const not_null<HistoryItem*> _data;
	std::unique_ptr<Media> _media;
	mutable ClickHandlerPtr _fromLink;
	bool _isScheduledUntilOnline = false;
	const QDateTime _dateTime;

	int _y = 0;
	Context _context = Context();

	Flags _flags = Flag::NeedsResize;

	HistoryBlock *_block = nullptr;
	int _indexInBlock = -1;

};

} // namespace HistoryView
