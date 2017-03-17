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
#include "media/media_audio_loader.h"

AudioPlayerLoader::AudioPlayerLoader(const FileLocation &file, const QByteArray &data)
: file(file)
, data(data) {
}

AudioPlayerLoader::~AudioPlayerLoader() {
	if (access) {
		file.accessDisable();
		access = false;
	}
}

bool AudioPlayerLoader::check(const FileLocation &file, const QByteArray &data) {
	return this->file == file && this->data.size() == data.size();
}

void AudioPlayerLoader::saveDecodedSamples(QByteArray *samples, int64 *samplesCount) {
	t_assert(_savedSamplesCount == 0);
	t_assert(_savedSamples.isEmpty());
	t_assert(!_holdsSavedSamples);
	samples->swap(_savedSamples);
	std::swap(*samplesCount, _savedSamplesCount);
	_holdsSavedSamples = true;
}

void AudioPlayerLoader::takeSavedDecodedSamples(QByteArray *samples, int64 *samplesCount) {
	t_assert(*samplesCount == 0);
	t_assert(samples->isEmpty());
	t_assert(_holdsSavedSamples);
	samples->swap(_savedSamples);
	std::swap(*samplesCount, _savedSamplesCount);
	_holdsSavedSamples = false;
}

bool AudioPlayerLoader::holdsSavedDecodedSamples() const {
	return _holdsSavedSamples;
}

bool AudioPlayerLoader::openFile() {
	if (data.isEmpty()) {
		if (f.isOpen()) f.close();
		if (!access) {
			if (!file.accessEnable()) {
				LOG(("Audio Error: could not open file access '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(f.error()).arg(f.errorString()));
				return false;
			}
			access = true;
		}
		f.setFileName(file.name());
		if (!f.open(QIODevice::ReadOnly)) {
			LOG(("Audio Error: could not open file '%1', data size '%2', error %3, %4").arg(file.name()).arg(data.size()).arg(f.error()).arg(f.errorString()));
			return false;
		}
	}
	dataPos = 0;
	return true;
}
