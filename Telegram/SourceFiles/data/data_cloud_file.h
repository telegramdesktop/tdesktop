/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "ui/image/image_location.h"

class FileLoader;

namespace Storage {
namespace Cache {
class Database;
} // namespace Cache
} // namespace Storage

namespace Data {

struct FileOrigin;

struct CloudFile final {
	enum class Flag : uchar {
		Cancelled = 0x01,
		Failed = 0x02,
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; };

	ImageLocation location;
	std::unique_ptr<FileLoader> loader;
	int byteSize = 0;
	base::flags<Flag> flags;
};

void UpdateCloudFile(
	CloudFile &file,
	const ImageWithLocation &data,
	Storage::Cache::Database &cache,
	Fn<void(FileOrigin)> restartLoader,
	Fn<void(QImage)> usePreloaded = nullptr);

} // namespace Data
