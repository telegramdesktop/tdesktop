/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item.h"

namespace HistoryView {
class Message;
} // namespace HistoryView

struct HistoryMessageEdited;

Fn<void(ChannelData*, MsgId)> HistoryDependentItemCallback(
	const FullMsgId &msgId);
MTPDmessage::Flags NewMessageFlags(not_null<PeerData*> peer);
QString GetErrorTextForForward(
	not_null<PeerData*> peer,
	const HistoryItemsList &items);
void FastShareMessage(not_null<HistoryItem*> item);
QString FormatViewsCount(int views);

class HistoryMessage
	: public HistoryItem {
public:
	HistoryMessage(
		not_null<History*> history,
		const MTPDmessage &data);
	HistoryMessage(
		not_null<History*> history,
		const MTPDmessageService &data);
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		MTPDmessage::Flags flags,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<HistoryMessage*> original); // local forwarded
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		MTPDmessage::Flags flags,
		MsgId replyTo,
		UserId viaBotId,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		const TextWithEntities &textWithEntities); // local message
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		MTPDmessage::Flags flags,
		MsgId replyTo,
		UserId viaBotId,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<DocumentData*> document,
		const TextWithEntities &caption,
		const MTPReplyMarkup &markup); // local document
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		MTPDmessage::Flags flags,
		MsgId replyTo,
		UserId viaBotId,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<PhotoData*> photo,
		const TextWithEntities &caption,
		const MTPReplyMarkup &markup); // local photo
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		MTPDmessage::Flags flags,
		MsgId replyTo,
		UserId viaBotId,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<GameData*> game,
		const MTPReplyMarkup &markup); // local game

	void refreshMedia(const MTPMessageMedia *media);
	void refreshSentMedia(const MTPMessageMedia *media);
	void setMedia(const MTPMessageMedia &media);
	static std::unique_ptr<Data::Media> CreateMedia(
		not_null<HistoryMessage*> item,
		const MTPMessageMedia &media);

	bool allowsForward() const override;
	bool allowsEdit(TimeId now) const override;
	bool uploading() const;

	void applyGroupAdminChanges(
		const base::flat_map<UserId, bool> &changes) override;

	void setViewsCount(int32 count) override;
	void setRealId(MsgId newId) override;

	void dependencyItemRemoved(HistoryItem *dependency) override;

	QString notificationHeader() const override;

	void applyEdition(const MTPDmessage &message) override;
	void applyEdition(const MTPDmessageService &message) override;
	void updateSentMedia(const MTPMessageMedia *media) override;
	void updateReplyMarkup(const MTPReplyMarkup *markup) override {
		setReplyMarkup(markup);
	}

	void addToUnreadMentions(UnreadMentionType type) override;
	void eraseFromUnreadMentions() override;
	Storage::SharedMediaTypesMask sharedMediaTypes() const override;

	void setText(const TextWithEntities &textWithEntities) override;
	TextWithEntities originalText() const override;
	TextWithEntities clipboardText() const override;
	bool textHasLinks() const override;

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

	std::unique_ptr<HistoryView::Element> createView(
		not_null<HistoryView::ElementDelegate*> delegate) override;

	~HistoryMessage();

private:
	void setEmptyText();
	bool hasAdminBadge() const {
		return _flags & MTPDmessage_ClientFlag::f_has_admin_badge;
	}

	// For an invoice button we replace the button text with a "Receipt" key.
	// It should show the receipt for the payed invoice. Still let mobile apps do that.
	void replaceBuyWithReceiptInMarkup();

	void applyEditionToEmpty();

	void setReplyMarkup(const MTPReplyMarkup *markup);

	struct CreateConfig;
	void createComponentsHelper(MTPDmessage::Flags flags, MsgId replyTo, UserId viaBotId, const QString &postAuthor, const MTPReplyMarkup &markup);
	void createComponents(const CreateConfig &config);

	void updateAdminBadgeState();
	ClickHandlerPtr fastReplyLink() const;

	QString _timeText;
	int _timeWidth = 0;

	mutable int32 _fromNameVersion = 0;

	friend class HistoryView::Element;
	friend class HistoryView::Message;

};
