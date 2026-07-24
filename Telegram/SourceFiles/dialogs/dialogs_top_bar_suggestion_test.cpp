/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "dialogs/dialogs_widget.h"

#ifdef _DEBUG

#include "api/api_authorizations.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/random.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_inner_widget.h"
#include "dialogs/dialogs_top_bar_suggestion.h"
#include "dialogs/ui/dialogs_top_bar_suggestion_content.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/controls/userpic_button.h"
#include "ui/painter.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/elastic_scroll.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"

#include <QShortcut>

namespace Dialogs {

void Widget::setupTopBarSuggestionTestHotkeys() {
	const auto install = [this](Ui::SlideWrap<Ui::RpWidget> *wrap) {
		_topBarSuggestionPlaceholder = nullptr;
		_topBarSuggestion = nullptr;
		if (!wrap) {
			_scroll->setBarTopInset(0);
			_topBarSuggestionHeightChanged.fire(0);
			return;
		}
		_topBarSuggestion.reset(wrap);
		_topBarSuggestion->toggle(false, anim::type::instant);
		MountTopBarSuggestion({
			.scroll = _scroll,
			.innerList = _innerList,
			.wrap = _topBarSuggestion.get(),
			.placeholder = &_topBarSuggestionPlaceholder,
			.heightChanged = [this](int h) {
				_topBarSuggestionHeightChanged.fire_copy(h);
			},
		});
		_topBarSuggestion->toggle(true, anim::type::normal);
	};

	const auto hideAndCleanup = [this] {
		if (!_topBarSuggestion) {
			return;
		}
		_topBarSuggestion->toggle(false, anim::type::normal);
		const auto wrap = _topBarSuggestion.get();
		base::call_delayed(st::slideWrapDuration * 2, wrap, [=, this] {
			_topBarSuggestionPlaceholder = nullptr;
			_topBarSuggestion = nullptr;
			_scroll->setBarTopInset(0);
			_topBarSuggestionHeightChanged.fire(0);
		});
	};

	const auto injectTestAuth = [this] {
		using Flag = MTPDupdateNewAuthorization::Flag;
		session().api().authorizations().apply(
			MTP_updateNewAuthorization(
				MTP_flags(Flag::f_unconfirmed),
				MTP_long(base::RandomValue<uint64>()),
				MTP_int(base::unixtime::now()),
				MTP_string("Test Device"),
				MTP_string("Test Location")));
	};

	const auto installBirthdayTest = [=, this] {
		using RightIcon = TopBarSuggestionContent::RightIcon;
		const auto content = Ui::CreateChild<TopBarSuggestionContent>(
			this);
		content->setRightIcon(RightIcon::Close);
		const auto user = session().user();
		content->setContent(
			tr::lng_dialogs_suggestions_birthday_contact_title(
				tr::now,
				lt_text,
				{ user->shortName() },
				tr::rich),
			tr::lng_dialogs_suggestions_birthday_contact_about(
				tr::now,
				TextWithEntities::Simple));
		content->setLeadingWidget(Ui::CreateChild<Ui::UserpicButton>(
			content,
			user,
			st::uploadUserpicButton));
		content->setHideCallback(hideAndCleanup);
		content->setNarrowExpandCallback(ExpandChatsListCallback(this));
		content->setCollapseProgress(_childListShown.value());
		_prepareTopBarSnapshot.events(
		) | rpl::on_next([content] {
			content->prepareCollapseSnapshot();
		}, content->lifetime());
		const auto wrap = Ui::CreateChild<
			Ui::SlideWrap<Ui::RpWidget>>(
				this,
				object_ptr<Ui::RpWidget>::fromRaw(content));
		install(wrap);
	};

	const auto regularShortcut = new QShortcut(
		QKeySequence(u"Ctrl+Shift+T"_q),
		this);
	QObject::connect(
		regularShortcut,
		&QShortcut::activated,
		this,
		installBirthdayTest);

	const auto authShortcut = new QShortcut(
		QKeySequence(u"Ctrl+Shift+A"_q),
		this);
	QObject::connect(
		authShortcut,
		&QShortcut::activated,
		this,
		injectTestAuth);
}

} // namespace Dialogs

#endif // _DEBUG
