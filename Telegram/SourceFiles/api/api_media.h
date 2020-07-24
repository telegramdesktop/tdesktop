/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;

namespace Api {

MTPInputMedia PrepareUploadedPhoto(const MTPInputFile &file);

MTPInputMedia PrepareUploadedDocument(
	not_null<HistoryItem*> item,
	const MTPInputFile &file,
	const std::optional<MTPInputFile> &thumb);

} // namespace Api
