/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class PhotoData;
class DocumentData;

namespace Data {

class MediaRotation final {
public:
	void set(not_null<PhotoData*> photo, int rotation);
	[[nodiscard]] int get(not_null<PhotoData*> photo) const;

	void set(not_null<DocumentData*> document, int rotation);
	[[nodiscard]] int get(not_null<DocumentData*> document) const;

private:
	base::flat_map<not_null<PhotoData*>, int> _photoRotations;
	base::flat_map<not_null<DocumentData*>, int> _documentRotations;

};

} // namespace Data
