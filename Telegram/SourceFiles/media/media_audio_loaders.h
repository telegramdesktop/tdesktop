/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/media_child_ffmpeg_loader.h"
#include "media/media_audio.h"
#include "media/media_child_ffmpeg_loader.h"

class AudioPlayerLoader;
class ChildFFMpegLoader;

namespace Media {
namespace Player {

class Loaders : public QObject {
	Q_OBJECT

public:
	Loaders(QThread *thread);
	void feedFromVideo(VideoSoundPart &&part);
	~Loaders();

signals:
	void error(const AudioMsgId &audio);
	void needToCheck();

	public slots:
	void onInit();

	void onStart(const AudioMsgId &audio, qint64 positionMs);
	void onLoad(const AudioMsgId &audio);
	void onCancel(const AudioMsgId &audio);

private:
	void videoSoundAdded();
	void clearFromVideoQueue();

	AudioMsgId _audio, _song, _video;
	std::unique_ptr<AudioPlayerLoader> _audioLoader;
	std::unique_ptr<AudioPlayerLoader> _songLoader;
	std::unique_ptr<AudioPlayerLoader> _videoLoader;

	QMutex _fromVideoMutex;
	QMap<AudioMsgId, QQueue<FFMpeg::AVPacketDataWrap>> _fromVideoQueues;
	SingleQueuedInvokation _fromVideoNotify;

	void emitError(AudioMsgId::Type type);
	AudioMsgId clear(AudioMsgId::Type type);
	void setStoppedState(Mixer::Track *m, State state = State::Stopped);

	enum SetupError {
		SetupErrorAtStart = 0,
		SetupErrorNotPlaying = 1,
		SetupErrorLoadedFull = 2,
		SetupNoErrorStarted = 3,
	};
	void loadData(AudioMsgId audio, TimeMs positionMs);
	AudioPlayerLoader *setupLoader(
		const AudioMsgId &audio,
		SetupError &err,
		TimeMs positionMs);
	Mixer::Track *checkLoader(AudioMsgId::Type type);

};

} // namespace Player
} // namespace Media
