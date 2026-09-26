/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/chat/attach/attach_send_files_way.h"

namespace Ui {
namespace {

constexpr auto kSerializedSendLargePhotos = int32(4);

} // namespace

void SendFilesWay::setSendImagesAsPhotos(bool value) {
	if (value) {
		_flags |= Flag::SendImagesAsPhotos;
	} else {
		if (hasCompressedStickers()) {
			setGroupFiles(false);
		}
		_flags &= ~Flag::SendImagesAsPhotos;
	}
}

void SendFilesWay::setGroupFiles(bool value) {
	if (value) {
		_flags |= Flag::GroupFiles;
		if (hasCompressedStickers()) {
			setSendImagesAsPhotos(true);
		}
	} else {
		_flags &= ~Flag::GroupFiles;
	}
}

void SendFilesWay::setSendLargePhotos(bool value) {
	if (value) {
		_flags |= Flag::SendLargePhotos;
	} else {
		_flags &= ~Flag::SendLargePhotos;
	}
}

void SendFilesWay::setHasCompressedStickers(bool value) {
	if (value) {
		_flags |= Flag::HasCompressedStickers;
	} else {
		_flags &= ~Flag::HasCompressedStickers;
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
	if (sendLargePhotos()) {
		result |= kSerializedSendLargePhotos;
	}
	return result;
}

std::optional<SendFilesWay> SendFilesWay::FromSerialized(int32 value) {
	if (value < 0 || value > 7) {
		return std::nullopt;
	}
	const auto sendLargePhotos = (value & kSerializedSendLargePhotos) != 0;
	value &= ~kSerializedSendLargePhotos;
	auto result = SendFilesWay();
	result.setGroupFiles((value == 0) || (value == 3));
	result.setSendImagesAsPhotos((value == 0) || (value == 1));
	result.setSendLargePhotos(sendLargePhotos);
	return result;
}

} // namespace Ui
