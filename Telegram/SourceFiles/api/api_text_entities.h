/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

#include "ui/text/text_entity.h"

namespace Main {
class Session;
} // namespace Main

namespace Api {

enum class ConvertOption {
	WithLocal,
	SkipLocal,
};

[[nodiscard]] EntitiesInText EntitiesFromMTP(
	Main::Session *session,
	const QVector<MTPMessageEntity> &entities);

[[nodiscard]] MTPVector<MTPMessageEntity> EntitiesToMTP(
	not_null<Main::Session*> session,
	const EntitiesInText &entities,
	ConvertOption option = ConvertOption::WithLocal);

} // namespace Api
