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
MTPDmessage_ClientFlags NewMessageClientFlags();
QString GetErrorTextForSending(
	not_null<PeerData*> peer,
	const HistoryItemsList &items,
	bool ignoreSlowmodeCountdown = false);
QString GetErrorTextForSending(
	not_null<PeerData*> peer,
	const HistoryItemsList &items,
	const TextWithTags &comment,
	bool ignoreSlowmodeCountdown = false);
void FastShareMessage(not_null<HistoryItem*> item);

class HistoryMessage : public HistoryItem {
public:
	HistoryMessage(
		not_null<History*> history,
		const MTPDmessage &data,
		MTPDmessage_ClientFlags clientFlags);
	HistoryMessage(
		not_null<History*> history,
		const MTPDmessageService &data,
		MTPDmessage_ClientFlags clientFlags);
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<HistoryMessage*> original); // local forwarded
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		MTPDmessage::Flags flags,
		MTPDmessage_ClientFlags clientFlags,
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
		MTPDmessage_ClientFlags clientFlags,
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
		MTPDmessage_ClientFlags clientFlags,
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
		MTPDmessage_ClientFlags clientFlags,
		MsgId replyTo,
		UserId viaBotId,
		TimeId date,
		UserId from,
		const QString &postAuthor,
		not_null<GameData*> game,
		const MTPReplyMarkup &markup); // local game

	void refreshMedia(const MTPMessageMedia *media);
	void refreshSentMedia(const MTPMessageMedia *media);
	void returnSavedMedia() override;
	void setMedia(const MTPMessageMedia &media);
	[[nodiscard]] static std::unique_ptr<Data::Media> CreateMedia(
		not_null<HistoryMessage*> item,
		const MTPMessageMedia &media);

	[[nodiscard]] bool allowsForward() const override;
	[[nodiscard]] bool allowsSendNow() const override;
	[[nodiscard]] bool allowsEdit(TimeId now) const override;
	[[nodiscard]] bool uploading() const;

	[[nodiscard]] const Ui::Text::String &messageBadge() const {
		return _messageBadge;
	}
	[[nodiscard]] bool hasMessageBadge() const {
		return !_messageBadge.isEmpty();
	}
	[[nodiscard]] bool hideEditedBadge() const {
		return (_flags & MTPDmessage::Flag::f_edit_hide);
	}

	void applyGroupAdminChanges(
		const base::flat_set<UserId> &changes) override;

	void setViewsCount(int32 count) override;
	void setRealId(MsgId newId) override;

	void dependencyItemRemoved(HistoryItem *dependency) override;

	[[nodiscard]] QString notificationHeader() const override;

	void applyEdition(const MTPDmessage &message) override;
	void applyEdition(const MTPDmessageService &message) override;
	void updateSentContent(
		const TextWithEntities &textWithEntities,
		const MTPMessageMedia *media) override;
	void updateReplyMarkup(const MTPReplyMarkup *markup) override {
		setReplyMarkup(markup);
	}
	void updateForwardedInfo(const MTPMessageFwdHeader *fwd) override;
	void contributeToSlowmode(TimeId realDate = 0) override;

	void addToUnreadMentions(UnreadMentionType type) override;
	void eraseFromUnreadMentions() override;
	[[nodiscard]] Storage::SharedMediaTypesMask sharedMediaTypes() const override;

	void setText(const TextWithEntities &textWithEntities) override;
	[[nodiscard]] Ui::Text::IsolatedEmoji isolatedEmoji() const override;
	[[nodiscard]] TextWithEntities originalText() const override;
	[[nodiscard]] TextForMimeData clipboardText() const override;
	[[nodiscard]] bool textHasLinks() const override;

	[[nodiscard]] int viewsCount() const override;
	bool updateDependencyItem() override;
	[[nodiscard]] MsgId dependencyMsgId() const override {
		return replyToId();
	}

	// dynamic_cast optimization.
	[[nodiscard]] HistoryMessage *toHistoryMessage() override {
		return this;
	}
	[[nodiscard]] const HistoryMessage *toHistoryMessage() const override {
		return this;
	}

	[[nodiscard]] std::unique_ptr<HistoryView::Element> createView(
		not_null<HistoryView::ElementDelegate*> delegate) override;

	~HistoryMessage();

private:
	void setEmptyText();
	[[nodiscard]] bool isTooOldForEdit(TimeId now) const;
	[[nodiscard]] bool isLegacyMessage() const {
		return _flags & MTPDmessage::Flag::f_legacy;
	}

	void clearIsolatedEmoji();
	void checkIsolatedEmoji();

	// For an invoice button we replace the button text with a "Receipt" key.
	// It should show the receipt for the payed invoice. Still let mobile apps do that.
	void replaceBuyWithReceiptInMarkup();

	void setReplyMarkup(const MTPReplyMarkup *markup);

	struct CreateConfig;
	void createComponentsHelper(MTPDmessage::Flags flags, MsgId replyTo, UserId viaBotId, const QString &postAuthor, const MTPReplyMarkup &markup);
	void createComponents(const CreateConfig &config);
	void setupForwardedComponent(const CreateConfig &config);

	static void FillForwardedInfo(
		CreateConfig &config,
		const MTPDmessageFwdHeader &data);

	void refreshMessageBadge();

	Ui::Text::String _messageBadge;

	QString _timeText;
	int _timeWidth = 0;

	mutable int32 _fromNameVersion = 0;

	friend class HistoryView::Element;
	friend class HistoryView::Message;

};
