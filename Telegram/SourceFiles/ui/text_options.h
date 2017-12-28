/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#pragma once

namespace Ui {

void InitTextOptions();

const TextParseOptions &ItemTextDefaultOptions();
const TextParseOptions &ItemTextBotDefaultOptions();
const TextParseOptions &ItemTextNoMonoOptions();
const TextParseOptions &ItemTextBotNoMonoOptions();
const TextParseOptions &ItemTextServiceOptions();

const TextParseOptions &WebpageTextTitleOptions();
const TextParseOptions &WebpageTextDescriptionOptions(
	const QString &siteName = QString());

const TextParseOptions &NameTextOptions();
const TextParseOptions &DialogTextOptions();

const TextParseOptions &ItemTextOptions(
	not_null<History*> history,
	not_null<PeerData*> author);
const TextParseOptions &ItemTextNoMonoOptions(
	not_null<History*> history,
	not_null<PeerData*> author);
const TextParseOptions &ItemTextOptions(not_null<const HistoryItem*> item);
const TextParseOptions &ItemTextNoMonoOptions(not_null<const HistoryItem*> item);

} // namespace Ui
