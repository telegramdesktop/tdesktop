/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace HistoryView {

class Element;
class Media;

[[nodiscard]] std::unique_ptr<Media> CreateGramTransferMedia(
	not_null<Element*> parent,
	Element *replacing);

} // namespace HistoryView
