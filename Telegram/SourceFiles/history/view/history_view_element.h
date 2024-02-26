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
#include "base/weak_ptr.h"

class History;
class HistoryBlock;
class HistoryItem;
struct HistoryMessageReply;

namespace Data {
struct Reaction;
struct ReactionId;
} // namespace Data

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class PathShiftGradient;
struct BubblePattern;
struct ChatPaintContext;
class ChatStyle;
struct ReactionFlyAnimationArgs;
class ReactionFlyAnimation;
class RippleAnimation;
} // namespace Ui

namespace HistoryView::Reactions {
struct ButtonParameters;
class InlineList;
} // namespace HistoryView::Reactions

namespace HistoryView {

using PaintContext = Ui::ChatPaintContext;
enum class PointState : char;
enum class InfoDisplayType : char;
struct StateRequest;
struct TextState;
class Media;
class Reply;

enum class Context : char {
	History,
	Replies,
	Pinned,
	AdminLog,
	ContactPreview,
	SavedSublist,
	TTLViewer,
	ShortcutMessages,
};

enum class OnlyEmojiAndSpaces : char {
	Unknown,
	Yes,
	No,
};

class Element;
class ElementDelegate {
public:
	virtual Context elementContext() = 0;
	virtual bool elementUnderCursor(not_null<const Element*> view) = 0;
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
	virtual bool elementAnimationsPaused() = 0;
	virtual bool elementHideReply(not_null<const Element*> view) = 0;
	virtual bool elementShownUnread(not_null<const Element*> view) = 0;
	virtual void elementSendBotCommand(
		const QString &command,
		const FullMsgId &context) = 0;
	virtual void elementSearchInList(
		const QString &query,
		const FullMsgId &context) = 0;
	virtual void elementHandleViaClick(not_null<UserData*> bot) = 0;
	virtual bool elementIsChatWide() = 0;
	virtual not_null<Ui::PathShiftGradient*> elementPathShiftGradient() = 0;
	virtual void elementReplyTo(const FullReplyTo &to) = 0;
	virtual void elementStartInteraction(not_null<const Element*> view) = 0;
	virtual void elementStartPremium(
		not_null<const Element*> view,
		Element *replacing) = 0;
	virtual void elementCancelPremium(not_null<const Element*> view) = 0;
	virtual QString elementAuthorRank(not_null<const Element*> view) = 0;

	virtual ~ElementDelegate() {
	}

};

[[nodiscard]] std::unique_ptr<Ui::PathShiftGradient> MakePathShiftGradient(
	not_null<const Ui::ChatStyle*> st,
	Fn<void()> update);

class DefaultElementDelegate : public ElementDelegate {
public:
	bool elementUnderCursor(not_null<const Element*> view) override;
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
	bool elementHideReply(not_null<const Element*> view) override;
	bool elementShownUnread(not_null<const Element*> view) override;
	void elementSendBotCommand(
		const QString &command,
		const FullMsgId &context) override;
	void elementSearchInList(
		const QString &query,
		const FullMsgId &context) override;
	void elementHandleViaClick(not_null<UserData*> bot) override;
	bool elementIsChatWide() override;
	void elementReplyTo(const FullReplyTo &to) override;
	void elementStartInteraction(not_null<const Element*> view) override;
	void elementStartPremium(
		not_null<const Element*> view,
		Element *replacing) override;
	void elementCancelPremium(not_null<const Element*> view) override;
	QString elementAuthorRank(not_null<const Element*> view) override;

};

class SimpleElementDelegate : public DefaultElementDelegate {
public:
	SimpleElementDelegate(
		not_null<Window::SessionController*> controller,
		Fn<void()> update);
	~SimpleElementDelegate();

	bool elementAnimationsPaused() override;
	not_null<Ui::PathShiftGradient*> elementPathShiftGradient() override;

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

// Any HistoryView::Element can have this Component for
// displaying some text in layout of a service message above the message.
struct ServicePreMessage
	: public RuntimeComponent<ServicePreMessage, Element> {
	void init(TextWithEntities string);

	int resizeToWidth(int newWidth, bool chatWide);

	void paint(
		Painter &p,
		const PaintContext &context,
		QRect g,
		bool chatWide) const;

	Ui::Text::String text;
	int width = 0;
	int height = 0;

};

struct FakeBotAboutTop : public RuntimeComponent<FakeBotAboutTop, Element> {
	void init();

	Ui::Text::String text;
	int maxWidth = 0;
	int height = 0;
};

struct TopicButton {
	std::unique_ptr<Ui::RippleAnimation> ripple;
	ClickHandlerPtr link;
	Ui::Text::String name;
	QPoint lastPoint;
	int nameVersion = 0;
};

struct SelectedQuote {
	HistoryItem *item = nullptr;
	TextWithEntities text;
	int offset = 0;

	explicit operator bool() const {
		return item && !text.empty();
	}
	friend inline bool operator==(SelectedQuote, SelectedQuote) = default;
};

class Element
	: public Object
	, public RuntimeComposer<Element>
	, public ClickHandlerHost
	, public base::has_weak_ptr {
public:
	enum class Flag : uint16 {
		ServiceMessage           = 0x0001,
		NeedsResize              = 0x0002,
		AttachedToPrevious       = 0x0004,
		AttachedToNext           = 0x0008,
		BubbleAttachedToPrevious = 0x0010,
		BubbleAttachedToNext     = 0x0020,
		HiddenByGroup            = 0x0040,
		SpecialOnlyEmoji         = 0x0080,
		CustomEmojiRepainting    = 0x0100,
		ScheduledUntilOnline     = 0x0200,
		TopicRootReply           = 0x0400,
		MediaOverriden           = 0x0800,
		HeavyCustomEmoji         = 0x1000,
	};
	using Flags = base::flags<Flag>;
	friend inline constexpr auto is_flag_type(Flag) { return true; }

	Element(
		not_null<ElementDelegate*> delegate,
		not_null<HistoryItem*> data,
		Element *replacing,
		Flag serviceFlag);

	[[nodiscard]] not_null<ElementDelegate*> delegate() const;
	[[nodiscard]] not_null<HistoryItem*> data() const;
	[[nodiscard]] not_null<History*> history() const;
	[[nodiscard]] Media *media() const;
	[[nodiscard]] Context context() const;
	void refreshDataId();

	[[nodiscard]] uint8 colorIndex() const;
	[[nodiscard]] uint8 contentColorIndex() const;
	[[nodiscard]] QDateTime dateTime() const;

	[[nodiscard]] int y() const;
	void setY(int y);

	[[nodiscard]] virtual int marginTop() const = 0;
	[[nodiscard]] virtual int marginBottom() const = 0;

	void setPendingResize();
	[[nodiscard]] bool pendingResize() const;
	[[nodiscard]] bool isUnderCursor() const;

	[[nodiscard]] bool isLastAndSelfMessage() const;

	[[nodiscard]] bool isAttachedToPrevious() const;
	[[nodiscard]] bool isAttachedToNext() const;
	[[nodiscard]] bool isBubbleAttachedToPrevious() const;
	[[nodiscard]] bool isBubbleAttachedToNext() const;

	[[nodiscard]] bool isTopicRootReply() const;

	[[nodiscard]] int skipBlockWidth() const;
	[[nodiscard]] int skipBlockHeight() const;
	[[nodiscard]] virtual int infoWidth() const;
	[[nodiscard]] virtual int bottomInfoFirstLineWidth() const;
	[[nodiscard]] virtual bool bottomInfoIsWide() const;

	[[nodiscard]] bool isHiddenByGroup() const;
	[[nodiscard]] virtual bool isHidden() const;

	[[nodiscard]] bool isIsolatedEmoji() const {
		return (_flags & Flag::SpecialOnlyEmoji)
			&& _text.isIsolatedEmoji();
	}
	[[nodiscard]] bool isOnlyCustomEmoji() const {
		return (_flags & Flag::SpecialOnlyEmoji)
			&& _text.isOnlyCustomEmoji();
	}

	[[nodiscard]] Ui::Text::IsolatedEmoji isolatedEmoji() const;
	[[nodiscard]] Ui::Text::OnlyCustomEmoji onlyCustomEmoji() const;

	[[nodiscard]] OnlyEmojiAndSpaces isOnlyEmojiAndSpaces() const;

	// For blocks context this should be called only from recountAttachToPreviousInBlocks().
	void setAttachToPrevious(bool attachToNext, Element *previous = nullptr);

	// For blocks context this should be called only from recountAttachToPreviousInBlocks()
	// of the next item or when the next item is removed through nextInBlocksRemoved() call.
	void setAttachToNext(bool attachToNext, Element *next = nullptr);

	// For blocks context this should be called only from recountDisplayDate().
	void setDisplayDate(bool displayDate);
	void setServicePreMessage(TextWithEntities text);

	bool computeIsAttachToPrevious(not_null<Element*> previous);

	void createUnreadBar(rpl::producer<QString> text);
	void destroyUnreadBar();

	[[nodiscard]] int displayedDateHeight() const;
	[[nodiscard]] bool displayDate() const;
	[[nodiscard]] bool isInOneDayWithPrevious() const;

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
	virtual TextForMimeData selectedText(TextSelection selection) const = 0;
	virtual SelectedQuote selectedQuote(
		TextSelection selection) const = 0;
	virtual TextSelection selectionFromQuote(
		const SelectedQuote &quote) const = 0;
	[[nodiscard]] virtual TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const;

	[[nodiscard]] static SelectedQuote FindSelectedQuote(
		const Ui::Text::String &text,
		TextSelection selection,
		not_null<HistoryItem*> item);
	[[nodiscard]] static TextSelection FindSelectionFromQuote(
		const Ui::Text::String &text,
		const SelectedQuote &quote);

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
	[[nodiscard]] bool displayReply() const;
	[[nodiscard]] virtual bool displayFromName() const;
	[[nodiscard]] virtual TopicButton *displayedTopicButton() const;
	[[nodiscard]] virtual bool displayForwardedFrom() const;
	[[nodiscard]] virtual bool hasOutLayout() const;
	[[nodiscard]] bool hasRightLayout() const;
	[[nodiscard]] virtual bool drawBubble() const;
	[[nodiscard]] virtual bool hasBubble() const;
	[[nodiscard]] virtual bool unwrapped() const;
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
	[[nodiscard]] virtual ClickHandlerPtr rightActionLink(
		std::optional<QPoint> pressPoint) const;
	[[nodiscard]] virtual TimeId displayedEditDate() const;
	[[nodiscard]] virtual bool hasVisibleText() const;
	virtual void applyGroupAdminChanges(
		const base::flat_set<UserId> &changes) {
	}
	[[nodiscard]] virtual bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &handler) const;
	[[nodiscard]] virtual bool allowTextSelectionByHandler(
		const ClickHandlerPtr &handler) const;

	[[nodiscard]] bool usesBubblePattern(const PaintContext &context) const;

	struct VerticalRepaintRange {
		int top = 0;
		int height = 0;
	};
	[[nodiscard]] virtual VerticalRepaintRange verticalRepaintRange() const;

	[[nodiscard]] virtual bool isSignedAuthorElided() const;

	virtual void itemDataChanged();
	void itemTextUpdated();

	[[nodiscard]] virtual bool hasHeavyPart() const;
	virtual void unloadHeavyPart();
	void checkHeavyPart();

	void paintCustomHighlight(
		Painter &p,
		const PaintContext &context,
		int y,
		int height,
		not_null<const HistoryItem*> item) const;

	// Legacy blocks structure.
	[[nodiscard]] HistoryBlock *block();
	[[nodiscard]] const HistoryBlock *block() const;
	void attachToBlock(not_null<HistoryBlock*> block, int index);
	void removeFromBlock();
	void refreshInBlock();
	void setIndexInBlock(int index);
	[[nodiscard]] int indexInBlock() const;
	[[nodiscard]] Element *previousInBlocks() const;
	[[nodiscard]] Element *previousDisplayedInBlocks() const;
	[[nodiscard]] Element *nextInBlocks() const;
	[[nodiscard]] Element *nextDisplayedInBlocks() const;
	void previousInBlocksChanged();
	void nextInBlocksRemoved();

	[[nodiscard]] virtual QRect innerGeometry() const = 0;

	void customEmojiRepaint();
	void prepareCustomEmojiPaint(
		Painter &p,
		const PaintContext &context,
		const Ui::Text::String &text) const;
	void prepareCustomEmojiPaint(
		Painter &p,
		const PaintContext &context,
		const Reactions::InlineList &reactions) const;
	void clearCustomEmojiRepaint() const;
	void hideSpoilers();
	void repaint() const;

	[[nodiscard]] ClickHandlerPtr fromPhotoLink() const {
		return fromLink();
	}

	[[nodiscard]] bool markSponsoredViewed(int shownFromTop) const;

	virtual void animateReaction(Ui::ReactionFlyAnimationArgs &&args);
	void animateUnreadReactions();
	[[nodiscard]] virtual auto takeReactionAnimations()
	-> base::flat_map<
		Data::ReactionId,
		std::unique_ptr<Ui::ReactionFlyAnimation>>;

	void overrideMedia(std::unique_ptr<Media> media);

	virtual bool consumeHorizontalScroll(QPoint position, int delta) {
		return false;
	}

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
	void paintHighlight(
		Painter &p,
		const PaintContext &context,
		int geometryHeight) const;

	[[nodiscard]] ClickHandlerPtr fromLink() const;

	virtual void refreshDataIdHook();

	[[nodiscard]] const Ui::Text::String &text() const;
	[[nodiscard]] int textHeightFor(int textWidth);
	void validateText();
	void validateTextSkipBlock(bool has, int width, int height);

	void clearSpecialOnlyEmoji();
	void checkSpecialOnlyEmoji();

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

	[[nodiscard]] bool countIsTopicRootReply() const;

	QSize countOptimalSize() final override;
	QSize countCurrentSize(int newWidth) final override;

	virtual QSize performCountOptimalSize() = 0;
	virtual QSize performCountCurrentSize(int newWidth) = 0;

	void refreshMedia(Element *replacing);
	void setTextWithLinks(
		const TextWithEntities &text,
		const std::vector<ClickHandlerPtr> &links = {});

	struct TextWithLinks {
		TextWithEntities text;
		std::vector<ClickHandlerPtr> links;
	};
	[[nodiscard]] TextWithLinks contextDependentServiceText();

	const not_null<ElementDelegate*> _delegate;
	const not_null<HistoryItem*> _data;
	HistoryBlock *_block = nullptr;
	std::unique_ptr<Media> _media;
	mutable ClickHandlerPtr _fromLink;
	const QDateTime _dateTime;

	mutable Ui::Text::String _text;
	mutable int _textWidth = -1;
	mutable int _textHeight = 0;

	int _y = 0;
	int _indexInBlock = -1;

	mutable Flags _flags = Flag(0);
	Context _context = Context();

};

constexpr auto size = sizeof(Element);

} // namespace HistoryView
