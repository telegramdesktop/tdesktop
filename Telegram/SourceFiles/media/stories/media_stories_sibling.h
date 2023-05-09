/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_stories.h"

#include "ui/effects/animations.h"

namespace Media::Stories {

class Controller;

class Sibling final {
public:
	Sibling(
		not_null<Controller*> controller,
		const Data::StoriesList &list);
	~Sibling();

	[[nodiscard]] Data::FullStoryId shownId() const;
	[[nodiscard]] bool shows(const Data::StoriesList &list) const;

	[[nodiscard]] QImage image() const;

private:
	class Loader;
	class LoaderPhoto;
	class LoaderVideo;

	void check();

	const not_null<Controller*> _controller;

	Data::FullStoryId _id;
	QImage _blurred;
	QImage _good;
	Ui::Animations::Simple _goodShown;

	std::unique_ptr<Loader> _loader;

};

} // namespace Media::Stories
