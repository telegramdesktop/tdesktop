/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "storage/storage_cloud_blob.h"

#include "base/zlib_help.h"
#include "lang/lang_keys.h"
#include "ui/text/format_values.h"
#include "main/main_account.h"
#include "main/main_session.h"

namespace Storage::CloudBlob {

namespace {

QByteArray ReadFinalFile(const QString &path) {
	constexpr auto kMaxZipSize = 10 * 1024 * 1024;
	auto file = QFile(path);
	if (file.size() > kMaxZipSize || !file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	return file.readAll();
}

bool ExtractZipFile(zlib::FileToRead &zip, const QString path) {
	constexpr auto kMaxSize = 25 * 1024 * 1024;
	const auto content = zip.readCurrentFileContent(kMaxSize);
	if (content.isEmpty() || zip.error() != UNZ_OK) {
		return false;
	}
	auto file = QFile(path);
	return file.open(QIODevice::WriteOnly)
		&& (file.write(content) == content.size());
}

} // namespace

bool UnpackBlob(
		const QString &path,
		const QString &folder,
		Fn<bool(const QString &)> checkNameCallback) {
	const auto bytes = ReadFinalFile(path);
	if (bytes.isEmpty()) {
		return false;
	}
	auto zip = zlib::FileToRead(bytes);
	if (zip.goToFirstFile() != UNZ_OK) {
		return false;
	}
	do {
		const auto name = zip.getCurrentFileName();
		const auto path = folder + '/' + name;
		if (checkNameCallback(name) && !ExtractZipFile(zip, path)) {
			return false;
		}

		const auto jump = zip.goToNextFile();
		if (jump == UNZ_END_OF_LIST_OF_FILE) {
			break;
		} else if (jump != UNZ_OK) {
			return false;
		}
	} while (true);
	return true;
}

QString StateDescription(const BlobState &state, tr::phrase<> activeText) {
	return v::match(state, [](const Available &data) {
		return tr::lng_emoji_set_download(
			tr::now,
			lt_size,
			Ui::FormatSizeText(data.size));
	}, [](const Ready &data) -> QString {
		return tr::lng_emoji_set_ready(tr::now);
	}, [&](const Active &data) -> QString {
		return activeText(tr::now);
	}, [](const Loading &data) {
		const auto percent = (data.size > 0)
			? std::clamp((data.already * 100) / float64(data.size), 0., 100.)
			: 0.;
		return tr::lng_emoji_set_loading(
			tr::now,
			lt_percent,
			QString::number(int(std::round(percent))) + '%',
			lt_progress,
			Ui::FormatDownloadText(data.already, data.size));
	}, [](const Failed &data) {
		return tr::lng_attach_failed(tr::now);
	});
}

BlobLoader::BlobLoader(
	QObject *parent,
	not_null<Main::Session*> session,
	int id,
	MTP::DedicatedLoader::Location location,
	const QString &folder,
	int size)
: QObject(parent)
, _folder(folder)
, _id(id)
, _state(Loading{ 0, size })
, _mtproto(session.get()) {
	const auto ready = [=](std::unique_ptr<MTP::DedicatedLoader> loader) {
		if (loader) {
			setImplementation(std::move(loader));
		} else {
			fail();
		}
	};
	MTP::StartDedicatedLoader(&_mtproto, location, _folder, ready);
}

int BlobLoader::id() const {
	return _id;
}

rpl::producer<BlobState> BlobLoader::state() const {
	return _state.value();
}

void BlobLoader::setImplementation(
		std::unique_ptr<MTP::DedicatedLoader> loader) {
	_implementation = std::move(loader);
	_state = _implementation->progress(
	) | rpl::map([](const Loading &state) {
		return BlobState(state);
	});
	_implementation->failed(
	) | rpl::start_with_next([=] {
		fail();
	}, _implementation->lifetime());

	_implementation->ready(
	) | rpl::start_with_next([=](const QString &filepath) {
		unpack(filepath);
	}, _implementation->lifetime());

	QDir(_folder).removeRecursively();
	_implementation->start();
}

void BlobLoader::fail() {
	_state = Failed();
}

} // namespace Storage::CloudBlob
