/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "data/data_stories.h"
#include "ui/effects/animations.h"
#include "ui/userpic_view.h"

namespace style {
struct TextStyle;
} // namespace style

namespace Media::Stories {

class Controller;
struct SiblingView;
struct SiblingLayout;

class Sibling final : public base::has_weak_ptr {
public:
	Sibling(
		not_null<Controller*> controller,
		const Data::StoriesSource &source,
		StoryId suggestedId);
	~Sibling();

	[[nodiscard]] FullStoryId shownId() const;
	[[nodiscard]] not_null<PeerData*> peer() const;
	[[nodiscard]] bool shows(
		const Data::StoriesSource &source,
		StoryId suggestedId) const;

	[[nodiscard]] SiblingView view(
		const SiblingLayout &layout,
		float64 over);

private:
	class Loader;
	class LoaderPhoto;
	class LoaderVideo;

	void checkStory();
	void check();

	void setBlackThumbnail();
	[[nodiscard]] QImage userpicImage(const SiblingLayout &layout);
	[[nodiscard]] QImage nameImage(const SiblingLayout &layout);
	[[nodiscard]] QPoint namePosition(
		const SiblingLayout &layout,
		const QImage &image) const;

	const not_null<Controller*> _controller;

	FullStoryId _id;
	not_null<PeerData*> _peer;
	QImage _blurred;
	QImage _good;
	Ui::Animations::Simple _goodShown;

	QImage _userpicImage;
	InMemoryKey _userpicKey = {};
	Ui::PeerUserpicView _userpicView;

	QImage _nameImage;
	std::unique_ptr<style::TextStyle> _nameStyle;
	std::optional<Ui::Text::String> _name;
	QString _nameText;
	int _nameAvailableWidth = 0;
	int _nameFontSize = 0;

	std::unique_ptr<Loader> _loader;

};

} // namespace Media::Stories
