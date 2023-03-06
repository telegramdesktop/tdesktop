/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/bytes.h"
#include "core/file_location.h"
#include "media/streaming/media_streaming_utility.h"

namespace Media {

class AudioPlayerLoader {
public:
	AudioPlayerLoader(
		const Core::FileLocation &file,
		const QByteArray &data,
		bytes::vector &&buffer);
	virtual ~AudioPlayerLoader();

	virtual bool check(const Core::FileLocation &file, const QByteArray &data);

	virtual bool open(crl::time positionMs) = 0;
	virtual int64 samplesCount() = 0;
	virtual int samplesFrequency() = 0;
	virtual int sampleSize() = 0;
	virtual int format() = 0;

	enum class ReadError {
		Other,
		NotYet,
		Wait,
		EndOfFile,
	};
	using ReadResult = std::variant<bytes::const_span, ReadError>;
	[[nodiscard]] virtual ReadResult readMore() = 0;

	virtual void enqueuePackets(std::deque<FFmpeg::Packet> &&packets) {
		Unexpected("enqueuePackets() call on not ChildFFMpegLoader.");
	}
	virtual void setForceToBuffer(bool force) {
		Unexpected("setForceToBuffer() call on not ChildFFMpegLoader.");
	}
	virtual bool forceToBuffer() const {
		return false;
	}

	void saveDecodedSamples(not_null<QByteArray*> samples);
	void takeSavedDecodedSamples(not_null<QByteArray*> samples);
	bool holdsSavedDecodedSamples() const;

protected:
	Core::FileLocation _file;
	bool _access = false;
	QByteArray _data;
	bytes::vector _bytes;

	QFile _f;
	int _dataPos = 0;

	bool openFile();

private:
	QByteArray _savedSamples;
	bool _holdsSavedSamples = false;

};

} // namespace Media
