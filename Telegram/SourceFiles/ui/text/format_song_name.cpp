/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/text/format_song_name.h"

namespace Ui::Text {

namespace {

FormatSongName::ComposedName ComputeComposedName(
		const QString &filename,
		const QString &songTitle,
		const QString &songPerformer) {
	const auto unknown = u"Unknown Track"_q;
	if (songTitle.isEmpty() && songPerformer.isEmpty()) {
		return {
			.title = filename.isEmpty() ? unknown : filename,
			.performer = QString(),
		};
	}

	if (songPerformer.isEmpty()) {
		return {
			.title = songTitle,
			.performer = QString(),
		};
	}

	return {
		.title = (songTitle.isEmpty() ? unknown : songTitle),
		.performer = songPerformer,
	};
}

} // namespace

FormatSongName::FormatSongName(
	const QString &filename,
	const QString &songTitle,
	const QString &songPerformer)
: _composedName(ComputeComposedName(filename, songTitle, songPerformer)) {
}

FormatSongName::ComposedName FormatSongName::composedName() const {
	return _composedName;
}

QString FormatSongName::string() const {
	const auto &[title, performer] = _composedName;
	const auto dash = (title.isEmpty() || performer.isEmpty())
		? QString()
		: QString::fromUtf8(" \xe2\x80\x93 ");
	return performer + dash + title;
}

TextWithEntities FormatSongName::textWithEntities(
		bool boldOnlyPerformer) const {
	TextWithEntities result;
	result.text = string();
	if (!boldOnlyPerformer || !_composedName.performer.isEmpty()) {
		result.entities.push_back({
			EntityType::Semibold,
			0,
			_composedName.performer.isEmpty()
				? result.text.size()
				: _composedName.performer.size(),
		});
	}
	return result;
}

} // namespace Ui::Text
