/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

struct HistoryMessageSuggestedPost;
struct HistoryServiceSuggestDecision;

namespace HistoryView {

class Element;
class MediaGeneric;
class MediaGenericPart;

auto GenerateSuggestDecisionMedia(
	not_null<Element*> parent,
	not_null<const HistoryServiceSuggestDecision*> decision
) -> Fn<void(
	not_null<MediaGeneric*>,
	Fn<void(std::unique_ptr<MediaGenericPart>)>)>;

auto GenerateSuggestRequestMedia(
	not_null<Element*> parent,
	not_null<const HistoryMessageSuggestedPost*> suggest
) -> Fn<void(
	not_null<MediaGeneric*>,
	Fn<void(std::unique_ptr<MediaGenericPart>)>)>;

} // namespace HistoryView
