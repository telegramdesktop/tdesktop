/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "base/bytes.h"

namespace Core {
class FileLocation;
} // namespace Core

namespace Media {
namespace Audio {

class Instance;

class Track {
public:
	Track(not_null<Instance*> instance);

	void samplePeakEach(crl::time peakDuration);

	void fillFromData(bytes::vector &&data);
	void fillFromFile(const Core::FileLocation &location);
	void fillFromFile(const QString &filePath);

	void playOnce() {
		playWithLooping(false);
	}
	void playInLoop() {
		playWithLooping(true);
	}

	bool isLooping() const {
		return _looping;
	}
	bool isActive() const {
		return _active;
	}
	bool failed() const {
		return _failed;
	}

	int64 getLengthMs() const {
		return _lengthMs;
	}
	float64 getPeakValue(crl::time when) const;

	void detachFromDevice();
	void reattachToDevice();
	void updateState();

	~Track();

private:
	void finish();
	void ensureSourceCreated();
	void playWithLooping(bool looping);

	not_null<Instance*> _instance;

	bool _failed = false;
	bool _active = false;
	bool _looping = false;
	float64 _volume = 1.;

	int64 _samplesCount = 0;
	int32 _sampleRate = 0;
	bytes::vector _samples;

	crl::time _peakDurationMs = 0;
	int _peakEachPosition = 0;
	std::vector<uint16> _peaks;
	uint16 _peakValueMin = 0;
	uint16 _peakValueMax = 0;

	crl::time _lengthMs = 0;
	crl::time _stateUpdatedAt = 0;

	int32 _alFormat = 0;
	int64 _alPosition = 0;
	uint32 _alSource = 0;
	uint32 _alBuffer = 0;

};

class Instance {
public:
	// Thread: Main.
	Instance();

	std::unique_ptr<Track> createTrack();

	void detachTracks();
	void reattachTracks();
	bool hasActiveTracks() const;

	void scheduleDetachFromDevice();
	void scheduleDetachIfNotUsed();
	void stopDetachIfNotUsed();

	~Instance();

private:
	friend class Track;
	void registerTrack(Track *track);
	void unregisterTrack(Track *track);
	void trackStarted(Track *track);
	void trackFinished(Track *track);

private:
	std::set<Track*> _tracks;

	base::Timer _updateTimer;

	base::Timer _detachFromDeviceTimer;
	bool _detachFromDeviceForce = false;

};

Instance &Current();

} // namespace Audio
} // namespace Media
