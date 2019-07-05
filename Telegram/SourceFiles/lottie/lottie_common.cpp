/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lottie/lottie_common.h"

#include "base/algorithm.h"

#include <QFile>

namespace Lottie {
namespace {

QByteArray ReadFile(const QString &filepath) {
	auto f = QFile(filepath);
	return (f.size() <= kMaxFileSize && f.open(QIODevice::ReadOnly))
		? f.readAll()
		: QByteArray();
}

} // namespace

QSize FrameRequest::size(const QSize &original) const {
	Expects(!empty());

	const auto result = original.scaled(box, Qt::KeepAspectRatio);
	const auto skipw = result.width() % 8;
	const auto skiph = result.height() % 8;
	return QSize(
		std::max(result.width() - skipw, 8),
		std::max(result.height() - skiph, 8));
}

QByteArray ReadContent(const QByteArray &data, const QString &filepath) {
	return data.isEmpty() ? ReadFile(filepath) : base::duplicate(data);
}

} // namespace Lottie
