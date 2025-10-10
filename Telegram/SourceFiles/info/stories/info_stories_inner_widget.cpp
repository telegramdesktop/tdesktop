/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/stories/info_stories_inner_widget.h"

#include "apiwrap.h"
#include "boxes/share_box.h"
#include "data/data_peer.h"
#include "data/data_saved_music.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_stories_content.h"
#include "dialogs/ui/dialogs_stories_list.h"
#include "info/media/info_media_buttons.h"
#include "info/media/info_media_list_widget.h"
#include "info/peer_gifts/info_peer_gifts_widget.h"
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
#include "mtproto/sender.h"
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
namespace {

class EditAlbumBox final : public Ui::BoxContent {
public:
	EditAlbumBox(
		QWidget*,
		not_null<Controller*> controller,
		not_null<PeerData*> peer,
		Fn<void()> reload,
		int albumId);

private:
	void prepare() override;

	void resizeEvent(QResizeEvent *e) override;

	const not_null<Window::SessionController*> _window;
	const not_null<WrapWidget*> _content;
	rpl::variable<Data::StoryAlbumUpdate> _changes;
	Fn<void()> _reload;
	bool _saving = false;

};

EditAlbumBox::EditAlbumBox(
	QWidget*,
	not_null<Controller*> controller,
	not_null<PeerData*> peer,
	Fn<void()> reload,
	int albumId)
: _window(controller->parentController())
, _content(Ui::CreateChild<WrapWidget>(
	this,
	_window,
	Wrap::StoryAlbumEdit,
	std::make_shared<Info::Memento>(
		std::vector<std::shared_ptr<ContentMemento>>(
			1,
			std::make_shared<Memento>(
				peer,
				Data::kStoriesAlbumIdArchive,
				albumId))).get()))
, _changes(Data::StoryAlbumUpdate{ .peer = peer, .albumId = albumId })
, _reload(std::move(reload)) {
	_content->selectedListValue(
	) | rpl::start_with_next([=](const SelectedItems &selection) {
		const auto stories = &_window->session().data().stories();
		auto ids = stories->albumKnownInArchive(peer->id, albumId);
		auto now = _changes.current();
		now.added.clear();
		now.added.reserve(selection.list.size());
		now.removed.clear();
		now.removed.reserve(ids.size());
		for (const auto &entry : selection.list) {
			const auto id = StoryIdFromMsgId(entry.globalId.itemId.msg);
			if (!ids.remove(id)) {
				now.added.push_back(id);
			}
		}
		for (const auto id : ids) {
			now.removed.push_back(id);
		}
		_changes = std::move(now);
	}, lifetime());
}

void EditAlbumBox::prepare() {
	setTitle(tr::lng_stories_album_add_title());
	setStyle(st::collectionEditBox);

	_content->desiredHeightValue(
	) | rpl::start_with_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, _content->lifetime());

	addTopButton(st::boxTitleClose, [=] {
		closeBox();
	});

	const auto weakBox = base::make_weak(this);
	auto text = _changes.value(
	) | rpl::map([=](const Data::StoryAlbumUpdate &update) {
		return (!update.added.empty() && update.removed.empty())
			? tr::lng_stories_album_add_button()
			: tr::lng_settings_save();
	}) | rpl::flatten_latest();
	addButton(std::move(text), [=] {
		if (_saving) {
			return;
		}
		auto add = QVector<MTPint>();
		auto remove = QVector<MTPint>();
		const auto &changes = _changes.current();
		for (const auto &id : changes.added) {
			add.push_back(MTP_int(id));
		}
		for (const auto &id : changes.removed) {
			remove.push_back(MTP_int(id));
		}
		if (add.empty() && remove.empty()) {
			closeBox();
			return;
		}
		_saving = true;
		const auto session = &_window->session();
		const auto reload = _reload;
		using Flag = MTPstories_UpdateAlbum::Flag;
		session->api().request(
			MTPstories_UpdateAlbum(
				MTP_flags(Flag()
					| (add.isEmpty() ? Flag() : Flag::f_add_stories)
					| (remove.isEmpty()
						? Flag()
						: Flag::f_delete_stories)),
				changes.peer->input,
				MTP_int(changes.albumId),
				MTPstring(),
				MTP_vector<MTPint>(remove),
				MTP_vector<MTPint>(add),
				MTPVector<MTPint>())
		).done([=] {
			if (const auto strong = weakBox.get()) {
				strong->_saving = false;
				strong->closeBox();
			}
			session->data().stories().notifyAlbumUpdate(
				base::duplicate(changes));
			if (const auto onstack = reload) {
				onstack();
			}
		}).fail([=](const MTP::Error &error) {
			if (const auto strong = weakBox.get()) {
				strong->_saving = false;
				strong->uiShow()->showToast(error.type());
			}
		}).send();
	});
}

void EditAlbumBox::resizeEvent(QResizeEvent *e) {
	_content->setGeometry(rect());
}

} // namespace

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
, _api(std::make_unique<MTP::Sender>(&_peer->session().mtp())) {
	preloadArchiveCount();

	_albumId.value(
	) | rpl::start_with_next([=](int albumId) {
		if (_albumsTabs
			&& (albumId == Data::kStoriesAlbumIdSaved
				|| ranges::contains(
					_albums,
					albumId,
					&Data::StoryAlbum::id))) {
			_albumsTabs->setActiveTab((albumId == Data::kStoriesAlbumIdSaved)
				? u"all"_q
				: QString::number(albumId));
		}
		_controller->replaceKey(Key(Tag(_peer, albumId, _addingToAlbumId)));
		reload();
	}, lifetime());
}

void InnerWidget::preloadArchiveCount() {
	constexpr auto kArchive = Data::kStoriesAlbumIdArchive;
	const auto stories = &_peer->owner().stories();
	if (!_peer->canEditStories()
		|| stories->albumIdsCountKnown(_peer->id, kArchive)) {
		return;
	}
	const auto key = Data::StoryAlbumIdsKey{ _peer->id, kArchive };
	stories->albumIdsLoadMore(_peer->id, kArchive);
	stories->albumIdsChanged() | rpl::filter(
		rpl::mappers::_1 == key
	) | rpl::take_while([=] {
		return !stories->albumIdsCountKnown(_peer->id, kArchive);
	}) | rpl::start_with_next([=] {
		refreshAlbumsTabs();
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
	if (_addingToAlbumId) {
		return;
	} else if (albumId == Data::kStoriesAlbumIdArchive) {
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
		_controller->showSection(PeerGifts::Make(_peer));
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

void InnerWidget::setupEmpty() {
	_list->resizeToWidth(width());

	const auto stories = &_controller->session().data().stories();
	const auto key = Data::StoryAlbumIdsKey{ _peer->id, _albumId.current() };
	rpl::combine(
		rpl::single(
			rpl::empty
		) | rpl::then(
			stories->albumIdsChanged() | rpl::filter(
				rpl::mappers::_1 == key
			) | rpl::to_empty
		),
		_list->heightValue()
	) | rpl::start_with_next([=](auto, int listHeight) {
		const auto padding = st::infoMediaMargin;
		if (const auto raw = _empty.release()) {
			raw->hide();
			raw->deleteLater();
		}
		_emptyLoading = false;
		if (listHeight <= padding.bottom() + padding.top()) {
			refreshEmpty();
		} else {
			_albumEmpty = false;
		}
		refreshHeight();
	}, _list->lifetime());
}

void InnerWidget::refreshEmpty() {
	const auto albumId = _albumId.current();
	const auto stories = &_controller->session().data().stories();
	const auto knownEmpty = stories->albumIdsCountKnown(_peer->id, albumId);
	const auto albumCanAdd = knownEmpty
		&& albumId
		&& (albumId != Data::kStoriesAlbumIdArchive)
		&& _peer->canEditStories();
	_albumEmpty = albumCanAdd;
	if (albumCanAdd) {
		auto empty = object_ptr<Ui::VerticalLayout>(this);
		empty->add(
			object_ptr<Ui::FlatLabel>(
				empty.get(),
				tr::lng_stories_album_empty_title(),
				st::collectionEmptyTitle),
			st::collectionEmptyTitleMargin,
			style::al_top);
		empty->add(
			object_ptr<Ui::FlatLabel>(
				empty.get(),
				tr::lng_stories_album_empty_text(),
				st::collectionEmptyText),
			st::collectionEmptyTextMargin,
			style::al_top);

		const auto button = empty->add(
			object_ptr<Ui::RoundButton>(
				empty.get(),
				rpl::single(QString()),
				st::collectionEmptyButton),
			st::collectionEmptyAddMargin,
			style::al_top);
		button->setText(tr::lng_stories_album_add_button(
		) | rpl::map([](const QString &text) {
			return Ui::Text::IconEmoji(&st::collectionAddIcon).append(text);
		}));
		button->setTextTransform(
			Ui::RoundButton::TextTransform::NoTransform);
		button->setClickedCallback([=] {
			editAlbumStories(albumId);
		});
		empty->show();
		_empty = std::move(empty);
	} else {
		_empty = object_ptr<Ui::FlatLabel>(
			this,
			(!knownEmpty
				? tr::lng_contacts_loading(Ui::Text::WithEntities)
				: _peer->isSelf()
				? tr::lng_stories_empty(Ui::Text::RichLangValue)
				: tr::lng_stories_empty_channel(Ui::Text::RichLangValue)),
			st::giftListAbout);
		_empty->show();
	}
	_emptyLoading = !albumCanAdd && !knownEmpty;
	resizeToWidth(width());
}

void InnerWidget::refreshAlbumsTabs() {
	Expects(!_addingToAlbumId);
	Expects(_albumsWrap != nullptr);

	const auto has = _peer->canEditStories()
		&& _peer->owner().stories().albumIdsCount(
			_peer->id,
			Data::kStoriesAlbumIdArchive);
	if (_albums.empty() && !has) {
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
	if (has) {
		tabs.push_back({
			.id = u"add"_q,
			.text = { '+' + tr::lng_stories_album_add(tr::now) },
		});
	}
	if (!_albumsTabs) {
		const auto tabsCount = tabs.size();
		_albumsTabs = std::make_unique<Ui::SubTabs>(
			_albumsWrap,
			st::collectionSubTabs,
			Ui::SubTabs::Options{
				.selected = selected,
				.centered = true,
			},
			std::move(tabs));
		_albumsTabs->setPinnedInterval(0, 1);
		if (has) {
			_albumsTabs->setPinnedInterval(tabsCount - 1, tabsCount);
		}
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
				_albumIdChanges.fire((id == u"all"_q) ? 0 : id.toInt());
			}
		}, _albumsTabs->lifetime());

		_albumsTabs->contextMenuRequests(
		) | rpl::start_with_next([=](const QString &id) {
			if (id == u"add"_q || id == u"all"_q) {
				return;
			}
			showMenuForAlbum(id.toInt());
		}, _albumsTabs->lifetime());

		using ReorderUpdate = Ui::SubTabsReorderUpdate;
		_albumsTabs->reorderUpdates(
		) | rpl::start_with_next([=](const ReorderUpdate &update) {
			if (update.state == ReorderUpdate::State::Applied) {
				reorderAlbumsLocally(update);
			}
		}, _albumsTabs->lifetime());
	} else {
		const auto tabsCount = tabs.size();
		_albumsTabs->setTabs(std::move(tabs));
		_albumsTabs->clearPinnedIntervals();
		_albumsTabs->setPinnedInterval(0, 1);
		if (has) {
			_albumsTabs->setPinnedInterval(tabsCount - 1, tabsCount);
		}
		if (!selected.isEmpty()) {
			_albumsTabs->setActiveTab(selected);
		}
	}
	resizeToWidth(width());
}

void InnerWidget::showMenuForAlbum(int id) {
	Expects(id > 0);

	if (_menu || _addingToAlbumId) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	const auto addAction = Ui::Menu::CreateAddActionCallback(_menu);

	if (_albumsTabs && _albumsTabs->reorderEnabled()) {
		addAction(
			tr::lng_gift_collection_reorder_exit(tr::now),
			[=] {
				flushAlbumReorder();
				_albumsTabs->setReorderEnabled(false);
			},
			&st::menuIconReorder);
		_menu->popup(QCursor::pos());
		return;
	}

	if (_peer->canEditStories()) {
		addAction(tr::lng_stories_album_add_button(tr::now), [=] {
			editAlbumStories(id);
		}, &st::menuIconStoriesSave);
	}
	if (const auto username = _peer->username(); !username.isEmpty()) {
		addAction(tr::lng_stories_album_share(tr::now), [=] {
			shareAlbumLink(username, id);
		}, &st::menuIconShare);
	}
	if (_peer->canEditStories()) {
		addAction(tr::lng_stories_album_edit(tr::now), [=] {
			editAlbumName(id);
		}, &st::menuIconEdit);
		if (_albumsTabs) {
			addAction(
				tr::lng_gift_collection_reorder(tr::now),
				[=] { _albumsTabs->setReorderEnabled(true); },
				&st::menuIconReorder);
		}
		addAction({
			.text = tr::lng_stories_album_delete(tr::now),
			.handler = [=] { confirmDeleteAlbum(id); },
			.icon = &st::menuIconDeleteAttention,
			.isAttention = true,
		});
	}
	if (_menu->empty()) {
		_menu = nullptr;
	} else {
		_menu->popup(QCursor::pos());
	}
}

rpl::producer<int> InnerWidget::albumIdChanges() const {
	return _albumIdChanges.events();
}

rpl::producer<Data::StoryAlbumUpdate> InnerWidget::changes() const {
	return _albumChanges.value();
}

void InnerWidget::reload() {
	auto old = std::exchange(_list, object_ptr<Media::ListWidget>(nullptr));
	setupList();
	setupEmpty();
	old.destroy();

	resizeToWidth(width());
}

void InnerWidget::editAlbumStories(int id) {
	const auto weak = base::make_weak(this);
	auto box = Box<EditAlbumBox>(_controller, _peer, crl::guard(this, [=] {
		if (_albumId.current() == id) {
			reload();
		}
	}), id);

	_controller->uiShow()->show(std::move(box));
}

void InnerWidget::shareAlbumLink(const QString &username, int id) {
	const auto url = _controller->session().createInternalLinkFull(
		username + u"/a/"_q + QString::number(id));
	FastShareLink(_controller->parentController(), url);
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
		albumRemoved(id);

		const auto stories = &_controller->session().data().stories();
		stories->albumDelete(_peer, id);

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

	_albumIdChanges.fire_copy(result.id);
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
		_albumIdChanges.fire_copy(0);
	}
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
	if (!newWidth) {
		return 0;
	}
	_inResize = true;
	auto guard = gsl::finally([this] { _inResize = false; });

	if (_top) {
		_top->resizeToWidth(newWidth);
	}
	if (_list) {
		_list->resizeToWidth(newWidth);
	}
	if (const auto empty = _empty.get()) {
		const auto margin = st::giftListAboutMargin;
		empty->resizeToWidth(newWidth - margin.left() - margin.right());
	}

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
	if (const auto empty = _empty.get()) {
		const auto margin = st::giftListAboutMargin;
		empty->moveToLeft(margin.left(), top + margin.top());
		top += margin.top() + empty->height() + margin.bottom();
	}
	if (_emptyLoading) {
		top = std::max(top, _lastNonLoadingHeight);
	} else {
		_lastNonLoadingHeight = top;
	}
	return top;
}

void InnerWidget::setScrollHeightValue(rpl::producer<int> value) {
}

rpl::producer<Ui::ScrollToRequest> InnerWidget::scrollToRequests() const {
	return _scrollToRequests.events();
}

void InnerWidget::reorderAlbumsLocally(
		const Ui::SubTabsReorderUpdate &update) {
	if (!_albumsTabs || !_peer->canEditStories()) {
		return;
	}

	const auto albumId = update.id.toInt();
	if (albumId <= 0) {
		return;
	}

	const auto it = ranges::find(
		_albums,
		albumId,
		&Data::StoryAlbum::id);
	if (it == _albums.end()) {
		return;
	}

	const auto album = *it;
	_albums.erase(it);

	const auto newPos = std::max(
		0,
		std::min(update.newPosition - 1, int(_albums.size())));
	_albums.insert(_albums.begin() + newPos, album);

	_pendingAlbumReorder = true;
}

void InnerWidget::flushAlbumReorder() {
	if (!_pendingAlbumReorder || !_peer->canEditStories()) {
		return;
	}

	if (_reorderRequestId) {
		_api->request(_reorderRequestId).cancel();
		_reorderRequestId = 0;
	}

	auto order = QVector<MTPint>();
	for (const auto &album : _albums) {
		order.push_back(MTP_int(album.id));
	}

	_reorderRequestId = _api->request(MTPstories_ReorderAlbums(
		_peer->input,
		MTP_vector<MTPint>(order)
	)).done([=] {
		_reorderRequestId = 0;
	}).fail([=, show = _controller->uiShow()](const MTP::Error &error) {
		_reorderRequestId = 0;
	}).send();

	_pendingAlbumReorder = false;
}

} // namespace Info::Stories
