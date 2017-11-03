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
#include "info/info_top_bar.h"

#include <rpl/never.h>
#include "styles/style_info.h"
#include "lang/lang_keys.h"
#include "info/info_wrap_widget.h"
#include "info/info_controller.h"
#include "storage/storage_shared_media.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/search_field_controller.h"

namespace Info {

TopBar::TopBar(QWidget *parent, const style::InfoTopBar &st)
: RpWidget(parent)
, _st(st) {
	setAttribute(Qt::WA_OpaquePaintEvent);
}

void TopBar::setTitle(rpl::producer<QString> &&title) {
	_title.create(this, std::move(title), _st.title);
	if (_back) {
		_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	updateControlsGeometry(width());
}

void TopBar::enableBackButton(bool enable) {
	if (enable) {
		_back.create(this, _st.back);
		_back->clicks()
			| rpl::start_to_stream(_backClicks, _back->lifetime());
	} else {
		_back.destroy();
	}
	if (_title) {
		_title->setAttribute(Qt::WA_TransparentForMouseEvents, enable);
	}
	updateControlsGeometry(width());
}

void TopBar::createSearchView(
		not_null<Ui::SearchFieldController*> controller,
		rpl::producer<bool> &&shown) {
	setSearchField(
		controller->createField(this, _st.searchRow.field),
		std::move(shown));
}

void TopBar::pushButton(base::unique_qptr<Ui::RpWidget> button) {
	auto weak = button.get();
	_buttons.push_back(std::move(button));
	weak->setParent(this);
	weak->widthValue()
		| rpl::start_with_next([this] {
			updateControlsGeometry(width());
		}, lifetime());
}

void TopBar::setSearchField(
		base::unique_qptr<Ui::InputField> field,
		rpl::producer<bool> &&shown) {
	if (auto value = field.release()) {
		createSearchView(value, std::move(shown));
	} else {
		_searchView = nullptr;
	}
}

void TopBar::createSearchView(
		not_null<Ui::InputField*> field,
		rpl::producer<bool> &&shown) {
	_searchView = base::make_unique_q<Ui::FixedHeightWidget>(
		this,
		_st.searchRow.height);
	auto wrap = _searchView.get();

	field->setParent(wrap);

	auto search = addButton(
		base::make_unique_q<Ui::FadeWrapScaled<Ui::IconButton>>(
			this,
			object_ptr<Ui::IconButton>(this, _st.search)));
	auto cancel = Ui::CreateChild<Ui::CrossButton>(
		wrap,
		_st.searchRow.fieldCancel);

	auto toggleSearchMode = [=](bool enabled, anim::type animated) {
		if (_title) {
			_title->setVisible(!enabled);
		}
		field->setVisible(enabled);
		cancel->toggleAnimated(enabled);
		if (animated == anim::type::instant) {
			cancel->finishAnimations();
		}
		search->toggle(!enabled, animated);
	};

	auto cancelSearch = [=] {
		if (!field->getLastText().isEmpty()) {
			field->setText(QString());
		} else {
			toggleSearchMode(false, anim::type::normal);
		}
	};

	cancel->addClickHandler(cancelSearch);
	field->connect(field, &Ui::InputField::cancelled, cancelSearch);

	wrap->widthValue()
		| rpl::start_with_next([=](int newWidth) {
			auto availableWidth = newWidth
				- _st.searchRow.fieldCancelSkip;
			field->setGeometryToLeft(
				_st.searchRow.padding.left(),
				_st.searchRow.padding.top(),
				availableWidth,
				field->height());
			cancel->moveToRight(0, 0);
		}, wrap->lifetime());

	widthValue()
		| rpl::start_with_next([=](int newWidth) {
			auto left = _back
				? _st.back.width
				: _st.titlePosition.x();
			wrap->setGeometryToLeft(
				left,
				0,
				newWidth - left,
				wrap->height(),
				newWidth);
		}, wrap->lifetime());

	search->entity()->addClickHandler([=] {
		toggleSearchMode(true, anim::type::normal);
		field->setFocus();
	});

	field->alive()
		| rpl::start_with_done([=] {
			field->setParent(nullptr);
			removeButton(search);
			setSearchField(nullptr, rpl::never<bool>());
		}, _searchView->lifetime());

	toggleSearchMode(
		!field->getLastText().isEmpty(),
		anim::type::instant);

	std::move(shown)
		| rpl::start_with_next([=](bool visible) {
			if (!field->getLastText().isEmpty()) {
				return;
			}
			toggleSearchMode(false, anim::type::instant);
			wrap->setVisible(visible);
			search->toggle(visible, anim::type::instant);
		}, wrap->lifetime());
}

void TopBar::removeButton(not_null<Ui::RpWidget*> button) {
	_buttons.erase(
		std::remove(_buttons.begin(), _buttons.end(), button),
		_buttons.end());
}

int TopBar::resizeGetHeight(int newWidth) {
	updateControlsGeometry(newWidth);
	return _st.height;
}

void TopBar::updateControlsGeometry(int newWidth) {
	auto right = 0;
	for (auto &button : _buttons) {
		if (!button) continue;
		button->moveToRight(right, 0, newWidth);
		right += button->width();
	}
	if (_back) {
		_back->setGeometryToLeft(
			0,
			0,
			newWidth - right,
			_back->height(),
			newWidth);
	}
	if (_title) {
		_title->moveToLeft(
			_back ? _st.back.width : _st.titlePosition.x(),
			_st.titlePosition.y(),
			newWidth);
	}
}

void TopBar::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), _st.bg);
}

rpl::producer<QString> TitleValue(
		const Section &section,
		not_null<PeerData*> peer) {
	return Lang::Viewer([&] {
		switch (section.type()) {
		case Section::Type::Profile:
			if (auto user = peer->asUser()) {
				return user->botInfo
					? lng_info_bot_title
					: lng_info_user_title;
			} else if (auto channel = peer->asChannel()) {
				return channel->isMegagroup()
					? lng_info_group_title
					: lng_info_channel_title;
			} else if (peer->isChat()) {
				return lng_info_group_title;
			}
			Unexpected("Bad peer type in Info::TitleValue()");

		case Section::Type::Media:
			switch (section.mediaType()) {
			case Section::MediaType::Photo:
				return lng_media_type_photos;
			case Section::MediaType::Video:
				return lng_media_type_videos;
			case Section::MediaType::MusicFile:
				return lng_media_type_songs;
			case Section::MediaType::File:
				return lng_media_type_files;
			case Section::MediaType::VoiceFile:
				return lng_media_type_audios;
			case Section::MediaType::Link:
				return lng_media_type_links;
			case Section::MediaType::RoundFile:
				return lng_media_type_rounds;
			}
			Unexpected("Bad media type in Info::TitleValue()");

		case Section::Type::CommonGroups:
			return lng_profile_common_groups_section;

		}
		Unexpected("Bad section type in Info::TitleValue()");
	}());
}

} // namespace Info
