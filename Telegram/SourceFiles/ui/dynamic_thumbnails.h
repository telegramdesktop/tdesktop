/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PeerData;

namespace Data {
class Story;
class Session;
} // namespace Data

namespace Ui {

class DynamicImage;

[[nodiscard]] std::shared_ptr<DynamicImage> MakeUserpicThumbnail(
	not_null<PeerData*> peer,
	bool forceRound = false);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeSavedMessagesThumbnail();
[[nodiscard]] std::shared_ptr<DynamicImage> MakeRepliesThumbnail();
[[nodiscard]] std::shared_ptr<DynamicImage> MakeStoryThumbnail(
	not_null<Data::Story*> story);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeIconThumbnail(
	const style::icon &icon);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeEmojiThumbnail(
	not_null<Data::Session*> owner,
	const QString &data);

} // namespace Ui
