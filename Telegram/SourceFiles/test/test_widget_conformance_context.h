/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "data/data_peer_id.h"
#include "test/test_widget_ops.h"

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <vector>

class PeerData;

// The widget conformance runner's reading of a manifest entry's message
// context: its first `message` spec, which decides the flags and state its
// run's message is injected with and the chat a launch must show to host
// it, and its `bot` table, which the played bot (test_widget_bot.h)
// answers; and the run file's record of both. Internal to
// test_widget_conformance.cpp; reference:
// docs/helpers/test_widget_conformance.md.

namespace Test::Conformance {

struct ConformanceRun;
struct ConformanceState;
struct ManifestMessage;
struct ManifestTest;

// Reads |entry|'s `message` into |test.message|. No `message`: kind
// "private", implicit, with the private flags and no state, CLIENTS.md's
// "private chat's context with no state". Otherwise its first spec: its
// `kind`, one of private, group, no-queries, copy, live-copy and preview,
// and its `chat_data` / `public_data` as given, a missing one unset and an
// empty one present and empty. The flags are WIDGET_MESSAGE_KINDS' of
// sdk-policy.ts as isCopy/canQuery/backgroundQuery: private F/T/T, group
// F/T/F, no-queries F/F/F, copy T/F/F, live-copy T/T/F, and for a preview
// the private ones, as which it runs. Never a widget id. A `message` that
// is not a non-empty array whose first item is an object, any other kind
// (via-bot included, which no v0.2.3 entry asks for) and a state that is
// not a string are added to |problems|, by name.
void ParseManifestMessage(
	const QJsonObject &entry,
	ManifestTest &test,
	QStringList &problems);

// The kind |message| runs as: a preview as private, any other as itself.
[[nodiscard]] QString EffectiveKind(const ManifestMessage &message);

// Why an auto entry whose message is a preview is omitted in every launch:
// the owner's deferral, naming tw.preview and behaviour.preview. Empty for
// every other entry, a visual preview included, which loads as private.
[[nodiscard]] QString PreviewOmission(const ManifestTest &test);

// Why a launch whose chat is |chat| cannot host |test|'s run, from the
// id's category alone, as setup knows it: a private context (no message,
// private, no-queries, a preview) needs a user, a group one a basic group
// or a channel, a copy any chat. Empty when it can, or when the kind was
// not read.
[[nodiscard]] QString ChatPartition(const ManifestTest &test, PeerId chat);

// Right before the injection, from the fixture's loaded peer: why |peer|
// cannot host |test|'s run, a group run in a broadcast channel among them.
// Empty when it can.
[[nodiscard]] QString ChatRefusal(
	const ManifestTest &test,
	not_null<PeerData*> peer);

// The run's context extras, inserted into its run file |entry|: `message`
// (the requested kind, the kind it ran as, the injected flags and state,
// and `derived`, the context the client derived, only once read) for a
// run of an entry whose kind was read, and `bot` for a run a bot was armed
// for, never for one never injected. RunObject() and IncompleteObject()
// both write them here.
void InsertContextExtras(
	QJsonObject &entry,
	const ConformanceState &state,
	const ConformanceRun &run);

// The run file's notes on the message contexts: the first-spec and default
// rule, the launch's chat and the chat types the client derived for its
// runs, the preview runs' requested and effective context, and what the
// played bot answers and leaves unmeasured.
[[nodiscard]] QStringList ContextNotes(const ConformanceState &state);

// Main thread, needs Active(). Pure controls on literal entries:
// ParseManifestMessage() for the pinned kinds, no message, a copy-shaped
// spec, a missing beside an empty state and the malformed shapes; each
// kind's chat requirement over every chat category and ChatPartition() by
// id (no real peer for ChatRefusal()); ParseManifestBot(); and the extras
// of a run whose record played two requests under a message id of peer 0
// beside a run never injected. Row names carry no prefix.
[[nodiscard]] std::vector<WidgetOpsSelfCheckRow> RunContextCheck();

// Reads |entry|'s `bot` into |test|: each entry's data and its `result`
// string, or none, into |test.bot|; every other key of an entry (`edit`,
// `editShared`, `repeat`) into |test.botUnplayed|. A `bot` that is not an
// object, an entry that is not an object and a `result` that is not a
// string are added to |problems|. No `bot` leaves both empty.
void ParseManifestBot(
	const QJsonObject &entry,
	ManifestTest &test,
	QStringList &problems);

} // namespace Test::Conformance
