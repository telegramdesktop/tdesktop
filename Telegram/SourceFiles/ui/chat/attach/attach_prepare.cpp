/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_prepare.h"

#include "ui/chat/attach/attach_send_files_way.h"
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

	auto &&all = ranges::view::concat(files, other.files);
	const auto hasNonGrouping = ranges::contains(
		all,
		PreparedFile::AlbumType::None,
		&PreparedFile::type);
	const auto hasFiles = ranges::contains(
		all,
		PreparedFile::AlbumType::File,
		&PreparedFile::type);
	const auto hasVideos = ranges::contains(
		all,
		PreparedFile::AlbumType::Video,
		&PreparedFile::type);

	// File-s and Video-s never can be grouped.
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
		PreparedFile::AlbumType::File,
		&PreparedFile::type);
	const auto hasNotGrouped = ranges::contains(
		files,
		PreparedFile::AlbumType::None,
		&PreparedFile::type);
	return !hasFiles && !hasNotGrouped;
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

	// For groupType Type::Video means media album,
	// Type::File means file album,
	// Type::None means no grouping.
	using Type = Ui::PreparedFile::AlbumType;
	auto groupType = Type::None;

	auto result = std::vector<PreparedGroup>();
	auto pushGroup = [&] {
		result.push_back(PreparedGroup{
			.list = base::take(group),
			.grouped = (groupType != Type::None)
		});
	};
	for (auto i = 0; i != list.files.size(); ++i) {
		auto &file = list.files[i];
		const auto fileGroupType = (file.type == Type::Video)
			? (groupFiles ? Type::Video : Type::None)
			: (file.type == Type::Photo)
			? ((groupFiles && sendImagesAsPhotos)
				? Type::Video
				: (groupFiles && !sendImagesAsPhotos)
				? Type::File
				: Type::None)
			: (file.type == Type::File)
			? (groupFiles ? Type::File : Type::None)
			: Type::None;
		if ((!group.files.empty() && groupType != fileGroupType)
			|| ((groupType != Type::None)
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

} // namespace Ui
