/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Api {

void ToggleFavedSticker(
	not_null<DocumentData*> document,
	Data::FileOrigin origin);

void ToggleFavedSticker(
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	bool faved);

void ToggleRecentSticker(
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	bool saved);

void ToggleSavedGif(
	not_null<DocumentData*> document,
	Data::FileOrigin origin,
	bool saved);

} // namespace Api
