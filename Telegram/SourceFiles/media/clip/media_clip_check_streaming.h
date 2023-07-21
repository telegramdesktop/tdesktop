/*
This file is part of exteraGram Desktop,
the unofficial app based on Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/exteraGramDesktop/exteraGramDesktop/blob/dev/LEGAL
*/
#pragma once

namespace Core {
class FileLocation;
} // namespace Core

namespace Media {
namespace Clip {

bool CheckStreamingSupport(
	const Core::FileLocation &location,
	QByteArray data);

} // namespace Clip
} // namespace Media
