/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_report.h"

#include "apiwrap.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_channel.h"
#include "data/data_photo.h"
#include "data/data_report.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/report_box_graphics.h"
#include "ui/layers/show.h"

namespace Api {

namespace {

[[nodiscard]] ReportResult ParseReportResult(const MTPReportResult &result) {
	return result.match([&](const MTPDreportResultChooseOption &data) {
		auto list = ReportResult::Options();
		list.reserve(data.voptions().v.size());
		for (const auto &tl : data.voptions().v) {
			list.emplace_back(ReportResult::Option{
				.id = tl.data().voption().v,
				.text = qs(tl.data().vtext()),
			});
		}
		return ReportResult{
			.options = std::move(list),
			.title = qs(data.vtitle()),
		};
	}, [&](const MTPDreportResultAddComment &data) -> ReportResult {
		return {
			.commentOption = ReportResult::CommentOption{
				.optional = data.is_optional(),
				.id = data.voption().v,
			}
		};
	}, [&](const MTPDreportResultReported &data) -> ReportResult {
		return { .successful = true };
	});
}

MTPreportReason ReasonToTL(const Ui::ReportReason &reason) {
	using Reason = Ui::ReportReason;
	switch (reason) {
	case Reason::Spam: return MTP_inputReportReasonSpam();
	case Reason::Fake: return MTP_inputReportReasonFake();
	case Reason::Violence: return MTP_inputReportReasonViolence();
	case Reason::ChildAbuse: return MTP_inputReportReasonChildAbuse();
	case Reason::Pornography: return MTP_inputReportReasonPornography();
	case Reason::Copyright: return MTP_inputReportReasonCopyright();
	case Reason::IllegalDrugs: return MTP_inputReportReasonIllegalDrugs();
	case Reason::PersonalDetails:
		return MTP_inputReportReasonPersonalDetails();
	case Reason::Other: return MTP_inputReportReasonOther();
	}
	Unexpected("Bad reason group value.");
}

} // namespace

void SendPhotoReport(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		Ui::ReportReason reason,
		const QString &comment,
		not_null<PhotoData*> photo) {
	peer->session().api().request(MTPaccount_ReportProfilePhoto(
		peer->input(),
		photo->mtpInput(),
		ReasonToTL(reason),
		MTP_string(comment)
	)).done([=] {
		show->showToast(tr::lng_report_thanks(tr::now));
	}).send();
}

auto CreateReportMessagesOrStoriesCallback(
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer)
-> Fn<void(Data::ReportInput, Fn<void(ReportResult)>)> {
	struct State final {
#ifdef _DEBUG
		~State() {
			qDebug() << "Messages or Stories Report ~State().";
		}
#endif
		mtpRequestId requestId = 0;
	};
	const auto state = std::make_shared<State>();

	return [=](
			Data::ReportInput reportInput,
			Fn<void(ReportResult)> done) {
		auto apiIds = QVector<MTPint>();
		apiIds.reserve(reportInput.ids.size() + reportInput.stories.size());
		for (const auto &id : reportInput.ids) {
			apiIds.push_back(MTP_int(id));
		}
		for (const auto &story : reportInput.stories) {
			apiIds.push_back(MTP_int(story));
		}

		const auto received = [=](
				const MTPReportResult &result,
				mtpRequestId requestId) {
			if (state->requestId != requestId) {
				return;
			}
			state->requestId = 0;
			done(ParseReportResult(result));
		};

		const auto fail = [=](const MTP::Error &error) {
			state->requestId = 0;
			done({ .error = error.type() });
		};

		if (!reportInput.stories.empty()) {
			state->requestId = peer->session().api().request(
				MTPstories_Report(
					peer->input(),
					MTP_vector<MTPint>(apiIds),
					MTP_bytes(reportInput.optionId),
					MTP_string(reportInput.comment))
			).done(received).fail(fail).send();
		} else {
			state->requestId = peer->session().api().request(
				MTPmessages_Report(
					peer->input(),
					MTP_vector<MTPint>(apiIds),
					MTP_bytes(reportInput.optionId),
					MTP_string(reportInput.comment))
			).done(received).fail(fail).send();
		}
	};
}

auto CreateReportEphemeralMessageCallback(
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer,
	int32 ephemeralId)
-> Fn<void(Data::ReportInput, Fn<void(ReportResult)>)> {
	struct State final {
		mtpRequestId requestId = 0;
	};
	const auto state = std::make_shared<State>();

	return [=](
			Data::ReportInput reportInput,
			Fn<void(ReportResult)> done) {
		const auto received = [=](
				const MTPReportResult &result,
				mtpRequestId requestId) {
			if (state->requestId != requestId) {
				return;
			}
			state->requestId = 0;
			done(ParseReportResult(result));
		};

		const auto fail = [=](const MTP::Error &error) {
			state->requestId = 0;
			done({ .error = error.type() });
		};

		state->requestId = peer->session().api().request(
			MTPephemeral_ReportMessage(
				peer->input(),
				MTP_int(ephemeralId),
				MTP_bytes(reportInput.optionId),
				MTP_string(reportInput.comment))
		).done(received).fail(fail).send();
	};
}

ReactionReportCapabilities GetReactionReportCapabilities(
		not_null<PeerData*> group,
		not_null<PeerData*> participant) {
	const auto channel = group->asMegagroup();
	return channel
		? ReactionReportCapabilities{
			.canReport = channel->isPublic() && !participant->isSelf(),
			.canBan = channel->canRestrictParticipant(participant),
		}
		: ReactionReportCapabilities();
}

void ReportReaction(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> group,
		MsgId messageId,
		not_null<PeerData*> participant) {
	group->session().api().request(MTPmessages_ReportReaction(
		group->input(),
		MTP_int(messageId.bare),
		participant->input()
	)).done([=] {
		if (show) {
			show->showToast(tr::lng_report_thanks(tr::now));
		}
	}).send();
}

void ReportSpam(
		not_null<PeerData*> sender,
		const MessageIdsList &ids) {
	if (ids.empty()) {
		return;
	}
	const auto peer = sender->owner().peer(ids.front().peer);
	const auto channel = peer->asChannel();
	if (!channel) {
		return;
	}

	auto msgIds = QVector<MTPint>();
	msgIds.reserve(ids.size());
	for (const auto &fullId : ids) {
		msgIds.push_back(MTP_int(fullId.msg));
	}

	sender->session().api().request(MTPchannels_ReportSpam(
		channel->inputChannel(),
		sender->input(),
		MTP_vector<MTPint>(msgIds)
	)).send();
}

} // namespace Api
