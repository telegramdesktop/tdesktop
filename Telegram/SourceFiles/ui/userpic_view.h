/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/xmdnx/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"

#include <QtGui/QImage>

namespace Ui {

class EmptyUserpic;

[[nodiscard]] float64 ForumUserpicRadiusMultiplier();

struct PeerUserpicView {
	[[nodiscard]] bool null() const {
		return cached.isNull() && !cloud && empty.null();
	}

	QImage cached;
	std::shared_ptr<QImage> cloud;
	base::weak_ptr<const EmptyUserpic> empty;
	int paletteVersion : 31 = 0;
	int forum : 1 = 0;
};

[[nodiscard]] bool PeerUserpicLoading(const PeerUserpicView &view);

void ValidateUserpicCache(
	PeerUserpicView &view,
	const QImage *cloud,
	const EmptyUserpic *empty,
	int size,
	bool forum);

} // namespace Ui
