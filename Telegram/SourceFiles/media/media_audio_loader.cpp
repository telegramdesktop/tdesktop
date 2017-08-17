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

AudioPlayerLoader::AudioPlayerLoader(const FileLocation &file, const QByteArray &data, base::byte_vector &&bytes)
: _file(file)
, _data(data)
, _bytes(std::move(bytes)) {
}

AudioPlayerLoader::~AudioPlayerLoader() {
	if (_access) {
		_file.accessDisable();
		_access = false;
	}
}

bool AudioPlayerLoader::check(const FileLocation &file, const QByteArray &data) {
	return this->_file == file && this->_data.size() == data.size();
}

void AudioPlayerLoader::saveDecodedSamples(QByteArray *samples, int64 *samplesCount) {
	Assert(_savedSamplesCount == 0);
	Assert(_savedSamples.isEmpty());
	Assert(!_holdsSavedSamples);
	samples->swap(_savedSamples);
	std::swap(*samplesCount, _savedSamplesCount);
	_holdsSavedSamples = true;
}

void AudioPlayerLoader::takeSavedDecodedSamples(QByteArray *samples, int64 *samplesCount) {
	Assert(*samplesCount == 0);
	Assert(samples->isEmpty());
	Assert(_holdsSavedSamples);
	samples->swap(_savedSamples);
	std::swap(*samplesCount, _savedSamplesCount);
	_holdsSavedSamples = false;
}

bool AudioPlayerLoader::holdsSavedDecodedSamples() const {
	return _holdsSavedSamples;
}

bool AudioPlayerLoader::openFile() {
	if (_data.isEmpty() && _bytes.empty()) {
		if (_f.isOpen()) _f.close();
		if (!_access) {
			if (!_file.accessEnable()) {
				LOG(("Audio Error: could not open file access '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(_f.error()).arg(_f.errorString()));
				return false;
			}
			_access = true;
		}
		_f.setFileName(_file.name());
		if (!_f.open(QIODevice::ReadOnly)) {
			LOG(("Audio Error: could not open file '%1', data size '%2', error %3, %4").arg(_file.name()).arg(_data.size()).arg(_f.error()).arg(_f.errorString()));
			return false;
		}
	}
	_dataPos = 0;
	return true;
}
