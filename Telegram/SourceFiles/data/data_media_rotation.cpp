/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_media_rotation.h"

namespace Data {
namespace {

[[nodiscard]] int NormalizeRotation(int rotation) {
	const auto result = rotation
		- ((rotation / 360) - ((rotation < 0) ? 1 : 0)) * 360;

	Ensures(result >= 0 && result < 360);
	return result;
}

} // namespace

void MediaRotation::set(not_null<PhotoData*> photo, int rotation) {
	if (rotation % 360) {
		_photoRotations[photo] = NormalizeRotation(rotation);
	} else {
		_photoRotations.remove(photo);
	}
}

int MediaRotation::get(not_null<PhotoData*> photo) const {
	const auto i = _photoRotations.find(photo);
	return (i != end(_photoRotations)) ? i->second : 0;
}

void MediaRotation::set(not_null<DocumentData*> document, int rotation) {
	if (rotation % 360) {
		_documentRotations[document] = NormalizeRotation(rotation);
	} else {
		_documentRotations.remove(document);
	}
}

int MediaRotation::get(not_null<DocumentData*> document) const {
	const auto i = _documentRotations.find(document);
	return (i != end(_documentRotations)) ? i->second : 0;
}

} // namespace Data
