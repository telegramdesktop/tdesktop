/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item.h"

namespace Api {
struct SendAction;
struct SendOptions;
} // namespace Api

namespace Data {
class Thread;
struct SponsoredFrom;
} // namespace Data

namespace HistoryView {
class Message;
} // namespace HistoryView

struct HistoryMessageEdited;
struct HistoryMessageReply;
struct HistoryMessageViews;
struct HistoryMessageMarkupData;

void RequestDependentMessageData(
	not_null<HistoryItem*> item,
	PeerId peerId,
	MsgId msgId);
[[nodiscard]] MessageFlags NewMessageFlags(not_null<PeerData*> peer);
[[nodiscard]] bool ShouldSendSilent(
	not_null<PeerData*> peer,
	const Api::SendOptions &options);
[[nodiscard]] MsgId LookupReplyToTop(
	not_null<History*> history,
	MsgId replyToId);
[[nodiscard]] MTPMessageReplyHeader NewMessageReplyHeader(
	const Api::SendAction &action);

struct SendingErrorRequest {
	MsgId topicRootId = 0;
	const HistoryItemsList *forward = nullptr;
	const TextWithTags *text = nullptr;
	bool ignoreSlowmodeCountdown = false;
};
[[nodiscard]] QString GetErrorTextForSending(
	not_null<PeerData*> peer,
	SendingErrorRequest request);
[[nodiscard]] QString GetErrorTextForSending(
	not_null<Data::Thread*> thread,
	SendingErrorRequest request);

[[nodiscard]] TextWithEntities DropCustomEmoji(TextWithEntities text);

class HistoryMessage final : public HistoryItem {
public:
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		const MTPDmessage &data,
		MessageFlags localFlags);
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		const MTPDmessageService &data,
		MessageFlags localFlags);
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<HistoryItem*> original,
		MsgId topicRootId); // local forwarded
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		MsgId replyTo,
		UserId viaBotId,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		const TextWithEntities &textWithEntities,
		const MTPMessageMedia &media,
		HistoryMessageMarkupData &&markup,
		uint64 groupedId); // local message
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		MsgId replyTo,
		UserId viaBotId,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<DocumentData*> document,
		const TextWithEntities &caption,
		HistoryMessageMarkupData &&markup); // local document
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		MsgId replyTo,
		UserId viaBotId,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<PhotoData*> photo,
		const TextWithEntities &caption,
		HistoryMessageMarkupData &&markup); // local photo
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		MsgId replyTo,
		UserId viaBotId,
		TimeId date,
		PeerId from,
		const QString &postAuthor,
		not_null<GameData*> game,
		HistoryMessageMarkupData &&markup); // local game
	HistoryMessage(
		not_null<History*> history,
		MsgId id,
		Data::SponsoredFrom from,
		const TextWithEntities &textWithEntities,
		HistoryItem *injectedAfter); // sponsored

	void refreshMedia(const MTPMessageMedia *media);
	void refreshSentMedia(const MTPMessageMedia *media);
	void returnSavedMedia() override;
	void setMedia(const MTPMessageMedia &media);
	void checkBuyButton() override;
	[[nodiscard]] static std::unique_ptr<Data::Media> CreateMedia(
		not_null<HistoryMessage*> item,
		const MTPMessageMedia &media);

	[[nodiscard]] bool allowsForward() const override;
	[[nodiscard]] bool allowsSendNow() const override;
	[[nodiscard]] bool allowsEdit(TimeId now) const override;

	bool changeViewsCount(int count) override;
	void setForwardsCount(int count) override;
	void setReplies(HistoryMessageRepliesData &&data) override;
	void clearReplies() override;
	void changeRepliesCount(int delta, PeerId replier) override;
	void setReplyFields(
		MsgId replyTo,
		MsgId replyToTop,
		bool isForumPost) override;
	void setPostAuthor(const QString &author) override;
	void setRealId(MsgId newId) override;
	void incrementReplyToTopCounter() override;

	void dependencyItemRemoved(HistoryItem *dependency) override;

	[[nodiscard]] QString notificationHeader() const override;

	// Looks on:
	//   f_edit_hide
	//   f_edit_date
	//   f_entities
	//   f_reply_markup
	//   f_media
	//   f_views
	//   f_forwards
	//   f_replies
	//   f_ttl_period
	void applyEdition(HistoryMessageEdition &&edition) override;

	void applyEdition(const MTPDmessageService &message) override;
	void applyEdition(const MTPMessageExtendedMedia &media) override;
	void updateSentContent(
		const TextWithEntities &textWithEntities,
		const MTPMessageMedia *media) override;
	void updateReplyMarkup(HistoryMessageMarkupData &&markup) override;
	void updateForwardedInfo(const MTPMessageFwdHeader *fwd) override;
	void contributeToSlowmode(TimeId realDate = 0) override;

	void addToUnreadThings(HistoryUnreadThings::AddType type) override;
	void destroyHistoryEntry() override;
	[[nodiscard]] Storage::SharedMediaTypesMask sharedMediaTypes() const override;

	[[nodiscard]] MsgId replyToId() const override;
	[[nodiscard]] MsgId replyToTop() const override;
	[[nodiscard]] MsgId topicRootId() const override;

	void setText(const TextWithEntities &textWithEntities) override;
	[[nodiscard]] TextWithEntities originalText() const override;
	[[nodiscard]] auto originalTextWithLocalEntities() const
		-> TextWithEntities override;
	[[nodiscard]] TextForMimeData clipboardText() const override;

	[[nodiscard]] int viewsCount() const override;
	[[nodiscard]] int repliesCount() const override;
	[[nodiscard]] bool repliesAreComments() const override;
	[[nodiscard]] bool externalReply() const override;

	void setCommentsInboxReadTill(MsgId readTillId) override;
	void setCommentsMaxId(MsgId maxId) override;
	void setCommentsPossibleMaxId(MsgId possibleMaxId) override;
	[[nodiscard]] bool areCommentsUnread() const override;

	[[nodiscard]] FullMsgId commentsItemId() const override;
	void setCommentsItemId(FullMsgId id) override;
	bool updateDependencyItem() override;
	[[nodiscard]] MsgId dependencyMsgId() const override {
		return replyToId();
	}

	void applySentMessage(const MTPDmessage &data) override;
	void applySentMessage(
		const QString &text,
		const MTPDupdateShortSentMessage &data,
		bool wasAlready) override;

	[[nodiscard]] std::unique_ptr<HistoryView::Element> createView(
		not_null<HistoryView::ElementDelegate*> delegate,
		HistoryView::Element *replacing = nullptr) override;

	~HistoryMessage();

private:
	void setTextValue(TextWithEntities text);
	[[nodiscard]] bool isTooOldForEdit(TimeId now) const;
	[[nodiscard]] bool isLegacyMessage() const {
		return _flags & MessageFlag::Legacy;
	}

	[[nodiscard]] bool checkCommentsLinkedChat(ChannelId id) const;

	// For an invoice button we replace the button text with a "Receipt" key.
	// It should show the receipt for the payed invoice. Still let mobile apps do that.
	void replaceBuyWithReceiptInMarkup();

	void setReplyMarkup(HistoryMessageMarkupData &&markup);

	struct CreateConfig;
	void createComponentsHelper(
		MessageFlags flags,
		MsgId replyTo,
		UserId viaBotId,
		const QString &postAuthor,
		HistoryMessageMarkupData &&markup);
	void createComponents(CreateConfig &&config);
	void setupForwardedComponent(const CreateConfig &config);
	void changeReplyToTopCounter(
		not_null<HistoryMessageReply*> reply,
		int delta);
	void refreshRepliesText(
		not_null<HistoryMessageViews*> views,
		bool forceResize = false);
	void setSponsoredFrom(const Data::SponsoredFrom &from);

	static void FillForwardedInfo(
		CreateConfig &config,
		const MTPDmessageFwdHeader &data);

	[[nodiscard]] bool generateLocalEntitiesByReply() const;
	[[nodiscard]] TextWithEntities withLocalEntities(
		const TextWithEntities &textWithEntities) const;

	[[nodiscard]] bool checkRepliesPts(
		const HistoryMessageRepliesData &data) const;

	friend class HistoryView::Element;
	friend class HistoryView::Message;

};
