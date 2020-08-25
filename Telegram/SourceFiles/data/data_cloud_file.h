/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/flags.h"
#include "ui/image/image.h"
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

	~CloudFile();

	ImageLocation location;
	std::unique_ptr<FileLoader> loader;
	int byteSize = 0;
	int progressivePartSize = 0;
	base::flags<Flag> flags;
};

class CloudImageView final {
public:
	void set(not_null<Main::Session*> session, QImage image);

	[[nodiscard]] Image *image();

private:
	std::optional<Image> _image;

};

class CloudImage final {
public:
	CloudImage();
	CloudImage(
		not_null<Main::Session*> session,
		const ImageWithLocation &data);

	// This method will replace the location and zero the _view pointer.
	void set(
		not_null<Main::Session*> session,
		const ImageWithLocation &data);

	void update(
		not_null<Main::Session*> session,
		const ImageWithLocation &data);

	[[nodiscard]] bool empty() const;
	[[nodiscard]] bool loading() const;
	[[nodiscard]] bool failed() const;
	void load(not_null<Main::Session*> session, FileOrigin origin);
	[[nodiscard]] const ImageLocation &location() const;
	[[nodiscard]] int byteSize() const;

	[[nodiscard]] std::shared_ptr<CloudImageView> createView();
	[[nodiscard]] std::shared_ptr<CloudImageView> activeView();
	[[nodiscard]] bool isCurrentView(
		const std::shared_ptr<CloudImageView> &view) const;

private:
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
	not_null<Main::Session*> session,
	CloudFile &file,
	FileOrigin origin,
	LoadFromCloudSetting fromCloud,
	bool autoLoading,
	uint8 cacheTag,
	Fn<bool()> finalCheck,
	Fn<void(QImage)> done,
	Fn<void(bool)> fail = nullptr,
	Fn<void()> progress = nullptr,
	int downloadFrontPartSize = 0);

void LoadCloudFile(
	not_null<Main::Session*> session,
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
