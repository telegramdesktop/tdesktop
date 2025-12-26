/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media::Streaming {
class Instance;
struct Update;
struct Information;
enum class Error;
} // namespace Media::Streaming

class PhotoData;
class PeerData;

namespace Ui {

class VideoUserpicPlayer final {
public:
	VideoUserpicPlayer();
	~VideoUserpicPlayer();

	void setup(not_null<PeerData*> peer, not_null<PhotoData*> photo);
	void clear();

	[[nodiscard]] QImage frame(QSize size, not_null<PeerData*> peer);
	[[nodiscard]] bool ready() const;

private:
	bool createStreaming(
		not_null<PeerData*> peer,
		not_null<PhotoData*> photo);
	void checkStarted();
	void handleUpdate(::Media::Streaming::Update &&update);
	void handleError(::Media::Streaming::Error &&error);
	void streamingReady(::Media::Streaming::Information &&info);

	std::unique_ptr<::Media::Streaming::Instance> _streamed;
	PhotoData *_streamedPhoto = nullptr;
	QImage _ellipseMask;
	QImage _monoforumMask;
	std::array<QImage, 4> _roundingCorners;
	PeerData *_peer = nullptr;

};

} // namespace Ui
