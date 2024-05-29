/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_element.h"
#include "history/view/history_view_bottom_info.h"
#include "ui/effects/animations.h"

class HistoryItem;
struct HistoryMessageEdited;
struct HistoryMessageForwarded;
struct HistoryMessageReplyMarkup;

namespace Data {
struct ReactionId;
} // namespace Data

namespace Ui {
struct BubbleRounding;
} // namespace Ui

namespace HistoryView {

class ViewButton;
class WebPage;

namespace Reactions {
class InlineList;
} // namespace Reactions

// Special type of Component for the channel actions log.
struct LogEntryOriginal
	: public RuntimeComponent<LogEntryOriginal, Element> {
	LogEntryOriginal();
	LogEntryOriginal(LogEntryOriginal &&other);
	LogEntryOriginal &operator=(LogEntryOriginal &&other);
	~LogEntryOriginal();

	std::unique_ptr<WebPage> page;
};

struct Factcheck
: public RuntimeComponent<Factcheck, Element> {
	std::unique_ptr<WebPage> page;
	bool expanded = false;
};

struct PsaTooltipState : public RuntimeComponent<PsaTooltipState, Element> {
	QString type;
	mutable ClickHandlerPtr link;
	mutable Ui::Animations::Simple buttonVisibleAnimation;
	mutable bool buttonVisible = true;
};

struct BottomRippleMask {
	QImage image;
	int shift = 0;
};

class Message final : public Element {
public:
	Message(
		not_null<ElementDelegate*> delegate,
		not_null<HistoryItem*> data,
		Element *replacing);
	~Message();

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

	[[nodiscard]] const HistoryMessageEdited *displayedEditBadge() const;
	[[nodiscard]] HistoryMessageEdited *displayedEditBadge();

	[[nodiscard]] bool embedReactionsInBottomInfo() const;
	[[nodiscard]] bool embedReactionsInBubble() const;

	int marginTop() const override;
	int marginBottom() const override;
	void draw(Painter &p, const PaintContext &context) const override;
	PointState pointState(QPoint point) const override;
	TextState textState(
		QPoint point,
		StateRequest request) const override;
	void updatePressed(QPoint point) override;
	void drawInfo(
		Painter &p,
		const PaintContext &context,
		int right,
		int bottom,
		int width,
		InfoDisplayType type) const override;
	TextState bottomInfoTextState(
		int right,
		int bottom,
		QPoint point,
		InfoDisplayType type) const override;
	TextForMimeData selectedText(TextSelection selection) const override;
	SelectedQuote selectedQuote(TextSelection selection) const override;
	TextSelection selectionFromQuote(
		const SelectedQuote &quote) const override;
	TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;

	Reactions::ButtonParameters reactionButtonParameters(
		QPoint position,
		const TextState &reactionState) const override;
	int reactionsOptimalWidth() const override;

	bool hasHeavyPart() const override;
	void unloadHeavyPart() override;

	// hasFromPhoto() returns true even if we don't display the photo
	// but we need to skip a place at the left side for this photo
	bool hasFromPhoto() const override;
	bool displayFromPhoto() const override;
	bool hasFromName() const override;
	bool displayFromName() const override;
	bool displayForwardedFrom() const override;
	bool hasOutLayout() const override;
	bool drawBubble() const override;
	bool hasBubble() const override;
	TopicButton *displayedTopicButton() const override;
	bool unwrapped() const override;
	int minWidthForMedia() const override;
	bool hasFastReply() const override;
	bool displayFastReply() const override;
	bool displayRightActionComments() const;
	std::optional<QSize> rightActionSize() const override;
	void drawRightAction(
		Painter &p,
		const PaintContext &context,
		int left,
		int top,
		int outerWidth) const override;
	[[nodiscard]] ClickHandlerPtr rightActionLink(
		std::optional<QPoint> pressPoint) const override;
	[[nodiscard]] TimeId displayedEditDate() const override;
	[[nodiscard]] bool toggleSelectionByHandlerClick(
		const ClickHandlerPtr &handler) const override;
	[[nodiscard]] bool allowTextSelectionByHandler(
		const ClickHandlerPtr &handler) const override;
	[[nodiscard]] int infoWidth() const override;
	[[nodiscard]] int bottomInfoFirstLineWidth() const override;
	[[nodiscard]] bool bottomInfoIsWide() const override;
	[[nodiscard]] bool isSignedAuthorElided() const override;

	void itemDataChanged() override;

	VerticalRepaintRange verticalRepaintRange() const override;

	void applyGroupAdminChanges(
		const base::flat_set<UserId> &changes) override;

	void animateReaction(Ui::ReactionFlyAnimationArgs &&args) override;
	auto takeReactionAnimations()
	-> base::flat_map<
		Data::ReactionId,
		std::unique_ptr<Ui::ReactionFlyAnimation>> override;

	void animateEffect(Ui::ReactionFlyAnimationArgs &&args) override;
	auto takeEffectAnimation()
	-> std::unique_ptr<Ui::ReactionFlyAnimation> override;

	QRect effectIconGeometry() const override;
	QRect innerGeometry() const override;
	[[nodiscard]] BottomRippleMask bottomRippleMask(int buttonHeight) const;

protected:
	void refreshDataIdHook() override;

private:
	struct CommentsButton;
	struct FromNameStatus;
	struct RightAction;

	void initLogEntryOriginal();
	void initPsa();
	void fromNameUpdated(int width) const;

	[[nodiscard]] TextSelection skipTextSelection(
		TextSelection selection) const;
	[[nodiscard]] TextSelection unskipTextSelection(
		TextSelection selection) const;

	void toggleCommentsButtonRipple(bool pressed);
	void createCommentsButtonRipple();

	void toggleTopicButtonRipple(bool pressed);
	void createTopicButtonRipple();

	void toggleRightActionRipple(bool pressed);

	void toggleReplyRipple(bool pressed);

	void paintCommentsButton(
		Painter &p,
		QRect &g,
		const PaintContext &context) const;
	void paintFromName(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const;
	void paintTopicButton(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const;
	void paintForwardedInfo(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const;
	void paintReplyInfo(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const;
	// This method draws "via @bot" if it is not painted
	// in forwarded info or in from name.
	void paintViaBotIdInfo(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const;
	void paintText(
		Painter &p,
		QRect &trect,
		const PaintContext &context) const;

	bool getStateCommentsButton(
		QPoint point,
		QRect &g,
		not_null<TextState*> outResult) const;
	bool getStateFromName(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult) const;
	bool getStateTopicButton(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult) const;
	bool getStateForwardedInfo(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult,
		StateRequest request) const;
	bool getStateReplyInfo(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult) const;
	bool getStateViaBotIdInfo(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult) const;
	bool getStateText(
		QPoint point,
		QRect &trect,
		not_null<TextState*> outResult,
		StateRequest request) const;

	void updateMediaInBubbleState();
	QRect countGeometry() const;
	[[nodiscard]] Ui::BubbleRounding countMessageRounding() const;
	[[nodiscard]] Ui::BubbleRounding countBubbleRounding(
		Ui::BubbleRounding messageRounding) const;
	[[nodiscard]] Ui::BubbleRounding countBubbleRounding() const;

	int resizeContentGetHeight(int newWidth);
	QSize performCountOptimalSize() override;
	QSize performCountCurrentSize(int newWidth) override;
	bool hasVisibleText() const override;
	[[nodiscard]] int visibleTextLength() const;
	[[nodiscard]] int visibleMediaTextLength() const;
	[[nodiscard]] bool needInfoDisplay() const;
	[[nodiscard]] bool invertMedia() const;

	[[nodiscard]] bool isPinnedContext() const;

	[[nodiscard]] bool displayFastShare() const;
	[[nodiscard]] bool displayGoToOriginal() const;
	[[nodiscard]] ClickHandlerPtr fastReplyLink() const;
	[[nodiscard]] ClickHandlerPtr prepareRightActionLink() const;

	void ensureRightAction() const;
	void refreshTopicButton();
	void refreshInfoSkipBlock();
	[[nodiscard]] int monospaceMaxWidth() const;

	void validateInlineKeyboard(HistoryMessageReplyMarkup *markup);
	void updateViewButtonExistence();
	[[nodiscard]] int viewButtonHeight() const;

	[[nodiscard]] WebPage *logEntryOriginal() const;
	[[nodiscard]] WebPage *factcheckBlock() const;

	[[nodiscard]] ClickHandlerPtr createGoToCommentsLink() const;
	[[nodiscard]] ClickHandlerPtr psaTooltipLink() const;
	void psaTooltipToggled(bool shown) const;

	void setReactions(std::unique_ptr<Reactions::InlineList> list);
	void refreshRightBadge();
	void refreshReactions();
	void validateFromNameText(PeerData *from) const;

	mutable std::unique_ptr<RightAction> _rightAction;
	mutable ClickHandlerPtr _fastReplyLink;
	mutable std::unique_ptr<ViewButton> _viewButton;
	std::unique_ptr<Reactions::InlineList> _reactions;
	std::unique_ptr<TopicButton> _topicButton;
	mutable std::unique_ptr<CommentsButton> _comments;

	mutable Ui::Text::String _fromName;
	mutable std::unique_ptr<FromNameStatus> _fromNameStatus;
	Ui::Text::String _rightBadge;
	mutable int _fromNameVersion = 0;
	uint32 _bubbleWidthLimit : 29 = 0;
	uint32 _invertMedia : 1 = 0;
	uint32 _hideReply : 1 = 0;
	uint32 _rightBadgeHasBoosts : 1 = 0;

	BottomInfo _bottomInfo;

};

} // namespace HistoryView
