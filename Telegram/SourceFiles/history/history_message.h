/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "history/history_item.h"

struct HistoryMessageEdited;

base::lambda<void(ChannelData*, MsgId)> HistoryDependentItemCallback(
	const FullMsgId &msgId);
MTPDmessage::Flags NewMessageFlags(not_null<PeerData*> peer);
QString GetErrorTextForForward(
	not_null<PeerData*> peer,
	const HistoryItemsList &items);
void FastShareMessage(not_null<HistoryItem*> item);

class HistoryMessage
	: public HistoryItem
	, private HistoryItemInstantiated<HistoryMessage> {
public:
	static not_null<HistoryMessage*> create(
			not_null<History*> history,
			const MTPDmessage &msg) {
		return _create(history, msg);
	}
	static not_null<HistoryMessage*> create(
			not_null<History*> history,
			const MTPDmessageService &msg) {
		return _create(history, msg);
	}
	static not_null<HistoryMessage*> create(
			not_null<History*> history,
			MsgId msgId,
			MTPDmessage::Flags flags,
			QDateTime date,
			UserId from,
			const QString &postAuthor,
			not_null<HistoryMessage*> fwd) {
		return _create(history, msgId, flags, date, from, postAuthor, fwd);
	}
	static not_null<HistoryMessage*> create(
			not_null<History*> history,
			MsgId msgId,
			MTPDmessage::Flags flags,
			MsgId replyTo,
			UserId viaBotId,
			QDateTime date,
			UserId from,
			const QString &postAuthor,
			const TextWithEntities &textWithEntities) {
		return _create(
			history,
			msgId,
			flags,
			replyTo,
			viaBotId,
			date,
			from,
			postAuthor,
			textWithEntities);
	}
	static not_null<HistoryMessage*> create(
			not_null<History*> history,
			MsgId msgId,
			MTPDmessage::Flags flags,
			MsgId replyTo,
			UserId viaBotId,
			QDateTime date,
			UserId from,
			const QString &postAuthor,
			not_null<DocumentData*> document,
			const QString &caption,
			const MTPReplyMarkup &markup) {
		return _create(
			history,
			msgId,
			flags,
			replyTo,
			viaBotId,
			date,
			from,
			postAuthor,
			document,
			caption,
			markup);
	}
	static not_null<HistoryMessage*> create(
			not_null<History*> history,
			MsgId msgId,
			MTPDmessage::Flags flags,
			MsgId replyTo,
			UserId viaBotId,
			QDateTime date,
			UserId from,
			const QString &postAuthor,
			not_null<PhotoData*> photo,
			const QString &caption,
			const MTPReplyMarkup &markup) {
		return _create(
			history,
			msgId,
			flags,
			replyTo,
			viaBotId,
			date,
			from,
			postAuthor,
			photo,
			caption,
			markup);
	}
	static not_null<HistoryMessage*> create(
			not_null<History*> history,
			MsgId msgId,
			MTPDmessage::Flags flags,
			MsgId replyTo,
			UserId viaBotId,
			QDateTime date,
			UserId from,
			const QString &postAuthor,
			not_null<GameData*> game,
			const MTPReplyMarkup &markup) {
		return _create(
			history,
			msgId,
			flags,
			replyTo,
			viaBotId,
			date,
			from,
			postAuthor,
			game,
			markup);
	}

	void initTime();
	void initMedia(const MTPMessageMedia *media);
	void initMediaFromDocument(DocumentData *doc, const QString &caption);
	void fromNameUpdated(int32 width) const;

	int32 plainMaxWidth() const;
	QRect countGeometry() const;

	bool drawBubble() const;
	bool hasBubble() const override {
		return drawBubble();
	}
	bool hasFromName() const;
	bool displayFromName() const {
		if (!hasFromName()) return false;
		if (isAttachedToPrevious()) return false;
		return true;
	}
	bool hasFastReply() const;
	bool displayFastReply() const;
	bool displayForwardedFrom() const;
	bool uploading() const;
	bool displayRightAction() const override;

	void applyGroupAdminChanges(
		const base::flat_map<UserId, bool> &changes) override;

	void drawInfo(Painter &p, int32 right, int32 bottom, int32 width, bool selected, InfoDisplayType type) const override;
	void drawRightAction(Painter &p, int left, int top, int outerWidth) const override;
	void setViewsCount(int32 count) override;
	void setId(MsgId newId) override;
	void draw(Painter &p, QRect clip, TextSelection selection, TimeMs ms) const override;
	ClickHandlerPtr rightActionLink() const override;

	void dependencyItemRemoved(HistoryItem *dependency) override;

	bool hasPoint(QPoint point) const override;
	bool pointInTime(int right, int bottom, QPoint point, InfoDisplayType type) const override;

	HistoryTextState getState(QPoint point, HistoryStateRequest request) const override;
	void updatePressed(QPoint point) override;

	TextSelection adjustSelection(TextSelection selection, TextSelectType type) const override;

	// ClickHandlerHost interface
	void clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) override;
	void clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) override;

	QString notificationHeader() const override;

	void applyEdition(const MTPDmessage &message) override;
	void applyEdition(const MTPDmessageService &message) override;
	void updateMedia(const MTPMessageMedia *media) override;
	void updateReplyMarkup(const MTPReplyMarkup *markup) override {
		setReplyMarkup(markup);
	}

	void addToUnreadMentions(UnreadMentionType type) override;
	void eraseFromUnreadMentions() override;
	Storage::SharedMediaTypesMask sharedMediaTypes() const override;

	TextWithEntities selectedText(TextSelection selection) const override;
	void setText(const TextWithEntities &textWithEntities) override;
	TextWithEntities originalText() const override;
	bool textHasLinks() const override;

	bool displayEditedBadge() const override;
	QDateTime displayedEditDate() const override;

	int infoWidth() const override;
	int timeLeft() const override;
	int timeWidth() const override {
		return _timeWidth;
	}

	int viewsCount() const override;
	not_null<PeerData*> displayFrom() const;
	bool updateDependencyItem() override;
	MsgId dependencyMsgId() const override {
		return replyToId();
	}

	HistoryMessage *toHistoryMessage() override { // dynamic_cast optimize
		return this;
	}
	const HistoryMessage *toHistoryMessage() const override { // dynamic_cast optimize
		return this;
	}

	// hasFromPhoto() returns true even if we don't display the photo
	// but we need to skip a place at the left side for this photo
	bool displayFromPhoto() const;
	bool hasFromPhoto() const;

	~HistoryMessage();

protected:
	void refreshEditedBadge() override;

private:
	HistoryMessage(
		not_null<History*> history,
		const MTPDmessage &msg);
	HistoryMessage(
		not_null<History*> history,
		const MTPDmessageService &msg);
	HistoryMessage(
		not_null<History*> history,
		MsgId msgId,
		MTPDmessage::Flags flags,
		QDateTime date,
		UserId from,
		const QString &postAuthor,
		not_null<HistoryMessage*> fwd); // local forwarded
	HistoryMessage(
		not_null<History*> history,
		MsgId msgId,
		MTPDmessage::Flags flags,
		MsgId replyTo,
		UserId viaBotId,
		QDateTime date,
		UserId from,
		const QString &postAuthor,
		const TextWithEntities &textWithEntities); // local message
	HistoryMessage(
		not_null<History*> history,
		MsgId msgId,
		MTPDmessage::Flags flags,
		MsgId replyTo,
		UserId viaBotId,
		QDateTime date,
		UserId from,
		const QString &postAuthor,
		not_null<DocumentData*> document,
		const QString &caption,
		const MTPReplyMarkup &markup); // local document
	HistoryMessage(
		not_null<History*> history,
		MsgId msgId,
		MTPDmessage::Flags flags,
		MsgId replyTo,
		UserId viaBotId,
		QDateTime date,
		UserId from,
		const QString &postAuthor,
		not_null<PhotoData*> photo,
		const QString &caption,
		const MTPReplyMarkup &markup); // local photo
	HistoryMessage(
		not_null<History*> history,
		MsgId msgId,
		MTPDmessage::Flags flags,
		MsgId replyTo,
		UserId viaBotId,
		QDateTime date,
		UserId from,
		const QString &postAuthor,
		not_null<GameData*> game,
		const MTPReplyMarkup &markup); // local game
	friend class HistoryItemInstantiated<HistoryMessage>;

	void setEmptyText();

	// For an invoice button we replace the button text with a "Receipt" key.
	// It should show the receipt for the payed invoice. Still let mobile apps do that.
	void replaceBuyWithReceiptInMarkup();

	void initDimensions() override;
	int resizeContentGetHeight() override;
	int performResizeGetHeight();
	void applyEditionToEmpty();
	QDateTime displayedEditDate(bool hasViaBotOrInlineMarkup) const;
	const HistoryMessageEdited *displayedEditBadge() const;
	HistoryMessageEdited *displayedEditBadge();

	void paintFromName(Painter &p, QRect &trect, bool selected) const;
	void paintForwardedInfo(Painter &p, QRect &trect, bool selected) const;
	void paintReplyInfo(Painter &p, QRect &trect, bool selected) const;
	// this method draws "via @bot" if it is not painted in forwarded info or in from name
	void paintViaBotIdInfo(Painter &p, QRect &trect, bool selected) const;
	void paintText(Painter &p, QRect &trect, TextSelection selection) const;

	bool getStateFromName(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult) const;
	bool getStateForwardedInfo(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult,
		const HistoryStateRequest &request) const;
	bool getStateReplyInfo(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult) const;
	bool getStateViaBotIdInfo(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult) const;
	bool getStateText(
		QPoint point,
		QRect &trect,
		not_null<HistoryTextState*> outResult,
		const HistoryStateRequest &request) const;

	void setMedia(const MTPMessageMedia *media);
	void setReplyMarkup(const MTPReplyMarkup *markup);

	bool displayFastShare() const;
	bool displayGoToOriginal() const;

	struct CreateConfig;
	void createComponentsHelper(MTPDmessage::Flags flags, MsgId replyTo, UserId viaBotId, const QString &postAuthor, const MTPReplyMarkup &markup);
	void createComponents(const CreateConfig &config);

	void updateMediaInBubbleState();
	void updateAdminBadgeState();
	ClickHandlerPtr fastReplyLink() const;

	QString _timeText;
	int _timeWidth = 0;

	mutable ClickHandlerPtr _rightActionLink;
	mutable ClickHandlerPtr _fastReplyLink;
	mutable int32 _fromNameVersion = 0;

};
