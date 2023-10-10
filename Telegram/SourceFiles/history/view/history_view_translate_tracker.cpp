/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_translate_tracker.h"

#include "apiwrap.h"
#include "api/api_text_entities.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h" // Data::AmPremiumValue.
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "spellcheck/platform/platform_language.h"

namespace HistoryView {
namespace {

constexpr auto kEnoughForRecognition = 10;
constexpr auto kEnoughForTranslation = 6;
constexpr auto kMaxCheckInBunch = 100;
constexpr auto kRequestLengthLimit = 24 * 1024;
constexpr auto kRequestCountLimit = 20;

} // namespace

TranslateTracker::TranslateTracker(not_null<History*> history)
: _history(history)
, _limit(kEnoughForRecognition) {
	setup();
}

TranslateTracker::~TranslateTracker() {
	cancelToRequest();
	cancelSentRequest();
}

rpl::producer<bool> TranslateTracker::trackingLanguage() const {
	return _trackingLanguage.value();
}

void TranslateTracker::setup() {
	const auto peer = _history->peer;
	peer->updateFull();

	using namespace rpl::mappers;
	_trackingLanguage = rpl::combine(
		Data::AmPremiumValue(&_history->session()),
		Core::App().settings().translateChatEnabledValue(),
		_1 && _2);

	_trackingLanguage.value(
	) | rpl::start_with_next([=](bool tracking) {
		_trackingLifetime.destroy();
		if (tracking) {
			recognizeCollected();
			trackSkipLanguages();
		} else {
			checkRecognized({});
			_history->translateTo({});
			if (const auto migrated = _history->migrateFrom()) {
				migrated->translateTo({});
			}
		}
	}, _lifetime);
}

bool TranslateTracker::enoughForRecognition() const {
	return _itemsForRecognize.size() >= kEnoughForRecognition;
}

void TranslateTracker::startBunch() {
	_addedInBunch = 0;
	_bunchTranslatedTo = _history->translatedTo();
	++_generation;
}

bool TranslateTracker::add(not_null<Element*> view) {
	const auto item = view->data();
	const auto only = view->isOnlyEmojiAndSpaces();
	if (only != OnlyEmojiAndSpaces::Unknown) {
		item->cacheOnlyEmojiAndSpaces(only == OnlyEmojiAndSpaces::Yes);
	}
	return add(item, false);
}

bool TranslateTracker::add(not_null<HistoryItem*> item) {
	return add(item, false);
}

bool TranslateTracker::add(
		not_null<HistoryItem*> item,
		bool skipDependencies) {
	Expects(_addedInBunch >= 0);

	if (item->out()
		|| item->isService()
		|| !item->isRegular()
		|| item->isOnlyEmojiAndSpaces()) {
		return false;
	}
	if (item->translationShowRequiresCheck(_bunchTranslatedTo)) {
		_switchTranslations[item] = _bunchTranslatedTo;
	}
	if (!skipDependencies) {
		if (const auto reply = item->Get<HistoryMessageReply>()) {
			if (const auto to = reply->resolvedMessage.get()) {
				add(to, true);
			}
		}
#if 0 // I hope this is not needed, although I'm not sure.
		if (item->groupId()) {
			if (const auto group = _history->owner().groups().find(item)) {
				for (const auto &other : group->items) {
					if (other != item) {
						add(other, true);
					}
				}
			}
		}
#endif
	}
	const auto id = item->fullId();
	const auto i = _itemsForRecognize.find(id);
	if (i != end(_itemsForRecognize)) {
		i->second.generation = _generation;
		return true;
	}
	const auto &text = item->originalText().text;
	_itemsForRecognize.emplace(id, ItemForRecognize{
		.generation = _generation,
		.id = (_trackingLanguage.current()
			? Platform::Language::Recognize(text)
			: MaybeLanguageId{ text }),
	});
	++_addedInBunch;
	return true;
}

void TranslateTracker::switchTranslation(
		not_null<HistoryItem*> item,
		LanguageId id) {
	if (item->translationShowRequiresRequest(id)) {
		_itemsToRequest.emplace(
			item->fullId(),
			ItemToRequest{ int(item->originalText().text.size()) });
	}
}

void TranslateTracker::finishBunch() {
	if (_addedInBunch > 0) {
		accumulate_max(_limit, _addedInBunch + kEnoughForRecognition);
		_addedInBunch = -1;
		applyLimit();
		if (_trackingLanguage.current()) {
			checkRecognized();
		}
	}
	if (!_switchTranslations.empty()) {
		auto switching = base::take(_switchTranslations);
		for (const auto &[item, id] : switching) {
			switchTranslation(item, id);
		}
		_switchTranslations = std::move(switching);
		_switchTranslations.clear();
	}
	requestSome();
}

void TranslateTracker::addBunchFromBlocks() {
	if (enoughForRecognition()) {
		return;
	}
	startBunch();
	const auto guard = gsl::finally([&] {
		finishBunch();
	});

	auto check = kMaxCheckInBunch;
	for (const auto &block : _history->blocks) {
		for (const auto &view : block->messages) {
			if (!check-- || (add(view.get()) && enoughForRecognition())) {
				return;
			}
		}
	}
}

void TranslateTracker::addBunchFrom(
		const std::vector<not_null<Element*>> &views) {
	if (enoughForRecognition()) {
		return;
	}
	startBunch();
	const auto guard = gsl::finally([&] {
		finishBunch();
	});

	auto check = kMaxCheckInBunch;
	for (const auto &view : views) {
		if (!check-- || (add(view.get()) && enoughForRecognition())) {
			return;
		}
	}
}

void TranslateTracker::cancelToRequest() {
	if (!_itemsToRequest.empty()) {
		const auto owner = &_history->owner();
		for (const auto &[id, entry] : base::take(_itemsToRequest)) {
			if (const auto item = owner->message(id)) {
				item->translationShowRequiresRequest({});
			}
		}
	}
}

void TranslateTracker::cancelSentRequest() {
	if (_requestId) {
		const auto owner = &_history->owner();
		for (const auto &id : base::take(_requested)) {
			if (const auto item = owner->message(id)) {
				item->translationShowRequiresRequest({});
			}
		}
		_history->session().api().request(base::take(_requestId)).cancel();
	}
}

void TranslateTracker::requestSome() {
	if (_requestId || _itemsToRequest.empty()) {
		return;
	}
	const auto to = _history->translatedTo();
	if (!to) {
		cancelToRequest();
		return;
	}
	_requested.clear();
	_requested.reserve(_itemsToRequest.size());
	const auto session = &_history->session();
	const auto peerId = _itemsToRequest.back().first.peer;
	auto peer = (peerId == _history->peer->id)
		? _history->peer
		: session->data().peer(peerId);
	auto length = 0;
	auto list = QVector<MTPint>();
	list.reserve(_itemsToRequest.size());
	for (auto i = _itemsToRequest.end(); i != _itemsToRequest.begin();) {
		if ((--i)->first.peer != peerId) {
			break;
		}
		length += i->second.length;
		_requested.push_back(i->first);
		list.push_back(MTP_int(i->first.msg));
		i = _itemsToRequest.erase(i);
		if (list.size() >= kRequestCountLimit
			|| length >= kRequestLengthLimit) {
			break;
		}
	}
	using Flag = MTPmessages_TranslateText::Flag;
	_requestId = session->api().request(MTPmessages_TranslateText(
		MTP_flags(Flag::f_peer | Flag::f_id),
		peer->input,
		MTP_vector<MTPint>(list),
		MTPVector<MTPTextWithEntities>(),
		MTP_string(to.twoLetterCode())
	)).done([=](const MTPmessages_TranslatedText &result) {
		requestDone(to, result.data().vresult().v);
	}).fail([=] {
		requestDone(to, {});
	}).send();
}

void TranslateTracker::requestDone(
		LanguageId to,
		const QVector<MTPTextWithEntities> &list) {
	auto index = 0;
	const auto session = &_history->session();
	const auto owner = &session->data();
	for (const auto &id : base::take(_requested)) {
		if (const auto item = owner->message(id)) {
			const auto data = (index >= list.size())
				? nullptr
				: &list[index].data();
			auto text = data ? TextWithEntities{
				qs(data->vtext()),
				Api::EntitiesFromMTP(session, data->ventities().v)
			} : TextWithEntities();
			item->translationDone(to, std::move(text));
		}
		++index;
	}
	_requestId = 0;
	requestSome();
}

void TranslateTracker::applyLimit() {
	const auto generationProjection = [](const auto &pair) {
		return pair.second.generation;
	};
	const auto owner = &_history->owner();

	// Erase starting with oldest generation till items count is not too big.
	while (_itemsForRecognize.size() > _limit) {
		const auto oldest = ranges::min_element(
			_itemsForRecognize,
			ranges::less(),
			generationProjection
		)->second.generation;
		for (auto i = begin(_itemsForRecognize)
			; i != end(_itemsForRecognize);) {
			if (i->second.generation == oldest) {
				if (const auto j = _itemsToRequest.find(i->first)
					; j != end(_itemsToRequest)) {
					if (const auto item = owner->message(i->first)) {
						item->translationShowRequiresRequest({});
					}
					_itemsToRequest.erase(j);
				}
				i = _itemsForRecognize.erase(i);
			} else {
				++i;
			}
		}
	}
}

void TranslateTracker::recognizeCollected() {
	for (auto &[id, entry] : _itemsForRecognize) {
		if (const auto text = std::get_if<QString>(&entry.id)) {
			entry.id = Platform::Language::Recognize(*text);
		}
	}
}

void TranslateTracker::trackSkipLanguages() {
	Core::App().settings().skipTranslationLanguagesValue(
	) | rpl::start_with_next([=](const std::vector<LanguageId> &skip) {
		checkRecognized(skip);
	}, _trackingLifetime);
}

void TranslateTracker::checkRecognized() {
	checkRecognized(Core::App().settings().skipTranslationLanguages());
}

void TranslateTracker::checkRecognized(const std::vector<LanguageId> &skip) {
	if (!_trackingLanguage.current()) {
		_history->translateOfferFrom({});
		return;
	}
	auto languages = base::flat_map<LanguageId, int>();
	for (const auto &[id, entry] : _itemsForRecognize) {
		if (const auto id = std::get_if<LanguageId>(&entry.id)) {
			if (*id && !ranges::contains(skip, *id)) {
				++languages[*id];
			}
		}
	}
	using namespace base;
	const auto count = int(_itemsForRecognize.size());
	constexpr auto p = &flat_multi_map_pair_type<LanguageId, int>::second;
	const auto threshold = (count > kEnoughForRecognition)
		? (count * kEnoughForTranslation / kEnoughForRecognition)
		: _allLoaded
		? std::min(count, kEnoughForTranslation)
		: kEnoughForTranslation;
	const auto translatable = ranges::accumulate(
		languages,
		0,
		ranges::plus(),
		p);
	if (count < kEnoughForTranslation) {
		// Don't change offer by small amount of messages.
	} else if (translatable >= threshold) {
		_history->translateOfferFrom(
			ranges::max_element(languages, ranges::less(), p)->first);
	} else {
		_history->translateOfferFrom({});
	}
}

} // namespace HistoryView
