/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_element.h"
#include "ui/effects/animations.h"
#include "base/weak_ptr.h"

class HistoryMessage;
struct HistoryMessageEdited;
struct HistoryMessageForwarded;

namespace HistoryView {

class WebPage;

// Special type of Component for the channel actions log.
struct LogEntryOriginal
	: public RuntimeComponent<LogEntryOriginal, Element> {
	LogEntryOriginal();
	LogEntryOriginal(LogEntryOriginal &&other);
	LogEntryOriginal &operator=(LogEntryOriginal &&other);
	~LogEntryOriginal();

	std::unique_ptr<WebPage> page;
};

struct PsaTooltipState : public RuntimeComponent<PsaTooltipState, Element> {
	QString type;
	mutable ClickHandlerPtr link;
	mutable Ui::Animations::Simple buttonVisibleAnimation;
	mutable bool buttonVisible = true;
};

class Message : public Element, public base::has_weak_ptr {
public:
	Message(
		not_null<ElementDelegate*> delegate,
		not_null<HistoryMessage*> data,
		Element *replacing);
	~Message();

	void clickHandlerPressedChanged(
		const ClickHandlerPtr &handler,
		bool pressed) override;

	int marginTop() const override;
	int marginBottom() const override;
	void draw(
		Painter &p,
		QRect clip,
		TextSelection selection,
		crl::time ms) const override;
	PointState pointState(QPoint point) const override;
	TextState textState(
		QPoint point,
		StateRequest request) const override;
	void updatePressed(QPoint point) override;
	void drawInfo(
		Painter &p,
		int right,
		int bottom,
		int width,
		bool selected,
		InfoDisplayType type) const override;
	bool pointInTime(
		int right,
		int bottom,
		QPoint point,
		InfoDisplayType type) const override;
	TextForMimeData selectedText(TextSelection selection) const override;
	TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;

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
	int minWidthForMedia() const override;
	bool hasFastReply() const override;
	bool displayFastReply() const override;
	bool displayRightActionComments() const;
	std::optional<QSize> rightActionSize() const override;
	void drawRightAction(
		Painter &p,
		int left,
		int top,
		int outerWidth) const override;
	ClickHandlerPtr rightActionLink() const override;
	bool displayEditedBadge() const override;
	TimeId displayedEditDate() const override;
	HistoryMessageReply *displayedReply() const override;
	int infoWidth() const override;

	VerticalRepaintRange verticalRepaintRange() const override;

	void applyGroupAdminChanges(
		const base::flat_set<UserId> &changes) override;

protected:
	void refreshDataIdHook() override;

private:
	struct CommentsButton;

	not_null<HistoryMessage*> message() const;

	void initLogEntryOriginal();
	void initPsa();
	void refreshEditedBadge();
	void fromNameUpdated(int width) const;

	[[nodiscard]] bool showForwardsFromSender() const;
	[[nodiscard]] TextSelection skipTextSelection(
		TextSelection selection) const;
	[[nodiscard]] TextSelection unskipTextSelection(
		TextSelection selection) const;

	void toggleCommentsButtonRipple(bool pressed);

	void paintCommentsButton(Painter &p, QRect &g, bool selected) const;
	void paintFromName(Painter &p, QRect &trect, bool selected) const;
	void paintForwardedInfo(Painter &p, QRect &trect, bool selected) const;
	void paintReplyInfo(Painter &p, QRect &trect, bool selected) const;
	// this method draws "via @bot" if it is not painted in forwarded info or in from name
	void paintViaBotIdInfo(Painter &p, QRect &trect, bool selected) const;
	void paintText(Painter &p, QRect &trect, TextSelection selection) const;

	bool getStateCommentsButton(
		QPoint point,
		QRect &g,
		not_null<TextState*> outResult) const;
	bool getStateFromName(
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

	int resizeContentGetHeight(int newWidth);
	QSize performCountOptimalSize() override;
	QSize performCountCurrentSize(int newWidth) override;
	bool hasVisibleText() const override;

	[[nodiscard]] bool isPinnedContext() const;

	[[nodiscard]] bool displayFastShare() const;
	[[nodiscard]] bool displayGoToOriginal() const;
	[[nodiscard]] ClickHandlerPtr fastReplyLink() const;
	[[nodiscard]] const HistoryMessageEdited *displayedEditBadge() const;
	[[nodiscard]] HistoryMessageEdited *displayedEditBadge();
	[[nodiscard]] bool displayPinIcon() const;

	void initTime();
	[[nodiscard]] int timeLeft() const;
	[[nodiscard]] int plainMaxWidth() const;
	[[nodiscard]] int monospaceMaxWidth() const;

	WebPage *logEntryOriginal() const;

	[[nodiscard]] ClickHandlerPtr createGoToCommentsLink() const;
	[[nodiscard]] ClickHandlerPtr psaTooltipLink() const;
	void psaTooltipToggled(bool shown) const;

	void refreshRightBadge();

	mutable ClickHandlerPtr _rightActionLink;
	mutable ClickHandlerPtr _fastReplyLink;
	mutable std::unique_ptr<CommentsButton> _comments;

	Ui::Text::String _rightBadge;
	int _bubbleWidthLimit = 0;

};

} // namespace HistoryView
