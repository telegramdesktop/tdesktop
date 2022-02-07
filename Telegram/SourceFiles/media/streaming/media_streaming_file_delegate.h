/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace FFmpeg {
class Packet;
} // namespace FFmpeg

namespace Media {
namespace Streaming {

struct Stream;
enum class Error;

class FileDelegate {
public:
	[[nodiscard]] virtual Mode fileOpenMode() = 0;
	[[nodiscard]] virtual bool fileReady(
		int headerSize,
		Stream &&video,
		Stream &&audio) = 0;
	virtual void fileError(Error error) = 0;
	virtual void fileWaitingForData() = 0;
	virtual void fileFullInCache(bool fullInCache) = 0;

	virtual void fileProcessEndOfFile() = 0;
	// Return true if reading and processing more packets is desired.
	// Return false if sleeping until 'wake()' is called is desired.
	[[nodiscard]] virtual bool fileProcessPackets(
		base::flat_map<int, std::vector<FFmpeg::Packet>> &packets) = 0;
	// Also returns true after fileProcessEndOfFile() if looping is desired.
	[[nodiscard]] virtual bool fileReadMore() = 0;
};

} // namespace Streaming
} // namespace Media
