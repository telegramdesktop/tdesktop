/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_send_files_way.h"

namespace Ui {

void SendFilesWay::setGroupMediaInAlbums(bool value) {
	if (value) {
		_flags |= (Flag::GroupMediaInAlbums | Flag::SendImagesAsPhotos);
	} else {
		_flags &= ~Flag::GroupMediaInAlbums;
	}
}

void SendFilesWay::setSendImagesAsPhotos(bool value) {
	if (value) {
		_flags |= Flag::SendImagesAsPhotos;
	} else {
		_flags &= ~(Flag::SendImagesAsPhotos | Flag::GroupMediaInAlbums);
	}
}

void SendFilesWay::setGroupFiles(bool value) {
	if (value) {
		_flags |= Flag::GroupFiles;
	} else {
		_flags &= ~Flag::GroupFiles;
	}
}

//enum class SendFilesWay { // Old way. Serialize should be compatible.
//	Album,
//	Photos,
//	Files,
//};

int32 SendFilesWay::serialize() const {
	auto result = groupMediaInAlbums()
		? int32(0)
		: sendImagesAsPhotos()
		? int32(1)
		: int32(2);
	if (!groupFiles()) {
		result |= 0x04;
	}
	return result;
}

std::optional<SendFilesWay> SendFilesWay::FromSerialized(int32 value) {
	auto result = SendFilesWay();
	result.setGroupFiles(!(value & 0x04));
	value &= ~0x04;
	switch (value) {
	case 0:
		result.setGroupMediaInAlbums(true);
		result.setSendImagesAsPhotos(true);
		break;
	case 1:
		result.setGroupMediaInAlbums(false);
		result.setSendImagesAsPhotos(true);
		break;
	case 2:
		result.setGroupMediaInAlbums(false);
		result.setSendImagesAsPhotos(false);
		break;
	default: return std::nullopt;
	}
	return result;
}

} // namespace Ui
