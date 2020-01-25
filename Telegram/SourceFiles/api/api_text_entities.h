/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_entity.h"

namespace Api {

EntitiesInText EntitiesFromMTP(const QVector<MTPMessageEntity> &entities);
enum class ConvertOption {
	WithLocal,
	SkipLocal,
};
MTPVector<MTPMessageEntity> EntitiesToMTP(
	const EntitiesInText &entities,
	ConvertOption option = ConvertOption::WithLocal);

} // namespace Api
