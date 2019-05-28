/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/flat_map.h"
#include "base/weak_ptr.h"
#include "base/timer.h"
#include "lottie/lottie_common.h"

#include <QSize>
#include <crl/crl_time.h>
#include <rpl/event_stream.h>
#include <vector>
#include <optional>

class QImage;
class QString;
class QByteArray;

namespace Lottie {

constexpr auto kMaxFileSize = 1024 * 1024;

class Animation;
class SharedState;
class FrameRenderer;

bool ValidateFile(const QString &path);
std::unique_ptr<Animation> FromFile(const QString &path);
std::unique_ptr<Animation> FromData(const QByteArray &data);

QImage ReadThumbnail(QByteArray &&content);

class Animation final : public base::has_weak_ptr {
public:
	explicit Animation(QByteArray &&content);
	~Animation();

	//void play(const PlaybackOptions &options);

	[[nodiscard]] QImage frame(const FrameRequest &request) const;

	[[nodiscard]] rpl::producer<Update, Error> updates() const;

	[[nodiscard]] bool ready() const;

	// Returns frame position, if any frame was marked as displayed.
	crl::time markFrameDisplayed(crl::time now);
	crl::time markFrameShown();

	void checkStep();

private:
	void parseDone(std::unique_ptr<SharedState> state);
	void parseFailed(Error error);

	void checkNextFrameAvailability();
	void checkNextFrameRender();

	//crl::time _started = 0;
	//PlaybackOptions _options;

	base::Timer _timer;
	crl::time _nextFrameTime = kTimeUnknown;
	SharedState *_state = nullptr;
	std::shared_ptr<FrameRenderer> _renderer;
	rpl::event_stream<Update, Error> _updates;
	rpl::lifetime _lifetime;

};

} // namespace Lottie
