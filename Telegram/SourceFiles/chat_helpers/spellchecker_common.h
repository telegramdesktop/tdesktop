/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifndef TDESKTOP_DISABLE_SPELLCHECK

#include "storage/storage_cloud_blob.h"
#include "base/unique_qptr.h"

namespace Main {
class Session;
} // namespace Main

namespace Spellchecker {

struct Dict : public Storage::CloudBlob::Blob {
};

int64 GetDownloadSize(int id);
MTP::DedicatedLoader::Location GetDownloadLocation(int id);

[[nodiscard]] QString DictionariesPath();
[[nodiscard]] QString DictPathByLangId(int langId);
bool UnpackDictionary(const QString &path, int langId);
[[nodiscard]] bool DictionaryExists(int langId);
bool RemoveDictionary(int langId);
[[nodiscard]] bool IsEn(int langId);

bool WriteDefaultDictionary();
std::vector<Dict> Dictionaries();

void Start(not_null<Main::Session*> session);
[[nodiscard]] rpl::producer<QString> ButtonManageDictsState(
	not_null<Main::Session*> session);

std::vector<int> DefaultLanguages();

class DictLoader : public Storage::CloudBlob::BlobLoader {
public:
	DictLoader(
		QObject *parent,
		not_null<Main::Session*> session,
		int id,
		MTP::DedicatedLoader::Location location,
		const QString &folder,
		int64 size,
		Fn<void()> destroyCallback);

	void destroy() override;

	rpl::lifetime &lifetime() {
		return _lifetime;
	}

private:
	void unpack(const QString &path) override;
	void fail() override;

	// Be sure to always call it in the main thread.
	Fn<void()> _destroyCallback;

	rpl::lifetime _lifetime;

};

std::shared_ptr<base::unique_qptr<DictLoader>> GlobalLoader();
rpl::producer<int> GlobalLoaderChanged();

} // namespace Spellchecker

#endif // !TDESKTOP_DISABLE_SPELLCHECK
