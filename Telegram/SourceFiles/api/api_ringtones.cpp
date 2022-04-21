/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_ringtones.h"

#include "api/api_toggling_media.h"
#include "apiwrap.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/notify/data_notify_settings.h"
#include "main/main_account.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "storage/file_upload.h"
#include "storage/localimageloader.h"

namespace Api {
namespace {

SendMediaReady PrepareRingtoneDocument(
		MTP::DcId dcId,
		const QString &filename,
		const QString &filemime,
		const QByteArray &content) {
	auto attributes = QVector<MTPDocumentAttribute>(
		1,
		MTP_documentAttributeFilename(MTP_string(filename)));
	const auto id = base::RandomValue<DocumentId>();
	const auto document = MTP_document(
		MTP_flags(0),
		MTP_long(id),
		MTP_long(0),
		MTP_bytes(),
		MTP_int(base::unixtime::now()),
		MTP_string(filemime),
		MTP_int(content.size()),
		MTP_vector<MTPPhotoSize>(),
		MTPVector<MTPVideoSize>(),
		MTP_int(dcId),
		MTP_vector<MTPDocumentAttribute>(std::move(attributes)));

	return SendMediaReady(
		SendMediaType::File,
		QString(), // filepath
		filename,
		content.size(),
		content,
		id,
		0,
		QString(),
		PeerId(),
		MTP_photoEmpty(MTP_long(0)),
		PreparedPhotoThumbs(),
		document,
		QByteArray(),
		0);
}

} // namespace

Ringtones::Ringtones(not_null<ApiWrap*> api)
: _session(&api->session())
, _api(&api->instance()) {
	crl::on_main(_session, [=] {
		// You can't use _session->lifetime() in the constructor,
		// only queued, because it is not constructed yet.
		_session->uploader().documentReady(
		) | rpl::start_with_next([=](const Storage::UploadedMedia &data) {
			ready(data.fullId, data.info.file);
		}, _session->lifetime());
	});
}

void Ringtones::upload(
		const QString &filename,
		const QString &filemime,
		const QByteArray &content) {
	const auto ready = PrepareRingtoneDocument(
		_api.instance().mainDcId(),
		filename,
		filemime,
		content);

	const auto uploadedData = UploadedData{ filename, filemime, content };
	const auto fakeId = FullMsgId(
		_session->userPeerId(),
		_session->data().nextLocalMessageId());
	const auto already = ranges::find_if(
		_uploads,
		[&](const auto &d) {
			return uploadedData.filemime == d.second.filemime
				&& uploadedData.filename == d.second.filename;
		});
	if (already != end(_uploads)) {
		_session->uploader().cancel(already->first);
		_uploads.erase(already);
	}
	_uploads.emplace(fakeId, uploadedData);
	_session->uploader().uploadMedia(fakeId, ready);
}

void Ringtones::ready(const FullMsgId &msgId, const MTPInputFile &file) {
	const auto maybeUploadedData = _uploads.take(msgId);
	if (!maybeUploadedData) {
		return;
	}
	const auto uploadedData = *maybeUploadedData;
	_api.request(MTPaccount_UploadRingtone(
		file,
		MTP_string(uploadedData.filename),
		MTP_string(uploadedData.filemime)
	)).done([=, content = uploadedData.content](const MTPDocument &result) {
		const auto document = _session->data().processDocument(result);
		_list.documents.insert(_list.documents.begin(), document->id);
		const auto media = document->createMediaView();
		media->setBytes(content);
		document->owner().notifySettings().cacheSound(document);
		_uploadDones.fire_copy(document->id);
	}).fail([=](const MTP::Error &error) {
		_uploadFails.fire_copy(error.type());
	}).send();
}

void Ringtones::requestList() {
	if (_list.requestId) {
		return;
	}
	_list.requestId = _api.request(
		MTPaccount_GetSavedRingtones(MTP_long(_list.hash))
	).done([=](const MTPaccount_SavedRingtones &result) {
		_list.requestId = 0;
		result.match([&](const MTPDaccount_savedRingtones &data) {
			_list.hash = data.vhash().v;
			_list.documents.clear();
			_list.documents.reserve(data.vringtones().v.size());
			for (const auto &d : data.vringtones().v) {
				const auto document = _session->data().processDocument(d);
				document->forceToCache(true);
				_list.documents.emplace_back(document->id);
			}
			_list.updates.fire({});
		}, [&](const MTPDaccount_savedRingtonesNotModified &) {
		});
	}).fail([=] {
		_list.requestId = 0;
	}).send();
}

const Ringtones::Ids &Ringtones::list() const {
	return _list.documents;
}

rpl::producer<> Ringtones::listUpdates() const {
	return _list.updates.events();
}

rpl::producer<QString> Ringtones::uploadFails() const {
	return _uploadFails.events();
}

rpl::producer<DocumentId> Ringtones::uploadDones() const {
	return _uploadDones.events();
}

void Ringtones::applyUpdate() {
	_list.hash = 0;
	_list.documents.clear();
	requestList();
}

void Ringtones::remove(DocumentId id) {
	if (const auto document = _session->data().document(id)) {
		ToggleSavedRingtone(
			document,
			Data::FileOriginRingtones(),
			crl::guard(&document->session(), [=] {
				const auto it = ranges::find(_list.documents, id);
				if (it != end(_list.documents)) {
					_list.documents.erase(it);
				}
			}),
			false);
	}
}

int Ringtones::maxSize() const {
	return int(base::SafeRound(_session->account().appConfig().get<double>(
		"ringtone_size_max",
		100 * 1024)));
}

int Ringtones::maxSavedCount() const {
	return int(base::SafeRound(_session->account().appConfig().get<double>(
		"ringtone_saved_count_max",
		100)));
}

int Ringtones::maxDuration() const {
	return int(base::SafeRound(_session->account().appConfig().get<double>(
		"ringtone_duration_max",
		5)));
}

} // namespace Api
