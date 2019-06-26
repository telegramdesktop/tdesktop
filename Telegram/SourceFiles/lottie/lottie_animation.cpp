/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_animation.h"

#include "lottie/lottie_frame_renderer.h"
#include "lottie/lottie_cache.h"
#include "storage/cache/storage_cache_database.h"
#include "base/algorithm.h"
#include "zlib.h"
#include "logs.h"

#include <QFile>
#include <QDebug>
#include <rlottie.h>
#include <crl/crl_async.h>
#include <crl/crl_on_main.h>

namespace Lottie {
namespace {

std::string UnpackGzip(const QByteArray &bytes) {
	const auto original = [&] {
		return std::string(bytes.constData(), bytes.size());
	};
	z_stream stream;
	stream.zalloc = nullptr;
	stream.zfree = nullptr;
	stream.opaque = nullptr;
	stream.avail_in = 0;
	stream.next_in = nullptr;
	int res = inflateInit2(&stream, 16 + MAX_WBITS);
	if (res != Z_OK) {
		return original();
	}
	const auto guard = gsl::finally([&] { inflateEnd(&stream); });

	auto result = std::string(kMaxFileSize + 1, char(0));
	stream.avail_in = bytes.size();
	stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));
	stream.avail_out = 0;
	while (!stream.avail_out) {
		stream.avail_out = result.size();
		stream.next_out = reinterpret_cast<Bytef*>(result.data());
		int res = inflate(&stream, Z_NO_FLUSH);
		if (res != Z_OK && res != Z_STREAM_END) {
			return original();
		} else if (!stream.avail_out) {
			return original();
		}
	}
	result.resize(result.size() - stream.avail_out);
	return result;
}

QByteArray ReadFile(const QString &filepath) {
	auto f = QFile(filepath);
	return (f.size() <= kMaxFileSize && f.open(QIODevice::ReadOnly))
		? f.readAll()
		: QByteArray();
}

QByteArray ReadContent(const QByteArray &data, const QString &filepath) {
	return data.isEmpty() ? ReadFile(filepath) : base::duplicate(data);
}

std::optional<Error> ContentError(const QByteArray &content) {
	if (content.size() > kMaxFileSize) {
		qWarning() << "Lottie Error: Too large file: " << content.size();
		return Error::ParseFailed;
	}
	return std::nullopt;
}

std::unique_ptr<rlottie::Animation> CreateImplementation(
		const QByteArray &content) {
	const auto string = UnpackGzip(content);
	Assert(string.size() <= kMaxFileSize);

	auto result = rlottie::Animation::loadFromData(string, std::string());
	if (!result) {
		qWarning() << "Lottie Error: Parse failed.";
	}
	return result;
}

details::InitData CheckSharedState(std::unique_ptr<SharedState> state) {
	Expects(state != nullptr);

	auto information = state->information();
	if (!information.frameRate
		|| information.framesCount <= 0
		|| information.size.isEmpty()) {
		return Error::NotSupported;
	}
	return state;
}

details::InitData Init(const QByteArray &content) {
	if (const auto error = ContentError(content)) {
		return *error;
	}
	auto animation = CreateImplementation(content);
	return animation
		? CheckSharedState(std::make_unique<SharedState>(
			std::move(animation)))
		: Error::ParseFailed;
}

details::InitData Init(
		const QByteArray &content,
		not_null<Storage::Cache::Database*> cache,
		Storage::Cache::Key key,
		const QByteArray &cached,
		QSize box) {
	if (const auto error = ContentError(content)) {
		return *error;
	}
	auto state = CacheState(cached, box);
	const auto prepare = !state.framesCount()
		|| (state.framesReady() < state.framesCount());
	auto animation = prepare ? CreateImplementation(content) : nullptr;
	return (!prepare || animation)
		? CheckSharedState(std::make_unique<SharedState>(
			std::move(animation)))
		: Error::ParseFailed;
}

} // namespace

std::unique_ptr<Animation> FromContent(
		const QByteArray &data,
		const QString &filepath) {
	return std::make_unique<Animation>(ReadContent(data, filepath));
}

std::unique_ptr<Animation> FromCached(
		not_null<Storage::Cache::Database*> cache,
		Storage::Cache::Key key,
		const QByteArray &data,
		const QString &filepath,
		QSize box) {
	return std::make_unique<Animation>(
		cache,
		key,
		ReadContent(data, filepath),
		box);
}

QImage ReadThumbnail(const QByteArray &content) {
	return Init(std::move(content)).match([](
		const std::unique_ptr<SharedState> &state) {
		return state->frameForPaint()->original;
	}, [](Error) {
		return QImage();
	});
}

Animation::Animation(const QByteArray &content)
: _timer([=] { checkNextFrameRender(); }) {
	const auto weak = base::make_weak(this);
	crl::async([=] {
		crl::on_main(weak, [=, data = Init(content)]() mutable {
			initDone(std::move(data));
		});
	});
}

Animation::Animation(
	not_null<Storage::Cache::Database*> cache,
	Storage::Cache::Key key,
	const QByteArray &content,
	QSize box)
: _timer([=] { checkNextFrameRender(); }) {
	const auto weak = base::make_weak(this);
	cache->get(key, [=](QByteArray &&cached) mutable {
		crl::async([=] {
			auto result = Init(content, cache, key, cached, box);
			crl::on_main(weak, [=, data = std::move(result)]() mutable {
				initDone(std::move(data));
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

void Animation::initDone(details::InitData &&data) {
	data.match([&](std::unique_ptr<SharedState> &state) {
		parseDone(std::move(state));
	}, [&](Error error) {
		parseFailed(error);
	});
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
	const auto changed = (frame->request != request);
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
