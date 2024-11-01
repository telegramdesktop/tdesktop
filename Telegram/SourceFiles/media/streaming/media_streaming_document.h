/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_player.h"
#include "ui/effects/radial_animation.h"
#include "ui/effects/animations.h"
#include "base/timer.h"

class DocumentData;

namespace Media::Streaming {

class Instance;
class Loader;

struct QualityDescriptor {
	uint32 sizeInBytes = 0;
	uint32 height = 0;
};

class Document {
public:
	Document(
		not_null<DocumentData*> document,
		std::shared_ptr<Reader> reader,
		std::vector<QualityDescriptor> otherQualities = {});
	Document(
		not_null<PhotoData*> photo,
		std::shared_ptr<Reader> reader,
		std::vector<QualityDescriptor> otherQualities = {});
	explicit Document(std::unique_ptr<Loader> loader);

	void play(const PlaybackOptions &options);
	void saveFrameToCover();

	[[nodiscard]] Player &player();
	[[nodiscard]] const Player &player() const;
	[[nodiscard]] const Information &info() const;

	[[nodiscard]] bool waitingShown() const;
	[[nodiscard]] float64 waitingOpacity() const;
	[[nodiscard]] Ui::RadialState waitingState() const;

	void setOtherQualities(std::vector<QualityDescriptor> value);
	[[nodiscard]] rpl::producer<int> switchQualityRequests() const;

private:
	Document(
		std::shared_ptr<Reader> reader,
		DocumentData *document,
		PhotoData *photo,
		std::vector<QualityDescriptor> otherQualities);

	friend class Instance;

	void registerInstance(not_null<Instance*> instance);
	void unregisterInstance(not_null<Instance*> instance);
	void refreshPlayerPriority();

	void waitingCallback();
	void checkForQualitySwitch(SpeedEstimate estimate);
	bool checkSwitchToHigherQuality();
	bool checkSwitchToLowerQuality();

	void handleUpdate(Update &&update);
	void handleError(Error &&error);

	void ready(Information &&info);
	void waitingChange(bool waiting);

	void validateGoodThumbnail();
	void resubscribe();

	DocumentData *_document = nullptr;
	PhotoData *_photo = nullptr;
	Player _player;
	Information _info;

	rpl::lifetime _subscription;

	mutable Ui::InfiniteRadialAnimation _radial;
	Ui::Animations::Simple _fading;
	base::Timer _timer;
	base::flat_set<not_null<Instance*>> _instances;
	std::vector<QualityDescriptor> _otherQualities;
	rpl::event_stream<int> _switchQualityRequests;
	SpeedEstimate _lastSpeedEstimate;
	bool _waiting = false;

};

} // namespace Media::Streaming
