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
	MsgId msgId = 0;
	HistoryItem *msg = nullptr;
	ClickHandlerPtr lnk;
};

struct HistoryServicePinned
	: public RuntimeComponent<HistoryServicePinned, HistoryItem>
	, public HistoryServiceDependentData {
};

struct HistoryServiceGameScore
	: public RuntimeComponent<HistoryServiceGameScore, HistoryItem>
	, public HistoryServiceDependentData {
	int score = 0;
};

struct HistoryServicePayment
	: public RuntimeComponent<HistoryServicePayment, HistoryItem>
	, public HistoryServiceDependentData {
	QString amount;
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

namespace HistoryView {
class ServiceMessagePainter;
} // namespace HistoryView

class HistoryService : public HistoryItem {
public:
	struct PreparedText {
		QString text;
		QList<ClickHandlerPtr> links;
	};

	HistoryService(
		not_null<History*> history,
		const MTPDmessage &data,
		MTPDmessage_ClientFlags clientFlags);
	HistoryService(
		not_null<History*> history,
		const MTPDmessageService &data,
		MTPDmessage_ClientFlags clientFlags);
	HistoryService(
		not_null<History*> history,
		MsgId id,
		MTPDmessage_ClientFlags clientFlags,
		TimeId date,
		const PreparedText &message,
		MTPDmessage::Flags flags = 0,
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

	void applyEdition(const MTPDmessageService &message) override;
	crl::time getSelfDestructIn(crl::time now) override;

	Storage::SharedMediaTypesMask sharedMediaTypes() const override;

	bool needCheck() const override;
	bool serviceMsg() const override {
		return true;
	}
	QString inDialogsText(DrawInDialog way) const override;
	QString inReplyText() const override;

	std::unique_ptr<HistoryView::Element> createView(
		not_null<HistoryView::ElementDelegate*> delegate,
		HistoryView::Element *replacing = nullptr) override;

	void setServiceText(const PreparedText &prepared);

	~HistoryService();

protected:
	friend class HistoryView::ServiceMessagePainter;

	void markMediaAsReadHook() override;

	QString fromLinkText() const;
	ClickHandlerPtr fromLink() const;

	void removeMedia();

private:
	HistoryServiceDependentData *GetDependentData() {
		if (auto pinned = Get<HistoryServicePinned>()) {
			return pinned;
		} else if (auto gamescore = Get<HistoryServiceGameScore>()) {
			return gamescore;
		} else if (auto payment = Get<HistoryServicePayment>()) {
			return payment;
		}
		return nullptr;
	}
	const HistoryServiceDependentData *GetDependentData() const {
		return const_cast<HistoryService*>(this)->GetDependentData();
	}
	bool updateDependent(bool force = false);
	void updateDependentText();
	void clearDependency();

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

	friend class HistoryView::Service;

};

not_null<HistoryService*> GenerateJoinedMessage(
	not_null<History*> history,
	TimeId inviteDate,
	not_null<UserData*> inviter,
	MTPDmessage::Flags flags);
