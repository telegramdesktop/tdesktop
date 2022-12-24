/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/history_item.h"

namespace HistoryView {
class Service;
} // namespace HistoryView

struct HistoryServiceDependentData {
	PeerId peerId = 0;
	HistoryItem *msg = nullptr;
	ClickHandlerPtr lnk;
	MsgId msgId = 0;
	MsgId topId = 0;
	bool topicPost = false;
};

struct HistoryServicePinned
: public RuntimeComponent<HistoryServicePinned, HistoryItem>
, public HistoryServiceDependentData {
};

struct HistoryServiceTopicInfo
: public RuntimeComponent<HistoryServiceTopicInfo, HistoryItem>
, public HistoryServiceDependentData {
	QString title;
	DocumentId iconId = 0;
	bool closed = false;
	bool reopened = false;
	bool reiconed = false;
	bool renamed = false;
};

struct HistoryServiceGameScore
: public RuntimeComponent<HistoryServiceGameScore, HistoryItem>
, public HistoryServiceDependentData {
	int score = 0;
};

struct HistoryServicePayment
: public RuntimeComponent<HistoryServicePayment, HistoryItem>
, public HistoryServiceDependentData {
	QString slug;
	QString amount;
	ClickHandlerPtr invoiceLink;
	bool recurringInit = false;
	bool recurringUsed = false;
};

struct HistoryServiceSelfDestruct
: public RuntimeComponent<HistoryServiceSelfDestruct, HistoryItem> {
	enum class Type {
		Photo,
		Video,
	};
	Type type = Type::Photo;
	crl::time timeToLive = 0;
	crl::time destructAt = 0;
};

struct HistoryServiceOngoingCall
: public RuntimeComponent<HistoryServiceOngoingCall, HistoryItem> {
	CallId id = 0;
	ClickHandlerPtr link;
	rpl::lifetime lifetime;
};

struct HistoryServiceChatThemeChange
: public RuntimeComponent<HistoryServiceChatThemeChange, HistoryItem> {
	ClickHandlerPtr link;
};

struct HistoryServiceTTLChange
: public RuntimeComponent<HistoryServiceTTLChange, HistoryItem> {
	ClickHandlerPtr link;
};

namespace HistoryView {
class ServiceMessagePainter;
} // namespace HistoryView

class HistoryService : public HistoryItem {
public:
	struct PreparedText {
		TextWithEntities text;
		std::vector<ClickHandlerPtr> links;
	};

	HistoryService(
		not_null<History*> history,
		MsgId id,
		const MTPDmessage &data,
		MessageFlags localFlags);
	HistoryService(
		not_null<History*> history,
		MsgId id,
		const MTPDmessageService &data,
		MessageFlags localFlags);
	HistoryService(
		not_null<History*> history,
		MsgId id,
		MessageFlags flags,
		TimeId date,
		PreparedText &&message,
		PeerId from = 0,
		PhotoData *photo = nullptr);

	bool updateDependencyItem() override;
	MsgId dependencyMsgId() const override {
		if (auto dependent = GetDependentData()) {
			return dependent->msgId;
		}
		return 0;
	}
	bool notificationReady() const override {
		if (auto dependent = GetDependentData()) {
			return (dependent->msg || !dependent->msgId);
		}
		return true;
	}

	const std::vector<ClickHandlerPtr> &customTextLinks() const override;

	void applyEdition(const MTPDmessageService &message) override;
	crl::time getSelfDestructIn(crl::time now) override;

	Storage::SharedMediaTypesMask sharedMediaTypes() const override;

	void dependencyItemRemoved(HistoryItem *dependency) override;

	bool needCheck() const override;
	bool isService() const override {
		return true;
	}
	ItemPreview toPreview(ToPreviewOptions options) const override;
	TextWithEntities inReplyText() const override;

	MsgId replyToId() const override;
	MsgId replyToTop() const override;
	MsgId topicRootId() const override;
	void setReplyFields(
		MsgId replyTo,
		MsgId replyToTop,
		bool isForumPost) override;

	std::unique_ptr<HistoryView::Element> createView(
		not_null<HistoryView::ElementDelegate*> delegate,
		HistoryView::Element *replacing = nullptr) override;

	void setServiceText(PreparedText &&prepared);

	~HistoryService();

protected:
	friend class HistoryView::ServiceMessagePainter;

	void markMediaAsReadHook() override;

	TextWithEntities fromLinkText() const;
	ClickHandlerPtr fromLink() const;

	void removeMedia();

private:
	HistoryServiceDependentData *GetDependentData() {
		if (const auto pinned = Get<HistoryServicePinned>()) {
			return pinned;
		} else if (const auto gamescore = Get<HistoryServiceGameScore>()) {
			return gamescore;
		} else if (const auto payment = Get<HistoryServicePayment>()) {
			return payment;
		} else if (const auto info = Get<HistoryServiceTopicInfo>()) {
			return info;
		}
		return nullptr;
	}
	const HistoryServiceDependentData *GetDependentData() const {
		return const_cast<HistoryService*>(this)->GetDependentData();
	}
	bool updateDependent(bool force = false);
	void updateDependentText();
	void updateText(PreparedText &&text);
	void clearDependency();
	void setupChatThemeChange();
	void setupTTLChange();

	void createFromMtp(const MTPDmessage &message);
	void createFromMtp(const MTPDmessageService &message);
	void setMessageByAction(const MTPmessageAction &action);
	void setSelfDestruct(
		HistoryServiceSelfDestruct::Type type,
		int ttlSeconds);
	void applyAction(const MTPMessageAction &action);

	PreparedText preparePinnedText();
	PreparedText prepareGameScoreText();
	PreparedText preparePaymentSentText();
	PreparedText prepareInvitedToCallText(
		const QVector<MTPlong> &users,
		CallId linkCallId);
	PreparedText prepareCallScheduledText(
		TimeId scheduleDate);

	friend class HistoryView::Service;

	std::vector<ClickHandlerPtr> _textLinks;

};

[[nodiscard]] not_null<HistoryService*> GenerateJoinedMessage(
	not_null<History*> history,
	TimeId inviteDate,
	not_null<UserData*> inviter,
	bool viaRequest);
[[nodiscard]] std::optional<bool> PeerHasThisCall(
	not_null<PeerData*> peer,
	CallId id);
