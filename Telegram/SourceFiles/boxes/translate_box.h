/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"

class PeerData;
struct LanguageId;

namespace Ui {

class BoxContent;
class GenericBox;

void TranslateBox(
	not_null<GenericBox*> box,
	not_null<PeerData*> peer,
	MsgId msgId,
	TextWithEntities text,
	bool hasCopyRestriction);

[[nodiscard]] bool SkipTranslate(TextWithEntities textWithEntities);

[[nodiscard]] object_ptr<BoxContent> EditSkipTranslationLanguages();
[[nodiscard]] object_ptr<BoxContent> ChooseTranslateToBox();

} // namespace Ui
