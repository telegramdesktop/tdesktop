/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/clip/media_clip_reader.h"

class Painter;

namespace Data {
class PhotoMedia;
} // namespace Data

namespace Ui {
struct PeerUserpicView;
} // namespace Ui

namespace Dialogs {
class Entry;
} // namespace Dialogs

namespace Dialogs::Ui {

using namespace ::Ui;

struct PaintContext;

class VideoUserpic final {
public:
	VideoUserpic(not_null<PeerData*> peer, Fn<void()> repaint);
	~VideoUserpic();

	[[nodiscard]] int frameIndex() const;

	void paintLeft(
		Painter &p,
		PeerUserpicView &view,
		int x,
		int y,
		int w,
		int size,
		bool paused);

private:
	void clipCallback(Media::Clip::Notification notification);
	[[nodiscard]] Media::Clip::FrameRequest request(int size) const;
	bool startReady(int size = 0);

	const not_null<PeerData*> _peer;
	const Fn<void()> _repaint;

	Media::Clip::ReaderPointer _video;
	int _lastSize = 0;
	std::shared_ptr<Data::PhotoMedia> _videoPhotoMedia;
	PhotoId _videoPhotoId = 0;

};

void PaintUserpic(
	Painter &p,
	not_null<Entry*> entry,
	PeerData *peer,
	VideoUserpic *videoUserpic,
	PeerUserpicView &view,
	const Ui::PaintContext &context);

void PaintUserpic(
	Painter &p,
	not_null<PeerData*> peer,
	VideoUserpic *videoUserpic,
	PeerUserpicView &view,
	int x,
	int y,
	int outerWidth,
	int size,
	bool paused);

} // namespace Dialogs::Ui
