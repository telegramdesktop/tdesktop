/*
This file is part of rabbitGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/rabbitGramDesktop/rabbitGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace UserpicBuilder {

struct Result {
	QImage&& image;
	DocumentId id = 0;
	std::vector<QColor> colors;
};

[[nodiscard]] QImage GenerateGradient(
	const QSize &size,
	const std::vector<QColor> &colors,
	bool circle = true,
	bool roundForumRect = false);

struct StartData {
	DocumentId documentId = DocumentId(0);
	int builderColorIndex = 0;
	rpl::producer<std::vector<DocumentId>> documents;
	std::vector<QColor> gradientEditorColors;
	bool isForum = false;
};

template <typename Result>
struct BothWayCommunication {
	rpl::producer<> triggers;
	Fn<void(Result)> result;
};

} // namespace UserpicBuilder
