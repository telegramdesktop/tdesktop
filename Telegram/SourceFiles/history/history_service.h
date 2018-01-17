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
	TimeMs timeToLive = 0;
	TimeMs destructAt = 0;
};

namespace HistoryView {
class ServiceMessagePainter;
} // namespace HistoryView

class HistoryService : public HistoryItem, private HistoryItemInstantiated<HistoryService> {
public:
	struct PreparedText {
		QString text;
		QList<ClickHandlerPtr> links;
	};

	static not_null<HistoryService*> create(not_null<History*> history, const MTPDmessage &message) {
		return _create(history, message);
	}
	static not_null<HistoryService*> create(not_null<History*> history, const MTPDmessageService &message) {
		return _create(history, message);
	}
	static not_null<HistoryService*> create(not_null<History*> history, MsgId msgId, QDateTime date, const PreparedText &message, MTPDmessage::Flags flags = 0, UserId from = 0, PhotoData *photo = nullptr) {
		return _create(history, msgId, date, message, flags, from, photo);
	}

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
	TimeMs getSelfDestructIn(TimeMs now) override;

	Storage::SharedMediaTypesMask sharedMediaTypes() const override;

	bool needCheck() const override {
		return false;
	}
	bool serviceMsg() const override {
		return true;
	}
	QString inDialogsText(DrawInDialog way) const override;
	QString inReplyText() const override;

	std::unique_ptr<HistoryView::Element> createView(
		not_null<HistoryView::ElementDelegate*> delegate) override;

	~HistoryService();

protected:
	friend class HistoryView::ServiceMessagePainter;

	HistoryService(not_null<History*> history, const MTPDmessage &message);
	HistoryService(not_null<History*> history, const MTPDmessageService &message);
	HistoryService(not_null<History*> history, MsgId msgId, QDateTime date, const PreparedText &message, MTPDmessage::Flags flags = 0, UserId from = 0, PhotoData *photo = 0);
	friend class HistoryItemInstantiated<HistoryService>;

	void markMediaAsReadHook() override;

	void setServiceText(const PreparedText &prepared);

	QString fromLinkText() const {
		return textcmdLink(1, _from->name);
	};
	ClickHandlerPtr fromLink() const {
		return _from->createOpenLink();
	};

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
	void setSelfDestruct(HistoryServiceSelfDestruct::Type type, int ttlSeconds);

	PreparedText preparePinnedText();
	PreparedText prepareGameScoreText();
	PreparedText preparePaymentSentText();

	friend class HistoryView::Service;

};

class HistoryJoined : public HistoryService, private HistoryItemInstantiated<HistoryJoined> {
public:
	static not_null<HistoryJoined*> create(not_null<History*> history, const QDateTime &inviteDate, not_null<UserData*> inviter, MTPDmessage::Flags flags) {
		return _create(history, inviteDate, inviter, flags);
	}

protected:
	HistoryJoined(not_null<History*> history, const QDateTime &inviteDate, not_null<UserData*> inviter, MTPDmessage::Flags flags);
	using HistoryItemInstantiated<HistoryJoined>::_create;
	friend class HistoryItemInstantiated<HistoryJoined>;

private:
	static PreparedText GenerateText(not_null<History*> history, not_null<UserData*> inviter);

};
