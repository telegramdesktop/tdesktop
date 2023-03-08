/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/audio/media_audio_loader.h"

namespace Media {

AudioPlayerLoader::AudioPlayerLoader(
	const Core::FileLocation &file,
	const QByteArray &data,
	bytes::vector &&buffer)
: _file(file)
, _data(data)
, _bytes(std::move(buffer)) {
}

AudioPlayerLoader::~AudioPlayerLoader() {
	if (_access) {
		_file.accessDisable();
		_access = false;
	}
}

bool AudioPlayerLoader::check(
		const Core::FileLocation &file,
		const QByteArray &data) {
	return (this->_file == file) && (this->_data.size() == data.size());
}

void AudioPlayerLoader::saveDecodedSamples(not_null<QByteArray*> samples) {
	Expects(_savedSamples.isEmpty());
	Expects(!_holdsSavedSamples);

	samples->swap(_savedSamples);
	_holdsSavedSamples = true;
}

void AudioPlayerLoader::takeSavedDecodedSamples(
		not_null<QByteArray*> samples) {
	Expects(samples->isEmpty());
	Expects(_holdsSavedSamples);

	samples->swap(_savedSamples);
	_holdsSavedSamples = false;
}

bool AudioPlayerLoader::holdsSavedDecodedSamples() const {
	return _holdsSavedSamples;
}

void AudioPlayerLoader::dropDecodedSamples() {
	_savedSamples = {};
	_holdsSavedSamples = false;
}

int AudioPlayerLoader::bytesPerBuffer() {
	if (!_bytesPerBuffer) {
		_bytesPerBuffer = samplesFrequency() * sampleSize();
	}
	return _bytesPerBuffer;
}

bool AudioPlayerLoader::openFile() {
	if (_data.isEmpty() && _bytes.empty()) {
		if (_f.isOpen()) _f.close();
		if (!_access) {
			if (!_file.accessEnable()) {
				LOG(("Audio Error: could not open file access '%1', "
					"data size '%2', error %3, %4"
					).arg(_file.name()
					).arg(_data.size()
					).arg(_f.error()
					).arg(_f.errorString()));
				return false;
			}
			_access = true;
		}
		_f.setFileName(_file.name());
		if (!_f.open(QIODevice::ReadOnly)) {
			LOG(("Audio Error: could not open file '%1', "
				"data size '%2', error %3, %4"
				).arg(_file.name()
				).arg(_data.size()
				).arg(_f.error()
				).arg(_f.errorString()));
			return false;
		}
	}
	_dataPos = 0;
	return true;
}

} // namespace Media
