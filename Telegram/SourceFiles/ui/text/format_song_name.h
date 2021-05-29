/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Ui::Text {

class FormatSongName final {
public:
	struct ComposedName {
		QString title;
		QString performer;
	};

	FormatSongName(
		const QString &filename,
		const QString &songTitle,
		const QString &songPerformer);

	[[nodiscard]] ComposedName composedName() const;
	[[nodiscard]] QString string() const;
	[[nodiscard]] TextWithEntities textWithEntities(
		bool boldOnlyPerformer = false) const;

private:
	const ComposedName _composedName;

};

} // namespace Ui::Text
