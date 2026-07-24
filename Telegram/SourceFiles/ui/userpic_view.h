/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

#include <QtGui/QImage>
#include <QtGui/QColor>

class QPainter;

namespace Ui {

class EmptyUserpic;

[[nodiscard]] float64 ForumUserpicRadiusMultiplier();

struct CommunityUserpicEffect {
	QImage image;
	int size = 0;
	QRgb color = 0;
	int paletteVersion = 0;
	int dpr = 0;
};

void PaintCommunityUserpicEffect(
	QPainter &p,
	CommunityUserpicEffect &cache,
	int x,
	int y,
	int size,
	QColor color);

enum class PeerUserpicShape : uint8 {
	Auto,
	Circle,
	Forum,
	Monoforum,
};

struct PeerUserpicView {
	[[nodiscard]] bool null() const {
		return cached.isNull() && !cloud && empty.null();
	}

	QImage cached;
	std::shared_ptr<QImage> cloud;
	base::weak_ptr<const EmptyUserpic> empty;
	uint32 paletteVersion : 30 = 0;
	uint32 shape : 2 = 0;
};

[[nodiscard]] bool PeerUserpicLoading(const PeerUserpicView &view);

void ValidateUserpicCache(
	PeerUserpicView &view,
	const QImage *cloud,
	const EmptyUserpic *empty,
	int size,
	PeerUserpicShape shape);

} // namespace Ui
