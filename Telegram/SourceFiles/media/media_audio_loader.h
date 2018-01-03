/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace FFMpeg {
struct AVPacketDataWrap;
} // namespace FFMpeg

class AudioPlayerLoader {
public:
	AudioPlayerLoader(const FileLocation &file, const QByteArray &data, base::byte_vector &&bytes);
	virtual ~AudioPlayerLoader();

	virtual bool check(const FileLocation &file, const QByteArray &data);

	virtual bool open(TimeMs positionMs) = 0;
	virtual int64 samplesCount() = 0;
	virtual int samplesFrequency() = 0;
	virtual int format() = 0;

	enum class ReadResult {
		Error,
		NotYet,
		Ok,
		Wait,
		EndOfFile,
	};
	virtual ReadResult readMore(QByteArray &samples, int64 &samplesCount) = 0;
	virtual void enqueuePackets(QQueue<FFMpeg::AVPacketDataWrap> &packets) {
		Unexpected("enqueuePackets() call on not ChildFFMpegLoader.");
	}

	void saveDecodedSamples(QByteArray *samples, int64 *samplesCount);
	void takeSavedDecodedSamples(QByteArray *samples, int64 *samplesCount);
	bool holdsSavedDecodedSamples() const;

protected:
	FileLocation _file;
	bool _access = false;
	QByteArray _data;
	base::byte_vector _bytes;

	QFile _f;
	int _dataPos = 0;

	bool openFile();

private:
	QByteArray _savedSamples;
	int64 _savedSamplesCount = 0;
	bool _holdsSavedSamples = false;

};
