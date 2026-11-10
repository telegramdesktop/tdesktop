/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "storage/storage_account.h"
#include "tgw/tgw_storage.h"

namespace Main {
class Account;
} // namespace Main

namespace Tgw {

struct StorageMeta {
	TimeId date = 0;
	TimeId expires = 0;
	MsgId topicRootId = 0;
	PeerId sublistPeerId = 0;
	PeerId from = 0;

	friend inline bool operator==(
		const StorageMeta &,
		const StorageMeta &) = default;
};

[[nodiscard]] QString StorageFolder(not_null<Main::Account*> account);
void EraseStorageFolder(const QString &folder);

class StorageDisk final {
public:
	using Area = std::map<std::string, std::string>;

	StorageDisk(QString folder, MTP::AuthKeyPtr key, uint64 owner);

	[[nodiscard]] bool empty() const;
	[[nodiscard]] std::optional<StorageMeta> lookup(FullMsgId key) const;
	[[nodiscard]] std::optional<Area> read(FullMsgId key);
	[[nodiscard]] std::optional<HeightRecord> height(FullMsgId key) const;
	[[nodiscard]] TimeId nextExpiry() const;

	bool write(FullMsgId key, const StorageMeta &meta, const Area &local);
	bool setHeight(
		FullMsgId key,
		const StorageMeta &meta,
		HeightRecord height);
	bool refresh(FullMsgId key, const StorageMeta &meta);
	bool refresh(Fn<std::optional<StorageMeta>(FullMsgId)> current);
	bool remove(Fn<bool(FullMsgId, const StorageMeta &)> matches);
	void flush();
	void erase();

private:
	struct Entry {
		Storage::FileKey file = 0;
		StorageMeta meta;
		HeightRecord height;
	};

	[[nodiscard]] bool readIndex();
	void cleanup(const QStringList &names);
	void writeIndex();
	void writeRecord(Storage::FileKey file, const Area &local);
	[[nodiscard]] Storage::FileKey generateFile() const;

	const QString _folder;
	const MTP::AuthKeyPtr _key;
	const uint64 _owner = 0;
	base::flat_map<FullMsgId, Entry> _entries;
	bool _indexDirty = false;

};

} // namespace Tgw
