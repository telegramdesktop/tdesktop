/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>

namespace Iv::Editor {

enum class InsertBlockType : uchar {
	Heading,
	Blockquote,
	Code,
	Math,
	Footer,
	Divider,
	Anchor,
	OrderedList,
	BulletList,
	TaskList,
	Pullquote,
	Photo,
	Video,
	Audio,
	Details,
	Table,
	Map,
};

struct InsertAction {
	InsertBlockType type;
	int headingLevel = 1;
	int orderedStart = 1;
	bool taskChecked = false;
	QString codeLanguage;
	double latitude = 0.;
	double longitude = 0.;
};

[[nodiscard]] bool BlockConversionExpandsToActiveLine(InsertBlockType type);

} // namespace Iv::Editor
