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
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/report_box.h"
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
	std::variant<
		v::null_t,
		MessageIdsList,
		not_null<PhotoData*>,
		StoryId> data) {
	auto done = [=] {
		show->showToast(tr::lng_report_thanks(tr::now));
	};
	v::match(data, [&](v::null_t) {
		peer->session().api().request(MTPaccount_ReportPeer(
			peer->input,
			ReasonToTL(reason),
			MTP_string(comment)
		)).done(std::move(done)).send();
	}, [&](const MessageIdsList &ids) {
		auto apiIds = QVector<MTPint>();
		apiIds.reserve(ids.size());
		for (const auto &fullId : ids) {
			apiIds.push_back(MTP_int(fullId.msg));
		}
		peer->session().api().request(MTPmessages_Report(
			peer->input,
			MTP_vector<MTPint>(apiIds),
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
	}, [&](StoryId id) {
		peer->session().api().request(MTPstories_Report(
			peer->input,
			MTP_vector<MTPint>(1, MTP_int(id)),
			ReasonToTL(reason),
			MTP_string(comment)
		)).done(std::move(done)).send();
	});
}

} // namespace Api
