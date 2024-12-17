/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/global_media/info_global_media_widget.h"

#include "info/global_media/info_global_media_inner_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/search_field_controller.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/scroll_area.h"
#include "ui/ui_utility.h"
#include "data/data_download_manager.h"
#include "data/data_user.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Info::GlobalMedia {

Memento::Memento(not_null<Controller*> controller)
: ContentMemento(Tag{ controller->session().user() })
, _media(controller) {
}

Memento::Memento(not_null<UserData*> self, Storage::SharedMediaType type)
: ContentMemento(Tag{ self })
, _media(self, 0, type) {
}

Memento::~Memento() = default;

Section Memento::section() const {
	return Section(_media.type(), Section::Type::GlobalMedia);
}

object_ptr<ContentWidget> Memento::createWidget(
		QWidget *parent,
		not_null<Controller*> controller,
		const QRect &geometry) {
	auto result = object_ptr<Widget>(parent, controller);
	result->setInternalState(geometry, this);
	return result;
}

Widget::Widget(QWidget *parent, not_null<Controller*> controller)
: ContentWidget(parent, controller) {
	_inner = setInnerWidget(object_ptr<InnerWidget>(
		this,
		controller));
	_inner->setScrollHeightValue(scrollHeightValue());
	_inner->scrollToRequests(
	) | rpl::start_with_next([this](Ui::ScrollToRequest request) {
		scrollTo(request);
	}, _inner->lifetime());
}

bool Widget::showInternal(not_null<ContentMemento*> memento) {
	if (auto globalMediaMemento = dynamic_cast<Memento*>(memento.get())) {
		restoreState(globalMediaMemento);
		return true;
	}
	return false;
}

void Widget::setInternalState(
		const QRect &geometry,
		not_null<Memento*> memento) {
	setGeometry(geometry);
	Ui::SendPendingMoveResizeEvents(this);
	restoreState(memento);
}

std::shared_ptr<ContentMemento> Widget::doCreateMemento() {
	auto result = std::make_shared<Memento>(controller());
	saveState(result.get());
	return result;
}

void Widget::saveState(not_null<Memento*> memento) {
	memento->setScrollTop(scrollTopSave());
	_inner->saveState(memento);
}

void Widget::restoreState(not_null<Memento*> memento) {
	_inner->restoreState(memento);
	scrollTopRestore(memento->scrollTop());
}

rpl::producer<SelectedItems> Widget::selectedListValue() const {
	return _inner->selectedListValue();
}

void Widget::selectionAction(SelectionAction action) {
	_inner->selectionAction(action);
}

void Widget::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	const auto window = controller()->parentController();
	const auto deleteAll = [=] {
		auto &manager = Core::App().downloadManager();
		const auto phrase = tr::lng_downloads_delete_sure_all(tr::now);
		const auto added = manager.loadedHasNonCloudFile()
			? QString()
			: tr::lng_downloads_delete_in_cloud(tr::now);
		const auto deleteSure = [=, &manager](Fn<void()> close) {
			Ui::PostponeCall(this, close);
			manager.deleteAll();
		};
		window->show(Ui::MakeConfirmBox({
			.text = phrase + (added.isEmpty() ? QString() : "\n\n" + added),
			.confirmed = deleteSure,
			.confirmText = tr::lng_box_delete(tr::now),
			.confirmStyle = &st::attentionBoxButton,
		}));
	};
	addAction(
		tr::lng_context_delete_all_files(tr::now),
		deleteAll,
		&st::menuIconDelete);
}

rpl::producer<QString> Widget::title() {
	return tr::lng_profile_shared_media();
}

std::shared_ptr<Info::Memento> Make(
		not_null<UserData*> self,
		Storage::SharedMediaType type) {
	return std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(
			1,
			std::make_shared<Memento>(self, type)));
}

} // namespace Info::GlobalMedia