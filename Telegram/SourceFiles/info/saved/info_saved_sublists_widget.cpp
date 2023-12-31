/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/saved/info_saved_sublists_widget.h"
//
#include "data/data_saved_messages.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_inner_widget.h"
#include "history/view/history_view_sublist_section.h"
#include "info/media/info_media_buttons.h"
#include "info/profile/info_profile_icon.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_info.h"

namespace Info::Saved {

SublistsMemento::SublistsMemento(not_null<Main::Session*> session)
: ContentMemento(session->user(), nullptr, PeerId()) {
}

Section SublistsMemento::section() const {
	return Section(Section::Type::SavedSublists);
}

object_ptr<ContentWidget> SublistsMemento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<SublistsWidget>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

SublistsMemento::~SublistsMemento() = default;

SublistsWidget::SublistsWidget(
	QWidget *parent,
	not_null<Controller*> controller)
: ContentWidget(parent, controller)
, _layout(setInnerWidget(object_ptr<Ui::VerticalLayout>(this))) {
	setupOtherTypes();

	_list = _layout->add(object_ptr<Dialogs::InnerWidget>(
		this,
		controller->parentController(),
		rpl::single(Dialogs::InnerWidget::ChildListShown())));
	_list->showSavedSublists();
	_list->setNarrowRatio(0.);

	_list->chosenRow() | rpl::start_with_next([=](Dialogs::ChosenRow row) {
		if (const auto sublist = row.key.sublist()) {
			controller->showSection(
				std::make_shared<HistoryView::SublistMemento>(sublist),
				Window::SectionShow::Way::Forward);
		}
	}, _list->lifetime());

	const auto saved = &controller->session().data().savedMessages();
	_list->heightValue() | rpl::start_with_next([=] {
		if (!saved->supported()) {
			crl::on_main(controller, [=] {
				controller->showSection(
					Memento::Default(controller->session().user()),
					Window::SectionShow::Way::Backward);
			});
		}
	}, lifetime());

	_list->setLoadMoreCallback([=] {
		saved->loadMore();
	});
}

void SublistsWidget::setupOtherTypes() {
	auto wrap = _layout->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_layout,
			object_ptr<Ui::VerticalLayout>(_layout)));
	auto content = wrap->entity();
	content->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		st::infoProfileSkip));

	using Type = Media::Type;
	auto tracker = Ui::MultiSlideTracker();
	const auto peer = controller()->session().user();
	const auto addMediaButton = [&](
			Type buttonType,
			const style::icon &icon) {
		auto result = Media::AddButton(
			content,
			controller(),
			peer,
			MsgId(), // topicRootId
			nullptr, // migrated
			buttonType,
			tracker);
		object_ptr<Profile::FloatingIcon>(
			result,
			icon,
			st::infoSharedMediaButtonIconPosition)->show();
	};

	addMediaButton(Type::Photo, st::infoIconMediaPhoto);
	addMediaButton(Type::Video, st::infoIconMediaVideo);
	addMediaButton(Type::File, st::infoIconMediaFile);
	addMediaButton(Type::MusicFile, st::infoIconMediaAudio);
	addMediaButton(Type::Link, st::infoIconMediaLink);
	addMediaButton(Type::RoundVoiceFile, st::infoIconMediaVoice);
	addMediaButton(Type::GIF, st::infoIconMediaGif);

	content->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		st::infoProfileSkip));
	wrap->toggleOn(tracker.atLeastOneShownValue());
	wrap->finishAnimating();

	_layout->add(object_ptr<Ui::BoxContentDivider>(_layout));
	_layout->add(object_ptr<Ui::FixedHeightWidget>(
		content,
		st::infoProfileSkip));
}

rpl::producer<QString> SublistsWidget::title() {
	return tr::lng_saved_messages();
}

rpl::producer<QString> SublistsWidget::subtitle() {
	const auto saved = &controller()->session().data().savedMessages();
	return saved->chatsList()->fullSize().value(
	) | rpl::map([=](int value) {
		return (value || saved->chatsList()->loaded())
			? tr::lng_filters_chats_count(tr::now, lt_count, value)
			: tr::lng_contacts_loading(tr::now);
	});
}

bool SublistsWidget::showInternal(not_null<ContentMemento*> memento) {
	if (!controller()->validateMementoPeer(memento)) {
		return false;
	}
	if (auto my = dynamic_cast<SublistsMemento*>(memento.get())) {
		restoreState(my);
		return true;
	}
	return false;
}

void SublistsWidget::setInternalState(
		const QRect &geometry,
		not_null<SublistsMemento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> SublistsWidget::doCreateMemento() {
	auto result = std::make_shared<SublistsMemento>(
		&controller()->session());
	saveState(result.get());
	return result;
}

void SublistsWidget::saveState(not_null<SublistsMemento*> memento) {
	memento->setScrollTop(scrollTopSave());
}

void SublistsWidget::restoreState(not_null<SublistsMemento*> memento) {
	scrollTopRestore(memento->scrollTop());
}

} // namespace Info::Saved
