/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_animation.h"

#include "lottie/lottie_frame_renderer.h"
#include "base/algorithm.h"

#include <crl/crl_async.h>
#include <crl/crl_on_main.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

#include "logs.h"
#include "rasterrenderer/rasterrenderer.h"

namespace Lottie {

bool ValidateFile(const QString &path) {
	if (!path.endsWith(qstr(".json"), Qt::CaseInsensitive)) {
		return false;
	}
	return true;
}

std::unique_ptr<Animation> FromFile(const QString &path) {
	if (!path.endsWith(qstr(".json"), Qt::CaseInsensitive)) {
		return nullptr;
	}
	auto f = QFile(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return nullptr;
	}
	const auto content = f.readAll();
	if (content.isEmpty()) {
		return nullptr;
	}
	return FromData(content);
}

std::unique_ptr<Animation> FromData(const QByteArray &data) {
	return std::make_unique<Animation>(data);
}

Animation::Animation(const QByteArray &content)
: _timer([=] { checkNextFrame(); }) {
	const auto weak = base::make_weak(this);
	crl::async([=] {
		const auto now = crl::now();
		auto error = QJsonParseError();
		const auto document = QJsonDocument::fromJson(content, &error);
		const auto parsed = crl::now();
		if (error.error != QJsonParseError::NoError) {
			qWarning()
				<< "Lottie Error: Parse failed with code "
				<< error.error
				<< "( " << error.errorString() << ")";
			crl::on_main(weak, [=] {
				parseFailed();
			});
		} else {
			auto state = std::make_unique<SharedState>(document.object());
			crl::on_main(weak, [this, result = std::move(state)]() mutable {
				parseDone(std::move(result));
			});
		}
		const auto finish = crl::now();
		LOG(("INIT: %1 (PARSE %2)").arg(finish - now).arg(parsed - now));
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
	if (!information.frameRate
		|| information.framesCount <= 0
		|| information.size.isEmpty()) {
		_updates.fire_error(Error::NotSupported);
	} else {
		_state = state.get();
		_state->start(this, crl::now());
		_renderer = FrameRenderer::Instance();
		_renderer->append(std::move(state));
		_updates.fire({ std::move(information) });
	}
}

void Animation::parseFailed() {
	_updates.fire_error(Error::ParseFailed);
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

void Animation::checkNextFrame() {
	Expects(_renderer != nullptr);

	const auto time = _state->nextFrameDisplayTime();
	if (time == kTimeUnknown) {
		return;
	}

	const auto now = crl::now();
	if (time > now) {
		_timer.callOnce(time - now);
	} else {
		_timer.cancel();

		const auto position = markFrameDisplayed(now);
		_updates.fire({ DisplayFrameRequest{ position } });
	}
}

//void Animation::play(const PlaybackOptions &options) {
//	_options = options;
//	_started = crl::now();
//}

} // namespace Lottie
