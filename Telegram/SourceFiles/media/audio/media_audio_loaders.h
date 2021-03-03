/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/audio/media_audio.h"
#include "media/audio/media_child_ffmpeg_loader.h"

class AudioPlayerLoader;
class ChildFFMpegLoader;

namespace Media {
namespace Player {

class Loaders : public QObject {
	Q_OBJECT

public:
	Loaders(QThread *thread);
	void feedFromExternal(ExternalSoundPart &&part);
	void forceToBufferExternal(const AudioMsgId &audioId);
	~Loaders();

Q_SIGNALS:
	void error(const AudioMsgId &audio);
	void needToCheck();

public Q_SLOTS:
	void onInit();

	void onStart(const AudioMsgId &audio, qint64 positionMs);
	void onLoad(const AudioMsgId &audio);
	void onCancel(const AudioMsgId &audio);

private:
	void videoSoundAdded();

	AudioMsgId _audio, _song, _video;
	std::unique_ptr<AudioPlayerLoader> _audioLoader;
	std::unique_ptr<AudioPlayerLoader> _songLoader;
	std::unique_ptr<AudioPlayerLoader> _videoLoader;

	QMutex _fromExternalMutex;
	base::flat_map<
		AudioMsgId,
		std::deque<FFmpeg::Packet>> _fromExternalQueues;
	base::flat_set<AudioMsgId> _fromExternalForceToBuffer;
	SingleQueuedInvokation _fromExternalNotify;

	void emitError(AudioMsgId::Type type);
	AudioMsgId clear(AudioMsgId::Type type);
	void setStoppedState(Mixer::Track *m, State state = State::Stopped);

	enum SetupError {
		SetupErrorAtStart = 0,
		SetupErrorNotPlaying = 1,
		SetupErrorLoadedFull = 2,
		SetupNoErrorStarted = 3,
	};
	void loadData(AudioMsgId audio, crl::time positionMs = 0);
	AudioPlayerLoader *setupLoader(
		const AudioMsgId &audio,
		SetupError &err,
		crl::time positionMs);
	Mixer::Track *checkLoader(AudioMsgId::Type type);

};

} // namespace Player
} // namespace Media
