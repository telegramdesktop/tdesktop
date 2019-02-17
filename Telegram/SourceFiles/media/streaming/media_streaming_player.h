/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "media/streaming/media_streaming_common.h"
#include "media/streaming/media_streaming_file_delegate.h"

// #TODO streaming move _audio away
#include "media/streaming/media_streaming_utility.h"

namespace Data {
class Session;
} // namespace Data

namespace Media {
namespace Streaming {

class Loader;
class File;

class Player final : private FileDelegate {
public:
	Player(not_null<Data::Session*> owner, std::unique_ptr<Loader> loader);

	void init(Mode mode, crl::time position);
	void pause();
	void resume();
	void stop();

	bool playing() const;

	rpl::producer<Update, Error> updates() const;

	~Player();

private:
	not_null<FileDelegate*> delegate();

	void fileReady(Stream &&video, Stream &&audio) override;
	void fileError() override;

	bool fileProcessPacket(Packet &&packet) override;
	bool fileReadMore() override;

	const std::unique_ptr<File> _file;
	bool _readTillEnd = false;

	Mode _mode = Mode::Both;

	Stream _audio;
	AudioMsgId _audioMsgId;

	rpl::event_stream<Update, Error> _updates;

};

} // namespace Streaming
} // namespace Media
