/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/flat_map.h"

#include <crl/crl_time.h>
#include <vector>

class QImage;
class QString;
class QByteArray;

class BMScene;

namespace Lottie {

class Animation;

bool ValidateFile(const QString &path);
std::unique_ptr<Animation> FromFile(const QString &path);

struct PlaybackOptions {
	float64 speed = 1.;
	bool loop = true;
};

class Animation final {
public:
	explicit Animation(const QByteArray &content);
	~Animation();

	void play(const PlaybackOptions &options);

	QImage frame(crl::time now) const;

	int frameRate() const;
	crl::time duration() const;

	void play();
	void pause();
	void resume();
	void stop();

	[[nodiscard]] bool active() const;
	[[nodiscard]] bool ready() const;
	[[nodiscard]] bool unsupported() const;

	[[nodiscard]] float64 speed() const;
	void setSpeed(float64 speed); // 0.5 <= speed <= 2.

	[[nodiscard]] bool playing() const;
	[[nodiscard]] bool buffering() const;
	[[nodiscard]] bool paused() const;
	[[nodiscard]] bool finished() const;

private:
	void parse(const QByteArray &content);

	bool _initialized = false;
	bool _unsupported = false;
	bool _failed = false;
	bool _paused = false;
	crl::time _started = 0;
	PlaybackOptions _options;

	std::unique_ptr<BMScene> _scene;

};

} // namespace Lottie
