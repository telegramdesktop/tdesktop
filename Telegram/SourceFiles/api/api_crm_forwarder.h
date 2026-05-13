/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/timer.h"
#include "data/data_file_origin.h"
#include "mtproto/sender.h"

#include <QtNetwork/QNetworkAccessManager>

class DocumentData;
class HistoryItem;

namespace Data {
class PhotoMedia;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Api {

class CrmForwarder final : public base::has_weak_ptr {
public:
	explicit CrmForwarder(not_null<Main::Session*> session);
	~CrmForwarder();

	void registerPhoneMapping(const QString &phone, PeerId peerId);
	void resolveAndRegisterUsername(const QString &tag);

private:
	struct PendingEntry {
		QString chatId;
		QByteArray payload;       // JSON metadata
		QString mediaFilePath;    // local file to upload (empty = no file)
		int retries = 0;
		QString logFilePath;      // path to retry log file on disk
	};

	struct WaitingEntry {
		PendingEntry crm;
		DocumentData *document = nullptr;
		std::shared_ptr<Data::PhotoMedia> photoMedia;
		Data::FileOrigin origin;
		QString tempFilePath;     // temp path for photo saves
		FullMsgId itemId;         // tracks ID change for outgoing photos
		int attempts = 0;         // bounded retry counter for media download
	};

	void onNewItem(not_null<HistoryItem*> item);
	void onItemEdited(not_null<HistoryItem*> item);
	void onItemDeleted(not_null<const HistoryItem*> item);
	void triggerDocumentDownload(
		WaitingEntry entry,
		not_null<DocumentData*> doc);
	void triggerPhotoDownload(
		WaitingEntry entry,
		not_null<HistoryItem*> item);
	void checkWaitingDownloads();
	void send(PendingEntry entry);
	void onReplyFinished(QNetworkReply *reply, PendingEntry entry);
	void retryAll();
	[[nodiscard]] QString saveToDisk(const PendingEntry &entry) const;
	void loadFromDisk();
	void loadConfig();
	[[nodiscard]] QString pendingDir() const;
	[[nodiscard]] QString tempDir() const;
	[[nodiscard]] QByteArray buildPayload(
		not_null<HistoryItem*> item,
		const QString &action = u"new"_q) const;
	[[nodiscard]] QString phoneMapPath() const;
	void loadPhoneMap();
	void savePhoneMap() const;
	void registerUsernameMapping(const QString &username, PeerId peerId);
	[[nodiscard]] QString usernameMapPath() const;
	void loadUsernameMap();
	void saveUsernameMap() const;

	not_null<Main::Session*> _session;
	MTP::Sender _api;
	QString _endpoint;
	QString _bearerToken;
	QNetworkAccessManager _network;
	base::Timer _retryTimer;
	std::vector<PendingEntry> _pending;
	std::vector<WaitingEntry> _waiting;
	std::vector<QNetworkReply*> _activeReplies;
	QJsonObject _phoneMap;    // peer_id (string) -> phone
	QJsonObject _usernameMap; // peer_id (string) -> username
	rpl::lifetime _lifetime;

};

} // namespace Api
