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

namespace Main {
class Session;
} // namespace Main

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

class CloudImageView final {
public:
	void set(not_null<Main::Session*> session, QImage image);

	[[nodiscard]] Image *image() const;

private:
	std::unique_ptr<Image> _image;

};

class CloudImage final {
public:
	explicit CloudImage(not_null<Main::Session*> session);

	void set(const ImageWithLocation &data);

	[[nodiscard]] bool empty() const;
	[[nodiscard]] bool loading() const;
	[[nodiscard]] bool failed() const;
	void load(FileOrigin origin);
	[[nodiscard]] const ImageLocation &location() const;
	[[nodiscard]] int byteSize() const;

	[[nodiscard]] std::shared_ptr<CloudImageView> createView();
	[[nodiscard]] std::shared_ptr<CloudImageView> activeView();

private:
	const not_null<Main::Session*> _session;
	CloudFile _file;
	std::weak_ptr<CloudImageView> _view;

};

void UpdateCloudFile(
	CloudFile &file,
	const ImageWithLocation &data,
	Storage::Cache::Database &cache,
	uint8 cacheTag,
	Fn<void(FileOrigin)> restartLoader,
	Fn<void(QImage)> usePreloaded = nullptr);

void LoadCloudFile(
	CloudFile &file,
	FileOrigin origin,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag,
	Fn<bool()> finalCheck,
	Fn<void(QImage)> done,
	Fn<void(bool)> fail = nullptr,
	Fn<void()> progress = nullptr);

void LoadCloudFile(
	CloudFile &file,
	FileOrigin origin,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag,
	Fn<bool()> finalCheck,
	Fn<void(QByteArray)> done,
	Fn<void(bool)> fail = nullptr,
	Fn<void()> progress = nullptr);

} // namespace Data
