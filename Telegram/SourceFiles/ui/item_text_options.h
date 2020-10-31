/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_options.h"

class History;
class PeerData;
class HistoryItem;

namespace Ui {

const TextParseOptions &ItemTextOptions(
	not_null<History*> history,
	not_null<PeerData*> author);
const TextParseOptions &ItemTextNoMonoOptions(
	not_null<History*> history,
	not_null<PeerData*> author);
const TextParseOptions &ItemTextOptions(not_null<const HistoryItem*> item);
const TextParseOptions &ItemTextNoMonoOptions(not_null<const HistoryItem*> item);

} // namespace Ui
