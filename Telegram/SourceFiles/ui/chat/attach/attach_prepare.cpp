/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_prepare.h"

#include "ui/chat/attach/attach_send_files_way.h"
#include "ui/image/image_prepare.h"
#include "ui/ui_utility.h"
#include "core/mime_type.h"

namespace Ui {
namespace {

constexpr auto kMaxAlbumCount = 10;

} // namespace

PreparedFile::PreparedFile(const QString &path) : path(path) {
}

PreparedFile::PreparedFile(PreparedFile &&other) = default;

PreparedFile &PreparedFile::operator=(PreparedFile &&other) = default;

PreparedFile::~PreparedFile() = default;

bool PreparedFile::canBeInAlbumType(AlbumType album) const {
	return CanBeInAlbumType(type, album);
}

AlbumType PreparedFile::albumType(bool sendImagesAsPhotos) const {
	switch (type) {
	case Type::Photo:
		return sendImagesAsPhotos ? AlbumType::PhotoVideo : AlbumType::File;
	case Type::Video:
		return AlbumType::PhotoVideo;
	case Type::Music:
		return AlbumType::Music;
	case Type::File:
		return AlbumType::File;
	case Type::None:
		return AlbumType::None;
	}
	Unexpected("PreparedFile::type in PreparedFile::albumType().");
}

bool CanBeInAlbumType(PreparedFile::Type type, AlbumType album) {
	Expects(album != AlbumType::None);

	using Type = PreparedFile::Type;
	switch (album) {
	case AlbumType::PhotoVideo:
		return (type == Type::Photo) || (type == Type::Video);
	case AlbumType::Music:
		return (type == Type::Music);
	case AlbumType::File:
		return (type == Type::Photo) || (type == Type::File);
	}
	Unexpected("AlbumType in CanBeInAlbumType.");
}

PreparedList PreparedList::Reordered(
		PreparedList &&list,
		std::vector<int> order) {
	Expects(list.error == PreparedList::Error::None);
	Expects(list.files.size() == order.size());

	auto result = PreparedList(list.error, list.errorData);
	result.files.reserve(list.files.size());
	for (auto index : order) {
		result.files.push_back(std::move(list.files[index]));
	}
	return result;
}

void PreparedList::mergeToEnd(PreparedList &&other, bool cutToAlbumSize) {
	if (error != Error::None) {
		return;
	}
	if (other.error != Error::None) {
		error = other.error;
		errorData = other.errorData;
		return;
	}
	files.reserve(std::min(
		size_t(cutToAlbumSize ? kMaxAlbumCount : INT_MAX),
		files.size() + other.files.size()));
	for (auto &file : other.files) {
		if (cutToAlbumSize && files.size() == kMaxAlbumCount) {
			break;
		}
		files.push_back(std::move(file));
	}
}

bool PreparedList::canBeSentInSlowmode() const {
	return canBeSentInSlowmodeWith(PreparedList());
}

bool PreparedList::canBeSentInSlowmodeWith(const PreparedList &other) const {
	if (!filesToProcess.empty() || !other.filesToProcess.empty()) {
		return false;
	} else if (files.size() + other.files.size() < 2) {
		return true;
	} else if (files.size() + other.files.size() > kMaxAlbumCount) {
		return false;
	}

	using Type = PreparedFile::Type;
	auto &&all = ranges::views::concat(files, other.files);
	const auto has = [&](Type type) {
		return ranges::contains(all, type, &PreparedFile::type);
	};
	const auto hasNonGrouping = has(Type::None);
	const auto hasPhotos = has(Type::Photo);
	const auto hasFiles = has(Type::File);
	const auto hasVideos = has(Type::Video);
	const auto hasMusic = has(Type::Music);

	// File-s and Video-s never can be grouped.
	// Music-s can be grouped only with themselves.
	if (hasNonGrouping) {
		return false;
	} else if (hasFiles) {
		return !hasMusic && !hasVideos;
	} else if (hasVideos) {
		return !hasMusic && !hasFiles;
	} else if (hasMusic) {
		return !hasVideos && !hasFiles && !hasPhotos;
	}
	return !hasNonGrouping && (!hasFiles || !hasVideos);
}

bool PreparedList::canAddCaption(bool sendingAlbum) const {
	if (!filesToProcess.empty()
		|| files.empty()
		|| files.size() > kMaxAlbumCount) {
		return false;
	}
	if (files.size() == 1) {
		Assert(files.front().information != nullptr);
		const auto isSticker = Core::IsMimeSticker(
			files.front().information->filemime)
			|| files.front().path.endsWith(
				qstr(".tgs"),
				Qt::CaseInsensitive);
		return !isSticker;
	} else if (!sendingAlbum) {
		return false;
	}
	const auto hasFiles = ranges::contains(
		files,
		PreparedFile::Type::File,
		&PreparedFile::type);
	const auto hasMusic = ranges::contains(
		files,
		PreparedFile::Type::Music,
		&PreparedFile::type);
	const auto hasNotGrouped = ranges::contains(
		files,
		PreparedFile::Type::None,
		&PreparedFile::type);
	return !hasFiles && !hasMusic && !hasNotGrouped;
}

bool PreparedList::hasGroupOption(bool slowmode) const {
	if (slowmode || files.size() < 2) {
		return false;
	}
	using Type = PreparedFile::Type;
	auto lastType = Type::None;
	for (const auto &file : files) {
		if ((file.type == lastType)
			|| (file.type == Type::Video && lastType == Type::Photo)
			|| (file.type == Type::Photo && lastType == Type::Video)
			|| (file.type == Type::File && lastType == Type::Photo)
			|| (file.type == Type::Photo && lastType == Type::File)) {
			if (lastType != Type::None) {
				return true;
			}
		}
		lastType = file.type;
	}
	return false;
}

bool PreparedList::hasSendImagesAsPhotosOption(bool slowmode) const {
	using Type = PreparedFile::Type;
	return slowmode
		? ((files.size() == 1) && (files.front().type == Type::Photo))
		: ranges::contains(files, Type::Photo, &PreparedFile::type);
}

int MaxAlbumItems() {
	return kMaxAlbumCount;
}

bool ValidateThumbDimensions(int width, int height) {
	return (width > 0)
		&& (height > 0)
		&& (width < 20 * height)
		&& (height < 20 * width);
}

std::vector<PreparedGroup> DivideByGroups(
		PreparedList &&list,
		SendFilesWay way,
		bool slowmode) {
	const auto sendImagesAsPhotos = way.sendImagesAsPhotos();
	const auto groupFiles = way.groupFiles() || slowmode;

	auto group = Ui::PreparedList();

	using Type = Ui::PreparedFile::Type;
	auto groupType = AlbumType::None;

	auto result = std::vector<PreparedGroup>();
	auto pushGroup = [&] {
		const auto type = (group.files.size() > 1)
			? groupType
			: AlbumType::None;
		result.push_back(PreparedGroup{
			.list = base::take(group),
			.type = type,
		});
	};
	for (auto i = 0; i != list.files.size(); ++i) {
		auto &file = list.files[i];
		const auto fileGroupType = (file.type == Type::Music)
			? (groupFiles ? AlbumType::Music : AlbumType::None)
			: (file.type == Type::Video)
			? (groupFiles ? AlbumType::PhotoVideo : AlbumType::None)
			: (file.type == Type::Photo)
			? ((groupFiles && sendImagesAsPhotos)
				? AlbumType::PhotoVideo
				: (groupFiles && !sendImagesAsPhotos)
				? AlbumType::File
				: AlbumType::None)
			: (file.type == Type::File)
			? (groupFiles ? AlbumType::File : AlbumType::None)
			: AlbumType::None;
		if ((!group.files.empty() && groupType != fileGroupType)
			|| ((groupType != AlbumType::None)
				&& (group.files.size() == Ui::MaxAlbumItems()))) {
			pushGroup();
		}
		group.files.push_back(std::move(file));
		groupType = fileGroupType;
	}
	if (!group.files.empty()) {
		pushGroup();
	}
	return result;
}

QPixmap PrepareSongCoverForThumbnail(QImage image, int size) {
	const auto scaledSize = image.size().scaled(
		size,
		size,
		Qt::KeepAspectRatioByExpanding);
	using Option = Images::Option;
	return PixmapFromImage(Images::prepare(
		std::move(image),
		scaledSize.width() * style::DevicePixelRatio(),
		scaledSize.height() * style::DevicePixelRatio(),
		Option::Circled | Option::Colored | Option::Smooth,
		size,
		size,
		&st::songCoverOverlayFg));
}

} // namespace Ui
