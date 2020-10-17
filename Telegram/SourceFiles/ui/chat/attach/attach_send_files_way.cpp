/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_send_files_way.h"

namespace Ui {

void SendFilesWay::setSendImagesAsPhotos(bool value) {
	if (value) {
		_flags |= Flag::SendImagesAsPhotos;
	} else {
		_flags &= ~Flag::SendImagesAsPhotos;
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
	auto result = (sendImagesAsPhotos() && groupFiles())
		? int32(0)
		: sendImagesAsPhotos()
		? int32(1)
		: groupFiles()
		? int32(3)
		: int32(2);
	return result;
}

std::optional<SendFilesWay> SendFilesWay::FromSerialized(int32 value) {
	if (value < 0 || value > 3) {
		return std::nullopt;
	}
	auto result = SendFilesWay();
	result.setGroupFiles((value == 0) || (value == 3));
	result.setSendImagesAsPhotos((value == 0) || (value == 1));
	return result;
}

} // namespace Ui
