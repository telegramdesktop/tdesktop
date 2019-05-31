/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Media {
namespace Streaming {

struct Stream;
class Packet;
enum class Error;

class FileDelegate {
public:
	[[nodiscard]] virtual bool fileReady(
		int headerSize,
		Stream &&video,
		Stream &&audio) = 0;
	virtual void fileError(Error error) = 0;
	virtual void fileWaitingForData() = 0;
	virtual void fileFullInCache(bool fullInCache) = 0;

	// Return true if reading and processing more packets is desired.
	// Return false if sleeping until 'wake()' is called is desired.
	// Return true after the EOF packet if looping is desired.
	[[nodiscard]] virtual bool fileProcessPacket(Packet &&packet) = 0;
	[[nodiscard]] virtual bool fileReadMore() = 0;
};

} // namespace Streaming
} // namespace Media
