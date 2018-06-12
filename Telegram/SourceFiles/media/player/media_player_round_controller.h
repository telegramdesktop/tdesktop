/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class HistoryItem;
class AudioMsgId;

namespace Window {
class Controller;
} // namespace Window

namespace Media {
namespace Clip {
class Playback;
} // namespace Clip
} // namespace Media

namespace Media {
namespace Player {

struct TrackState;
enum class State;

class RoundController : private base::Subscriber {
	struct CreateTag;

public:
	static std::unique_ptr<RoundController> TryStart(
		not_null<Window::Controller*> parent,
		not_null<HistoryItem*> item);

	FullMsgId contextId() const;
	void pauseResume();
	Clip::Reader *reader() const;
	Clip::Playback *playback() const;

	rpl::lifetime &lifetime();

	RoundController(
		CreateTag&&,
		not_null<Window::Controller*> parent,
		not_null<HistoryItem*> item);
	~RoundController();

private:
	void stop(State state);
	bool checkReaderState();
	void callback(Clip::Notification notification);
	void handleAudioUpdate(const TrackState &audioId);

	not_null<Window::Controller*> _parent;
	not_null<DocumentData*> _data;
	not_null<HistoryItem*> _context;
	Clip::ReaderPointer _reader;
	std::unique_ptr<Clip::Playback> _playback;

	rpl::lifetime _lifetime;

};

} // namespace Player
} // namespace Media
