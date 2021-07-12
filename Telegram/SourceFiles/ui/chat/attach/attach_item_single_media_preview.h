/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/chat/attach/attach_abstract_single_media_preview.h"

namespace Data {
class DocumentMedia;
class PhotoMedia;
} // namespace Data

namespace Media {
namespace Streaming {
class Instance;
class Document;
struct Update;
enum class Error;
struct Information;
} // namespace Streaming
} // namespace Media

class HistoryItem;

namespace Ui {

class ItemSingleMediaPreview final : public AbstractSingleMediaPreview {
public:
	ItemSingleMediaPreview(
		QWidget *parent,
		Fn<bool()> gifPaused,
		not_null<HistoryItem*> item,
		AttachControls::Type type);

	std::shared_ptr<::Data::PhotoMedia> sharedPhotoMedia() const;

protected:
	bool drawBackground() const override;
	bool tryPaintAnimation(Painter &p) override;
	bool isAnimatedPreviewReady() const override;

private:
	void prepareStreamedPreview();
	void checkStreamedIsStarted();
	void setupStreamedPreview(
		std::shared_ptr<::Media::Streaming::Document> shared);
	void handleStreamingUpdate(::Media::Streaming::Update &&update);
	void handleStreamingError(::Media::Streaming::Error &&error);
	void streamingReady(::Media::Streaming::Information &&info);
	void startStreamedPlayer();

	const Fn<bool()> _gifPaused;
	const FullMsgId _fullId;

	std::shared_ptr<::Data::PhotoMedia> _photoMedia;
	std::shared_ptr<::Data::DocumentMedia> _documentMedia;

	std::unique_ptr<::Media::Streaming::Instance> _streamed;


	rpl::lifetime _lifetimeDownload;

};

} // namespace Ui
