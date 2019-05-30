/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_animation.h"

#include "lottie/lottie_frame_renderer.h"
#include "rasterrenderer/rasterrenderer.h"
#include "json.h"
#include "base/algorithm.h"
#include "zlib.h"
#include "logs.h"

#include <QFile>
#include <crl/crl_async.h>
#include <crl/crl_on_main.h>

namespace Lottie {
namespace {

QByteArray UnpackGzip(const QByteArray &bytes) {
	z_stream stream;
	stream.zalloc = nullptr;
	stream.zfree = nullptr;
	stream.opaque = nullptr;
	stream.avail_in = 0;
	stream.next_in = nullptr;
	int res = inflateInit2(&stream, 16 + MAX_WBITS);
	if (res != Z_OK) {
		return bytes;
	}
	const auto guard = gsl::finally([&] { inflateEnd(&stream); });

	auto result = QByteArray(kMaxFileSize + 1, Qt::Uninitialized);
	stream.avail_in = bytes.size();
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));
	stream.avail_out = 0;
	while (!stream.avail_out) {
		stream.avail_out = result.size();
		stream.next_out = reinterpret_cast<Bytef*>(result.data());
		int res = inflate(&stream, Z_NO_FLUSH);
		if (res != Z_OK && res != Z_STREAM_END) {
			return bytes;
		} else if (!stream.avail_out) {
			return bytes;
		}
	}
	result.resize(result.size() - stream.avail_out);
	return result;
}

} // namespace

std::unique_ptr<Animation> FromFile(const QString &path) {
	return FromData([&] {
		auto f = QFile(path);
		return (f.size() <= kMaxFileSize && f.open(QIODevice::ReadOnly))
			? f.readAll()
			: QByteArray();
	}());
}

std::unique_ptr<Animation> FromData(const QByteArray &data) {
	return std::make_unique<Animation>(base::duplicate(data));
}

auto Init(QByteArray &&content)
-> base::variant<std::unique_ptr<SharedState>, Error> {
	if (content.size() > kMaxFileSize) {
		qWarning()
			<< "Lottie Error: Too large file: "
			<< content.size();
		return Error::ParseFailed;
	}
	content = UnpackGzip(content);
	if (content.size() > kMaxFileSize) {
		qWarning()
			<< "Lottie Error: Too large file: "
			<< content.size();
		return Error::ParseFailed;
	}
	const auto document = JsonDocument(std::move(content));
	if (const auto error = document.error()) {
		qWarning()
			<< "Lottie Error: Parse failed with code: "
			<< error;
		return Error::ParseFailed;
	}
	auto result = std::make_unique<SharedState>(document.root());
	auto information = result->information();
	if (!information.frameRate
		|| information.framesCount <= 0
		|| information.size.isEmpty()) {
		return Error::NotSupported;
	}
	return std::move(result);
}

QImage ReadThumbnail(QByteArray &&content) {
	return Init(std::move(content)).match([](
		const std::unique_ptr<SharedState> &state) {
		return state->frameForPaint()->original;
	}, [](Error) {
		return QImage();
	});
}

Animation::Animation(QByteArray &&content)
: _timer([=] { checkNextFrameRender(); }) {
	const auto weak = base::make_weak(this);
	crl::async([=, content = base::take(content)]() mutable {
		crl::on_main(weak, [this, result = Init(std::move(content))]() mutable {
			result.match([&](std::unique_ptr<SharedState> &state) {
				parseDone(std::move(state));
			}, [&](Error error) {
				parseFailed(error);
			});
		});
	});
}

Animation::~Animation() {
	if (_renderer) {
		Assert(_state != nullptr);
		_renderer->remove(_state);
	}
}

void Animation::parseDone(std::unique_ptr<SharedState> state) {
	Expects(state != nullptr);

	auto information = state->information();
	_state = state.get();
	_state->start(this, crl::now());
	_renderer = FrameRenderer::Instance();
	_renderer->append(std::move(state));
	_updates.fire({ std::move(information) });

	crl::on_main_update_requests(
	) | rpl::start_with_next([=] {
		checkStep();
	}, _lifetime);
}

void Animation::parseFailed(Error error) {
	_updates.fire_error(std::move(error));
}

QImage Animation::frame(const FrameRequest &request) const {
	Expects(_renderer != nullptr);

	const auto frame = _state->frameForPaint();
	const auto changed = (frame->request != request)
		&& (request.strict || !frame->request.strict);
	if (changed) {
		frame->request = request;
		_renderer->updateFrameRequest(_state, request);
	}
	return PrepareFrameByRequest(frame, !changed);
}

rpl::producer<Update, Error> Animation::updates() const {
	return _updates.events();
}

bool Animation::ready() const {
	return (_renderer != nullptr);
}

crl::time Animation::markFrameDisplayed(crl::time now) {
	Expects(_renderer != nullptr);

	const auto result = _state->markFrameDisplayed(now);

	return result;
}

crl::time Animation::markFrameShown() {
	Expects(_renderer != nullptr);

	const auto result = _state->markFrameShown();
	_renderer->frameShown(_state);

	return result;
}

void Animation::checkStep() {
	if (_nextFrameTime != kTimeUnknown) {
		checkNextFrameRender();
	} else {
		checkNextFrameAvailability();
	}
}

void Animation::checkNextFrameAvailability() {
	Expects(_renderer != nullptr);

	_nextFrameTime = _state->nextFrameDisplayTime();
	if (_nextFrameTime != kTimeUnknown) {
		checkStep();
	}
}

void Animation::checkNextFrameRender() {
	Expects(_nextFrameTime != kTimeUnknown);

	const auto now = crl::now();
	if (now < _nextFrameTime) {
		if (!_timer.isActive()) {
			_timer.callOnce(_nextFrameTime - now);
		}
	} else {
		_timer.cancel();

		_nextFrameTime = kTimeUnknown;
		const auto position = markFrameDisplayed(now);
		_updates.fire({ DisplayFrameRequest{ position } });
	}
}

//void Animation::play(const PlaybackOptions &options) {
//	_options = options;
//	_started = crl::now();
//}

} // namespace Lottie
