/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/player/media_player_listen_tracker.h"

#include "apiwrap.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"

namespace Media::Player {
namespace {

constexpr auto kReportDurationSecondsMin = TimeId(3);

} // namespace

MusicListenTracker::MusicListenTracker()
: _pauseTimer([=] { pauseTimedOut(); }) {
}

void MusicListenTracker::update(const TrackState &state) {
	const auto document = state.id.audio();
	if (!document || !document->isSong()) {
		finalize();
		return;
	} else if (_document != document) {
		finalize();
		_document = document;
		_contextId = state.id.contextId();
		_listenedMs = 0;
		_playing = false;
		_pauseTimer.cancel();
	}
	const auto stopped = IsStopped(state.state);
	const auto playing = !stopped && !IsPausedOrPausing(state.state);
	if (_playing == playing) {
		return;
	} else if (!_playing) {
		_playing = true;
		_playStartedAt = crl::now();
		_pauseTimer.cancel();
	} else {
		_playing = false;
		_listenedMs += crl::now() - _playStartedAt;
		if (stopped) {
			report();
		} else {
			_pauseTimer.callOnce(60 * crl::time(1000));
		}
	}
}

void MusicListenTracker::finalize() {
	if (base::take(_playing)) {
		_listenedMs += crl::now() - _playStartedAt;
	}
	_pauseTimer.cancel();
	report();
}

void MusicListenTracker::report() {
	const auto document = base::take(_document);
	const auto contextId = base::take(_contextId);
	const auto duration = static_cast<int>(base::take(_listenedMs) / 1000);
	if (!document || duration < kReportDurationSecondsMin) {
		return;
	}

	const auto origin = Data::FileOrigin(
		Data::FileOriginMessage(contextId));
	const auto send = [=](auto resend) -> void {
		const auto usedFileReference = document->fileReference();
		document->session().api().request(MTPmessages_ReportMusicListen(
			document->mtpInput(),
			MTP_int(duration)
		)).fail([=](const MTP::Error &error) {
			if (error.code() == 400
				&& error.type().startsWith(u"FILE_REFERENCE_"_q)) {
				document->session().api().refreshFileReference(
					origin,
					[=](const auto &) {
						if (document->fileReference() != usedFileReference) {
							resend(resend);
						}
					});
			}
		}).send();
	};
	send(send);
}

void MusicListenTracker::pauseTimedOut() {
	report();
}

} // namespace Media::Player
