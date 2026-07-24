/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_poll.h"

#include "api/api_text_entities.h"
#include "countries/countries_instance.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "base/call_delayed.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/text/text_options.h"

namespace {

constexpr auto kShortPollTimeout = 30 * crl::time(1000);
constexpr auto kReloadAfterAutoCloseDelay = crl::time(1000);

void ProcessPollMedia(
		not_null<Data::Session*> owner,
		const MTPMessageMedia &media) {
	media.match([&](const MTPDmessageMediaPhoto &media) {
		if (const auto photo = media.vphoto()) {
			photo->match([&](const MTPDphoto &) {
				owner->processPhoto(*photo);
			}, [](const auto &) {
			});
		}
	}, [&](const MTPDmessageMediaDocument &media) {
		if (const auto document = media.vdocument()) {
			document->match([&](const MTPDdocument &) {
				owner->processDocument(*document);
			}, [](const auto &) {
			});
		}
	}, [](const auto &) {
	});
}

const PollAnswer *AnswerByOption(
		const std::vector<PollAnswer> &list,
		const QByteArray &option) {
	const auto i = ranges::find(
		list,
		option,
		[](const PollAnswer &a) { return a.option; });
	return (i != end(list)) ? &*i : nullptr;
}

PollAnswer *AnswerByOption(
		std::vector<PollAnswer> &list,
		const QByteArray &option) {
	return const_cast<PollAnswer*>(AnswerByOption(
		std::as_const(list),
		option));
}

} // namespace

PollData::PollData(not_null<Data::Session*> owner, PollId id)
: id(id)
, _owner(owner) {
}

Data::Session &PollData::owner() const {
	return *_owner;
}

Main::Session &PollData::session() const {
	return _owner->session();
}

bool PollData::closeByTimer() {
	if (closed()) {
		return false;
	}
	_flags |= Flag::Closed;
	++version;
	base::call_delayed(kReloadAfterAutoCloseDelay, &_owner->session(), [=] {
		_lastResultsUpdate = -1; // Force reload results.
		++version;
		_owner->notifyPollUpdateDelayed(this);
	});
	return true;
}

bool PollData::applyChanges(const MTPDpoll &poll) {
	Expects(poll.vid().v == id);

	const auto newQuestion = Api::ParseTextWithEntities(
		&session(),
		poll.vquestion());
	const auto newFlags = (poll.is_closed() ? Flag::Closed : Flag(0))
		| (poll.is_public_voters() ? Flag::PublicVotes : Flag(0))
		| (poll.is_multiple_choice() ? Flag::MultiChoice : Flag(0))
		| (poll.is_quiz() ? Flag::Quiz : Flag(0))
		| (poll.is_shuffle_answers() ? Flag::ShuffleAnswers : Flag(0))
		| (poll.is_revoting_disabled() ? Flag::RevotingDisabled : Flag(0))
		| (poll.is_open_answers() ? Flag::OpenAnswers : Flag(0))
		| (poll.is_hide_results_until_close()
			? Flag::HideResultsUntilClose
			: Flag(0))
		| (poll.is_creator() ? Flag::Creator : Flag(0))
		| (poll.is_subscribers_only() ? Flag::SubscribersOnly : Flag(0));
	const auto newCloseDate = poll.vclose_date().value_or_empty();
	const auto newClosePeriod = poll.vclose_period().value_or_empty();
	auto newCountries = std::vector<QString>();
	if (const auto countries = poll.vcountries_iso2()) {
		newCountries.reserve(countries->v.size());
		for (const auto &country : countries->v) {
			newCountries.push_back(qs(country));
		}
	}
	auto newAnswers = ranges::views::all(
		poll.vanswers().v
	) | ranges::views::transform([&](const MTPPollAnswer &data) {
		return data.match([&](const MTPDpollAnswer &answer) {
			auto result = PollAnswer();
			result.option = answer.voption().v;
			result.text = Api::ParseTextWithEntities(
				&session(),
				answer.vtext());
			if (const auto media = answer.vmedia()) {
				ProcessPollMedia(_owner, *media);
				result.media = PollMediaFromMTP(_owner, *media);
			}
			if (const auto addedBy = answer.vadded_by()) {
				result.addedBy = _owner->peer(peerFromMTP(*addedBy));
				result.addedDate = answer.vdate().value_or_empty();
			}
			return result;
		}, [&](const MTPDinputPollAnswer &answer) {
			auto result = PollAnswer();
			result.text = Api::ParseTextWithEntities(
				&session(),
				answer.vtext());
			if (const auto media = answer.vmedia()) {
				result.media = PollMediaFromInputMTP(_owner, *media);
			}
			return result;
		}, [](const auto &) {
			return PollAnswer();
		});
	}) | ranges::views::take(
		kMaxOptions
	) | ranges::to_vector;

	const auto changed1 = (question != newQuestion)
		|| (closeDate != newCloseDate)
		|| (closePeriod != newClosePeriod)
		|| (countries != newCountries)
		|| (_flags != newFlags);
	const auto changed2 = (answers != newAnswers);
	if (!changed1 && !changed2) {
		return false;
	}
	if (changed1) {
		question = newQuestion;
		closeDate = newCloseDate;
		closePeriod = newClosePeriod;
		countries = std::move(newCountries);
		_flags = newFlags;
	}
	if (changed2) {
		std::swap(answers, newAnswers);
		for (const auto &old : newAnswers) {
			if (const auto current = answerByOption(old.option)) {
				current->votes = old.votes;
				current->chosen = old.chosen;
				current->correct = old.correct;
			}
		}
	}
	hash = poll.vhash().v;
	++version;
	return true;
}

bool PollData::applyResults(const MTPPollResults &results) {
	return results.match([&](const MTPDpollResults &results) {
		_lastResultsUpdate = crl::now();

		const auto newTotalVoters
			= results.vtotal_voters().value_or(totalVoters);
		auto changed = (newTotalVoters != totalVoters);
		const auto setCanViewStats = [&](bool value) {
			const auto previous = (_flags & Flag::CanViewStats);
			if (bool(previous) == value) {
				return;
			}
			if (value) {
				_flags |= Flag::CanViewStats;
			} else {
				_flags &= ~Flag::CanViewStats;
			}
			changed = true;
		};
		if (!results.is_min() || results.is_can_view_stats()) {
			setCanViewStats(results.is_can_view_stats());
		}
		if (const auto list = results.vresults()) {
			for (const auto &result : list->v) {
				if (applyResultToAnswers(result, results.is_min())) {
					changed = true;
				}
			}
		} else if (!results.is_min()
			&& newTotalVoters == 0
			&& voted()) {
			for (auto &answer : answers) {
				if (answer.chosen) {
					answer.chosen = false;
					changed = true;
				}
			}
		}
		if (const auto recent = results.vrecent_voters()) {
			const auto recentChanged = !ranges::equal(
				recentVoters,
				recent->v,
				ranges::equal_to(),
				&PeerData::id,
				peerFromMTP);
			if (recentChanged) {
				changed = true;
				recentVoters = ranges::views::all(
					recent->v
				) | ranges::views::transform([&](MTPPeer peerId) {
					const auto peer = _owner->peer(peerFromMTP(peerId));
					return peer->isMinimalLoaded() ? peer.get() : nullptr;
				}) | ranges::views::filter([](PeerData *peer) {
					return peer != nullptr;
				}) | ranges::views::transform([](PeerData *peer) {
					return not_null(peer);
				}) | ranges::to_vector;
			}
		}
		if (results.vsolution()) {
			auto newSolution = TextWithEntities{
				results.vsolution().value_or_empty(),
				Api::EntitiesFromMTP(
					&_owner->session(),
					results.vsolution_entities().value_or_empty())
			};
			if (solution != newSolution) {
				solution = std::move(newSolution);
				changed = true;
			}
		}
		if (const auto media = results.vsolution_media()) {
			ProcessPollMedia(_owner, *media);
			const auto parsed = PollMediaFromMTP(_owner, *media);
			if (solutionMedia != parsed) {
				solutionMedia = parsed;
				changed = true;
			}
		}
		if (!changed) {
			return false;
		}
		totalVoters = newTotalVoters;
		++version;
		return changed;
	});
}

bool PollData::checkResultsReload(crl::time now) {
	if (_lastResultsUpdate > 0
		&& _lastResultsUpdate + kShortPollTimeout > now) {
		return false;
	} else if (closed() && _lastResultsUpdate >= 0) {
		return false;
	}
	_lastResultsUpdate = now;
	return true;
}

PollAnswer *PollData::answerByOption(const QByteArray &option) {
	return AnswerByOption(answers, option);
}

const PollAnswer *PollData::answerByOption(const QByteArray &option) const {
	return AnswerByOption(answers, option);
}

bool PollData::applyResultToAnswers(
		const MTPPollAnswerVoters &result,
		bool isMinResults) {
	return result.match([&](const MTPDpollAnswerVoters &voters) {
		const auto &option = voters.voption().v;
		const auto answer = answerByOption(option);
		if (!answer) {
			return false;
		}
		auto changed = false;
		if (const auto count = voters.vvoters()) {
			if (answer->votes != count->v) {
				answer->votes = count->v;
				changed = true;
			}
		}
		if (!isMinResults) {
			if (answer->chosen != voters.is_chosen()) {
				answer->chosen = voters.is_chosen();
				changed = true;
			}
		}
		if (voters.is_correct() && !answer->correct) {
			answer->correct = voters.is_correct();
			changed = true;
		}
		if (const auto recent = voters.vrecent_voters()) {
			const auto recentChanged = !ranges::equal(
				answer->recentVoters,
				recent->v,
				ranges::equal_to(),
				&PeerData::id,
				peerFromMTP);
			if (recentChanged) {
				changed = true;
				answer->recentVoters = ranges::views::all(
					recent->v
				) | ranges::views::transform([&](MTPPeer peerId) {
					const auto peer = _owner->peer(
						peerFromMTP(peerId));
					return peer->isMinimalLoaded()
						? peer.get()
						: nullptr;
				}) | ranges::views::filter([](PeerData *peer) {
					return peer != nullptr;
				}) | ranges::views::transform([](PeerData *peer) {
					return not_null(peer);
				}) | ranges::to_vector;
			}
		}
		return changed;
	});
}

void PollData::setFlags(Flags flags) {
	if (_flags != flags) {
		_flags = flags;
		++version;
	}
}

PollData::Flags PollData::flags() const {
	return _flags;
}

bool PollData::voted() const {
	return ranges::contains(answers, true, &PollAnswer::chosen);
}

bool PollData::closed() const {
	return (_flags & Flag::Closed);
}

bool PollData::publicVotes() const {
	return (_flags & Flag::PublicVotes);
}

bool PollData::multiChoice() const {
	return (_flags & Flag::MultiChoice);
}

bool PollData::quiz() const {
	return (_flags & Flag::Quiz);
}

bool PollData::shuffleAnswers() const {
	return (_flags & Flag::ShuffleAnswers);
}

bool PollData::revotingDisabled() const {
	return (_flags & Flag::RevotingDisabled);
}

bool PollData::openAnswers() const {
	return (_flags & Flag::OpenAnswers);
}

bool PollData::hideResultsUntilClose() const {
	return (_flags & Flag::HideResultsUntilClose);
}

bool PollData::creator() const {
	return (_flags & Flag::Creator);
}

bool PollData::subscribersOnly() const {
	return (_flags & Flag::SubscribersOnly);
}

bool PollData::canViewStats() const {
	return (_flags & Flag::CanViewStats);
}

void PollData::setVoteRestriction(VoteRestriction restriction) {
	_voteRestrictionUpdated = (restriction == VoteRestriction::None)
		? 0
		: crl::now();
	if (_voteRestriction != restriction) {
		_voteRestriction = restriction;
		++version;
	}
}

PollData::VoteRestriction PollData::voteRestriction() const {
	return _voteRestriction;
}

crl::time PollData::voteRestrictionUpdated() const {
	return _voteRestrictionUpdated;
}

QString PollData::debugString() const {
	auto result = QString();
	result += u"Poll #"_q + QString::number(id) + u'\n';
	result += u"Q: "_q + question.text + u'\n';
	if (quiz()) {
		result += u"[Quiz]"_q;
	}
	if (multiChoice()) {
		result += u"[MultiChoice]"_q;
	}
	if (closed()) {
		result += u"[Closed]"_q;
	}
	if (publicVotes()) {
		result += u"[PublicVotes]"_q;
	}
	if (subscribersOnly()) {
		result += u"[SubscribersOnly]"_q;
	}
	if (canViewStats()) {
		result += u"[CanViewStats]"_q;
	}
	if (!result.endsWith(u'\n')) {
		result += u'\n';
	}
	result += u"Total voters: "_q + QString::number(totalVoters) + u'\n';
	for (const auto &answer : answers) {
		result += u"  - "_q + answer.text.text
			+ u" ["_q + QString::number(answer.votes) + u" votes"_q;
		if (answer.chosen) {
			result += u", chosen"_q;
		}
		if (answer.correct) {
			result += u", correct"_q;
		}
		result += u"]\n"_q;
	}
	if (!solution.text.isEmpty()) {
		result += u"Solution: "_q + solution.text + u'\n';
	}
	if (!countries.empty()) {
		result += u"Countries: "_q + countries.front();
		for (auto i = 1, count = int(countries.size()); i != count; ++i) {
			result += u", "_q + countries[i];
		}
		result += u'\n';
	}
	return result;
}

MTPInputMedia PollMediaToMTP(const PollMedia &media) {
	if (media.photo) {
		return MTP_inputMediaPhoto(
			MTP_flags(MTPDinputMediaPhoto::Flag(0)),
			media.photo->mtpInput(),
			MTP_int(0),
			MTPInputDocument());
	} else if (media.document) {
		return MTP_inputMediaDocument(
			MTP_flags(MTPDinputMediaDocument::Flag(0)),
			media.document->mtpInput(),
			MTPInputPhoto(),
			MTP_int(0),
			MTP_int(0),
			MTPstring());
	} else if (media.geo) {
		return MTP_inputMediaGeoPoint(
			MTP_inputGeoPoint(
				MTP_flags(0),
				MTP_double(media.geo->lat()),
				MTP_double(media.geo->lon()),
				MTPint())); // accuracy_radius
	} else if (!media.url.isEmpty()) {
		return MTP_inputMediaWebPage(
			MTP_flags(MTPDinputMediaWebPage::Flag::f_optional),
			MTP_string(media.url));
	}
	return MTPInputMedia();
}

PollMedia PollMediaFromMTP(
		not_null<Data::Session*> owner,
		const MTPMessageMedia &media) {
	auto result = PollMedia();
	media.match([&](const MTPDmessageMediaPhoto &data) {
		if (const auto photo = data.vphoto()) {
			photo->match([&](const MTPDphoto &) {
				result.photo = owner->processPhoto(*photo);
			}, [](const auto &) {
			});
		}
	}, [&](const MTPDmessageMediaDocument &data) {
		if (const auto document = data.vdocument()) {
			document->match([&](const MTPDdocument &) {
				result.document = owner->processDocument(*document);
			}, [](const auto &) {
			});
		}
	}, [&](const MTPDmessageMediaGeo &data) {
		data.vgeo().match([&](const MTPDgeoPoint &point) {
			result.geo = Data::LocationPoint(point);
		}, [](const MTPDgeoPointEmpty &) {
		});
	}, [&](const MTPDmessageMediaVenue &data) {
		data.vgeo().match([&](const MTPDgeoPoint &point) {
			result.geo = Data::LocationPoint(point);
		}, [](const MTPDgeoPointEmpty &) {
		});
	}, [&](const MTPDmessageMediaWebPage &data) {
		data.vwebpage().match([&](const MTPDwebPage &page) {
			result.webpage = owner->processWebpage(page);
		}, [&](const MTPDwebPagePending &page) {
			result.webpage = owner->processWebpage(page);
		}, [&](const MTPDwebPageEmpty &page) {
			if (const auto url = page.vurl()) {
				result.url = qs(*url);
			}
		}, [&](const MTPDwebPageNotModified &page) {
		});
		if (result.webpage && !result.webpage->url.isEmpty()) {
			result.url = result.webpage->url;
		}
	}, [](const auto &) {
	});
	return result;
}

PollMedia PollMediaFromInputMTP(
		not_null<Data::Session*> owner,
		const MTPInputMedia &media) {
	auto result = PollMedia();
	media.match([&](const MTPDinputMediaPhoto &data) {
		data.vid().match([&](const MTPDinputPhoto &photo) {
			result.photo = owner->photo(photo.vid().v);
		}, [](const auto &) {
		});
	}, [&](const MTPDinputMediaDocument &data) {
		data.vid().match([&](const MTPDinputDocument &document) {
			result.document = owner->document(document.vid().v);
		}, [](const auto &) {
		});
	}, [&](const MTPDinputMediaGeoPoint &data) {
		data.vgeo_point().match([&](const MTPDinputGeoPoint &point) {
			result.geo.emplace(
				point.vlat().v,
				point.vlong().v,
				Data::LocationPoint::NoAccessHash);
		}, [](const auto &) {
		});
	}, [&](const MTPDinputMediaWebPage &data) {
		result.url = qs(data.vurl());
	}, [](const auto &) {
	});
	return result;
}

QByteArray PollOptionFromLink(const QString &value) {
	return QByteArray::fromBase64(value.toLatin1());
}

QString PollOptionToLink(const QByteArray &option) {
	return QString::fromLatin1(
		option.toBase64(QByteArray::OmitTrailingEquals));
}

MTPPoll PollDataToMTP(not_null<const PollData*> poll, bool close) {
	const auto convert = [&](const PollAnswer &answer) {
		const auto flags = answer.media
			? MTPDinputPollAnswer::Flag::f_media
			: MTPDinputPollAnswer::Flag(0);
		return MTP_inputPollAnswer(
			MTP_flags(flags),
			MTP_textWithEntities(
				MTP_string(answer.text.text),
				Api::EntitiesToMTP(&poll->session(), answer.text.entities)),
			answer.media
				? PollMediaToMTP(answer.media)
				: MTPInputMedia());
	};
	auto answers = QVector<MTPPollAnswer>();
	answers.reserve(poll->answers.size());
	ranges::transform(
		poll->answers,
		ranges::back_inserter(answers),
		convert);
	auto countries = QVector<MTPstring>();
	countries.reserve(poll->countries.size());
	for (const auto &country : poll->countries) {
		countries.push_back(MTP_string(country));
	}
	using Flag = MTPDpoll::Flag;
	const auto flags = ((poll->closed() || close) ? Flag::f_closed : Flag(0))
		| (poll->multiChoice() ? Flag::f_multiple_choice : Flag(0))
		| (poll->publicVotes() ? Flag::f_public_voters : Flag(0))
		| (poll->quiz() ? Flag::f_quiz : Flag(0))
		| (poll->shuffleAnswers() ? Flag::f_shuffle_answers : Flag(0))
		| (poll->revotingDisabled() ? Flag::f_revoting_disabled : Flag(0))
		| (poll->openAnswers() ? Flag::f_open_answers : Flag(0))
		| (poll->hideResultsUntilClose()
			? Flag::f_hide_results_until_close
			: Flag(0))
		| (poll->subscribersOnly() ? Flag::f_subscribers_only : Flag(0))
		| (poll->closePeriod > 0 ? Flag::f_close_period : Flag(0))
		| (poll->closeDate > 0 ? Flag::f_close_date : Flag(0))
		| (countries.isEmpty() ? Flag(0) : Flag::f_countries_iso2);
	return MTP_poll(
		MTP_long(poll->id),
		MTP_flags(flags),
		MTP_textWithEntities(
			MTP_string(poll->question.text),
			Api::EntitiesToMTP(&poll->session(), poll->question.entities)),
		MTP_vector<MTPPollAnswer>(answers),
		MTP_int(poll->closePeriod),
		MTP_int(poll->closeDate),
		MTP_vector<MTPstring>(std::move(countries)), // countries_iso2
		MTP_long(0));
}

MTPInputMedia PollDataToInputMedia(
		not_null<const PollData*> poll,
		bool close) {
	auto inputFlags = MTPDinputMediaPoll::Flag(0)
		| (poll->quiz()
			? MTPDinputMediaPoll::Flag::f_correct_answers
			: MTPDinputMediaPoll::Flag(0));
	auto correct = QVector<MTPint>();
	for (auto i = 0, count = int(poll->answers.size()); i < count; ++i) {
		if (poll->answers[i].correct) {
			correct.push_back(MTP_int(i));
		}
	}

	auto solution = poll->solution;
	const auto prepareFlags = Ui::ItemTextDefaultOptions().flags;
	TextUtilities::PrepareForSending(solution, prepareFlags);
	TextUtilities::Trim(solution);
	const auto sentEntities = Api::EntitiesToMTP(
		&poll->session(),
		solution.entities,
		Api::ConvertOption::SkipLocal);
	if (!solution.text.isEmpty()) {
		inputFlags |= MTPDinputMediaPoll::Flag::f_solution;
	}
	if (!sentEntities.v.isEmpty()) {
		inputFlags |= MTPDinputMediaPoll::Flag::f_solution_entities;
	}
	if (poll->attachedMedia) {
		inputFlags |= MTPDinputMediaPoll::Flag::f_attached_media;
	}
	if (poll->solutionMedia) {
		inputFlags |= MTPDinputMediaPoll::Flag::f_solution_media;
	}
	return MTP_inputMediaPoll(
		MTP_flags(inputFlags),
		PollDataToMTP(poll, close),
		MTP_vector<MTPint>(correct),
		poll->attachedMedia
			? PollMediaToMTP(poll->attachedMedia)
			: MTPInputMedia(),
		MTP_string(solution.text),
		sentEntities,
		poll->solutionMedia
			? PollMediaToMTP(poll->solutionMedia)
			: MTPInputMedia());
}

QString JoinPollCountries(const std::vector<QString> &countriesIso2) {
	auto countries = QStringList();
	countries.reserve(int(countriesIso2.size()));
	const auto &instance = Countries::Instance();
	for (const auto &iso2 : countriesIso2) {
		const auto name = instance.countryNameByISO2(
			iso2,
			Countries::Naming::Polls);
		countries.push_back(name.isEmpty() ? iso2 : name);
	}
	if (countries.empty()) {
		return QString();
	}
	auto result = countries.front();
	for (auto i = 1, count = int(countries.size()); i != count; ++i) {
		result = ((i + 1 == count)
			? tr::lng_prizes_countries_and_last
			: tr::lng_prizes_countries_and_one)(
				tr::now,
				lt_countries,
				result,
				lt_country,
				countries[i]);
	}
	return result;
}

TextWithEntities PollCountriesRestrictionText(
		const std::vector<QString> &countries) {
	const auto joined = JoinPollCountries(countries);
	return joined.isEmpty()
		? tr::lng_polls_vote_restricted_countries(tr::now, tr::rich)
		: tr::lng_polls_vote_restricted_countries_list(
			tr::now,
			lt_countries,
			tr::bold(joined),
			tr::rich);
}

TextWithEntities PollVoteRestrictionText(
		PollData::VoteRestriction restriction,
		not_null<PeerData*> peer,
		not_null<const PollData*> poll) {
	switch (restriction) {
	case PollData::VoteRestriction::SubscribersOnly: {
		const auto channel = peer->name();
		return channel.isEmpty()
			? tr::lng_polls_vote_restricted_subscribers(tr::now, tr::rich)
			: tr::lng_polls_vote_restricted_subscribers_channel(
				tr::now,
				lt_channel,
				tr::bold(channel),
				tr::rich);
	}
	case PollData::VoteRestriction::SubscribersJoinedTooRecently:
		return tr::lng_polls_vote_restricted_subscribers_recent(
			tr::now,
			tr::rich);
	case PollData::VoteRestriction::Countries:
		return PollCountriesRestrictionText(poll->countries);
	case PollData::VoteRestriction::None:
		break;
	}
	return {};
}
