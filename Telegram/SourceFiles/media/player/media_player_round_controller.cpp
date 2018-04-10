/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_round_controller.h"

#include "media/media_clip_reader.h"
#include "media/media_audio.h"
#include "media/player/media_player_instance.h"
#include "media/view/media_clip_playback.h"
#include "history/history_item.h"
#include "window/window_controller.h"
#include "data/data_media_types.h"
#include "data/data_document.h"
#include "data/data_session.h"
#include "auth_session.h"

namespace Media {
namespace Player {

struct RoundController::CreateTag {
};

std::unique_ptr<RoundController> RoundController::TryStart(
		not_null<Window::Controller*> parent,
		not_null<HistoryItem*> item) {
	const auto media = item->media();
	if (!media) {
		return nullptr;
	}
	const auto document = media->document();
	if (!document || !document->isVideoMessage()) {
		return nullptr;
	}
	return std::make_unique<RoundController>(CreateTag(), parent, item);
}

RoundController::RoundController(
	CreateTag&&,
	not_null<Window::Controller*> parent,
	not_null<HistoryItem*> item)
: _parent(parent)
, _data(item->media()->document())
, _context(item) {
	Expects(_data->isVideoMessage());

	subscribe(instance()->updatedNotifier(), [this](const TrackState &state) {
		handleAudioUpdate(state);
	});

	_reader = Clip::MakeReader(
		_data,
		_context->fullId(),
		[=](Clip::Notification notification) { callback(notification); },
		Clip::Reader::Mode::Video);
	_playback = std::make_unique<Clip::Playback>();
	_playback->setValueChangedCallback([=](float64 value) {
		Auth().data().requestItemRepaint(_context);
	});
	Auth().data().markMediaRead(_data);
	Auth().data().itemRemoved(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		if (item == _context) {
			stop(State::Stopped);
		}
	}, lifetime());
	Auth().data().itemRepaintRequest(
	) | rpl::start_with_next([=](not_null<const HistoryItem*> item) {
		if (item == _context) {
			checkReaderState();
		}
	}, lifetime());
}

rpl::lifetime &RoundController::lifetime() {
	return _lifetime;
}

FullMsgId RoundController::contextId() const {
	return _context->fullId();
}

void RoundController::pauseResume() {
	if (checkReaderState()) {
		_reader->pauseResumeVideo();
	}
}

Clip::Reader *RoundController::reader() const {
	return _reader ? _reader.get() : nullptr;
}

Clip::Playback *RoundController::playback() const {
	return _playback.get();
}

void RoundController::handleAudioUpdate(const TrackState &state) {
	if (state.id.type() != AudioMsgId::Type::Voice) {
		return;
	}
	const auto audio = _reader->audioMsgId();
	const auto another = (state.id != _reader->audioMsgId());
	const auto stopped = IsStoppedOrStopping(state.state);
	if ((another && !stopped) || (!another && stopped)) {
		stop(State::Stopped);
		return;
	} else if (another) {
		return;
	}
	if (_playback) {
		_playback->updateState(state);
	}
	if (IsPaused(state.state) || state.state == State::Pausing) {
		if (!_reader->videoPaused()) {
			_reader->pauseResumeVideo();
		}
	} else {
		if (_reader->videoPaused()) {
			_reader->pauseResumeVideo();
		}
	}
}

void RoundController::callback(Clip::Notification notification) {
	if (!_reader) {
		return;
	}
	switch (notification) {
	case Clip::NotificationReinit: {
		if (checkReaderState()) {
			Auth().data().requestItemResize(_context);
		}
	} break;

	case Clip::NotificationRepaint: {
		Auth().data().requestItemRepaint(_context);
	} break;
	}
}

bool RoundController::checkReaderState() {
	if (!_reader) {
		return false;
	}
	const auto state = _reader->state();
	if (state == Media::Clip::State::Error) {
		stop(State::StoppedAtError);
		return false;
	} else if (state == Media::Clip::State::Finished) {
		stop(State::StoppedAtEnd);
		return false;
	} else if (_reader->ready() && !_reader->started()) {
		const auto size = QSize(_reader->width(), _reader->height())
			/ cIntRetinaFactor();
		_reader->start(
			size.width(),
			size.height(),
			size.width(),
			size.height(),
			ImageRoundRadius::Ellipse,
			RectPart::AllCorners);
	}
	return true;
}

void RoundController::stop(State state) {
	if (const auto audioId = _reader->audioMsgId()) {
		mixer()->stop(audioId, state);
	}
	_parent->roundVideoFinished(this);
}

RoundController::~RoundController() = default;

} // namespace Player
} // namespace Media
