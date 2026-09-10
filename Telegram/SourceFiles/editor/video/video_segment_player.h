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
} // namespace Media::Streaming

namespace Editor {

class SegmentPlayer final {
public:
	SegmentPlayer(QString path, QByteArray content, bool keepAlpha = false);
	~SegmentPlayer();

	void start();
	void stop();
	[[nodiscard]] bool valid() const;
	[[nodiscard]] bool ready() const;

	[[nodiscard]] crl::time from() const {
		return _from;
	}
	[[nodiscard]] crl::time till() const {
		return _till;
	}
	[[nodiscard]] crl::time position() const {
		return _position;
	}
	[[nodiscard]] bool held() const {
		return _paused || _seeking;
	}

	void setSegment(crl::time from, crl::time till);
	void restart(crl::time position);
	void setPaused(bool paused);
	void setSeeking(bool seeking);

	[[nodiscard]] QImage frame(QSize size);

	[[nodiscard]] rpl::producer<crl::time> positionUpdates() const {
		return _positionUpdates.events();
	}
	[[nodiscard]] rpl::producer<> repaints() const {
		return _repaints.events();
	}

private:
	void handleUpdate(Media::Streaming::Update &&update);
	void applyHeld();
	void keepLastFrame();
	[[nodiscard]] crl::time segmentTill() const;

	const QString _path;
	const QByteArray _content;
	const bool _keepAlpha = false;

	std::unique_ptr<Media::Streaming::Instance> _instance;
	QImage _lastFrame;
	QSize _frameSize;
	crl::time _from = 0;
	crl::time _till = 0;
	crl::time _position = 0;
	bool _paused = false;
	bool _seeking = false;

	rpl::event_stream<crl::time> _positionUpdates;
	rpl::event_stream<> _repaints;

};

} // namespace Editor
