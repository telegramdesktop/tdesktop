/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "info/info_top_bar_override.h"

#include <rpl/merge.h>
#include "styles/style_info.h"
#include "lang/lang_keys.h"
#include "info/info_wrap_widget.h"
#include "storage/storage_shared_media.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/widgets/shadow.h"
#include "mainwidget.h"
#include "boxes/confirm_box.h"
#include "boxes/peer_list_controllers.h"

namespace Info {

ChooseRecipientBoxController::ChooseRecipientBoxController(
	base::lambda<void(not_null<PeerData*>)> callback)
: _callback(std::move(callback)) {
}

void ChooseRecipientBoxController::prepareViewHook() {
	delegate()->peerListSetTitle(langFactory(lng_forward_choose));
}

void ChooseRecipientBoxController::rowClicked(not_null<PeerListRow*> row) {
	_callback(row->peer());
}

auto ChooseRecipientBoxController::createRow(
		not_null<History*> history) -> std::unique_ptr<Row> {
	return std::make_unique<Row>(history);
}

TopBarOverride::TopBarOverride(
	QWidget *parent,
	const style::InfoTopBar &st,
	SelectedItems &&items)
: RpWidget(parent)
, _st(st)
, _items(std::move(items))
, _canDelete(computeCanDelete())
, _cancel(this, _st.mediaCancel)
, _text(this, generateText(), Ui::FlatLabel::InitType::Simple, _st.title)
, _forward(this, _st.mediaForward)
, _delete(this, _st.mediaDelete) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	updateControlsVisibility();

	_forward->addClickHandler([this] { performForward(); });
	_delete->addClickHandler([this] { performDelete(); });
}

QString TopBarOverride::generateText() const {
	using Type = Storage::SharedMediaType;
	auto phrase = [&] {
		switch (_items.type) {
		case Type::Photo: return lng_profile_photos;
		case Type::Video: return lng_profile_videos;
		case Type::File: return lng_profile_files;
		case Type::MusicFile: return lng_profile_songs;
		case Type::Link: return lng_profile_shared_links;
		case Type::VoiceFile: return lng_profile_audios;
		case Type::RoundFile: return lng_profile_rounds;
		}
		Unexpected("Type in TopBarOverride::generateText()");
	}();
	return phrase(lt_count, _items.list.size());
}

bool TopBarOverride::computeCanDelete() const {
	return base::find_if(_items.list, [](const SelectedItem &item) {
		return !item.canDelete;
	}) == _items.list.end();
}

void TopBarOverride::setItems(SelectedItems &&items) {
	_items = std::move(items);
	_canDelete = computeCanDelete();

	_text->setText(generateText());
	updateControlsVisibility();
	updateControlsGeometry(width());
}

rpl::producer<> TopBarOverride::cancelRequests() const {
	return rpl::merge(
		_cancel->clicks(),
		_correctionCancelRequests.events());
}

int TopBarOverride::resizeGetHeight(int newWidth) {
	updateControlsGeometry(newWidth);
	return _st.height;
}

void TopBarOverride::updateControlsGeometry(int newWidth) {
	auto right = _st.mediaActionsSkip;
	if (_canDelete) {
		_delete->moveToRight(right, 0, newWidth);
		right += _delete->width();
	}
	_forward->moveToRight(right, 0, newWidth);
	_cancel->moveToLeft(0, 0);
	_text->moveToLeft(_cancel->width(), _st.titlePosition.y());
}

void TopBarOverride::updateControlsVisibility() {
	_delete->setVisible(_canDelete);
}

void TopBarOverride::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), _st.bg);
}

SelectedItemSet TopBarOverride::collectItems() const {
	auto result = SelectedItemSet();
	for (auto value : _items.list) {
		if (auto item = App::histItemById(value.msgId)) {
			result.insert(result.size(), item);
		}
	}
	return result;
}

void TopBarOverride::performForward() {
	auto items = collectItems();
	if (items.empty()) {
		_correctionCancelRequests.fire({});
		return;
	}
	auto callback = [items = std::move(items)](not_null<PeerData*> peer) {
		App::main()->setForwardDraft(peer->id, items);
	};
	Ui::show(Box<PeerListBox>(
		std::make_unique<ChooseRecipientBoxController>(std::move(callback)),
		[](not_null<PeerListBox*> box) {
			box->addButton(langFactory(lng_cancel), [box] {
				box->closeBox();
			});
		}));
}

void TopBarOverride::performDelete() {
	auto items = collectItems();
	if (items.empty()) {
		_correctionCancelRequests.fire({});
	} else {
		Ui::show(Box<DeleteMessagesBox>(items));
	}
}

} // namespace Info
