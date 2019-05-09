/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_animation.h"

#include "lottie/lottie_frame_renderer.h"
#include "base/algorithm.h"

#include <range/v3/view/reverse.hpp>
#include <QtMath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QFile>
#include <QPointF>
#include <QPainter>
#include <QImage>
#include <QTimer>
#include <QMetaObject>
#include <QLoggingCategory>
#include <QThread>
#include <math.h>

#include <QtBodymovin/private/bmscene_p.h>

#include "rasterrenderer/lottierasterrenderer.h"

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
	return std::make_unique<Lottie::Animation>(content);
}

Animation::Animation(const QByteArray &content) {
	parse(content);
}

Animation::~Animation() {
}

QImage Animation::frame(crl::time now) const {
	if (_scene->startFrame() == _scene->endFrame()
		|| _scene->width() <= 0
		|| _scene->height() <= 0) {
		return QImage();
	}
	auto result = QImage(
		_scene->width(),
		_scene->height(),
		QImage::Format_ARGB32_Premultiplied);
	result.fill(Qt::transparent);

	{
		QPainter p(&result);
		p.setRenderHints(QPainter::Antialiasing);
		p.setRenderHints(QPainter::SmoothPixmapTransform);

		const auto position = now;
		const auto elapsed = int((_scene->frameRate() * position + 500) / 1000);
		const auto frames = (_scene->endFrame() - _scene->startFrame());
		const auto frame = _options.loop
			? (_scene->startFrame() + (elapsed % frames))
			: std::min(_scene->startFrame() + elapsed, _scene->endFrame());

		_scene->updateProperties(frame);

		LottieRasterRenderer renderer(&p);
		_scene->render(renderer, frame);
	}
	return result;
}

int Animation::frameRate() const {
	return _scene->frameRate();
}

crl::time Animation::duration() const {
	return (_scene->endFrame() - _scene->startFrame()) * crl::time(1000) / _scene->frameRate();
}

void Animation::play(const PlaybackOptions &options) {
	_options = options;
	_started = crl::now();
}

void Animation::parse(const QByteArray &content) {
	const auto document = QJsonDocument::fromJson(content);
	const auto root = document.object();

	if (root.empty()) {
		_failed = true;
		return;
	}

	_scene = std::make_unique<BMScene>();
	_scene->parse(root);
}

} // namespace Lottie
