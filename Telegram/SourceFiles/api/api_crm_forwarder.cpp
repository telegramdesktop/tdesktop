/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_crm_forwarder.h"

#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history.h"
#include "data/data_session.h"
#include "data/data_changes.h"
#include "data/data_user.h"
#include "data/data_media_types.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "main/main_session.h"
#include "settings.h"

#include <crl/crl_on_main.h>
#include <rpl/filter.h>

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDirIterator>
#include <QtCore/QDateTime>
#include <QtCore/QTimeZone>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QHttpMultiPart>

namespace Api {
namespace {

constexpr auto kMaxRetries = 10;
constexpr auto kRetryInterval = crl::time(60'000);
constexpr auto kMaxWaitingAttempts = 30;

[[nodiscard]] QByteArray mimeTypeForMediaType(const QString &type) {
	if (type == u"voice"_q)      return "audio/ogg";
	if (type == u"audio"_q)      return "audio/mpeg";
	if (type == u"video_note"_q) return "video/mp4";
	if (type == u"photo"_q)      return "image/jpeg";
	return "application/octet-stream";
}

} // namespace

CrmForwarder::CrmForwarder(not_null<Main::Session*> session)
: _session(session)
, _api(&session->mtp())
, _retryTimer([=] { retryAll(); }) {
	loadConfig();
	loadPhoneMap();
	loadUsernameMap();
	loadFromDisk();
	if (!_pending.empty()) {
		_retryTimer.callEach(kRetryInterval);
	}

	_session->data().newItemAdded(
	) | rpl::filter([](not_null<HistoryItem*> item) {
		const auto user = item->history()->peer->asUser();
		return user && !user->isBot();
	}) | rpl::on_next([=](not_null<HistoryItem*> item) {
		onNewItem(item);
	}, _lifetime);

	_session->downloaderTaskFinished(
	) | rpl::on_next([=] {
		checkWaitingDownloads();
	}, _lifetime);

	_session->changes().messageUpdates(
		Data::MessageUpdate::Flag::Edited
	) | rpl::filter([](const Data::MessageUpdate &update) {
		const auto user = update.item->history()->peer->asUser();
		return user && !user->isBot();
	}) | rpl::on_next([=](const Data::MessageUpdate &update) {
		onItemEdited(update.item);
	}, _lifetime);

	_session->data().itemRemoved(
	) | rpl::filter([](not_null<const HistoryItem*> item) {
		const auto user = item->history()->peer->asUser();
		return user && !user->isBot();
	}) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		onItemDeleted(item);
	}, _lifetime);

	// When an outgoing message gets its real server ID the media gains a
	// valid server location. Update origin for both photo and document
	// waiting entries, then re-trigger downloads.
	_session->data().itemIdChanged(
	) | rpl::on_next([=](const Data::Session::IdChange &change) {
		auto remaining = std::vector<WaitingEntry>{};
		for (auto &w : _waiting) {
			const auto msgId = std::get_if<Data::FileOriginMessage>(
				&w.origin.data);
			if (!msgId
				|| msgId->msg != change.oldId
				|| msgId->peer != change.newId.peer) {
				remaining.push_back(std::move(w));
				continue;
			}
			// Patch real server message_id into the stored payload.
			{
				auto doc = QJsonDocument::fromJson(w.crm.payload);
				if (doc.isObject()) {
					auto root = doc.object();
					root[u"message_id"_q] = change.newId.msg.bare;
					w.crm.payload = QJsonDocument(root).toJson(
						QJsonDocument::Compact);
				}
			}
			w.itemId = change.newId;
			w.origin = Data::FileOrigin(
				Data::FileOriginMessage(change.newId));
			if (w.document) {
				// Re-trigger download: doc now has a valid server location.
				w.document->save(w.origin, w.tempFilePath);
				remaining.push_back(std::move(w));
			} else if (w.photoMedia) {
				remaining.push_back(std::move(w));
			} else {
				// Text-only outgoing: real ID is ready, send now.
				send(std::move(w.crm));
			}
		}
		_waiting = std::move(remaining);
		checkWaitingDownloads();
	}, _lifetime);
}

CrmForwarder::~CrmForwarder() {
	for (auto *reply : _activeReplies) {
		reply->disconnect();
		reply->abort();
		reply->deleteLater();
	}
}

void CrmForwarder::loadConfig() {
	const auto path = cWorkingDir() + u"tdata/crm_config.json"_q;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto doc = QJsonDocument::fromJson(file.readAll());
	if (!doc.isObject()) {
		return;
	}
	const auto obj = doc.object();
	_endpoint = obj.value(u"endpoint"_q).toString();
	_bearerToken = obj.value(u"bearer_token"_q).toString();
}

QString CrmForwarder::phoneMapPath() const {
	return cWorkingDir() + u"tdata/crm_phone_map.json"_q;
}

void CrmForwarder::loadPhoneMap() {
	QFile file(phoneMapPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto doc = QJsonDocument::fromJson(file.readAll());
	if (doc.isObject()) {
		_phoneMap = doc.object();
	}
}

void CrmForwarder::savePhoneMap() const {
	QFile file(phoneMapPath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(_phoneMap).toJson(QJsonDocument::Compact));
	}
}

QString CrmForwarder::usernameMapPath() const {
	return cWorkingDir() + u"tdata/crm_username_map.json"_q;
}

void CrmForwarder::loadUsernameMap() {
	QFile file(usernameMapPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto doc = QJsonDocument::fromJson(file.readAll());
	if (doc.isObject()) {
		_usernameMap = doc.object();
	}
}

void CrmForwarder::saveUsernameMap() const {
	QFile file(usernameMapPath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(_usernameMap).toJson(QJsonDocument::Compact));
	}
}

void CrmForwarder::registerUsernameMapping(
		const QString &username,
		PeerId peerId) {
	const auto key = QString::number(peerId.value);
	if (_usernameMap.value(key).toString() == username) {
		return;
	}
	_usernameMap[key] = username;
	saveUsernameMap();
}

void CrmForwarder::resolveAndRegisterUsername(const QString &tag) {
	const auto username = tag.startsWith(u'@') ? tag.mid(1) : tag;
	if (username.isEmpty()) {
		return;
	}
	if (const auto peer = _session->data().peerByUsername(username)) {
		registerUsernameMapping(username, peer->id);
		return;
	}
	_api.request(MTPcontacts_ResolveUsername(
		MTP_flags(0),
		MTP_string(username),
		MTP_string()
	)).done([=](const MTPcontacts_ResolvedPeer &result) {
		result.match([&](const MTPDcontacts_resolvedPeer &data) {
			_session->data().processUsers(data.vusers());
			_session->data().processChats(data.vchats());
			if (const auto peerId = peerFromMTP(data.vpeer())) {
				registerUsernameMapping(username, peerId);
			}
		});
	}).send();
}

void CrmForwarder::registerPhoneMapping(const QString &phone, PeerId peerId) {
	const auto key = QString::number(peerId.value);
	auto trimmed = phone;
	trimmed.remove(QChar(u'+')).remove(QChar(u' ')).remove(QChar(u'-'));
	if (_phoneMap.value(key).toString() == trimmed) {
		return;
	}
	_phoneMap[key] = trimmed;
	savePhoneMap();
}

QString CrmForwarder::pendingDir() const {
	return cWorkingDir() + u"tdata/crm_pending/"_q;
}

QString CrmForwarder::tempDir() const {
	return cWorkingDir() + u"tdata/crm_temp/"_q;
}

void CrmForwarder::onNewItem(not_null<HistoryItem*> item) {
	if (_endpoint.isEmpty() || _bearerToken.isEmpty()) {
		return;
	}

	const auto origin = Data::FileOrigin(
		Data::FileOriginMessage(item->fullId()));

	const auto media = item->media();
	if (!media) {
		if (item->out()) {
			// Outgoing: hold until itemIdChanged gives us the real server ID.
			_waiting.push_back(WaitingEntry{
				.crm = PendingEntry{
					.chatId = QString::number(
						item->history()->peer->id.value),
					.payload = buildPayload(item),
				},
				.origin = origin,
			});
		} else {
			send(PendingEntry{
				.chatId = QString::number(item->history()->peer->id.value),
				.payload = buildPayload(item),
			});
		}
		return;
	}

	if (const auto doc = media->document()) {
		const auto path = doc->filepath();
		if (!path.isEmpty()) {
			send(PendingEntry{
				.chatId = QString::number(item->history()->peer->id.value),
				.payload = buildPayload(item),
				.mediaFilePath = path,
			});
		} else {
			// Check if bytes are already in memory via an EXISTING media view.
			// Do NOT call createMediaView() here — creating one would cause
			// doc->save() (inside triggerDocumentDownload) to see a loaded()
			// view and take the early-return path without starting a download
			// loader, so downloaderTaskFinished never fires.
			const auto existingView = doc->activeMediaView();
			const auto bytes = existingView ? existingView->bytes() : QByteArray{};
			if (!bytes.isEmpty()) {
				QDir().mkpath(tempDir());
				const auto tempPath = tempDir()
					+ QString::number(QDateTime::currentMSecsSinceEpoch())
					+ u"_"_q
					+ doc->filename();
				QFile f(tempPath);
				if (f.open(QIODevice::WriteOnly)) {
					f.write(bytes);
					f.close();
					send(PendingEntry{
						.chatId = QString::number(
							item->history()->peer->id.value),
						.payload = buildPayload(item),
						.mediaFilePath = tempPath,
					});
				} else {
					triggerDocumentDownload(
						WaitingEntry{
							.crm = PendingEntry{
								.chatId = QString::number(
									item->history()->peer->id.value),
								.payload = buildPayload(item),
							},
							.document = doc,
							.origin = origin,
						},
						doc);
				}
			} else {
				triggerDocumentDownload(
					WaitingEntry{
						.crm = PendingEntry{
							.chatId = QString::number(
								item->history()->peer->id.value),
							.payload = buildPayload(item),
						},
						.document = doc,
						.origin = origin,
					},
					doc);
			}
		}
	} else if (media->photo()) {
		triggerPhotoDownload(
			WaitingEntry{
				.crm = PendingEntry{
					.chatId = QString::number(
						item->history()->peer->id.value),
					.payload = buildPayload(item),
				},
				.origin = origin,
			},
			item);
	} else {
		send(PendingEntry{
			.chatId = QString::number(item->history()->peer->id.value),
			.payload = buildPayload(item),
		});
	}
}

void CrmForwarder::triggerDocumentDownload(
		WaitingEntry entry,
		not_null<DocumentData*> doc) {
	QDir().mkpath(tempDir());
	const auto tempPath = tempDir()
		+ QString::number(QDateTime::currentMSecsSinceEpoch())
		+ u"_"_q
		+ doc->filename();
	doc->save(entry.origin, tempPath);
	entry.tempFilePath = tempPath;
	_waiting.push_back(std::move(entry));
}

void CrmForwarder::triggerPhotoDownload(
		WaitingEntry entry,
		not_null<HistoryItem*> item) {
	const auto photo = item->media()->photo();
	auto mediaView = photo->createMediaView();
	QDir().mkpath(tempDir());
	const auto tempPath = tempDir()
		+ QString::number(QDateTime::currentMSecsSinceEpoch())
		+ u".jpg"_q;
	mediaView->wanted(Data::PhotoSize::Large, entry.origin);
	entry.photoMedia = std::move(mediaView);
	entry.tempFilePath = tempPath;
	entry.itemId = item->fullId();
	_waiting.push_back(std::move(entry));
}

void CrmForwarder::checkWaitingDownloads() {
	auto remaining = std::vector<WaitingEntry>{};
	for (auto &w : _waiting) {
		auto resolved = false;
		if (w.document) {
			const auto path = w.document->filepath();
			if (!path.isEmpty()) {
				w.crm.mediaFilePath = path;
				resolved = true;
			}
		} else if (w.photoMedia) {
			if (w.photoMedia->loaded()) {
				if (w.photoMedia->saveToFile(w.tempFilePath)) {
					w.crm.mediaFilePath = w.tempFilePath;
				}
				resolved = true;
			} else {
				// Retry wanted() — the photo may have gained a valid server
				// location since the last check (e.g. after itemIdChanged).
				w.photoMedia->wanted(Data::PhotoSize::Large, w.origin);
			}
		}

		if (resolved) {
			send(std::move(w.crm));
		} else if (++w.attempts >= kMaxWaitingAttempts) {
			// Media never became available (e.g. user blocked, no access).
			// Send the metadata-only payload instead of waiting forever.
			send(std::move(w.crm));
		} else {
			remaining.push_back(std::move(w));
		}
	}
	_waiting = std::move(remaining);
}

void CrmForwarder::onItemEdited(not_null<HistoryItem*> item) {
	if (_endpoint.isEmpty() || _bearerToken.isEmpty()) {
		return;
	}
	send(PendingEntry{
		.chatId = QString::number(item->history()->peer->id.value),
		.payload = buildPayload(item, u"edit"_q),
	});
}

void CrmForwarder::onItemDeleted(not_null<const HistoryItem*> item) {
	if (_endpoint.isEmpty() || _bearerToken.isEmpty()) {
		return;
	}
	const auto peer = item->history()->peer;
	const auto payload = QJsonDocument(QJsonObject{
		{ u"action"_q, u"delete"_q },
		{ u"chat_id"_q, QString::number(peer->id.value) },
		{ u"message_id"_q, item->id.bare },
		{ u"date"_q, qint64(item->date()) },
	}).toJson(QJsonDocument::Compact);
	send(PendingEntry{
		.chatId = QString::number(peer->id.value),
		.payload = payload,
	});
}

QByteArray CrmForwarder::buildPayload(
		not_null<HistoryItem*> item,
		const QString &action) const {
	const auto from = item->from();
	const auto peer = item->history()->peer;

	const auto cachedPhone = [&](PeerData *p) -> QString {
		if (!p) return {};
		const auto key = QString::number(p->id.value);
		return _phoneMap.value(key).toString();
	};
	const auto cachedUsername = [&](PeerData *p) -> QString {
		if (!p) return {};
		const auto key = QString::number(p->id.value);
		return _usernameMap.value(key).toString();
	};

	auto senderObj = QJsonObject{};
	senderObj[u"id"_q] = QString::number(from->id.value);
	if (const auto user = from->asUser()) {
		senderObj[u"first_name"_q] = user->firstName;
		senderObj[u"last_name"_q] = user->lastName;
		const auto uname = user->username().isEmpty()
			? cachedUsername(user)
			: user->username();
		senderObj[u"username"_q] = uname;
		const auto phone = user->phone().isEmpty()
			? cachedPhone(user)
			: user->phone();
		senderObj[u"phone"_q] = phone;
	} else {
		senderObj[u"name"_q] = from->name();
	}

	// Outgoing: recipient is the peer. Incoming: recipient is the local user.
	const auto recipientUser = item->out()
		? peer->asUser()
		: _session->user().get();
	auto recipientObj = QJsonObject{};
	if (recipientUser) {
		const auto rUname = recipientUser->username().isEmpty()
			? cachedUsername(recipientUser)
			: recipientUser->username();
		recipientObj[u"username"_q] = rUname;
		const auto rPhone = recipientUser->phone().isEmpty()
			? cachedPhone(recipientUser)
			: recipientUser->phone();
		recipientObj[u"phone"_q] = rPhone;
	}

	// reply_to_message_id
	const auto replyToId = item->replyToId();
	const auto replyToIdValue = replyToId
		? QJsonValue(replyToId.bare)
		: QJsonValue(QJsonValue::Null);

	// forward_from
	auto forwardObj = QJsonValue(QJsonValue::Null);
	if (const auto fwd = item->Get<HistoryMessageForwarded>()) {
		auto f = QJsonObject{};
		f[u"date"_q] = qint64(fwd->originalDate);
		if (fwd->originalSender) {
			f[u"sender_id"_q] = QString::number(
				fwd->originalSender->id.value);
			if (const auto user = fwd->originalSender->asUser()) {
				f[u"sender_name"_q] = user->firstName + ' ' + user->lastName;
				f[u"sender_username"_q] = user->username();
			} else {
				f[u"sender_name"_q] = fwd->originalSender->name();
			}
		} else if (fwd->originalHiddenSenderInfo) {
			f[u"sender_name"_q] = fwd->originalHiddenSenderInfo->name;
		}
		if (fwd->originalId) {
			f[u"original_message_id"_q] = fwd->originalId.bare;
		}
		if (!fwd->originalPostAuthor.isEmpty()) {
			f[u"post_author"_q] = fwd->originalPostAuthor;
		}
		forwardObj = f;
	}

	auto mediaObj = QJsonValue(QJsonValue::Null);
	if (const auto media = item->media()) {
		auto m = QJsonObject{};
		if (media->photo()) {
			m[u"type"_q] = u"photo"_q;
		} else if (const auto doc = media->document()) {
			if (doc->isVoiceMessage()) {
				m[u"type"_q] = u"voice"_q;
			} else if (doc->isSong()) {
				m[u"type"_q] = u"audio"_q;
			} else if (doc->isVideoMessage()) {
				m[u"type"_q] = u"video_note"_q;
			} else {
				m[u"type"_q] = u"document"_q;
				m[u"file_name"_q] = doc->filename();
			}
		}
		if (!m.isEmpty()) {
			mediaObj = m;
		}
	}

	const auto timezone = QString::fromLatin1(
		QTimeZone::systemTimeZone().id());

	return QJsonDocument(QJsonObject{
		{ u"action"_q, action },
		{ u"direction"_q, item->out() ? u"outgoing"_q : u"incoming"_q },
		{ u"chat_id"_q, QString::number(peer->id.value) },
		{ u"message_id"_q, item->id.bare },
		{ u"date"_q, qint64(item->date()) },
		{ u"timezone"_q, timezone },
		{ u"sender"_q, senderObj },
		{ u"recipient"_q, recipientObj },
		{ u"text"_q, item->originalText().text },
		{ u"media"_q, mediaObj },
		{ u"reply_to_message_id"_q, replyToIdValue },
		{ u"forward_from"_q, forwardObj },
	}).toJson(QJsonDocument::Compact);
}

void CrmForwarder::send(PendingEntry entry) {
	auto request = QNetworkRequest(QUrl(_endpoint));
	request.setRawHeader(
		"Authorization",
		("Bearer " + _bearerToken).toUtf8());

	QNetworkReply *reply = nullptr;

	const auto hasFile = !entry.mediaFilePath.isEmpty()
		&& QFileInfo::exists(entry.mediaFilePath);

	if (hasFile) {
		const auto fileName = QFileInfo(entry.mediaFilePath).fileName();

		// Inject file_name into media object so the JSON payload is
		// self-contained (useful for retries and server-side logging).
		const auto payloadBytes = [&]() -> QByteArray {
			auto doc = QJsonDocument::fromJson(entry.payload);
			if (!doc.isObject()) {
				return entry.payload;
			}
			auto root = doc.object();
			auto media = root.value(u"media"_q).toObject();
			if (!media.isEmpty() && media.value(u"file_name"_q).toString().isEmpty()) {
				media[u"file_name"_q] = fileName;
				root[u"media"_q] = media;
			}
			return QJsonDocument(root).toJson(QJsonDocument::Compact);
		}();

		const auto mediaType = QJsonDocument::fromJson(payloadBytes)
			.object()
			.value(u"media"_q)
			.toObject()
			.value(u"type"_q)
			.toString();

		auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

		QHttpPart payloadPart;
		payloadPart.setHeader(
			QNetworkRequest::ContentDispositionHeader,
			QVariant(u"form-data; name=\"payload\""_q));
		payloadPart.setHeader(
			QNetworkRequest::ContentTypeHeader,
			QVariant(u"application/json"_q));
		payloadPart.setBody(payloadBytes);
		multipart->append(payloadPart);

		auto *file = new QFile(entry.mediaFilePath, multipart);
		if (file->open(QIODevice::ReadOnly)) {
			QHttpPart filePart;
			filePart.setHeader(
				QNetworkRequest::ContentDispositionHeader,
				QVariant(
					u"form-data; name=\"file\"; filename=\"%1\""_q
					.arg(fileName)));
			filePart.setHeader(
				QNetworkRequest::ContentTypeHeader,
				QVariant(QString::fromLatin1(
					mimeTypeForMediaType(mediaType))));
			filePart.setBodyDevice(file);
			multipart->append(filePart);
		}

		reply = _network.post(request, multipart);
		multipart->setParent(reply);
	} else {
		request.setHeader(
			QNetworkRequest::ContentTypeHeader,
			QByteArray("application/json"));
		reply = _network.post(request, entry.payload);
	}

	_activeReplies.push_back(reply);
	QObject::connect(
		reply,
		&QNetworkReply::finished,
		crl::guard(this, [=, entry = std::move(entry)]() mutable {
			onReplyFinished(reply, std::move(entry));
		}));
}

void CrmForwarder::onReplyFinished(
		QNetworkReply *reply,
		PendingEntry entry) {
	_activeReplies.erase(
		std::remove(_activeReplies.begin(), _activeReplies.end(), reply),
		_activeReplies.end());

	const auto error = reply->error();
	reply->deleteLater();

	if (error == QNetworkReply::NoError) {
		if (!entry.logFilePath.isEmpty()) {
			QFile::remove(entry.logFilePath);
		}
		// Clean up temp files created for photos.
		if (entry.mediaFilePath.contains(u"/crm_temp/"_q)) {
			QFile::remove(entry.mediaFilePath);
		}
		return;
	}

	if (entry.retries >= kMaxRetries) {
		if (!entry.logFilePath.isEmpty()) {
			QFile::remove(entry.logFilePath);
		}
		return;
	}
	if (!entry.logFilePath.isEmpty()) {
		QFile::remove(entry.logFilePath);
	}
	++entry.retries;
	entry.logFilePath = saveToDisk(entry);
	_pending.push_back(std::move(entry));
	if (!_retryTimer.isActive()) {
		_retryTimer.callEach(kRetryInterval);
	}
}

QString CrmForwarder::saveToDisk(const PendingEntry &entry) const {
	const auto dir = pendingDir() + entry.chatId + '/';
	QDir().mkpath(dir);

	const auto path = dir
		+ QString::number(QDateTime::currentMSecsSinceEpoch())
		+ u".json"_q;
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return {};
	}
	file.write(QJsonDocument(QJsonObject{
		{ u"chat_id"_q, entry.chatId },
		{ u"retries"_q, entry.retries },
		{ u"payload"_q, QString::fromUtf8(entry.payload) },
		{ u"media_file_path"_q, entry.mediaFilePath },
	}).toJson(QJsonDocument::Compact));
	return path;
}

void CrmForwarder::loadFromDisk() {
	QDirIterator chatDirs(
		pendingDir(),
		QDir::Dirs | QDir::NoDotAndDotDot);
	while (chatDirs.hasNext()) {
		const auto chatDir = chatDirs.next();
		QDirIterator files(
			chatDir,
			{ u"*.json"_q },
			QDir::Files);
		while (files.hasNext()) {
			QFile file(files.next());
			if (!file.open(QIODevice::ReadOnly)) {
				continue;
			}
			const auto doc = QJsonDocument::fromJson(file.readAll());
			if (!doc.isObject()) {
				continue;
			}
			const auto obj = doc.object();
			_pending.push_back(PendingEntry{
				.chatId = obj.value(u"chat_id"_q).toString(),
				.payload = obj.value(u"payload"_q).toString().toUtf8(),
				.mediaFilePath = obj.value(u"media_file_path"_q).toString(),
				.retries = obj.value(u"retries"_q).toInt(),
				.logFilePath = file.fileName(),
			});
		}
	}
}

void CrmForwarder::retryAll() {
	if (_endpoint.isEmpty() || _bearerToken.isEmpty()) {
		_retryTimer.cancel();
		return;
	}
	auto toRetry = std::exchange(_pending, {});
	for (auto &entry : toRetry) {
		send(std::move(entry));
	}
	if (_pending.empty() && _activeReplies.empty()) {
		_retryTimer.cancel();
	}
}

} // namespace Api
