/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "export/view/export_view_content.h"

#include "lang/lang_keys.h"

namespace Export {
namespace View {

Content ContentFromState(const ProcessingState &state) {
	using Step = ProcessingState::Step;

	auto result = Content();
	const auto push = [&](
			const QString &id,
			const QString &label,
			const QString &info,
			float64 progress) {
		result.rows.push_back({ id, label, info, progress });
	};
	switch (state.step) {
	case Step::Initializing:
	case Step::LeftChannelsList:
	case Step::DialogsList:
	case Step::PersonalInfo:
	case Step::Userpics:
	case Step::Contacts:
	case Step::Sessions:
	case Step::LeftChannels:
	case Step::Dialogs:
		push("init", lang(lng_export_state_initializing), QString(), 0.);
		if (state.entityCount > 0) {
			push("entity", QString(), QString::number(state.entityIndex) + '/' + QString::number(state.entityCount), 0.);
		}
		if (state.itemCount > 0) {
			push("item", QString(), QString::number(state.itemIndex) + '/' + QString::number(state.itemCount), 0.);
		}
		if (state.bytesCount > 0) {
			push("bytes", QString(), QString::number(state.bytesLoaded) + '/' + QString::number(state.bytesCount), 0.);
		}
		break;
	default: Unexpected("Step in ContentFromState.");
	}
	return result;
}

} // namespace View
} // namespace Export
