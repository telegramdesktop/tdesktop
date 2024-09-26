/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_report.h"

#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_report.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/report_box_graphics.h"
#include "ui/layers/show.h"

namespace Api {

namespace {

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

void SendReport(
		std::shared_ptr<Ui::Show> show,
		not_null<PeerData*> peer,
		Ui::ReportReason reason,
		const QString &comment,
		std::variant<v::null_t, not_null<PhotoData*>> data) {
	auto done = [=] {
		show->showToast(tr::lng_report_thanks(tr::now));
	};
	v::match(data, [&](v::null_t) {
		peer->session().api().request(MTPaccount_ReportPeer(
			peer->input,
			ReasonToTL(reason),
			MTP_string(comment)
		)).done(std::move(done)).send();
	}, [&](not_null<PhotoData*> photo) {
		peer->session().api().request(MTPaccount_ReportProfilePhoto(
			peer->input,
			photo->mtpInput(),
			ReasonToTL(reason),
			MTP_string(comment)
		)).done(std::move(done)).send();
	});
}

auto CreateReportMessagesOrStoriesCallback(
	std::shared_ptr<Ui::Show> show,
	not_null<PeerData*> peer)
-> Fn<void(Data::ReportInput, Fn<void(ReportResult)>)> {
	using TLChoose = MTPDreportResultChooseOption;
	using TLAddComment = MTPDreportResultAddComment;
	using TLReported = MTPDreportResultReported;
	using Result = ReportResult;

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
			Fn<void(Result)> done) {
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
			done(result.match([&](const TLChoose &data) {
				const auto t = qs(data.vtitle());
				auto list = Result::Options();
				list.reserve(data.voptions().v.size());
				for (const auto &tl : data.voptions().v) {
					list.emplace_back(Result::Option{
						.id = tl.data().voption().v,
						.text = qs(tl.data().vtext()),
					});
				}
				return Result{ .options = std::move(list), .title = t };
			}, [&](const TLAddComment &data) -> Result {
				return {
					.commentOption = ReportResult::CommentOption{
						.optional = data.is_optional(),
						.id = data.voption().v,
					}
				};
			}, [&](const TLReported &data) -> Result {
				return { .successful = true };
			}));
		};

		const auto fail = [=](const MTP::Error &error) {
			state->requestId = 0;
			done({ .error = error.type() });
		};

		if (!reportInput.stories.empty()) {
			state->requestId = peer->session().api().request(
				MTPstories_Report(
					peer->input,
					MTP_vector<MTPint>(apiIds),
					MTP_bytes(reportInput.optionId),
					MTP_string(reportInput.comment))
			).done(received).fail(fail).send();
		} else {
			state->requestId = peer->session().api().request(
				MTPmessages_Report(
					peer->input,
					MTP_vector<MTPint>(apiIds),
					MTP_bytes(reportInput.optionId),
					MTP_string(reportInput.comment))
			).done(received).fail(fail).send();
		}
	};
}

} // namespace Api
