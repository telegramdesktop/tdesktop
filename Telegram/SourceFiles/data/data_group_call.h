/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class UserData;
class ChannelData;

namespace Data {

class GroupCall final {
public:
	GroupCall(not_null<ChannelData*> channel, uint64 id, uint64 accessHash);

	[[nodiscard]] uint64 id() const;
	[[nodiscard]] MTPInputGroupCall input() const;

	struct Participant {
		not_null<UserData*> user;
		TimeId date = 0;
		int source = 0;
		bool muted = false;
		bool canSelfUnmute = false;
		bool left = false;
	};

	[[nodiscard]] auto participants() const
		-> const std::vector<Participant> &;
	void requestParticipants();
	[[nodiscard]] bool participantsLoaded() const;

	[[nodiscard]] int fullCount() const;

private:
	const not_null<ChannelData*> _channel;
	const uint64 _id = 0;
	const uint64 _accessHash = 0;

	int _version = 0;
	UserId _adminId = 0;
	uint64 _reflectorId = 0;
	mtpRequestId _participantsRequestId = 0;

	std::vector<Participant> _participants;
	int _fullCount = 0;
	bool _allReceived = false;

};

} // namespace Data
