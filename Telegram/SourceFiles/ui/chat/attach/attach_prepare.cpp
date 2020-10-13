/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_prepare.h"

#include "core/mime_type.h"

namespace Ui {
namespace {

constexpr auto kMaxAlbumCount = 10;

} // namespace

PreparedFile::PreparedFile(const QString &path) : path(path) {
}

PreparedFile::PreparedFile(PreparedFile &&other) = default;

PreparedFile &PreparedFile::operator=(PreparedFile && other) = default;

PreparedFile::~PreparedFile() = default;

PreparedList PreparedList::Reordered(
		PreparedList &&list,
		std::vector<int> order) {
	Expects(list.error == PreparedList::Error::None);
	Expects(list.files.size() == order.size());

	auto result = PreparedList(list.error, list.errorData);
	result.albumIsPossible = list.albumIsPossible;
	result.allFilesForCompress = list.allFilesForCompress;
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
	allFilesForCompress = allFilesForCompress && other.allFilesForCompress;
	files.reserve(std::min(
		size_t(cutToAlbumSize ? kMaxAlbumCount : INT_MAX),
		files.size() + other.files.size()));
	for (auto &file : other.files) {
		if (cutToAlbumSize && files.size() == kMaxAlbumCount) {
			break;
		}
		files.push_back(std::move(file));
	}
	if (files.size() > 1 && files.size() <= kMaxAlbumCount) {
		const auto badIt = ranges::find(
			files,
			PreparedFile::AlbumType::None,
			[](const PreparedFile &file) { return file.type; });
		albumIsPossible = (badIt == files.end());
	} else {
		albumIsPossible = false;
	}
}

bool PreparedList::canAddCaption(bool isAlbum, bool compressImages) const {
	const auto isSticker = [&] {
		if (files.empty()) {
			return false;
		}
		return Core::IsMimeSticker(files.front().mime)
			|| files.front().path.endsWith(
				qstr(".tgs"),
				Qt::CaseInsensitive);
	};
	return isAlbum || (files.size() == 1 && !isSticker());
}

int MaxAlbumItems() {
	return kMaxAlbumCount;
}

} // namespace Ui
