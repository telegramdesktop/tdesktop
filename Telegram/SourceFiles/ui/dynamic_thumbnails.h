/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class DocumentData;
class PeerData;
class PhotoData;

namespace Main {
class Session;
} // namespace Main

namespace Data {
class CloudImage;
class Story;
class Session;
struct FileOrigin;
} // namespace Data

namespace Ui {

class DynamicImage;

[[nodiscard]] std::shared_ptr<DynamicImage> MakeUserpicThumbnail(
	not_null<PeerData*> peer,
	bool forceRound = false);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeSavedMessagesThumbnail();
[[nodiscard]] std::shared_ptr<DynamicImage> MakeRepliesThumbnail();
[[nodiscard]] std::shared_ptr<DynamicImage> MakeHiddenAuthorThumbnail();
[[nodiscard]] std::shared_ptr<DynamicImage> MakeStoryThumbnail(
	not_null<Data::Story*> story);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeIconThumbnail(
	const style::icon &icon);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeEmojiThumbnail(
	not_null<Data::Session*> owner,
	const QString &data,
	Fn<bool()> paused = nullptr,
	Fn<QColor()> textColor = nullptr,
	int loopLimit = 0);
[[nodiscard]] std::shared_ptr<DynamicImage> MakePhotoThumbnail(
	not_null<PhotoData*> photo,
	FullMsgId fullId);
[[nodiscard]] std::shared_ptr<DynamicImage> MakePhotoThumbnailCenterCrop(
	not_null<PhotoData*> photo,
	FullMsgId fullId);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeDocumentThumbnail(
	not_null<DocumentData*> document,
	FullMsgId fullId);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeDocumentThumbnail(
	not_null<DocumentData*> document,
	Data::FileOrigin origin);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeDocumentThumbnailFit(
	not_null<DocumentData*> document,
	Data::FileOrigin origin);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeDocumentThumbnailCenterCrop(
	not_null<DocumentData*> document,
	FullMsgId fullId);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeDocumentFilePreviewThumbnail(
	not_null<DocumentData*> document,
	FullMsgId fullId);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeGeoThumbnail(
	not_null<Data::CloudImage*> data,
	not_null<Main::Session*> session,
	Data::FileOrigin origin);
[[nodiscard]] std::shared_ptr<DynamicImage> MakeGeoThumbnailWithPin(
	not_null<Data::CloudImage*> data,
	not_null<Main::Session*> session,
	Data::FileOrigin origin);

} // namespace Ui
