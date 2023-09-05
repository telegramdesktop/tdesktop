/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
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
