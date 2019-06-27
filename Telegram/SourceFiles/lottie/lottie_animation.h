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

namespace rlottie {
class Animation;
} // namespace rlottie

namespace Lottie {

inline constexpr auto kMaxFileSize = 1024 * 1024;

class SharedState;
class Animation;
class FrameRenderer;

std::unique_ptr<Animation> FromContent(
	const QByteArray &data,
	const QString &filepath,
	const FrameRequest &request);
std::unique_ptr<Animation> FromCached(
	FnMut<void(FnMut<void(QByteArray &&cached)>)> get, // Main thread.
	FnMut<void(QByteArray &&cached)> put, // Unknown thread.
	const QByteArray &data,
	const QString &filepath,
	const FrameRequest &request);

QImage ReadThumbnail(const QByteArray &content);

namespace details {

using InitData = base::variant<std::unique_ptr<SharedState>, Error>;

std::unique_ptr<rlottie::Animation> CreateFromContent(
	const QByteArray &content);

} // namespace details

class Animation final : public base::has_weak_ptr {
public:
	explicit Animation(
		const QByteArray &content,
		const FrameRequest &request);
	Animation(
		FnMut<void(FnMut<void(QByteArray &&cached)>)> get, // Main thread.
		FnMut<void(QByteArray &&cached)> put, // Unknown thread.
		const QByteArray &content,
		const FrameRequest &request);
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
	void initDone(details::InitData &&data);
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
