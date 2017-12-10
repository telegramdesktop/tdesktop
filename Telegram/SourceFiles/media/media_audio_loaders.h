/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
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
