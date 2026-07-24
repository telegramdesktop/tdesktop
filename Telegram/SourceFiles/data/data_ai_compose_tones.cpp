/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_ai_compose_tones.h"

#include "apiwrap.h"
#include "api/api_text_entities.h"
#include "data/data_session.h"
#include "main/main_session.h"

namespace Data {
namespace {

constexpr auto kRefreshInterval = 3600 * crl::time(1000);

} // namespace

AiComposeTones::AiComposeTones(not_null<Main::Session*> session)
: _session(session)
, _refreshTimer([=] { refresh(); }) {
	refresh();
	_refreshTimer.callEach(kRefreshInterval);
}

void AiComposeTones::refresh() {
	refreshWithHash(_hash);
}

void AiComposeTones::refreshWithHash(uint64 hash) {
	if (_refreshRequestId) {
		if (hash == 0) {
			_pendingRefresh = PendingRefresh::Full;
		} else if (_pendingRefresh == PendingRefresh::None) {
			_pendingRefresh = PendingRefresh::Incremental;
		}
		return;
	}
	_refreshRequestId = _session->api().request(MTPaicompose_GetTones(
		MTP_long(hash)
	)).done([=](const MTPaicompose_Tones &result) {
		_refreshRequestId = 0;
		result.match([&](const MTPDaicompose_tones &data) {
			_session->data().processUsers(data.vusers());
			_hash = data.vhash().v;
			parseTones(data.vtones().v);
			_updates.fire({});
		}, [](const MTPDaicompose_tonesNotModified &) {
		});
		finishRefresh();
	}).fail([=] {
		_refreshRequestId = 0;
		finishRefresh();
	}).send();
}

void AiComposeTones::parseTones(const QVector<MTPAiComposeTone> &list) {
	_list.clear();
	_list.reserve(list.size());
	for (const auto &tone : list) {
		_list.push_back(parseTone(tone));
	}
	reapplyRecentCustomToneOrder();
}

AiComposeTone AiComposeTones::parseTone(
		const MTPAiComposeTone &tone) const {
	return tone.match([&](const MTPDaiComposeTone &data) {
		auto result = AiComposeTone{
			.id = data.vid().v,
			.accessHash = data.vaccess_hash().v,
			.slug = qs(data.vslug()),
			.title = qs(data.vtitle()),
			.emojiId = data.vemoji_id().value_or_empty(),
			.prompt = qs(data.vprompt().value_or_empty()),
			.installsCount = data.vinstalls_count().value_or_empty(),
			.authorId = data.vauthor_id()
				? UserId(data.vauthor_id()->v)
				: UserId(0),
			.creator = data.is_creator(),
		};
		if (const auto example = data.vexample_english()) {
			example->match([&](const MTPDaiComposeToneExample &d) {
				result.firstExample = AiComposeToneExample{
					.from = Api::ParseTextWithEntities(_session, d.vfrom()),
					.to = Api::ParseTextWithEntities(_session, d.vto()),
				};
			});
		}
		return result;
	}, [&](const MTPDaiComposeToneDefault &data) {
		return AiComposeTone{
			.title = qs(data.vtitle()),
			.emojiId = data.vemoji_id().v,
			.isDefault = true,
			.defaultType = qs(data.vtone()),
		};
	});
}

void AiComposeTones::create(
		const QString &title,
		const QString &prompt,
		DocumentId emojiId,
		bool displayAuthor,
		Fn<void(AiComposeTone)> done,
		Fn<void(const MTP::Error &)> fail) {
	using Flag = MTPaicompose_CreateTone::Flag;
	auto flags = MTPaicompose_CreateTone::Flags(0);
	if (displayAuthor) {
		flags |= Flag::f_display_author;
	}
	_session->api().request(MTPaicompose_CreateTone(
		MTP_flags(flags),
		MTP_long(emojiId),
		MTP_string(title),
		MTP_string(prompt)
	)).done([=](const MTPAiComposeTone &result) {
		auto parsed = parseTone(result);
		promoteCustomTone(parsed);
		_hash = 0;
		_updates.fire({});
		if (done) {
			done(parsed);
		}
		refresh();
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error);
		}
	}).send();
}

void AiComposeTones::update(
		const AiComposeTone &tone,
		std::optional<QString> title,
		std::optional<QString> prompt,
		std::optional<DocumentId> emojiId,
		std::optional<bool> displayAuthor,
		Fn<void(AiComposeTone)> done,
		Fn<void(const MTP::Error &)> fail) {
	using Flag = MTPaicompose_UpdateTone::Flag;
	auto flags = MTPaicompose_UpdateTone::Flags(0);
	if (displayAuthor) {
		flags |= Flag::f_display_author;
	}
	if (emojiId) {
		flags |= Flag::f_emoji_id;
	}
	if (title) {
		flags |= Flag::f_title;
	}
	if (prompt) {
		flags |= Flag::f_prompt;
	}
	_session->api().request(MTPaicompose_UpdateTone(
		MTP_flags(flags),
		toneToMTP(tone),
		displayAuthor
			? (*displayAuthor ? MTP_boolTrue() : MTP_boolFalse())
			: MTPBool(),
		MTP_long(emojiId.value_or(0)),
		MTP_string(title.value_or(QString())),
		MTP_string(prompt.value_or(QString()))
	)).done([=](const MTPAiComposeTone &result) {
		auto parsed = parseTone(result);
		promoteCustomTone(parsed);
		_hash = 0;
		_updates.fire({});
		if (done) {
			done(parsed);
		}
		refresh();
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error);
		}
	}).send();
}

void AiComposeTones::save(
		const AiComposeTone &tone,
		bool unsave,
		Fn<void()> done,
		Fn<void(const MTP::Error &)> fail) {
	_session->api().request(MTPaicompose_SaveTone(
		toneToMTP(tone),
		unsave ? MTP_boolTrue() : MTP_boolFalse()
	)).done([=] {
		if (unsave) {
			removeCustomTone(tone.id);
			forgetRecentCustomTone(tone.id);
		} else {
			promoteCustomTone(tone);
		}
		_hash = 0;
		_updates.fire({});
		if (done) {
			done();
		}
		refresh();
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error);
		}
	}).send();
}

void AiComposeTones::remove(
		const AiComposeTone &tone,
		Fn<void()> done) {
	const auto toneCopy = tone;
	_session->api().request(MTPaicompose_DeleteTone(
		toneToMTP(tone)
	)).done([=] {
		if (!toneCopy.isDefault) {
			removeCustomTone(toneCopy.id);
			forgetRecentCustomTone(toneCopy.id);
		}
		_hash = 0;
		_updates.fire({});
		if (done) {
			done();
		}
		refresh();
	}).fail([=] {
	}).send();
}

void AiComposeTones::resolve(
		const QString &slug,
		Fn<void(AiComposeTone)> done,
		Fn<void(const MTP::Error &)> fail) {
	_session->api().request(MTPaicompose_GetTone(
		MTP_inputAiComposeToneSlug(MTP_string(slug))
	)).done([=](const MTPaicompose_Tones &result) {
		result.match([&](const MTPDaicompose_tones &data) {
			_session->data().processUsers(data.vusers());
			const auto &tones = data.vtones().v;
			if (!tones.isEmpty()) {
				if (done) {
					done(parseTone(tones.front()));
				}
			} else if (fail) {
				fail(MTP::Error::Local(
					"TONE_NOT_FOUND",
					"Tone not found."));
			}
		}, [&](const MTPDaicompose_tonesNotModified &) {
			if (fail) {
				fail(MTP::Error::Local(
					"TONE_NOT_MODIFIED",
					"Tone not modified."));
			}
		});
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error);
		}
	}).send();
}

void AiComposeTones::getToneExample(
		const AiComposeTone &tone,
		int num,
		Fn<void(AiComposeToneExample)> done,
		Fn<void(const MTP::Error &)> fail) {
	_session->api().request(MTPaicompose_GetToneExample(
		toneToMTP(tone),
		MTP_int(num)
	)).done([=](const MTPAiComposeToneExample &result) {
		result.match([&](const MTPDaiComposeToneExample &data) {
			if (done) {
				done(AiComposeToneExample{
					.from = Api::ParseTextWithEntities(_session, data.vfrom()),
					.to = Api::ParseTextWithEntities(_session, data.vto()),
				});
			}
		});
	}).fail([=](const MTP::Error &error) {
		if (fail) {
			fail(error);
		}
	}).send();
}

void AiComposeTones::applyUpdate() {
	refresh();
}

void AiComposeTones::finishRefresh() {
	const auto pending = _pendingRefresh;
	_pendingRefresh = PendingRefresh::None;
	if (pending == PendingRefresh::None) {
		return;
	}
	refreshWithHash((pending == PendingRefresh::Full) ? 0 : _hash);
}

void AiComposeTones::promoteCustomTone(AiComposeTone tone) {
	if (tone.isDefault) {
		return;
	}
	removeCustomTone(tone.id);
	rememberRecentCustomTone(tone.id);
	_list.insert(begin(_list), std::move(tone));
}

void AiComposeTones::removeCustomTone(uint64 id) {
	const auto i = ranges::find(_list, id, &AiComposeTone::id);
	if (i != end(_list)) {
		_list.erase(i);
	}
}

void AiComposeTones::rememberRecentCustomTone(uint64 id) {
	const auto i = ranges::find(_recentCustomToneIds, id);
	if (i != end(_recentCustomToneIds)) {
		_recentCustomToneIds.erase(i);
	}
	_recentCustomToneIds.insert(begin(_recentCustomToneIds), id);
}

void AiComposeTones::forgetRecentCustomTone(uint64 id) {
	const auto i = ranges::find(_recentCustomToneIds, id);
	if (i != end(_recentCustomToneIds)) {
		_recentCustomToneIds.erase(i);
	}
}

void AiComposeTones::reapplyRecentCustomToneOrder() {
	auto reordered = std::vector<AiComposeTone>();
	reordered.reserve(_list.size());

	auto recent = std::vector<uint64>();
	recent.reserve(_recentCustomToneIds.size());
	for (const auto id : _recentCustomToneIds) {
		const auto i = ranges::find(_list, id, &AiComposeTone::id);
		if (i != end(_list) && !i->isDefault) {
			reordered.push_back(*i);
			recent.push_back(id);
		}
	}
	for (const auto &tone : _list) {
		const auto promoted = !tone.isDefault
			&& (ranges::find(recent, tone.id) != end(recent));
		if (!promoted) {
			reordered.push_back(tone);
		}
	}
	_recentCustomToneIds = std::move(recent);
	_list = std::move(reordered);
}

MTPInputAiComposeTone AiComposeTones::toneToMTP(
		const AiComposeTone &tone) const {
	return tone.isDefault
		? MTP_inputAiComposeToneDefault(MTP_string(tone.defaultType))
		: MTP_inputAiComposeToneID(
			MTP_long(tone.id),
			MTP_long(tone.accessHash));
}

const std::vector<AiComposeTone> &AiComposeTones::list() const {
	return _list;
}

rpl::producer<> AiComposeTones::updated() const {
	return _updates.events();
}

} // namespace Data
