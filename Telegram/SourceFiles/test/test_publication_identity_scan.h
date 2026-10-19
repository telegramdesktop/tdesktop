/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "test/test_secrecy_scan.h"

namespace Test {

// Explicit public input only; no fixture/account discovery. Names use NFC,
// case folding and collapsed whitespace (256 UTF-16 units per component).
// Usernames use ASCII folding (64 characters); phones are + plus at most
// 15 international digits; ids are canonical positive decimals (20 digits).
struct PublicationIdentity {
	QString firstName;
	QString lastName;
	QString username;
	QString phone;
	QString userId;
};

struct LaunchLogEnumerationArgs {
	std::vector<SecrecyEarlierPart> earlierParts;
	QString testLogControl;
	QString appLogControl;
	QString debugLogControl;
	QString mtpLogControl;
};

struct PublicationIdentityScanArgs {
	PublicationIdentity identity;
	LaunchLogEnumerationArgs logs;
};

enum class LaunchLogRefusal {
	UnspecifiedSelection,
	InvalidTimes,
	ReversedWindow,
	CappedWindow,
	CandidateInventoryMismatch,
	EarlierInventoryMismatch,
	EmptyPlantedControl,
	NoCandidates,
	NoAcceptedSource,
	MissingSource,
	ForeignSource,
	UnreadableSource,
	InvalidEarlierName,
	MissingPlantedControl,
	MissingNamedControl,
	MissingEarlierControl,
	MissingSendHeader,
	MissingRecvHeader,
};

struct LaunchLogClassEnumeration {
	SecrecyClass cls = SecrecyClass::TestLog;
	int candidates = 0;
	int files = 0;
	int foreign = 0;
	int unreadable = 0;
	int missing = 0;
	int invalidNames = 0;
	int withoutControl = 0;
	int lines = 0;
	int sendHeaders = 0;
	int recvHeaders = 0;
	int plantedHits = 0;
	int controlHits = 0;
	bool decided = false;
	std::vector<LaunchLogRefusal> refusals;
};

// Coverage only: neither secrecy nor absence of identity is asserted.
struct LaunchLogEnumerationReading {
	std::vector<LaunchLogClassEnumeration> classes;
	LaunchLogSelection selection = LaunchLogSelection::Unspecified;
	int candidates = 0;
	int accepted = 0;
	int foreign = 0;
	int missing = 0;
	int unreadable = 0;
	bool decided = false;
	std::vector<LaunchLogRefusal> refusals;

	[[nodiscard]] const LaunchLogClassEnumeration *find(SecrecyClass cls) const;
};

enum class PublicationIdentityFamily {
	Profile,
	Username,
	Phone,
	UserId,
};

enum class PublicationIdentityRefusal {
	NoUsableIdentity,
	InvalidFirstName,
	InvalidLastName,
	InvalidUsername,
	InvalidPhone,
	InvalidUserId,
	ParserDepthLimit,
	ParserStringLimit,
	ParserProfileLimit,
	UnterminatedProfile,
	UnterminatedString,
	UnsupportedStringEscape,
	LostObjectScope,
	MatcherControlFailed,
};

struct PublicationIdentityClassReading {
	SecrecyClass cls = SecrecyClass::TestLog;
	int profiles = 0;
	int usernames = 0;
	int phones = 0;
	int userIds = 0;
};

// Counts/classifications only; supplied values and observed text stay private.
struct PublicationIdentityReading {
	LaunchLogEnumerationReading enumeration;
	std::vector<PublicationIdentityClassReading> classes;
	std::vector<PublicationIdentityRefusal> refusals;
	int controlFamilies = 0;
	int positiveControls = 0;
	int negativeControls = 0;
	bool controlsHold = false;
	bool decided = false;
	bool clean = false;

	[[nodiscard]] const PublicationIdentityClassReading *find(
		SecrecyClass cls) const;
	[[nodiscard]] int findings() const;
};

[[nodiscard]] LaunchLogEnumerationReading ReadLaunchLogEnumeration(
	const SecrecyLaunchLogs &logs,
	const QString &plantedControl,
	const LaunchLogEnumerationArgs &args = {});
[[nodiscard]] PublicationIdentityReading ReadPublicationIdentity(
	const PublicationIdentity &identity,
	const SecrecyLaunchLogs &logs,
	const QString &plantedControl,
	const LaunchLogEnumerationArgs &args = {});
[[nodiscard]] QStringList LaunchLogEnumerationRows(
	const LaunchLogEnumerationReading &reading);
[[nodiscard]] QStringList PublicationIdentityRows(
	const PublicationIdentityReading &reading);
bool CheckLaunchLogEnumeration(const LaunchLogEnumerationArgs &args = {});
bool CheckPublicationIdentity(const PublicationIdentityScanArgs &args);
void AppendPublicationIdentityScanSelfTest(not_null<Runner*> runner);

} // namespace Test
