/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/stories/info_stories_inner_widget.h"

#include "apiwrap.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_stories_content.h"
#include "dialogs/ui/dialogs_stories_list.h"
#include "info/media/info_media_buttons.h"
#include "info/media/info_media_list_widget.h"
#include "info/profile/info_profile_actions.h"
#include "info/profile/info_profile_icon.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_widget.h"
#include "info/stories/info_stories_albums.h"
#include "info/stories/info_stories_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/sub_tabs.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "styles/style_credits.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Info::Stories {

class EmptyWidget : public Ui::RpWidget {
public:
	EmptyWidget(QWidget *parent);

	void setFullHeight(rpl::producer<int> fullHeightValue);

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;

private:
	object_ptr<Ui::FlatLabel> _text;
	int _height = 0;

};

EmptyWidget::EmptyWidget(QWidget *parent)
: RpWidget(parent)
, _text(this, st::infoEmptyLabel) {
}

void EmptyWidget::setFullHeight(rpl::producer<int> fullHeightValue) {
	std::move(
		fullHeightValue
	) | rpl::start_with_next([this](int fullHeight) {
		// Make icon center be on 1/3 height.
		auto iconCenter = fullHeight / 3;
		auto iconHeight = st::infoEmptyStories.height();
		auto iconTop = iconCenter - iconHeight / 2;
		_height = iconTop + st::infoEmptyIconTop;
		resizeToWidth(width());
	}, lifetime());
}

int EmptyWidget::resizeGetHeight(int newWidth) {
	auto labelTop = _height - st::infoEmptyLabelTop;
	auto labelWidth = newWidth - 2 * st::infoEmptyLabelSkip;
	_text->resizeToNaturalWidth(labelWidth);

	auto labelLeft = (newWidth - _text->width()) / 2;
	_text->moveToLeft(labelLeft, labelTop, newWidth);

	update();
	return _height;
}

void EmptyWidget::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);

	const auto iconLeft = (width() - st::infoEmptyStories.width()) / 2;
	const auto iconTop = height() - st::infoEmptyIconTop;
	st::infoEmptyStories.paint(p, iconLeft, iconTop, width());
}

InnerWidget::InnerWidget(
	QWidget *parent,
	not_null<Controller*> controller,
	rpl::producer<int> albumId,
	int addingToAlbumId)
: RpWidget(parent)
, _controller(controller)
, _peer(controller->key().storiesPeer())
, _addingToAlbumId(addingToAlbumId)
, _albumId(std::move(albumId))
, _albumChanges(Data::StoryAlbumUpdate{
	.peer = _peer,
	.albumId = _addingToAlbumId,
})
, _empty(this) {
	_empty->heightValue(
	) | rpl::start_with_next([=] {
		refreshHeight();
	}, _empty->lifetime());
	setupList();

	_albumId.changes(
	) | rpl::start_with_next([=](int albumId) {
		_list.destroy();
		_controller->replaceKey(Key(Tag(_peer, albumId)));
		setupList();
		resizeToWidth(width());
	}, lifetime());
}

void InnerWidget::setupAlbums() {
	Ui::AddSkip(_top);
	_albumsWrap = _top->add(object_ptr<Ui::BoxContentDivider>(_top));

	_peer->owner().stories().albumsListValue(
		_peer->id
	) | rpl::start_with_next([=](std::vector<Data::StoryAlbum> &&albums) {
		_albums = std::move(albums);
		refreshAlbumsTabs();
	}, lifetime());
}

InnerWidget::~InnerWidget() = default;

void InnerWidget::setupTop() {
	const auto albumId = _albumId.current();
	if (albumId == Data::kStoriesAlbumIdArchive) {
		createAboutArchive();
	} else if (_isStackBottom) {
		if (_peer->isSelf()) {
			createProfileTop();
		} else if (_peer->owner().stories().hasArchive(_peer)) {
			createButtons();
		} else {
			startTop();
			finalizeTop();
		}
	} else {
		startTop();
		finalizeTop();
	}
}

void InnerWidget::startTop() {
	_top.create(this);
	_top->show();
	_topHeight = _top->heightValue();
}

void InnerWidget::createProfileTop() {
	startTop();

	using namespace Profile;
	AddCover(_top, _controller, _peer, nullptr, nullptr);
	AddDetails(_top, _controller, _peer, nullptr, nullptr, { v::null });

	auto tracker = Ui::MultiSlideTracker();
	const auto dividerWrap = _top->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_top,
			object_ptr<Ui::VerticalLayout>(_top)));
	const auto divider = dividerWrap->entity();
	Ui::AddDivider(divider);
	Ui::AddSkip(divider);

	addGiftsButton(tracker);
	addArchiveButton(tracker);
	addRecentButton(tracker);

	dividerWrap->toggleOn(tracker.atLeastOneShownValue());

	finalizeTop();
}

void InnerWidget::createButtons() {
	startTop();
	auto tracker = Ui::MultiSlideTracker();
	addArchiveButton(tracker);
	addRecentButton(tracker);
	finalizeTop();
}

void InnerWidget::addArchiveButton(Ui::MultiSlideTracker &tracker) {
	Expects(_top != nullptr);

	const auto stories = &_peer->owner().stories();

	constexpr auto kArchive = Data::kStoriesAlbumIdArchive;
	if (!stories->albumIdsCountKnown(_peer->id, kArchive)) {
		stories->albumIdsLoadMore(_peer->id, kArchive);
	}

	auto count = rpl::single(
		rpl::empty
	) | rpl::then(
		stories->albumIdsChanged(
		) | rpl::filter(
			rpl::mappers::_1 == Data::StoryAlbumIdsKey{ _peer->id, kArchive }
		) | rpl::to_empty
	) | rpl::map([=] {
		return stories->albumIdsCount(_peer->id, kArchive);
	}) | rpl::start_spawning(_top->lifetime());

	const auto archiveWrap = _top->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			_top,
			object_ptr<Ui::SettingsButton>(
				_top,
				tr::lng_stories_archive_button(),
				st::infoSharedMediaButton))
	)->setDuration(
		st::infoSlideDuration
	)->toggleOn(rpl::duplicate(count) | rpl::map(rpl::mappers::_1 > 0));

	const auto archive = archiveWrap->entity();
	archive->addClickHandler([=] {
		_controller->showSection(Make(_peer, kArchive));
	});
	auto label = rpl::duplicate(
		count
	) | rpl::filter(
		rpl::mappers::_1 > 0
	) | rpl::map([=](int count) {
		return (count > 0) ? QString::number(count) : QString();
	});
	::Settings::CreateRightLabel(
		archive,
		std::move(label),
		st::infoSharedMediaButton,
		tr::lng_stories_archive_button());
	object_ptr<Profile::FloatingIcon>(
		archive,
		st::infoIconMediaStoriesArchive,
		st::infoSharedMediaButtonIconPosition)->show();
	tracker.track(archiveWrap);
}

void InnerWidget::addRecentButton(Ui::MultiSlideTracker &tracker) {
	Expects(_top != nullptr);

	const auto recentWrap = _top->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			_top,
			object_ptr<Ui::SettingsButton>(
				_top,
				tr::lng_stories_recent_button(),
				st::infoSharedMediaButton)));

	using namespace Dialogs::Stories;
	auto last = LastForPeer(
		_peer
	) | rpl::map([=](Content &&content) {
		for (auto &element : content.elements) {
			element.unreadCount = 0;
		}
		return std::move(content);
	}) | rpl::start_spawning(recentWrap->lifetime());
	const auto recent = recentWrap->entity();
	const auto thumbs = Ui::CreateChild<List>(
		recent,
		st::dialogsStoriesListMine,
		rpl::duplicate(last) | rpl::filter([](const Content &content) {
			return !content.elements.empty();
		}));
	thumbs->show();
	rpl::combine(
		recent->sizeValue(),
		rpl::duplicate(last)
	) | rpl::start_with_next([=](QSize size, const Content &content) {
		if (content.elements.empty()) {
			return;
		}
		const auto &small = st::dialogsStories;
		const auto height = small.photo + 2 * small.photoTop;
		const auto top = (size.height() - height) / 2;
		const auto right = st::settingsButtonRightSkip
			- small.left
			- small.photoLeft;
		const auto left = size.width() - right;
		thumbs->setLayoutConstraints({ left, top }, style::al_right);
	}, thumbs->lifetime());
	thumbs->setAttribute(Qt::WA_TransparentForMouseEvents);
	recent->addClickHandler([=] {
		_controller->parentController()->openPeerStories(_peer->id);
	});
	object_ptr<Profile::FloatingIcon>(
		recent,
		st::infoIconMediaStoriesRecent,
		st::infoSharedMediaButtonIconPosition)->show();
	recentWrap->toggleOn(rpl::duplicate(
		last
	) | rpl::map([](const Content &content) {
		return !content.elements.empty();
	}));
	tracker.track(recentWrap);
}

void InnerWidget::addGiftsButton(Ui::MultiSlideTracker &tracker) {
	Expects(_top != nullptr);

	const auto user = _peer->asUser();
	Assert(user != nullptr);

	auto count = Profile::PeerGiftsCountValue(
		user
	) | rpl::start_spawning(_top->lifetime());

	const auto giftsWrap = _top->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			_top,
			object_ptr<Ui::SettingsButton>(
				_top,
				tr::lng_peer_gifts_title(),
				st::infoSharedMediaButton))
	)->setDuration(
		st::infoSlideDuration
	)->toggleOn(rpl::duplicate(count) | rpl::map(rpl::mappers::_1 > 0));

	const auto gifts = giftsWrap->entity();
	gifts->addClickHandler([=] {
		_controller->showSection(
			std::make_shared<Info::Memento>(
				user,
				Section::Type::PeerGifts));
	});
	auto label = rpl::duplicate(
		count
	) | rpl::filter(
		rpl::mappers::_1 > 0
	) | rpl::map([=](int count) {
		return (count > 0) ? QString::number(count) : QString();
	});
	::Settings::CreateRightLabel(
		gifts,
		std::move(label),
		st::infoSharedMediaButton,
		tr::lng_stories_archive_button());
	object_ptr<Profile::FloatingIcon>(
		gifts,
		st::infoIconMediaGifts,
		st::infoSharedMediaButtonIconPosition)->show();
	tracker.track(giftsWrap);
}

void InnerWidget::finalizeTop() {
	const auto addPossibleAlbums = !_addingToAlbumId
		&& (_albumId.current() != Data::kStoriesAlbumIdArchive);
	if (addPossibleAlbums) {
		setupAlbums();
	}
	_top->resizeToWidth(width());

	_top->heightValue(
	) | rpl::start_with_next([=] {
		refreshHeight();
	}, _top->lifetime());
}

void InnerWidget::createAboutArchive() {
	startTop();

	_top->add(object_ptr<Ui::DividerLabel>(
		_top,
		object_ptr<Ui::FlatLabel>(
			_top,
			(_peer->isChannel()
				? tr::lng_stories_channel_archive_about
				: tr::lng_stories_archive_about)(),
			st::infoStoriesAboutArchive),
		st::infoStoriesAboutArchivePadding));

	finalizeTop();
}

void InnerWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
}

bool InnerWidget::showInternal(not_null<Memento*> memento) {
	if (memento->section().type() == Section::Type::Stories) {
		restoreState(memento);
		return true;
	}
	return false;
}

void InnerWidget::setupList() {
	Expects(!_list);

	_list = object_ptr<Media::ListWidget>(
		this,
		_controller);
	const auto raw = _list.data();

	raw->heightValue(
	) | rpl::start_with_next([=] {
		refreshHeight();
	}, raw->lifetime());
	using namespace rpl::mappers;
	raw->scrollToRequests(
	) | rpl::map([=](int to) {
		return Ui::ScrollToRequest {
			raw->y() + to,
			-1
		};
	}) | rpl::start_to_stream(_scrollToRequests, raw->lifetime());
	_selectedLists.fire(raw->selectedListValue());
	_listTops.fire(raw->topValue());

	raw->show();
}

void InnerWidget::refreshAlbumsTabs() {
	Expects(!_addingToAlbumId);
	Expects(_albumsWrap != nullptr);

	if (_albums.empty() && !_peer->canEditStories()) {
		if (base::take(_albumsTabs)) {
			resizeToWidth(width());
		}
		return;
	}
	auto tabs = std::vector<Ui::SubTabs::Tab>();
	auto selected = QString();
	if (!_albums.empty()) {
		tabs.push_back({
			.id = u"all"_q,
			.text = tr::lng_stories_album_all(
				tr::now,
				Ui::Text::WithEntities),
		});
		for (const auto &album : _albums) {
			auto title = TextWithEntities();
			title.append(album.title);
			tabs.push_back({
				.id = QString::number(album.id),
				.text = std::move(title),
			});
			if (_albumId.current() == album.id) {
				selected = tabs.back().id;
			}
		}
		if (selected.isEmpty()) {
			selected = tabs.front().id;
		}
	}
	if (_peer->canEditStories()) {
		tabs.push_back({
			.id = u"add"_q,
			.text = { '+' + tr::lng_stories_album_add(tr::now) },
		});
	}
	if (!_albumsTabs) {
		_albumsTabs = std::make_unique<Ui::SubTabs>(
			_albumsWrap,
			Ui::SubTabs::Options{
				.selected = selected,
				.centered = true,
			},
			std::move(tabs));
		_albumsTabs->show();

		const auto padding = st::giftBoxPadding;
		_albumsWrap->resize(
			_albumsWrap->width(),
			padding.top() + _albumsTabs->height() + padding.top());
		_albumsWrap->widthValue() | rpl::start_with_next([=](int width) {
			_albumsTabs->resizeToWidth(width);
		}, _albumsTabs->lifetime());
		_albumsTabs->move(0, padding.top());

		_albumsTabs->activated(
		) | rpl::start_with_next([=](const QString &id) {
			if (id == u"add"_q) {
				const auto added = [=](Data::StoryAlbum album) {
					albumAdded(album);
				};
				_controller->uiShow()->show(Box(
					NewAlbumBox,
					_controller,
					_peer,
					StoryId(),
					added));
			} else {
				_albumsTabs->setActiveTab(id);
				_albumIdChanges.fire((id == u"all"_q) ? 0 : id.toInt());
			}
		}, _albumsTabs->lifetime());

		_albumsTabs->contextMenuRequests(
		) | rpl::start_with_next([=](const QString &id) {
			if (id == u"add"_q
				|| id == u"all"_q
				|| !_peer->canEditStories()) {
				return;
			}
			showMenuForAlbum(id.toInt());
		}, _albumsTabs->lifetime());
	} else {
		_albumsTabs->setTabs(std::move(tabs));
		if (!selected.isEmpty()) {
			_albumsTabs->setActiveTab(selected);
		}
	}
	resizeToWidth(width());
}

void InnerWidget::showMenuForAlbum(int id) {
	if (_menu || _addingToAlbumId) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	const auto addAction = Ui::Menu::CreateAddActionCallback(_menu);
	addAction(tr::lng_stories_album_add_title(tr::now), [=] {
		editAlbumStories(id);
	}, &st::menuIconStoriesSave);
	addAction(tr::lng_stories_album_edit(tr::now), [=] {
		editAlbumName(id);
	}, &st::menuIconEdit);
	addAction({
		.text = tr::lng_stories_album_delete(tr::now),
		.handler = [=] { confirmDeleteAlbum(id); },
		.icon = &st::menuIconDeleteAttention,
		.isAttention = true,
	});
	_menu->popup(QCursor::pos());
}

rpl::producer<int> InnerWidget::albumIdChanges() const {
	return _albumIdChanges.events();
}

rpl::producer<Data::StoryAlbumUpdate> InnerWidget::changes() const {
	return _albumChanges.value();
}

void InnerWidget::reloadAlbum(int id) {
	// #TODO stories
}

void InnerWidget::editAlbumStories(int id) {
	const auto weak = base::make_weak(this);
	_controller->uiShow()->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_stories_album_add_title());
		box->setWidth(st::boxWideWidth);
		box->setStyle(st::collectionEditBox);

		struct State {
			rpl::variable<Data::StoryAlbumUpdate> changes;
			base::unique_qptr<Ui::PopupMenu> menu;
			bool saving = false;
		};
		const auto state = box->lifetime().make_state<State>(State{
			.changes = Data::StoryAlbumUpdate{
				.peer = _peer,
				.albumId = id,
			}
		});
		const auto content = box->addRow(
			object_ptr<InnerWidget>(
				box,
				_controller,
				rpl::single(Data::kStoriesAlbumIdArchive),
				id/*,
				_peer,
				state->descriptor.value(),
				id,
				(_all.filter == Filter()) ? _all : Entries()*/),
			{});
		state->changes = content->changes();

		content->scrollToRequests(
		) | rpl::start_with_next([=](Ui::ScrollToRequest request) {
			box->scrollTo(request);
		}, content->lifetime());

		box->addTopButton(st::boxTitleClose, [=] {
			box->closeBox();
		});
		const auto weakBox = base::make_weak(box);
		auto text = state->changes.value(
		) | rpl::map([=](const Data::StoryAlbumUpdate &update) {
			return (!update.added.empty() && update.removed.empty())
				? tr::lng_stories_album_add_title()
				: tr::lng_settings_save();
		}) | rpl::flatten_latest();
		box->addButton(std::move(text), [=] {
			if (state->saving) {
				return;
			}
			auto add = QVector<MTPint>();
			auto remove = QVector<MTPint>();
			const auto &changes = state->changes.current();
			for (const auto &id : changes.added) {
				add.push_back(MTP_int(id));
			}
			for (const auto &id : changes.removed) {
				remove.push_back(MTP_int(id));
			}
			if (add.empty() && remove.empty()) {
				box->closeBox();
				return;
			}
			state->saving = true;
			const auto session = &_controller->session();
			using Flag = MTPstories_UpdateAlbum::Flag;
			session->api().request(
				MTPstories_UpdateAlbum(
					MTP_flags(Flag()
						| (add.isEmpty() ? Flag() : Flag::f_add_stories)
						| (remove.isEmpty()
							? Flag()
							: Flag::f_delete_stories)),
					_peer->input,
					MTP_int(id),
					MTPstring(),
					MTP_vector<MTPint>(remove),
					MTP_vector<MTPint>(add),
					MTPVector<MTPint>())
			).done([=] {
				if (const auto strong = weakBox.get()) {
					state->saving = false;
					strong->closeBox();
				}
				session->data().stories().notifyAlbumUpdate(
					base::duplicate(changes));
				if (const auto strong = weak.get()) {
					strong->reloadAlbum(id);
				}
			}).fail([=](const MTP::Error &error) {
				if (const auto strong = weakBox.get()) {
					state->saving = false;
					strong->uiShow()->showToast(error.type());
				}
			}).send();
		});
	}));
}

void InnerWidget::editAlbumName(int id) {
	const auto done = [=](QString name) {
		albumRenamed(id, name);
	};
	const auto i = ranges::find(_albums, id, &Data::StoryAlbum::id);
	if (i == end(_albums)) {
		return;
	}
	_controller->uiShow()->show(Box(
		EditAlbumNameBox,
		_controller->parentController(),
		_peer,
		id,
		i->title,
		done));
}

void InnerWidget::confirmDeleteAlbum(int id) {
	const auto done = [=](Fn<void()> close) {
		_controller->session().api().request(
			MTPstories_DeleteAlbum(_peer->input, MTP_int(id))
		).send();
		albumRemoved(id);
		close();
	};
	_controller->uiShow()->show(Ui::MakeConfirmBox({
		.text = tr::lng_stories_album_delete_sure(),
		.confirmed = crl::guard(this, done),
		.confirmText = tr::lng_stories_album_delete_button(),
		.confirmStyle = &st::attentionBoxButton,
	}));
}

void InnerWidget::albumAdded(Data::StoryAlbum result) {
	Expects(ranges::contains(_albums, result.id, &Data::StoryAlbum::id));

	_albumId = result.id;
}

void InnerWidget::albumRenamed(int id, QString name) {
	const auto i = ranges::find(_albums, id, &Data::StoryAlbum::id);
	if (i != end(_albums)) {
		i->title = name;
		refreshAlbumsTabs();
	}
}

void InnerWidget::albumRemoved(int id) {
	auto now = _albumId.current();
	if (now == id) {
		_albumId = 0;
	}
	//const auto removeFrom = [&](Entries &entries) {
	//	for (auto &entry : entries.list) {
	//		entry.gift.collectionIds.erase(
	//			ranges::remove(entry.gift.collectionIds, id),
	//			end(entry.gift.collectionIds));
	//	}
	//};
	//removeFrom(_all);
	//for (auto &[_, entries] : _perCollection) {
	//	removeFrom(entries);
	//}

	const auto i = ranges::find(_albums, id, &Data::StoryAlbum::id);
	if (i != end(_albums)) {
		_albums.erase(i);
		refreshAlbumsTabs();
	}
}

void InnerWidget::saveState(not_null<Memento*> memento) {
	_list->saveState(&memento->media());
}

void InnerWidget::restoreState(not_null<Memento*> memento) {
	_list->restoreState(&memento->media());
}

rpl::producer<SelectedItems> InnerWidget::selectedListValue() const {
	return _selectedLists.events_starting_with(
		_list->selectedListValue()
	) | rpl::flatten_latest();
}

void InnerWidget::selectionAction(SelectionAction action) {
	_list->selectionAction(action);
}

int InnerWidget::resizeGetHeight(int newWidth) {
	_inResize = true;
	auto guard = gsl::finally([this] { _inResize = false; });

	if (_top) {
		_top->resizeToWidth(newWidth);
	}
	_list->resizeToWidth(newWidth);
	_empty->resizeToWidth(newWidth);
	return recountHeight();
}

void InnerWidget::refreshHeight() {
	if (_inResize) {
		return;
	}
	resize(width(), recountHeight());
}

int InnerWidget::recountHeight() {
	auto top = 0;
	if (_top) {
		_top->moveToLeft(0, top);
		top += _top->heightNoMargins() - st::lineWidth;
	}
	auto listHeight = 0;
	if (_list) {
		_list->moveToLeft(0, top);
		listHeight = _list->heightNoMargins();
		top += listHeight;
	}
	if (listHeight > 0) {
		_empty->hide();
	} else {
		_empty->show();
		_empty->moveToLeft(0, top);
		top += _empty->heightNoMargins();
	}
	return top;
}

void InnerWidget::setScrollHeightValue(rpl::producer<int> value) {
	using namespace rpl::mappers;
	_empty->setFullHeight(rpl::combine(
		std::move(value),
		_listTops.events_starting_with(
			_list->topValue()
		) | rpl::flatten_latest(),
		_topHeight.value(),
		_1 - _2 + _3));
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

} // namespace Info::Stories
