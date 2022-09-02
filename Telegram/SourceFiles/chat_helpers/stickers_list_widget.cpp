/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_list_widget.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_file_origin.h"
#include "data/data_cloud_file.h"
#include "data/data_changes.h"
#include "data/data_peer_values.h"
#include "menu/menu_send.h" // SendMenu::FillSendMenu
#include "chat_helpers/stickers_lottie.h"
#include "chat_helpers/stickers_list_footer.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/effects/animations.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/effects/premium_graphics.h"
#include "ui/image/image.h"
#include "ui/cached_round_corners.h"
#include "lottie/lottie_multi_player.h"
#include "lottie/lottie_single_player.h"
#include "lottie/lottie_animation.h"
#include "boxes/stickers_box.h"
#include "inline_bots/inline_bot_result.h"
#include "storage/storage_account.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "dialogs/ui/dialogs_layout.h"
#include "boxes/sticker_set_box.h"
#include "boxes/stickers_box.h"
#include "ui/boxes/confirm_box.h"
#include "window/window_session_controller.h" // GifPauseReason.
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "media/clip/media_clip_reader.h"
#include "apiwrap.h"
#include "api/api_toggling_media.h" // Api::ToggleFavedSticker
#include "api/api_premium.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_window.h"
#include "styles/style_menu_icons.h"

#include <QtWidgets/QApplication>

namespace ChatHelpers {
namespace {

constexpr auto kSearchRequestDelay = 400;
constexpr auto kRecentDisplayLimit = 20;
constexpr auto kPreloadOfficialPages = 4;
constexpr auto kOfficialLoadLimit = 40;
constexpr auto kMinRepaintDelay = crl::time(33);
constexpr auto kMinAfterScrollDelay = crl::time(33);

using Data::StickersSet;
using Data::StickersPack;
using Data::StickersSetThumbnailView;
using SetFlag = Data::StickersSetFlag;

[[nodiscard]] bool SetInMyList(Data::StickersSetFlags flags) {
	return (flags & SetFlag::Installed) && !(flags & SetFlag::Archived);
}

} // namespace

struct StickersListWidget::Sticker {
	not_null<DocumentData*> document;
	std::shared_ptr<Data::DocumentMedia> documentMedia;
	Lottie::Animation *lottie = nullptr;
	Media::Clip::ReaderPointer webm;
	QImage savedFrame;
	QSize savedFrameFor;
	QImage premiumLock;

	void ensureMediaCreated();
};

struct StickersListWidget::Set {
	Set(
		uint64 id,
		Data::StickersSet *set,
		Data::StickersSetFlags flags,
		const QString &title,
		const QString &shortName,
		int count,
		bool externalLayout,
		std::vector<Sticker> &&stickers = {});
	Set(Set &&other);
	Set &operator=(Set &&other);
	~Set();

	uint64 id = 0;
	Data::StickersSet *set = nullptr;
	DocumentData *thumbnailDocument = nullptr;
	Data::StickersSetFlags flags;
	QString title;
	QString shortName;
	std::vector<Sticker> stickers;
	std::unique_ptr<Ui::RippleAnimation> ripple;
	crl::time lastUpdateTime = 0;

	std::unique_ptr<Lottie::MultiPlayer> lottiePlayer;
	rpl::lifetime lottieLifetime;

	int count = 0;
	bool externalLayout = false;
};

auto StickersListWidget::PrepareStickers(
	const QVector<DocumentData*> &pack,
	bool skipPremium)
-> std::vector<Sticker> {
	return ranges::views::all(
		pack
	) | ranges::views::filter([&](DocumentData *document) {
		return !skipPremium || !document->isPremiumSticker();
	}) | ranges::views::transform([](DocumentData *document) {
		return Sticker{ document };
	}) | ranges::to_vector;
}

StickersListWidget::Set::Set(
	uint64 id,
	StickersSet *set,
	Data::StickersSetFlags flags,
	const QString &title,
	const QString &shortName,
	int count,
	bool externalLayout,
	std::vector<Sticker> &&stickers)
: id(id)
, set(set)
, flags(flags)
, title(title)
, shortName(shortName)
, stickers(std::move(stickers))
, count(count)
, externalLayout(externalLayout) {
}

StickersListWidget::Set::Set(Set &&other) = default;
StickersListWidget::Set &StickersListWidget::Set::operator=(
	Set &&other) = default;
StickersListWidget::Set::~Set() = default;

void StickersListWidget::Sticker::ensureMediaCreated() {
	if (documentMedia) {
		return;
	}
	documentMedia = document->createMediaView();
}

StickersListWidget::StickersListWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	Window::GifPauseReason level,
	bool masks)
: Inner(
	parent,
	st::defaultEmojiPan,
	&controller->session(),
	Window::PausedIn(controller, level))
, _controller(controller)
, _api(&session().mtp())
, _localSetsManager(std::make_unique<LocalStickersManager>(&session()))
, _section(Section::Stickers)
, _isMasks(masks)
, _updateItemsTimer([=] { updateItems(); })
, _updateSetsTimer([=] { updateSets(); })
, _pathGradient(std::make_unique<Ui::PathShiftGradient>(
	st::windowBgRipple,
	st::windowBgOver,
	[=] { update(); }))
, _megagroupSetAbout(st::columnMinimalWidthThird - st::emojiScroll.width - st().headerLeft)
, _addText(tr::lng_stickers_featured_add(tr::now).toUpper())
, _addWidth(st::stickersTrendingAdd.font->width(_addText))
, _settings(this, tr::lng_stickers_you_have(tr::now))
, _previewTimer([=] { showPreview(); })
, _premiumMark(std::make_unique<StickerPremiumMark>(&session()))
, _searchRequestTimer([=] { sendSearchRequest(); }) {
	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	_settings->addClickHandler([=] {
		using Section = StickersBox::Section;
		controller->show(
			Box<StickersBox>(controller, Section::Installed, _isMasks),
			Ui::LayerOption::KeepOther);
	});

	session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		if (isVisible()) {
			updateItems();
			readVisibleFeatured(getVisibleTop(), getVisibleBottom());
		}
	}, lifetime());

	session().changes().peerUpdates(
		Data::PeerUpdate::Flag::StickersSet
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return (update.peer.get() == _megagroupSet);
	}) | rpl::start_with_next([=] {
		refreshStickers();
	}, lifetime());

	session().data().stickers().recentUpdated(
		_isMasks ? Data::StickersType::Masks : Data::StickersType::Stickers
	) | rpl::start_with_next([=] {
		refreshRecent();
	}, lifetime());

	positionValue(
	) | rpl::skip(1) | rpl::map_to(
		TabbedSelector::Action::Update
	) | rpl::start_to_stream(_choosingUpdated, lifetime());

	rpl::merge(
		Data::AmPremiumValue(&session()) | rpl::to_empty,
		session().api().premium().cloudSetUpdated()
	) | rpl::start_with_next([=] {
		refreshStickers();
	}, lifetime());
}

rpl::producer<TabbedSelector::FileChosen> StickersListWidget::chosen() const {
	return _chosen.events();
}

rpl::producer<> StickersListWidget::scrollUpdated() const {
	return _scrollUpdated.events();
}

auto StickersListWidget::choosingUpdated() const
-> rpl::producer<TabbedSelector::Action> {
	return _choosingUpdated.events();
}

object_ptr<TabbedSelector::InnerFooter> StickersListWidget::createFooter() {
	Expects(_footer == nullptr);

	using FooterDescriptor = StickersListFooter::Descriptor;
	auto result = object_ptr<StickersListFooter>(FooterDescriptor{
		.session = &session(),
		.paused = pausedMethod(),
		.parent = this,
		.searchButtonVisible = !_isMasks,
		.settingsButtonVisible = true,
		.barSelection = true,
	});
	_footer = result;

	_footer->setChosen(
	) | rpl::start_with_next([=](uint64 setId) {
		showStickerSet(setId);
	}, _footer->lifetime());

	_footer->openSettingsRequests(
	) | rpl::start_with_next([=] {
		const auto onlyFeatured = _footer->hasOnlyFeaturedSets();
		_controller->show(Box<StickersBox>(
			_controller,
			(onlyFeatured
				? StickersBox::Section::Featured
				: _isMasks
				? StickersBox::Section::Masks
				: StickersBox::Section::Installed),
			onlyFeatured ? false : _isMasks),
		Ui::LayerOption::KeepOther);
	}, _footer->lifetime());

	_footer->searchRequests(
	) | rpl::start_with_next([=](StickersListFooter::SearchRequest request) {
		if (request.forced) {
			sendSearchRequest();
		} else {
			searchForSets(request.text);
		}
	}, _footer->lifetime());

	return result;
}

void StickersListWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	const auto top = getVisibleTop();
	Inner::visibleTopBottomUpdated(visibleTop, visibleBottom);
	if (top != getVisibleTop()) {
		_lastScrolledAt = crl::now();
		_repaintSetsIds.clear();
		update();
	}
	if (_section == Section::Featured) {
		checkVisibleFeatured(visibleTop, visibleBottom);
	} else {
		checkVisibleLottie();
	}
	if (_footer) {
		_footer->validateSelectedIcon(
			currentSet(visibleTop),
			ValidateIconAnimations::Full);
	}
}

void StickersListWidget::checkVisibleFeatured(
		int visibleTop,
		int visibleBottom) {
	readVisibleFeatured(visibleTop, visibleBottom);

	const auto visibleHeight = visibleBottom - visibleTop;

	if (visibleBottom > height() - visibleHeight * kPreloadOfficialPages) {
		preloadMoreOfficial();
	}

	const auto rowHeight = featuredRowHeight();
	const auto destroyAbove = floorclamp(visibleTop - visibleHeight, rowHeight, 0, _officialSets.size());
	const auto destroyBelow = ceilclamp(visibleBottom + visibleHeight, rowHeight, 0, _officialSets.size());
	for (auto i = 0; i != destroyAbove; ++i) {
		clearHeavyIn(_officialSets[i]);
	}
	for (auto i = destroyBelow; i != _officialSets.size(); ++i) {
		clearHeavyIn(_officialSets[i]);
	}
}

void StickersListWidget::preloadMoreOfficial() {
	if (_officialRequestId) {
		return;
	}
	_officialRequestId = _api.request(MTPmessages_GetOldFeaturedStickers(
		MTP_int(_officialOffset),
		MTP_int(kOfficialLoadLimit),
		MTP_long(0) // hash
	)).done([=](const MTPmessages_FeaturedStickers &result) {
		_officialRequestId = 0;
		result.match([&](const MTPDmessages_featuredStickersNotModified &d) {
			LOG(("Api Error: messages.featuredStickersNotModified."));
		}, [&](const MTPDmessages_featuredStickers &data) {
			const auto &list = data.vsets().v;
			_officialOffset += list.size();
			for (int i = 0, l = list.size(); i != l; ++i) {
				const auto set = session().data().stickers().feedSet(
					list[i]);
				if (set->stickers.empty() && set->covers.empty()) {
					continue;
				}
				const auto externalLayout = true;
				appendSet(
					_officialSets,
					set->id,
					externalLayout,
					AppendSkip::Installed);
			}
		});
		resizeToWidth(width());
		repaintItems();
	}).send();
}

void StickersListWidget::readVisibleFeatured(
		int visibleTop,
		int visibleBottom) {
	const auto rowHeight = featuredRowHeight();
	const auto rowFrom = floorclamp(visibleTop, rowHeight, 0, _featuredSetsCount);
	const auto rowTo = ceilclamp(visibleBottom, rowHeight, 0, _featuredSetsCount);
	for (auto i = rowFrom; i < rowTo; ++i) {
		auto &set = _officialSets[i];
		if (!(set.flags & SetFlag::Unread)) {
			continue;
		}
		if (i * rowHeight < visibleTop || (i + 1) * rowHeight > visibleBottom) {
			continue;
		}
		int count = qMin(int(set.stickers.size()), _columnCount);
		int loaded = 0;
		for (int j = 0; j < count; ++j) {
			if (!set.stickers[j].document->hasThumbnail()
				|| !set.stickers[j].document->thumbnailLoading()
				|| (set.stickers[j].documentMedia
					&& set.stickers[j].documentMedia->loaded())) {
				++loaded;
			}
		}
		if (count > 0 && loaded == count) {
			session().api().readFeaturedSetDelayed(set.id);
		}
	}
}

int StickersListWidget::featuredRowHeight() const {
	return st::stickersTrendingHeader
		+ _singleSize.height()
		+ st::stickersTrendingSkip;
}

template <typename Callback>
bool StickersListWidget::enumerateSections(Callback callback) const {
	auto info = SectionInfo();
	const auto &sets = shownSets();
	for (auto i = 0; i != sets.size(); ++i) {
		auto &set = sets[i];
		info.section = i;
		info.count = set.stickers.size();
		const auto titleSkip = set.externalLayout
			? st::stickersTrendingHeader
			: setHasTitle(set)
			? st().header
			: st::stickerPanPadding;
		info.rowsTop = info.top + titleSkip;
		if (set.externalLayout) {
			info.rowsCount = 1;
			info.rowsBottom = info.top + featuredRowHeight();
		} else if (set.id == Data::Stickers::MegagroupSetId && !info.count) {
			info.rowsCount = 0;
			info.rowsBottom = info.rowsTop + _megagroupSetButtonRect.y() + _megagroupSetButtonRect.height() + st::stickerGroupCategoryAddMargin.bottom();
		} else {
			info.rowsCount = (info.count / _columnCount) + ((info.count % _columnCount) ? 1 : 0);
			info.rowsBottom = info.rowsTop + info.rowsCount * _singleSize.height();
		}
		if (!callback(info)) {
			return false;
		}
		info.top = info.rowsBottom;
	}
	return true;
}

StickersListWidget::SectionInfo StickersListWidget::sectionInfo(int section) const {
	Expects(section >= 0 && section < shownSets().size());

	auto result = SectionInfo();
	enumerateSections([searchForSection = section, &result](const SectionInfo &info) {
		if (info.section == searchForSection) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

StickersListWidget::SectionInfo StickersListWidget::sectionInfoByOffset(int yOffset) const {
	auto result = SectionInfo();
	enumerateSections([this, &result, yOffset](const SectionInfo &info) {
		if (yOffset < info.rowsBottom || info.section == shownSets().size() - 1) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

int StickersListWidget::countDesiredHeight(int newWidth) {
	if (newWidth <= st::stickerPanWidthMin) {
		return 0;
	}
	auto availableWidth = newWidth - (st::stickerPanPadding - st().margin.left());
	auto columnCount = availableWidth / st::stickerPanWidthMin;
	auto singleWidth = availableWidth / columnCount;
	auto fullWidth = (st().margin.left() + newWidth + st::emojiScroll.width);
	auto rowsRight = (fullWidth - columnCount * singleWidth) / 2;
	accumulate_max(rowsRight, st::emojiScroll.width);
	_rowsLeft = fullWidth
		- columnCount * singleWidth
		- rowsRight
		- st().margin.left();
	_singleSize = QSize(singleWidth, singleWidth);
	setColumnCount(columnCount);

	auto visibleHeight = minimalHeight();
	auto minimalHeight = (visibleHeight - st::stickerPanPadding);
	auto countResult = [this](int minimalLastHeight) {
		const auto &sets = shownSets();
		if (sets.empty()) {
			return 0;
		}
		const auto info = sectionInfo(sets.size() - 1);
		return info.top
			+ qMax(info.rowsBottom - info.top, minimalLastHeight);
	};
	const auto minimalLastHeight = (_section == Section::Stickers)
		? minimalHeight
		: 0;
	return qMax(minimalHeight, countResult(minimalLastHeight))
		+ st::stickerPanPadding;
}

void StickersListWidget::sendSearchRequest() {
	if (_searchRequestId || _searchNextQuery.isEmpty()) {
		return;
	}

	_searchRequestTimer.cancel();
	_searchQuery = _searchNextQuery;

	auto it = _searchCache.find(_searchQuery);
	if (it != _searchCache.cend()) {
		_footer->setLoading(false);
		return;
	}

	_footer->setLoading(true);
	const auto hash = uint64(0);
	_searchRequestId = _api.request(MTPmessages_SearchStickerSets(
		MTP_flags(0),
		MTP_string(_searchQuery),
		MTP_long(hash)
	)).done([=](const MTPmessages_FoundStickerSets &result) {
		searchResultsDone(result);
	}).fail([=] {
		// show error?
		_footer->setLoading(false);
		_searchRequestId = 0;
	}).handleAllErrors().send();
}

void StickersListWidget::searchForSets(const QString &query) {
	const auto cleaned = query.trimmed();
	if (cleaned.isEmpty()) {
		cancelSetsSearch();
		return;
	}

	if (_searchQuery != cleaned) {
		_footer->setLoading(false);
		if (const auto requestId = base::take(_searchRequestId)) {
			_api.request(requestId).cancel();
		}
		if (_searchCache.find(cleaned) != _searchCache.cend()) {
			_searchRequestTimer.cancel();
			_searchQuery = _searchNextQuery = cleaned;
		} else {
			_searchNextQuery = cleaned;
			_searchRequestTimer.callOnce(kSearchRequestDelay);
		}
		showSearchResults();
	}
}

void StickersListWidget::cancelSetsSearch() {
	_footer->setLoading(false);
	if (const auto requestId = base::take(_searchRequestId)) {
		_api.request(requestId).cancel();
	}
	_searchRequestTimer.cancel();
	_searchQuery = _searchNextQuery = QString();
	_searchCache.clear();
	refreshSearchRows(nullptr);
}

void StickersListWidget::showSearchResults() {
	refreshSearchRows();
	scrollTo(0);
}

void StickersListWidget::refreshSearchRows() {
	auto it = _searchCache.find(_searchQuery);
	auto sets = (it != end(_searchCache))
		? &it->second
		: nullptr;
	refreshSearchRows(sets);
}

void StickersListWidget::refreshSearchRows(
		const std::vector<uint64> *cloudSets) {
	clearSelection();

	const auto wasSection = _section;
	auto wasSets = base::take(_searchSets);
	const auto guard = gsl::finally([&] {
		if (_section == wasSection && _section == Section::Search) {
			takeHeavyData(_searchSets, wasSets);
		}
	});

	fillLocalSearchRows(_searchNextQuery);

	if (!cloudSets && _searchNextQuery.isEmpty()) {
		showStickerSet(!_mySets.empty()
			? _mySets[0].id
			: Data::Stickers::FeaturedSetId);
		return;
	}

	setSection(Section::Search);
	if (cloudSets) {
		fillCloudSearchRows(*cloudSets);
	}
	refreshIcons(ValidateIconAnimations::Scroll);

	_lastMousePosition = QCursor::pos();

	resizeToWidth(width());
	updateSelected();
}

void StickersListWidget::fillLocalSearchRows(const QString &query) {
	const auto searchWordsList = TextUtilities::PrepareSearchWords(query);
	if (searchWordsList.isEmpty()) {
		return;
	}
	auto searchWordInTitle = [](
			const QStringList &titleWords,
			const QString &searchWord) {
		for (const auto &titleWord : titleWords) {
			if (titleWord.startsWith(searchWord)) {
				return true;
			}
		}
		return false;
	};
	auto allSearchWordsInTitle = [&](
			const QStringList &titleWords) {
		for (const auto &searchWord : searchWordsList) {
			if (!searchWordInTitle(titleWords, searchWord)) {
				return false;
			}
		}
		return true;
	};

	const auto &sets = session().data().stickers().sets();
	for (const auto &[setId, titleWords] : _searchIndex) {
		if (allSearchWordsInTitle(titleWords)) {
			if (const auto it = sets.find(setId); it != sets.end()) {
				addSearchRow(it->second.get());
			}
		}
	}
}

void StickersListWidget::fillCloudSearchRows(
		const std::vector<uint64> &cloudSets) {
	const auto &sets = session().data().stickers().sets();
	for (const auto setId : cloudSets) {
		if (const auto it = sets.find(setId); it != sets.end()) {
			addSearchRow(it->second.get());
		}
	}
}

void StickersListWidget::addSearchRow(not_null<StickersSet*> set) {
	const auto skipPremium = !session().premiumPossible();
	auto elements = PrepareStickers(
		set->stickers.empty() ? set->covers : set->stickers,
		skipPremium);
	_searchSets.emplace_back(
		set->id,
		set,
		set->flags,
		set->title,
		set->shortName,
		set->count,
		!SetInMyList(set->flags),
		std::move(elements));
}

void StickersListWidget::takeHeavyData(
		std::vector<Set> &to,
		std::vector<Set> &from) {
	auto indices = base::flat_map<uint64, int>();
	indices.reserve(from.size());
	auto index = 0;
	for (const auto &set : from) {
		indices.emplace(set.id, index++);
	}
	for (auto &toSet : to) {
		const auto i = indices.find(toSet.id);
		if (i != end(indices)) {
			takeHeavyData(toSet, from[i->second]);
		}
	}
}

void StickersListWidget::takeHeavyData(Set &to, Set &from) {
	to.lottiePlayer = std::move(from.lottiePlayer);
	to.lottieLifetime = std::move(from.lottieLifetime);
	auto &toList = to.stickers;
	auto &fromList = from.stickers;
	const auto same = ranges::equal(
		toList,
		fromList,
		ranges::equal_to(),
		&Sticker::document,
		&Sticker::document);
	if (same) {
		for (auto i = 0, count = int(toList.size()); i != count; ++i) {
			takeHeavyData(toList[i], fromList[i]);
		}
	} else {
		auto indices = base::flat_map<not_null<DocumentData*>, int>();
		indices.reserve(fromList.size());
		auto index = 0;
		for (const auto &fromSticker : fromList) {
			indices.emplace(fromSticker.document, index++);
		}
		for (auto &toSticker : toList) {
			const auto i = indices.find(toSticker.document);
			if (i != end(indices)) {
				takeHeavyData(toSticker, fromList[i->second]);
			}
		}
		for (const auto &sticker : fromList) {
			if (sticker.lottie) {
				to.lottiePlayer->remove(sticker.lottie);
			}
		}
	}
}

void StickersListWidget::takeHeavyData(Sticker &to, Sticker &from) {
	to.documentMedia = std::move(from.documentMedia);
	to.savedFrame = std::move(from.savedFrame);
	to.savedFrameFor = from.savedFrameFor;
	to.lottie = base::take(from.lottie);
	to.webm = base::take(from.webm);
}

auto StickersListWidget::shownSets() const -> const std::vector<Set> & {
	switch (_section) {
	case Section::Featured: return _officialSets;
	case Section::Search: return _searchSets;
	case Section::Stickers: return _mySets;
	}
	Unexpected("Section in StickersListWidget.");
}

auto StickersListWidget::shownSets() -> std::vector<Set> & {
	switch (_section) {
	case Section::Featured: return _officialSets;
	case Section::Search: return _searchSets;
	case Section::Stickers: return _mySets;
	}
	Unexpected("Section in StickersListWidget.");
}

void StickersListWidget::searchResultsDone(
		const MTPmessages_FoundStickerSets &result) {
	_footer->setLoading(false);
	_searchRequestId = 0;

	if (result.type() == mtpc_messages_foundStickerSetsNotModified) {
		LOG(("API Error: "
			"messages.foundStickerSetsNotModified not expected."));
		return;
	}

	Assert(result.type() == mtpc_messages_foundStickerSets);

	auto it = _searchCache.find(_searchQuery);
	if (it == _searchCache.cend()) {
		it = _searchCache.emplace(
			_searchQuery,
			std::vector<uint64>()).first;
	}
	auto &d = result.c_messages_foundStickerSets();
	for (const auto &data : d.vsets().v) {
		const auto set = session().data().stickers().feedSet(data);
		if (set->stickers.empty() && set->covers.empty()) {
			continue;
		}
		it->second.push_back(set->id);
	}
	showSearchResults();
}

int StickersListWidget::stickersLeft() const {
	return _rowsLeft;
}

QRect StickersListWidget::stickerRect(int section, int sel) {
	auto info = sectionInfo(section);
	if (sel >= shownSets()[section].stickers.size()) {
		sel -= shownSets()[section].stickers.size();
	}
	auto countTillItem = (sel - (sel % _columnCount));
	auto rowsToSkip = (countTillItem / _columnCount) + ((countTillItem % _columnCount) ? 1 : 0);
	auto x = stickersLeft() + ((sel % _columnCount) * _singleSize.width());
	auto y = info.rowsTop + rowsToSkip * _singleSize.height();
	return QRect(QPoint(x, y), _singleSize);
}

void StickersListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	p.fillRect(clip, st::emojiPanBg);

	paintStickers(p, clip);
}

void StickersListWidget::paintStickers(Painter &p, QRect clip) {
	auto fromColumn = floorclamp(clip.x() - stickersLeft(), _singleSize.width(), 0, _columnCount);
	auto toColumn = ceilclamp(clip.x() + clip.width() - stickersLeft(), _singleSize.width(), 0, _columnCount);
	if (rtl()) {
		qSwap(fromColumn, toColumn);
		fromColumn = _columnCount - fromColumn;
		toColumn = _columnCount - toColumn;
	}

	_pathGradient->startFrame(0, width(), width() / 2);

	auto &sets = shownSets();
	auto selectedSticker = std::get_if<OverSticker>(&_selected);
	auto selectedButton = std::get_if<OverButton>(!v::is_null(_pressed)
		? &_pressed
		: &_selected);

	const auto now = crl::now();
	const auto paused = this->paused();
	if (sets.empty() && _section == Section::Search) {
		paintEmptySearchResults(p);
	}
	enumerateSections([&](const SectionInfo &info) {
		if (clip.top() >= info.rowsBottom) {
			return true;
		} else if (clip.top() + clip.height() <= info.top) {
			return false;
		}
		auto &set = sets[info.section];
		if (set.externalLayout) {
			const auto loadedCount = int(set.stickers.size());
			const auto count = (set.flags & SetFlag::NotLoaded)
				? set.count
				: loadedCount;

			auto widthForTitle = stickersRight() - (st().headerLeft - st().margin.left());
			if (featuredHasAddButton(info.section)) {
				auto add = featuredAddRect(info);
				auto selected = selectedButton ? (selectedButton->section == info.section) : false;
				auto &textBg = selected ? st::stickersTrendingAdd.textBgOver : st::stickersTrendingAdd.textBg;

				Ui::FillRoundRect(p, myrtlrect(add), textBg, ImageRoundRadius::Small);
				if (set.ripple) {
					set.ripple->paint(p, add.x(), add.y(), width());
					if (set.ripple->empty()) {
						set.ripple.reset();
					}
				}
				p.setFont(st::stickersTrendingAdd.font);
				p.setPen(selected ? st::stickersTrendingAdd.textFgOver : st::stickersTrendingAdd.textFg);
				p.drawTextLeft(add.x() - (st::stickersTrendingAdd.width / 2), add.y() + st::stickersTrendingAdd.textTop, width(), _addText, _addWidth);

				widthForTitle -= add.width() - (st::stickersTrendingAdd.width / 2);
			} else {
				auto add = featuredAddRect(info);
				int checkx = add.left() + (add.width() - st::stickersFeaturedInstalled.width()) / 2;
				int checky = add.top() + (add.height() - st::stickersFeaturedInstalled.height()) / 2;
				st::stickersFeaturedInstalled.paint(p, QPoint(checkx, checky), width());
			}
			if (set.flags & SetFlag::Unread) {
				widthForTitle -= st::stickersFeaturedUnreadSize + st::stickersFeaturedUnreadSkip;
			}

			auto titleText = set.title;
			auto titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			if (titleWidth > widthForTitle) {
				titleText = st::stickersTrendingHeaderFont->elided(titleText, widthForTitle);
				titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			}
			p.setFont(st::stickersTrendingHeaderFont);
			p.setPen(st::stickersTrendingHeaderFg);
			p.drawTextLeft(st().headerLeft - st().margin.left(), info.top + st::stickersTrendingHeaderTop, width(), titleText, titleWidth);

			if (set.flags & SetFlag::Unread) {
				p.setPen(Qt::NoPen);
				p.setBrush(st::stickersFeaturedUnreadBg);

				{
					PainterHighQualityEnabler hq(p);
					p.drawEllipse(style::rtlrect(st().headerLeft - st().margin.left() + titleWidth + st::stickersFeaturedUnreadSkip, info.top + st::stickersTrendingHeaderTop + st::stickersFeaturedUnreadTop, st::stickersFeaturedUnreadSize, st::stickersFeaturedUnreadSize, width()));
				}
			}

			auto statusText = (count > 0) ? tr::lng_stickers_count(tr::now, lt_count, count) : tr::lng_contacts_loading(tr::now);
			p.setFont(st::stickersTrendingSubheaderFont);
			p.setPen(st::stickersTrendingSubheaderFg);
			p.drawTextLeft(st().headerLeft - st().margin.left(), info.top + st::stickersTrendingSubheaderTop, width(), statusText);

			if (info.rowsTop >= clip.y() + clip.height()) {
				return true;
			}

			for (int j = fromColumn; j < toColumn; ++j) {
				int index = j;
				if (index >= loadedCount) break;

				auto selected = selectedSticker ? (selectedSticker->section == info.section && selectedSticker->index == index) : false;
				auto deleteSelected = false;
				paintSticker(p, set, info.rowsTop, info.section, index, now, paused, selected, deleteSelected);
			}
			if (!paused) {
				markLottieFrameShown(set);
			}
			return true;
		}
		if (setHasTitle(set) && clip.top() < info.rowsTop) {
			auto titleText = set.title;
			auto titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			auto widthForTitle = stickersRight() - (st().headerLeft - st().margin.left());
			if (hasRemoveButton(info.section)) {
				auto remove = removeButtonRect(info);
				auto selected = selectedButton ? (selectedButton->section == info.section) : false;
				if (set.ripple) {
					set.ripple->paint(p, remove.x() + st::stickerPanRemoveSet.rippleAreaPosition.x(), remove.y() + st::stickerPanRemoveSet.rippleAreaPosition.y(), width());
					if (set.ripple->empty()) {
						set.ripple.reset();
					}
				}
				(selected ? st::stickerPanRemoveSet.iconOver : st::stickerPanRemoveSet.icon).paint(p, remove.topLeft() + st::stickerPanRemoveSet.iconPosition, width());

				widthForTitle -= remove.width();
			}
			if (titleWidth > widthForTitle) {
				titleText = st::stickersTrendingHeaderFont->elided(titleText, widthForTitle);
				titleWidth = st::stickersTrendingHeaderFont->width(titleText);
			}
			p.setFont(st::emojiPanHeaderFont);
			p.setPen(st::emojiPanHeaderFg);
			p.drawTextLeft(st().headerLeft - st().margin.left(), info.top + st().headerTop, width(), titleText, titleWidth);
		}
		if (clip.top() + clip.height() <= info.rowsTop) {
			return true;
		} else if (set.id == Data::Stickers::MegagroupSetId && set.stickers.empty()) {
			auto buttonSelected = (std::get_if<OverGroupAdd>(&_selected) != nullptr);
			paintMegagroupEmptySet(p, info.rowsTop, buttonSelected);
			return true;
		}
		auto fromRow = floorclamp(clip.y() - info.rowsTop, _singleSize.height(), 0, info.rowsCount);
		auto toRow = ceilclamp(clip.y() + clip.height() - info.rowsTop, _singleSize.height(), 0, info.rowsCount);
		for (int i = fromRow; i < toRow; ++i) {
			for (int j = fromColumn; j < toColumn; ++j) {
				int index = i * _columnCount + j;
				if (index >= info.count) break;

				auto selected = selectedSticker ? (selectedSticker->section == info.section && selectedSticker->index == index) : false;
				auto deleteSelected = selected && selectedSticker->overDelete;
				paintSticker(p, set, info.rowsTop, info.section, index, now, paused, selected, deleteSelected);
			}
		}
		if (!paused) {
			markLottieFrameShown(set);
		}
		return true;
	});
}

void StickersListWidget::markLottieFrameShown(Set &set) {
	if (const auto player = set.lottiePlayer.get()) {
		player->markFrameShown();
	}
}

void StickersListWidget::checkVisibleLottie() {
	if (shownSets().empty()) {
		return;
	}
	const auto visibleTop = getVisibleTop();
	const auto visibleBottom = getVisibleBottom();
	const auto destroyAfterDistance = (visibleBottom - visibleTop) * 2;
	const auto destroyAbove = visibleTop - destroyAfterDistance;
	const auto destroyBelow = visibleBottom + destroyAfterDistance;
	enumerateSections([&](const SectionInfo &info) {
		if (destroyBelow <= info.rowsTop
			|| destroyAbove >= info.rowsBottom) {
			clearHeavyIn(shownSets()[info.section]);
		} else if ((visibleTop > info.rowsTop && visibleTop < info.rowsBottom)
			|| (visibleBottom > info.rowsTop
				&& visibleBottom < info.rowsBottom)) {
			pauseInvisibleLottieIn(info);
		}
		return true;
	});
}

void StickersListWidget::clearHeavyIn(Set &set, bool clearSavedFrames) {
	const auto player = base::take(set.lottiePlayer);
	const auto lifetime = base::take(set.lottieLifetime);
	for (auto &sticker : set.stickers) {
		if (clearSavedFrames) {
			sticker.savedFrame = QImage();
			sticker.savedFrameFor = QSize();
		}
		sticker.webm = nullptr;
		sticker.lottie = nullptr;
		sticker.documentMedia = nullptr;
	}
}

void StickersListWidget::pauseInvisibleLottieIn(const SectionInfo &info) {
	auto &set = shownSets()[info.section];
	const auto player = set.lottiePlayer.get();
	if (!player) {
		return;
	}
	const auto pauseInRows = [&](int fromRow, int tillRow) {
		Expects(fromRow <= tillRow);

		for (auto i = fromRow; i != tillRow; ++i) {
			for (auto j = 0; j != _columnCount; ++j) {
				const auto index = i * _columnCount + j;
				if (index >= info.count) {
					break;
				}
				if (const auto animated = set.stickers[index].lottie) {
					player->pause(animated);
				}
			}
		}
	};

	const auto visibleTop = getVisibleTop();
	const auto visibleBottom = getVisibleBottom();
	if (visibleTop >= info.rowsTop + _singleSize.height()
		&& visibleTop < info.rowsBottom) {
		const auto pauseHeight = (visibleTop - info.rowsTop);
		const auto pauseRows = std::min(
			pauseHeight / _singleSize.height(),
			info.rowsCount);
		pauseInRows(0, pauseRows);
	}
	if (visibleBottom > info.rowsTop
		&& visibleBottom + _singleSize.height() <= info.rowsBottom) {
		const auto pauseHeight = (info.rowsBottom - visibleBottom);
		const auto pauseRows = std::min(
			pauseHeight / _singleSize.height(),
			info.rowsCount);
		pauseInRows(info.rowsCount - pauseRows, info.rowsCount);
	}
}

void StickersListWidget::paintEmptySearchResults(Painter &p) {
	const auto iconLeft = (width() - st::stickersEmpty.width()) / 2;
	const auto iconTop = (height() / 3) - (st::stickersEmpty.height() / 2);
	st::stickersEmpty.paint(p, iconLeft, iconTop, width());

	const auto text = tr::lng_stickers_nothing_found(tr::now);
	const auto textWidth = st::normalFont->width(text);
	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawTextLeft(
		(width() - textWidth) / 2,
		iconTop + st::stickersEmpty.height() - st::normalFont->height,
		width(),
		text,
		textWidth);
}

int StickersListWidget::megagroupSetInfoLeft() const {
	return st().headerLeft - st().margin.left();
}

void StickersListWidget::paintMegagroupEmptySet(Painter &p, int y, bool buttonSelected) {
	p.setPen(st::emojiPanHeaderFg);

	auto infoLeft = megagroupSetInfoLeft();
	_megagroupSetAbout.drawLeft(p, infoLeft, y, width() - infoLeft, width());

	auto &textBg = buttonSelected
		? st::stickerGroupCategoryAdd.textBgOver
		: st::stickerGroupCategoryAdd.textBg;

	auto button = _megagroupSetButtonRect.translated(0, y);
	Ui::FillRoundRect(p, myrtlrect(button), textBg, ImageRoundRadius::Small);
	if (_megagroupSetButtonRipple) {
		_megagroupSetButtonRipple->paint(p, button.x(), button.y(), width());
		if (_megagroupSetButtonRipple->empty()) {
			_megagroupSetButtonRipple.reset();
		}
	}
	p.setFont(st::stickerGroupCategoryAdd.font);
	p.setPen(buttonSelected ? st::stickerGroupCategoryAdd.textFgOver : st::stickerGroupCategoryAdd.textFg);
	p.drawTextLeft(button.x() - (st::stickerGroupCategoryAdd.width / 2), button.y() + st::stickerGroupCategoryAdd.textTop, width(), _megagroupSetButtonText, _megagroupSetButtonTextWidth);
}

void StickersListWidget::ensureLottiePlayer(Set &set) {
	if (set.lottiePlayer) {
		return;
	}
	set.lottiePlayer = std::make_unique<Lottie::MultiPlayer>(
		Lottie::Quality::Default,
		getLottieRenderer());
	const auto raw = set.lottiePlayer.get();

	raw->updates(
	) | rpl::start_with_next([=] {
		auto &sets = shownSets();
		enumerateSections([&](const SectionInfo &info) {
			if (sets[info.section].lottiePlayer.get() != raw) {
				return true;
			}
			updateSet(info);
			return false;
		});
	}, set.lottieLifetime);
}

void StickersListWidget::setupLottie(Set &set, int section, int index) {
	auto &sticker = set.stickers[index];
	ensureLottiePlayer(set);

	// Document should be loaded already for the animation to be set up.
	Assert(sticker.documentMedia != nullptr);
	sticker.lottie = LottieAnimationFromDocument(
		set.lottiePlayer.get(),
		sticker.documentMedia.get(),
		StickerLottieSize::StickersPanel,
		boundingBoxSize() * cIntRetinaFactor());
}

void StickersListWidget::setupWebm(Set &set, int section, int index) {
	auto &sticker = set.stickers[index];

	// Document should be loaded already for the animation to be set up.
	Assert(sticker.documentMedia != nullptr);
	const auto setId = set.id;
	const auto document = sticker.document;
	auto callback = [=](Media::Clip::Notification notification) {
		clipCallback(notification, setId, document, index);
	};
	sticker.webm = Media::Clip::MakeReader(
		sticker.documentMedia->owner()->location(),
		sticker.documentMedia->bytes(),
		std::move(callback));
}

void StickersListWidget::clipCallback(
		Media::Clip::Notification notification,
		uint64 setId,
		not_null<DocumentData*> document,
		int indexHint) {
	Expects(indexHint >= 0);

	auto &sets = shownSets();
	enumerateSections([&](const SectionInfo &info) {
		auto &set = sets[info.section];
		if (set.id != setId) {
			return true;
		}
		using namespace Media::Clip;
		switch (notification) {
		case Notification::Reinit: {
			const auto j = (indexHint < set.stickers.size()
				&& set.stickers[indexHint].document == document)
				? (begin(set.stickers) + indexHint)
				: ranges::find(set.stickers, document, &Sticker::document);
			if (j == end(set.stickers) || !j->webm) {
				break;
			}
			const auto index = j - begin(set.stickers);
			auto &webm = j->webm;
			if (webm->state() == State::Error) {
				webm.setBad();
			} else if (webm->ready() && !webm->started()) {
				const auto size = ComputeStickerSize(
					j->document,
					boundingBoxSize());
				webm->start({ .frame = size, .keepAlpha = true });
			} else if (webm->autoPausedGif() && !itemVisible(info, index)) {
				webm = nullptr;
			}
		} break;

		case Notification::Repaint: break;
		}

		updateSet(info);
		return false;
	});
}

bool StickersListWidget::itemVisible(
		const SectionInfo &info,
		int index) const {
	const auto visibleTop = getVisibleTop();
	const auto visibleBottom = getVisibleBottom();
	const auto row = index / _columnCount;
	const auto top = info.rowsTop + row * _singleSize.height();
	const auto bottom = top + _singleSize.height();
	return (visibleTop < bottom) && (visibleBottom > top);
}

void StickersListWidget::updateSets() {
	if (_repaintSetsIds.empty()) {
		return;
	}
	auto repaint = base::take(_repaintSetsIds);
	auto &sets = shownSets();
	enumerateSections([&](const SectionInfo &info) {
		if (repaint.contains(sets[info.section].id)) {
			updateSet(info);
		}
		return true;
	});
}

void StickersListWidget::updateSet(const SectionInfo &info) {
	auto &set = shownSets()[info.section];

	const auto now = crl::now();
	const auto delay = std::max(
		_lastScrolledAt + kMinAfterScrollDelay - now,
		set.lastUpdateTime + kMinRepaintDelay - now);
	if (delay <= 0) {
		repaintItems(info, now);
	} else {
		_repaintSetsIds.emplace(set.id);
		if (!_updateSetsTimer.isActive()
			|| _updateSetsTimer.remainingTime() > kMinRepaintDelay) {
			_updateSetsTimer.callOnce(std::max(delay, kMinRepaintDelay));
		}
	}
}

void StickersListWidget::repaintItems(
		const SectionInfo &info,
		crl::time now) {
	update(
		0,
		info.rowsTop,
		width(),
		info.rowsBottom - info.rowsTop);
	auto &set = shownSets()[info.section];
	set.lastUpdateTime = now;
}

void StickersListWidget::updateItems() {
	const auto now = crl::now();
	const auto delay = std::max(
		_lastScrolledAt + kMinAfterScrollDelay - now,
		_lastFullUpdatedAt + kMinRepaintDelay - now);
	if (delay <= 0) {
		repaintItems(now);
	} else if (!_updateItemsTimer.isActive()
		|| _updateItemsTimer.remainingTime() > kMinRepaintDelay) {
		_updateItemsTimer.callOnce(std::max(delay, kMinRepaintDelay));
	}
}

void StickersListWidget::repaintItems(crl::time now) {
	update();
	_repaintSetsIds.clear();
	if (!now) {
		now = crl::now();
	}
	_lastFullUpdatedAt = now;
	for (auto &set : shownSets()) {
		set.lastUpdateTime = now;
	}
}

QSize StickersListWidget::boundingBoxSize() const {
	return QSize(
		_singleSize.width() - st::roundRadiusSmall * 2,
		_singleSize.height() - st::roundRadiusSmall * 2);
}

void StickersListWidget::paintSticker(
		Painter &p,
		Set &set,
		int y,
		int section,
		int index,
		crl::time now,
		bool paused,
		bool selected,
		bool deleteSelected) {
	auto &sticker = set.stickers[index];
	sticker.ensureMediaCreated();
	const auto document = sticker.document;
	const auto &media = sticker.documentMedia;
	if (!document->sticker()) {
		return;
	}

	const auto premium = document->isPremiumSticker();
	const auto isLottie = document->sticker()->isLottie();
	const auto isWebm = document->sticker()->isWebm();
	if (isLottie
		&& !sticker.lottie
		&& media->loaded()) {
		setupLottie(set, section, index);
	} else if (isWebm && !sticker.webm && media->loaded()) {
		setupWebm(set, section, index);
	}

	int row = (index / _columnCount), col = (index % _columnCount);

	auto pos = QPoint(stickersLeft() + col * _singleSize.width(), y + row * _singleSize.height());
	if (selected) {
		auto tl = pos;
		if (rtl()) tl.setX(width() - tl.x() - _singleSize.width());
		Ui::FillRoundRect(p, QRect(tl, _singleSize), st::emojiPanHover, Ui::StickerHoverCorners);
	}

	media->checkStickerSmall();

	const auto size = ComputeStickerSize(document, boundingBoxSize());
	const auto ppos = pos + QPoint(
		(_singleSize.width() - size.width()) / 2,
		(_singleSize.height() - size.height()) / 2);

	auto lottieFrame = QImage();
	if (sticker.lottie && sticker.lottie->ready()) {
		auto request = Lottie::FrameRequest();
		request.box = boundingBoxSize() * cIntRetinaFactor();
		lottieFrame = sticker.lottie->frame(request);
		p.drawImage(
			QRect(ppos, lottieFrame.size() / cIntRetinaFactor()),
			lottieFrame);
		if (sticker.savedFrame.isNull()) {
			sticker.savedFrame = lottieFrame;
			sticker.savedFrame.setDevicePixelRatio(cRetinaFactor());
			sticker.savedFrameFor = _singleSize;
		}
		set.lottiePlayer->unpause(sticker.lottie);
	} else if (sticker.webm && sticker.webm->started()) {
		const auto frame = sticker.webm->current(
			{ .frame = size, .keepAlpha = true },
			paused ? 0 : now);
		if (sticker.savedFrame.isNull()) {
			sticker.savedFrame = frame;
			sticker.savedFrame.setDevicePixelRatio(cRetinaFactor());
			sticker.savedFrameFor = _singleSize;
		}
		p.drawImage(ppos, frame);
	} else {
		const auto image = media->getStickerSmall();
		const auto useSavedFrame = !sticker.savedFrame.isNull()
			&& (sticker.savedFrameFor == _singleSize);
		if (useSavedFrame) {
			p.drawImage(ppos, sticker.savedFrame);
			if (premium) {
				lottieFrame = sticker.savedFrame;
			}
		} else if (image) {
			const auto pixmap = image->pixSingle(size, { .outer = size });
			p.drawPixmapLeft(ppos, width(), pixmap);
			if (sticker.savedFrame.isNull()) {
				sticker.savedFrame = pixmap.toImage().convertToFormat(
					QImage::Format_ARGB32_Premultiplied);
				sticker.savedFrameFor = _singleSize;
			}
			if (premium) {
				lottieFrame = pixmap.toImage().convertToFormat(
					QImage::Format_ARGB32_Premultiplied);
			}
		} else {
			p.setOpacity(1.);
			PaintStickerThumbnailPath(
				p,
				media.get(),
				QRect(ppos, size),
				_pathGradient.get());
		}
	}

	if (selected && stickerHasDeleteButton(set, index)) {
		auto xPos = pos + QPoint(_singleSize.width() - st::stickerPanDeleteIconBg.width(), 0);
		p.setOpacity(deleteSelected ? st::stickerPanDeleteOpacityBgOver : st::stickerPanDeleteOpacityBg);
		st::stickerPanDeleteIconBg.paint(p, xPos, width());
		p.setOpacity(deleteSelected ? st::stickerPanDeleteOpacityFgOver : st::stickerPanDeleteOpacityFg);
		st::stickerPanDeleteIconFg.paint(p, xPos, width());
		p.setOpacity(1.);
	}

	if (premium) {
		_premiumMark->paint(
			p,
			lottieFrame,
			sticker.premiumLock,
			pos,
			_singleSize,
			width());
	}
}

int StickersListWidget::stickersRight() const {
	return stickersLeft() + (_columnCount * _singleSize.width());
}

bool StickersListWidget::featuredHasAddButton(int index) const {
	if (index < 0
		|| index >= shownSets().size()
		|| !shownSets()[index].externalLayout) {
		return false;
	}
	const auto flags = shownSets()[index].flags;
	return !SetInMyList(flags);
}

QRect StickersListWidget::featuredAddRect(int index) const {
	return featuredAddRect(sectionInfo(index));
}

QRect StickersListWidget::featuredAddRect(const SectionInfo &info) const {
	auto addw = _addWidth - st::stickersTrendingAdd.width;
	auto addh = st::stickersTrendingAdd.height;
	auto addx = stickersRight() - addw;
	auto addy = info.top + st::stickersTrendingAddTop;
	return QRect(addx, addy, addw, addh);
}

bool StickersListWidget::hasRemoveButton(int index) const {
	if (index < 0 || index >= shownSets().size()) {
		return false;
	}
	auto &set = shownSets()[index];
	if (set.externalLayout) {
		return false;
	}
	auto flags = set.flags;
	if (!(flags & SetFlag::Special)) {
		return true;
	}
	if (set.id == Data::Stickers::MegagroupSetId) {
		Assert(_megagroupSet != nullptr);
		if (index + 1 != shownSets().size()) {
			return true;
		}
		return !set.stickers.empty() && _megagroupSet->canEditStickers();
	} else if (set.id == Data::Stickers::PremiumSetId) {
		return !set.stickers.empty();
	}
	return false;
}

QRect StickersListWidget::removeButtonRect(int index) const {
	return removeButtonRect(sectionInfo(index));
}

QRect StickersListWidget::removeButtonRect(const SectionInfo &info) const {
	auto buttonw = st::stickerPanRemoveSet.width;
	auto buttonh = st::stickerPanRemoveSet.height;
	auto buttonx = stickersRight() - buttonw;
	auto buttony = info.top + (st().header - buttonh) / 2;
	return QRect(buttonx, buttony, buttonw, buttonh);
}

void StickersListWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePosition = e->globalPos();
	updateSelected();

	setPressed(_selected);
	ClickHandler::pressed();
	_previewTimer.callOnce(QApplication::startDragTime());
}

void StickersListWidget::setPressed(OverState newPressed) {
	if (auto button = std::get_if<OverButton>(&_pressed)) {
		auto &sets = shownSets();
		Assert(button->section >= 0 && button->section < sets.size());
		auto &set = sets[button->section];
		if (set.ripple) {
			set.ripple->lastStop();
		}
	} else if (std::get_if<OverGroupAdd>(&_pressed)) {
		if (_megagroupSetButtonRipple) {
			_megagroupSetButtonRipple->lastStop();
		}
	}
	_pressed = newPressed;
	if (auto button = std::get_if<OverButton>(&_pressed)) {
		auto &sets = shownSets();
		Assert(button->section >= 0 && button->section < sets.size());
		auto &set = sets[button->section];
		if (!set.ripple) {
			set.ripple = createButtonRipple(button->section);
		}
		set.ripple->add(mapFromGlobal(QCursor::pos()) - buttonRippleTopLeft(button->section));
	} else if (std::get_if<OverGroupAdd>(&_pressed)) {
		if (!_megagroupSetButtonRipple) {
			auto maskSize = _megagroupSetButtonRect.size();
			auto mask = Ui::RippleAnimation::roundRectMask(maskSize, st::roundRadiusSmall);
			_megagroupSetButtonRipple = std::make_unique<Ui::RippleAnimation>(st::stickerGroupCategoryAdd.ripple, std::move(mask), [this] {
				rtlupdate(megagroupSetButtonRectFinal());
			});
		}
		_megagroupSetButtonRipple->add(mapFromGlobal(QCursor::pos()) - myrtlrect(megagroupSetButtonRectFinal()).topLeft());
	}
}

QRect StickersListWidget::megagroupSetButtonRectFinal() const {
	auto result = QRect();
	if (_section == Section::Stickers) {
		enumerateSections([this, &result](const SectionInfo &info) {
			if (shownSets()[info.section].id == Data::Stickers::MegagroupSetId) {
				result = _megagroupSetButtonRect.translated(0, info.rowsTop);
				return false;
			}
			return true;
		});
	}
	return result;
}

std::unique_ptr<Ui::RippleAnimation> StickersListWidget::createButtonRipple(int section) {
	Expects(section >= 0 && section < shownSets().size());

	if (shownSets()[section].externalLayout) {
		auto maskSize = QSize(_addWidth - st::stickersTrendingAdd.width, st::stickersTrendingAdd.height);
		auto mask = Ui::RippleAnimation::roundRectMask(maskSize, st::roundRadiusSmall);
		return std::make_unique<Ui::RippleAnimation>(
			st::stickersTrendingAdd.ripple,
			std::move(mask),
			[this, section] { rtlupdate(featuredAddRect(section)); });
	}
	auto maskSize = QSize(st::stickerPanRemoveSet.rippleAreaSize, st::stickerPanRemoveSet.rippleAreaSize);
	auto mask = Ui::RippleAnimation::ellipseMask(maskSize);
	return std::make_unique<Ui::RippleAnimation>(
		st::stickerPanRemoveSet.ripple,
		std::move(mask),
		[this, section] { rtlupdate(removeButtonRect(section)); });
}

QPoint StickersListWidget::buttonRippleTopLeft(int section) const {
	Expects(section >= 0 && section < shownSets().size());

	if (shownSets()[section].externalLayout) {
		return myrtlrect(featuredAddRect(section)).topLeft();
	}
	return myrtlrect(removeButtonRect(section)).topLeft() + st::stickerPanRemoveSet.rippleAreaPosition;
}

void StickersListWidget::showStickerSetBox(not_null<DocumentData*> document) {
	if (document->sticker() && document->sticker()->set) {
		checkHideWithBox(StickerSetBox::Show(_controller, document));
	}
}

base::unique_qptr<Ui::PopupMenu> StickersListWidget::fillContextMenu(
		SendMenu::Type type) {
	auto selected = _selected;
	auto &sets = shownSets();
	if (v::is_null(selected) || !v::is_null(_pressed)) {
		return nullptr;
	}
	const auto sticker = std::get_if<OverSticker>(&selected);
	if (!sticker) {
		return nullptr;
	}
	const auto section = sticker->section;
	const auto index = sticker->index;
	Assert(section >= 0 && section < sets.size());
	auto &set = sets[section];
	Assert(index >= 0 && index < set.stickers.size());

	auto menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);

	const auto document = set.stickers[sticker->index].document;
	const auto send = [=](Api::SendOptions options) {
		_chosen.fire({
			.document = document,
			.options = options,
			.messageSendingFrom = options.scheduled
				? Ui::MessageSendingAnimationFrom()
				: messageSentAnimationInfo(section, index, document),
		});
	};
	SendMenu::FillSendMenu(
		menu,
		type,
		SendMenu::DefaultSilentCallback(send),
		SendMenu::DefaultScheduleCallback(this, type, send));

	const auto window = _controller;
	const auto toggleFavedSticker = [=] {
		Api::ToggleFavedSticker(
			window,
			document,
			Data::FileOriginStickerSet(Data::Stickers::FavedSetId, 0));
	};
	const auto isFaved = document->owner().stickers().isFaved(document);
	menu->addAction(
		(isFaved
			? tr::lng_faved_stickers_remove
			: tr::lng_faved_stickers_add)(tr::now),
		toggleFavedSticker,
		isFaved ? &st::menuIconUnfave : &st::menuIconFave);

	menu->addAction(tr::lng_context_pack_info(tr::now), [=] {
		showStickerSetBox(document);
	}, &st::menuIconStickers);

	if (const auto id = set.id; id == Data::Stickers::RecentSetId) {
		menu->addAction(tr::lng_recent_stickers_remove(tr::now), [=] {
			Api::ToggleRecentSticker(
				document,
				Data::FileOriginStickerSet(id, 0),
				false);
		}, &st::menuIconDelete);
	}
	return menu;
}

Ui::MessageSendingAnimationFrom StickersListWidget::messageSentAnimationInfo(
		int section,
		int index,
		not_null<DocumentData*> document) {
	const auto rect = stickerRect(section, index);
	const auto size = ComputeStickerSize(document, boundingBoxSize());
	const auto innerPos = QPoint(
		(rect.width() - size.width()) / 2,
		(rect.height() - size.height()) / 2);

	return {
		.type = Ui::MessageSendingAnimationFrom::Type::Sticker,
		.localId = session().data().nextLocalMessageId(),
		.globalStartGeometry = mapToGlobal(
			QRect(rect.topLeft() + innerPos, size)),
	};
}

void StickersListWidget::mouseReleaseEvent(QMouseEvent *e) {
	_previewTimer.cancel();

	auto pressed = _pressed;
	setPressed(v::null);
	if (pressed != _selected) {
		repaintItems();
	}

	auto activated = ClickHandler::unpressed();
	if (_previewShown) {
		_previewShown = false;
		return;
	}

	_lastMousePosition = e->globalPos();
	updateSelected();

	auto &sets = shownSets();
	if (!v::is_null(pressed) && pressed == _selected) {
		if (auto sticker = std::get_if<OverSticker>(&pressed)) {
			Assert(sticker->section >= 0 && sticker->section < sets.size());
			auto &set = sets[sticker->section];
			Assert(sticker->index >= 0 && sticker->index < set.stickers.size());
			if (stickerHasDeleteButton(set, sticker->index) && sticker->overDelete) {
				if (set.id == Data::Stickers::RecentSetId) {
					removeRecentSticker(sticker->section, sticker->index);
				} else if (set.id == Data::Stickers::FavedSetId) {
					removeFavedSticker(sticker->section, sticker->index);
				} else {
					Unexpected("Single sticker delete click.");
				}
				return;
			}
			const auto document = set.stickers[sticker->index].document;
			if (e->modifiers() & Qt::ControlModifier) {
				showStickerSetBox(document);
			} else {
				_chosen.fire({
					.document = document,
					.messageSendingFrom = messageSentAnimationInfo(
						sticker->section,
						sticker->index,
						document),
				});
			}
		} else if (auto set = std::get_if<OverSet>(&pressed)) {
			Assert(set->section >= 0 && set->section < sets.size());
			displaySet(sets[set->section].id);
		} else if (auto button = std::get_if<OverButton>(&pressed)) {
			Assert(button->section >= 0 && button->section < sets.size());
			if (sets[button->section].externalLayout) {
				_localSetsManager->install(sets[button->section].id);
			} else {
				removeSet(sets[button->section].id);
			}
		} else if (std::get_if<OverGroupAdd>(&pressed)) {
			_controller->show(Box<StickersBox>(_controller, _megagroupSet));
		}
	}
}

void StickersListWidget::removeRecentSticker(int section, int index) {
	if ((_section != Section::Stickers)
		|| (section >= int(_mySets.size()))
		|| (_mySets[section].id != Data::Stickers::RecentSetId)) {
		return;
	}

	clearSelection();
	bool refresh = false;
	const auto &sticker = _mySets[section].stickers[index];
	const auto document = sticker.document;
	auto &recent = session().data().stickers().getRecentPack();
	for (int32 i = 0, l = recent.size(); i < l; ++i) {
		if (recent.at(i).first == document) {
			recent.removeAt(i);
			session().saveSettings();
			refresh = true;
			break;
		}
	}
	auto &sets = session().data().stickers().setsRef();
	auto it = sets.find(Data::Stickers::CustomSetId);
	if (it != sets.cend()) {
		const auto set = it->second.get();
		for (int i = 0, l = set->stickers.size(); i < l; ++i) {
			if (set->stickers.at(i) == document) {
				set->stickers.removeAt(i);
				if (set->stickers.isEmpty()) {
					sets.erase(it);
				}
				session().local().writeInstalledStickers();
				refresh = true;
				break;
			}
		}
	}
	if (refresh) {
		refreshRecentStickers();
		updateSelected();
		repaintItems();
	}
}

void StickersListWidget::removeFavedSticker(int section, int index) {
	if ((_section != Section::Stickers)
		|| (section >= int(_mySets.size()))
		|| (_mySets[section].id != Data::Stickers::FavedSetId)) {
		return;
	}

	clearSelection();
	const auto &sticker = _mySets[section].stickers[index];
	const auto document = sticker.document;
	session().data().stickers().setFaved(_controller, document, false);
	Api::ToggleFavedSticker(
		_controller,
		document,
		Data::FileOriginStickerSet(Data::Stickers::FavedSetId, 0),
		false);
}

void StickersListWidget::setColumnCount(int count) {
	Expects(count > 0);

	if (_columnCount != count) {
		_columnCount = count;
		refreshFooterIcons();
	}
}

void StickersListWidget::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePosition = e->globalPos();
	updateSelected();
}

void StickersListWidget::resizeEvent(QResizeEvent *e) {
	_settings->moveToLeft(
		(width() - _settings->width()) / 2,
		height() / 3);
	if (!_megagroupSetAbout.isEmpty()) {
		refreshMegagroupSetGeometry();
	}
}

void StickersListWidget::leaveEventHook(QEvent *e) {
	clearSelection();
}

void StickersListWidget::leaveToChildEvent(QEvent *e, QWidget *child) {
	clearSelection();
}

void StickersListWidget::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePosition = QCursor::pos();
	updateSelected();
}

void StickersListWidget::clearSelection() {
	setPressed(v::null);
	setSelected(v::null);
	repaintItems();
}

TabbedSelector::InnerFooter *StickersListWidget::getFooter() const {
	return _footer;
}

void StickersListWidget::processHideFinished() {
	_choosingUpdated.fire(TabbedSelector::Action::Cancel);
	clearSelection();
	clearHeavyData();
	if (_footer) {
		_footer->clearHeavyData();
	}
}

void StickersListWidget::processPanelHideFinished() {
	if (_localSetsManager->clearInstalledLocally()) {
		refreshStickers();
	}
	clearHeavyData();
	if (_footer) {
		_footer->clearHeavyData();
	}
	// Preserve panel state through visibility toggles.
	//// Reset to the recent stickers section.
	//if (_section == Section::Featured && (!_footer || !_footer->hasOnlyFeaturedSets())) {
	//	setSection(Section::Stickers);
	//	validateSelectedIcon(ValidateIconAnimations::None);
	//}
}

void StickersListWidget::setSection(Section section) {
	if (_section == section) {
		return;
	}
	clearHeavyData();
	_section = section;
}

void StickersListWidget::clearHeavyData() {
	for (auto &set : shownSets()) {
		clearHeavyIn(set, false);
	}
}

void StickersListWidget::refreshStickers() {
	clearSelection();

	refreshMySets();
	refreshFeaturedSets();
	refreshSearchSets();

	resizeToWidth(width());

	if (_footer) {
		refreshFooterIcons();
	}
	refreshSettingsVisibility();

	_lastMousePosition = QCursor::pos();
	updateSelected();
	repaintItems();

	visibleTopBottomUpdated(getVisibleTop(), getVisibleBottom());
}

void StickersListWidget::refreshMySets() {
	auto wasSets = base::take(_mySets);
	_favedStickersMap.clear();
	_mySets.reserve(defaultSetsOrder().size() + 3);

	refreshPremiumStickers();
	refreshFavedStickers();
	refreshRecentStickers(false);
	refreshMegagroupStickers(GroupStickersPlace::Visible);

	const auto i = ranges::find(
		_mySets,
		Data::Stickers::PremiumSetId,
		&Set::id);
	_premiumsIndex = (i != end(_mySets)) ? int(i - begin(_mySets)) : -1;

	for (const auto setId : defaultSetsOrder()) {
		const auto externalLayout = false;
		appendSet(_mySets, setId, externalLayout, AppendSkip::Archived);
	}
	if (_premiumsIndex >= 0) {
		appendPremiumCloudSet();
	}

	if (_premiumsIndex >= 0 && _mySets[_premiumsIndex].stickers.empty()) {
		_mySets.erase(_mySets.begin() + _premiumsIndex);
		_premiumsIndex = -1;
	}

	refreshMegagroupStickers(GroupStickersPlace::Hidden);

	takeHeavyData(_mySets, wasSets);
}

void StickersListWidget::appendPremiumCloudSet() {
	Expects(_premiumsIndex >= 0 && _premiumsIndex < _mySets.size());

	auto &set = _mySets[_premiumsIndex];
	for (const auto &document : session().api().premium().cloudSet()) {
		set.stickers.push_back(Sticker{ document });
		++set.count;
	}
}

void StickersListWidget::refreshFeaturedSets() {
	auto wasFeaturedSetsCount = base::take(_featuredSetsCount);
	auto wereOfficial = base::take(_officialSets);
	_officialSets.reserve(
		session().data().stickers().featuredSetsOrder().size()
		+ wereOfficial.size()
		- wasFeaturedSetsCount);
	for (const auto setId : session().data().stickers().featuredSetsOrder()) {
		const auto externalLayout = true;
		appendSet(_officialSets, setId, externalLayout, AppendSkip::Installed);
	}
	_featuredSetsCount = _officialSets.size();
	if (wereOfficial.size() > wasFeaturedSetsCount) {
		const auto &sets = session().data().stickers().sets();
		const auto from = begin(wereOfficial) + wasFeaturedSetsCount;
		const auto till = end(wereOfficial);
		for (auto i = from; i != till; ++i) {
			auto &set = *i;
			auto it = sets.find(set.id);
			if (it == sets.cend()
				|| ((it->second->flags & SetFlag::Installed)
					&& !(it->second->flags & SetFlag::Archived)
					&& !_localSetsManager->isInstalledLocally(set.id))) {
				continue;
			}
			set.flags = it->second->flags;
			_officialSets.push_back(std::move(set));
		}
	}
}

void StickersListWidget::refreshSearchSets() {
	refreshSearchIndex();

	const auto &sets = session().data().stickers().sets();
	const auto skipPremium = !session().premiumPossible();
	for (auto &entry : _searchSets) {
		if (const auto it = sets.find(entry.id); it != sets.end()) {
			const auto set = it->second.get();
			entry.flags = set->flags;
			auto elements = PrepareStickers(set->stickers, skipPremium);
			if (!elements.empty()) {
				entry.lottiePlayer = nullptr;
				entry.stickers = std::move(elements);
			}
			if (!SetInMyList(entry.flags)) {
				_localSetsManager->removeInstalledLocally(entry.id);
				entry.externalLayout = true;
			}
		}
	}
}

void StickersListWidget::refreshSearchIndex() {
	_searchIndex.clear();
	for (const auto &set : _mySets) {
		if (set.flags & SetFlag::Special) {
			continue;
		}
		const auto string = set.title + ' ' + set.shortName;
		const auto list = TextUtilities::PrepareSearchWords(string);
		_searchIndex.emplace_back(set.id, list);
	}
}

void StickersListWidget::refreshSettingsVisibility() {
	const auto visible = (_section == Section::Stickers)
		&& _mySets.empty()
		&& !_isMasks;
	_settings->setVisible(visible);
}

void StickersListWidget::refreshFooterIcons() {
	refreshIcons(ValidateIconAnimations::None);
	if (_footer->hasOnlyFeaturedSets() && _section != Section::Featured) {
		showStickerSet(Data::Stickers::FeaturedSetId);
	}
}

void StickersListWidget::preloadImages() {
	if (_footer) {
		_footer->preloadImages();
	}
}

uint64 StickersListWidget::currentSet(int yOffset) const {
	if (_section == Section::Featured) {
		return Data::Stickers::FeaturedSetId;
	}
	const auto &sets = shownSets();
	return sets.empty()
		? Data::Stickers::RecentSetId
		: sets[sectionInfoByOffset(yOffset).section].id;
}

bool StickersListWidget::appendSet(
		std::vector<Set> &to,
		uint64 setId,
		bool externalLayout,
		AppendSkip skip) {
	const auto &sets = session().data().stickers().sets();
	auto it = sets.find(setId);
	if (it == sets.cend()
		|| (!externalLayout && it->second->stickers.isEmpty())) {
		return false;
	}
	const auto set = it->second.get();
	if ((skip == AppendSkip::Archived)
		&& (set->flags & SetFlag::Archived)) {
		return false;
	}
	if ((skip == AppendSkip::Installed)
		&& (set->flags & SetFlag::Installed)
		&& !(set->flags & SetFlag::Archived)) {
		if (!_localSetsManager->isInstalledLocally(setId)) {
			return false;
		}
	}
	const auto skipPremium = !session().premiumPossible();
	auto elements = PrepareStickers(
		((set->stickers.empty() && externalLayout)
			? set->covers
			: set->stickers),
		skipPremium);
	if (elements.empty()) {
		return false;
	}
	to.emplace_back(
		set->id,
		set,
		set->flags,
		set->title,
		set->shortName,
		set->count,
		externalLayout,
		std::move(elements));
	to.back().thumbnailDocument = set->lookupThumbnailDocument();
	if (!externalLayout && _premiumsIndex >= 0 && session().premium()) {
		for (const auto &sticker : to.back().stickers) {
			const auto document = sticker.document;
			if (document->isPremiumSticker()) {
				to[_premiumsIndex].stickers.push_back(Sticker{ document });
				++to[_premiumsIndex].count;
			}
		}
	}
	return true;
}

void StickersListWidget::refreshRecent() {
	if (_section == Section::Stickers) {
		refreshRecentStickers();
	}
	if (_footer && _footer->hasOnlyFeaturedSets() && _section != Section::Featured) {
		showStickerSet(Data::Stickers::FeaturedSetId);
	}
}

auto StickersListWidget::collectRecentStickers() -> std::vector<Sticker> {
	_custom.clear();
	auto result = std::vector<Sticker>();

	const auto &sets = session().data().stickers().sets();
	const auto &recent = _isMasks
		? RecentStickerPack()
		: session().data().stickers().getRecentPack();
	const auto customIt = _isMasks
		? sets.cend()
		: sets.find(Data::Stickers::CustomSetId);
	const auto cloudIt = sets.find(_isMasks
		? Data::Stickers::CloudRecentAttachedSetId
		: Data::Stickers::CloudRecentSetId);
	const auto customCount = (customIt != sets.cend())
		? customIt->second->stickers.size()
		: 0;
	const auto cloudCount = (cloudIt != sets.cend())
		? cloudIt->second->stickers.size()
		: 0;
	result.reserve(cloudCount + recent.size() + customCount);
	_custom.reserve(cloudCount + recent.size() + customCount);

	auto add = [&](not_null<DocumentData*> document, bool custom) {
		if (result.size() >= kRecentDisplayLimit) {
			return;
		}
		const auto i = ranges::find(result, document, &Sticker::document);
		if (i != end(result)) {
			const auto index = (i - begin(result));
			if (index >= cloudCount && custom) {
				// Mark stickers from local recent as custom.
				_custom[index] = true;
			}
		} else if (!_favedStickersMap.contains(document)) {
			result.push_back(Sticker{
				document
			});
			_custom.push_back(custom);
		}
	};

	if (cloudCount > 0) {
		for (const auto document : std::as_const(cloudIt->second->stickers)) {
			add(document, false);
		}
	}
	for (const auto &recentSticker : recent) {
		add(recentSticker.first, false);
	}
	if (customCount > 0) {
		for (const auto document : std::as_const(customIt->second->stickers)) {
			add(document, true);
		}
	}
	return result;
}

void StickersListWidget::refreshRecentStickers(bool performResize) {
	clearSelection();

	auto recentPack = collectRecentStickers();
	auto recentIt = std::find_if(_mySets.begin(), _mySets.end(), [](auto &set) {
		return set.id == Data::Stickers::RecentSetId;
	});
	if (!recentPack.empty()) {
		const auto shortName = QString();
		const auto externalLayout = false;
		auto set = Set(
			Data::Stickers::RecentSetId,
			nullptr,
			(SetFlag::Official | SetFlag::Special),
			tr::lng_recent_stickers(tr::now),
			shortName,
			recentPack.size(),
			externalLayout,
			std::move(recentPack));
		if (recentIt == _mySets.end()) {
			const auto where = (_mySets.empty()
				|| _mySets.begin()->id != Data::Stickers::FavedSetId)
				? _mySets.begin()
				: (_mySets.begin() + 1);
			_mySets.insert(where, std::move(set));
		} else {
			std::swap(*recentIt, set);
			takeHeavyData(*recentIt, set);
		}
	} else if (recentIt != _mySets.end()) {
		_mySets.erase(recentIt);
	}

	if (performResize && (_section == Section::Stickers || _section == Section::Featured)) {
		resizeToWidth(width());
		updateSelected();
	}
}

void StickersListWidget::refreshPremiumStickers() {
	if (_isMasks || session().settings().skipPremiumStickersSet()) {
		return;
	}
	clearSelection();
	const auto externalLayout = false;
	const auto shortName = QString();
	const auto count = 0;
	_mySets.insert(_mySets.begin(), Set{
		Data::Stickers::PremiumSetId,
		nullptr,
		(SetFlag::Official | SetFlag::Special),
		tr::lng_premium_stickers(tr::now),
		shortName,
		count,
		externalLayout
	});
}

void StickersListWidget::refreshFavedStickers() {
	if (_isMasks) {
		return;
	}
	clearSelection();
	const auto &sets = session().data().stickers().sets();
	const auto it = sets.find(Data::Stickers::FavedSetId);
	if (it == sets.cend()) {
		return;
	}
	const auto skipPremium = !session().premiumPossible();
	const auto set = it->second.get();
	const auto externalLayout = false;
	const auto shortName = QString();
	auto elements = PrepareStickers(set->stickers, skipPremium);
	if (elements.empty()) {
		return;
	}
	_mySets.insert(_mySets.begin(), Set{
		Data::Stickers::FavedSetId,
		nullptr,
		(SetFlag::Official | SetFlag::Special),
		Lang::Hard::FavedSetTitle(),
		shortName,
		set->count,
		externalLayout,
		std::move(elements)
	});
	_favedStickersMap = base::flat_set<not_null<DocumentData*>> {
		set->stickers.begin(),
		set->stickers.end()
	};
}

void StickersListWidget::refreshMegagroupStickers(GroupStickersPlace place) {
	if (!_megagroupSet || _isMasks) {
		return;
	}
	auto canEdit = _megagroupSet->canEditStickers();
	auto isShownHere = [place](bool hidden) {
		return (hidden == (place == GroupStickersPlace::Hidden));
	};
	if (!_megagroupSet->mgInfo->stickerSet) {
		if (canEdit) {
			auto hidden = session().settings().isGroupStickersSectionHidden(
				_megagroupSet->id);
			if (isShownHere(hidden)) {
				const auto shortName = QString();
				const auto externalLayout = false;
				const auto count = 0;
				_mySets.emplace_back(
					Data::Stickers::MegagroupSetId,
					nullptr,
					SetFlag::Special,
					tr::lng_group_stickers(tr::now),
					shortName,
					count,
					externalLayout);
			}
		}
		return;
	}
	auto hidden = session().settings().isGroupStickersSectionHidden(_megagroupSet->id);
	auto removeHiddenForGroup = [this, &hidden] {
		if (hidden) {
			session().settings().removeGroupStickersSectionHidden(_megagroupSet->id);
			session().saveSettings();
			hidden = false;
		}
	};
	if (canEdit && hidden) {
		removeHiddenForGroup();
	}
	const auto &set = _megagroupSet->mgInfo->stickerSet;
	if (!set.id) {
		return;
	}
	const auto &sets = session().data().stickers().sets();
	const auto it = sets.find(set.id);
	if (it != sets.cend()) {
		const auto set = it->second.get();
		auto isInstalled = (set->flags & SetFlag::Installed)
			&& !(set->flags & SetFlag::Archived);
		if (isInstalled && !canEdit) {
			removeHiddenForGroup();
		} else if (isShownHere(hidden)) {
			const auto shortName = QString();
			const auto externalLayout = false;
			const auto skipPremium = !session().premiumPossible();
			auto elements = PrepareStickers(set->stickers, skipPremium);
			if (!elements.empty()) {
				_mySets.emplace_back(
					Data::Stickers::MegagroupSetId,
					set,
					SetFlag::Special,
					tr::lng_group_stickers(tr::now),
					shortName,
					set->count,
					externalLayout,
					std::move(elements));
			}
		}
		return;
	} else if (!isShownHere(hidden) || _megagroupSetIdRequested == set.id) {
		return;
	}
	_megagroupSetIdRequested = set.id;
	_api.request(MTPmessages_GetStickerSet(
		Data::InputStickerSet(set),
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		result.match([&](const MTPDmessages_stickerSet &data) {
			if (const auto set = session().data().stickers().feedSetFull(data)) {
				refreshStickers();
				if (set->id == _megagroupSetIdRequested) {
					_megagroupSetIdRequested = 0;
				} else {
					LOG(("API Error: Got different set."));
				}
			}
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
	}).send();
}

std::vector<StickerIcon> StickersListWidget::fillIcons() {
	auto result = std::vector<StickerIcon>();
	result.reserve(_mySets.size() + 1);
	if (!_officialSets.empty() && !_isMasks) {
		result.emplace_back(Data::Stickers::FeaturedSetId);
	}

	auto i = 0;
	if (i != _mySets.size() && _mySets[i].id == Data::Stickers::FavedSetId) {
		++i;
		result.emplace_back(Data::Stickers::FavedSetId);
	}
	if (i != _mySets.size() && _mySets[i].id == Data::Stickers::RecentSetId) {
		++i;
		if (result.empty() || result.back().setId != Data::Stickers::FavedSetId) {
			result.emplace_back(Data::Stickers::RecentSetId);
		}
	}
	if (i != _mySets.size() && _mySets[i].id == Data::Stickers::PremiumSetId) {
		++i;
		result.emplace_back(Data::Stickers::PremiumSetId);
	}
	for (auto l = _mySets.size(); i != l; ++i) {
		if (_mySets[i].id == Data::Stickers::MegagroupSetId) {
			result.emplace_back(Data::Stickers::MegagroupSetId);
			result.back().megagroup = _megagroupSet;
			continue;
		}
		const auto set = _mySets[i].set;
		Assert(set != nullptr);
		const auto s = _mySets[i].thumbnailDocument;
		const auto availw = st::stickerIconWidth - 2 * st::stickerIconPadding;
		const auto availh = st().footer - 2 * st::stickerIconPadding;
		const auto size = set->hasThumbnail()
			? QSize(
				set->thumbnailLocation().width(),
				set->thumbnailLocation().height())
			: s->hasThumbnail()
			? QSize(
				s->thumbnailLocation().width(),
				s->thumbnailLocation().height())
			: QSize();
		auto thumbw = size.width(), thumbh = size.height(), pixw = 1, pixh = 1;
		if (availw * thumbh > availh * thumbw) {
			pixh = availh;
			pixw = (pixh * thumbw) / thumbh;
		} else {
			pixw = availw;
			pixh = thumbw ? ((pixw * thumbh) / thumbw) : 1;
		}
		if (pixw < 1) pixw = 1;
		if (pixh < 1) pixh = 1;
		result.emplace_back(set, s, pixw, pixh);
	}
	return result;
}

void StickersListWidget::updateSelected() {
	if (!v::is_null(_pressed) && !_previewShown) {
		return;
	}

	auto newSelected = OverState { v::null };
	auto p = mapFromGlobal(_lastMousePosition);
	if (!rect().contains(p)
		|| p.y() < getVisibleTop() || p.y() >= getVisibleBottom()
		|| !isVisible()) {
		clearSelection();
		return;
	}
	auto &sets = shownSets();
	auto sx = (rtl() ? width() - p.x() : p.x()) - stickersLeft();
	if (!shownSets().empty()) {
		auto info = sectionInfoByOffset(p.y());
		auto section = info.section;
		if (p.y() >= info.top && p.y() < info.rowsTop) {
			if (hasRemoveButton(section) && myrtlrect(removeButtonRect(info)).contains(p.x(), p.y())) {
				newSelected = OverButton{ section };
			} else if (featuredHasAddButton(section) && myrtlrect(featuredAddRect(info)).contains(p.x(), p.y())) {
				newSelected = OverButton{ section };
			} else if (!(sets[section].flags & SetFlag::Special)) {
				newSelected = OverSet{ section };
			} else if (sets[section].id == Data::Stickers::MegagroupSetId
					&& (_megagroupSet->canEditStickers() || !sets[section].stickers.empty())) {
				newSelected = OverSet{ section };
			}
		} else if (p.y() >= info.rowsTop && p.y() < info.rowsBottom && sx >= 0) {
			auto yOffset = p.y() - info.rowsTop;
			auto &set = sets[section];
			if (set.id == Data::Stickers::MegagroupSetId && set.stickers.empty()) {
				if (_megagroupSetButtonRect.contains(stickersLeft() + sx, yOffset)) {
					newSelected = OverGroupAdd{};
				}
			} else {
				auto rowIndex = qFloor(yOffset / _singleSize.height());
				auto columnIndex = qFloor(sx / _singleSize.width());
				auto index = rowIndex * _columnCount + columnIndex;
				if (index >= 0 && index < set.stickers.size()) {
					auto overDelete = false;
					if (stickerHasDeleteButton(set, index)) {
						auto inx = sx - (columnIndex * _singleSize.width());
						auto iny = yOffset - (rowIndex * _singleSize.height());
						if (inx >= _singleSize.width() - st::stickerPanDeleteIconBg.width() && iny < st::stickerPanDeleteIconBg.height()) {
							overDelete = true;
						}
					}
					newSelected = OverSticker { section, index, overDelete };
				}
			}
		}
	}

	setSelected(newSelected);
}

bool StickersListWidget::setHasTitle(const Set &set) const {
	if (set.id == Data::Stickers::FavedSetId) {
		return false;
	} else if (set.id == Data::Stickers::RecentSetId) {
		return !_mySets.empty()
			&& (_isMasks || (_mySets[0].id == Data::Stickers::FavedSetId));
	}
	return true;
}

bool StickersListWidget::stickerHasDeleteButton(const Set &set, int index) const {
	if (set.id == Data::Stickers::RecentSetId) {
		Assert(index >= 0 && index < _custom.size());
		return _custom[index];
	}
	return (set.id == Data::Stickers::FavedSetId);
}

void StickersListWidget::setSelected(OverState newSelected) {
	if (_selected != newSelected) {
		setCursor(!v::is_null(newSelected)
			? style::cur_pointer
			: style::cur_default);

		auto &sets = shownSets();
		auto updateSelected = [&]() {
			if (auto sticker = std::get_if<OverSticker>(&_selected)) {
				rtlupdate(stickerRect(sticker->section, sticker->index));
			} else if (auto button = std::get_if<OverButton>(&_selected)) {
				if (button->section >= 0
					&& button->section < sets.size()
					&& sets[button->section].externalLayout) {
					rtlupdate(featuredAddRect(button->section));
				} else {
					rtlupdate(removeButtonRect(button->section));
				}
			} else if (std::get_if<OverGroupAdd>(&_selected)) {
				rtlupdate(megagroupSetButtonRectFinal());
			}
		};
		updateSelected();
		_selected = newSelected;
		updateSelected();

		if (_previewShown && _pressed != _selected) {
			if (const auto sticker = std::get_if<OverSticker>(&_selected)) {
				_pressed = _selected;
				Assert(sticker->section >= 0 && sticker->section < sets.size());
				const auto &set = sets[sticker->section];
				Assert(sticker->index >= 0 && sticker->index < set.stickers.size());
				const auto document = set.stickers[sticker->index].document;
				_controller->widget()->showMediaPreview(
					document->stickerSetOrigin(),
					document);
			}
		}
	}
}

void StickersListWidget::showPreview() {
	if (const auto sticker = std::get_if<OverSticker>(&_pressed)) {
		const auto &sets = shownSets();
		Assert(sticker->section >= 0 && sticker->section < sets.size());
		const auto &set = sets[sticker->section];
		Assert(sticker->index >= 0 && sticker->index < set.stickers.size());
		const auto document = set.stickers[sticker->index].document;
		_controller->widget()->showMediaPreview(
			document->stickerSetOrigin(),
			document);
		_previewShown = true;
	}
}

auto StickersListWidget::getLottieRenderer()
-> std::shared_ptr<Lottie::FrameRenderer> {
	if (auto result = _lottieRenderer.lock()) {
		return result;
	}
	auto result = Lottie::MakeFrameRenderer();
	_lottieRenderer = result;
	return result;
}

void StickersListWidget::showStickerSet(uint64 setId) {
	clearSelection();

	if (setId == Data::Stickers::FeaturedSetId) {
		if (_section != Section::Featured) {
			setSection(Section::Featured);
			refreshRecentStickers(true);
			refreshSettingsVisibility();
			refreshIcons(ValidateIconAnimations::Scroll);
			repaintItems();
		}

		scrollTo(0);
		_scrollUpdated.fire({});
		return;
	}

	auto needRefresh = (_section != Section::Stickers);
	if (needRefresh) {
		setSection(Section::Stickers);
		refreshRecentStickers(true);
		refreshSettingsVisibility();
	}

	auto y = 0;
	enumerateSections([this, setId, &y](const SectionInfo &info) {
		if (shownSets()[info.section].id == setId) {
			y = info.top;
			return false;
		}
		return true;
	});
	scrollTo(y);
	_scrollUpdated.fire({});

	if (needRefresh) {
		refreshIcons(ValidateIconAnimations::Scroll);
	}

	_lastMousePosition = QCursor::pos();

	repaintItems();
}

void StickersListWidget::refreshIcons(ValidateIconAnimations animations) {
	if (_footer) {
		_footer->refreshIcons(
			fillIcons(),
			currentSet(getVisibleTop()),
			[=] { return getLottieRenderer(); },
			animations);
	}
}

void StickersListWidget::refreshMegagroupSetGeometry() {
	auto left = megagroupSetInfoLeft();
	auto availableWidth = (width() - left);
	auto top = _megagroupSetAbout.countHeight(availableWidth) + st::stickerGroupCategoryAddMargin.top();
	_megagroupSetButtonTextWidth = st::stickerGroupCategoryAdd.font->width(_megagroupSetButtonText);
	auto buttonWidth = _megagroupSetButtonTextWidth - st::stickerGroupCategoryAdd.width;
	_megagroupSetButtonRect = QRect(left, top, buttonWidth, st::stickerGroupCategoryAdd.height);
}

void StickersListWidget::showMegagroupSet(ChannelData *megagroup) {
	Expects(!megagroup || megagroup->isMegagroup());

	if (_megagroupSet != megagroup) {
		_megagroupSet = megagroup;

		if (_megagroupSetAbout.isEmpty()) {
			_megagroupSetAbout.setText(
				st::stickerGroupCategoryAbout,
				tr::lng_group_stickers_description(tr::now));
			_megagroupSetButtonText = tr::lng_group_stickers_add(tr::now).toUpper();
			refreshMegagroupSetGeometry();
		}
		_megagroupSetButtonRipple.reset();

		refreshStickers();
	}
}

void StickersListWidget::afterShown() {
	if (_footer) {
		_footer->stealFocus();
	}
}

void StickersListWidget::beforeHiding() {
	if (_footer) {
		_footer->returnFocus();
	}
}

void StickersListWidget::displaySet(uint64 setId) {
	if (setId == Data::Stickers::MegagroupSetId) {
		if (_megagroupSet->canEditStickers()) {
			checkHideWithBox(_controller->show(
				Box<StickersBox>(_controller, _megagroupSet),
				Ui::LayerOption::KeepOther).data());
			return;
		} else if (_megagroupSet->mgInfo->stickerSet.id) {
			setId = _megagroupSet->mgInfo->stickerSet.id;
		} else {
			return;
		}
	}
	const auto &sets = session().data().stickers().sets();
	auto it = sets.find(setId);
	if (it != sets.cend()) {
		checkHideWithBox(_controller->show(
			Box<StickerSetBox>(_controller, it->second.get()),
			Ui::LayerOption::KeepOther).data());
	}
}

void StickersListWidget::removeMegagroupSet(bool locally) {
	if (locally) {
		session().settings().setGroupStickersSectionHidden(_megagroupSet->id);
		session().saveSettings();
		refreshStickers();
		return;
	}
	const auto cancelled = [](Fn<void()> &&close) {
		close();
	};
	checkHideWithBox(_controller->show(Ui::MakeConfirmBox({
		.text = tr::lng_stickers_remove_group_set(),
		.confirmed = crl::guard(this, [this, group = _megagroupSet](
				Fn<void()> &&close) {
			Expects(group->mgInfo != nullptr);

			if (group->mgInfo->stickerSet) {
				session().api().setGroupStickerSet(group, {});
			}
			close();
		}),
		.cancelled = cancelled,
	})));
}

void StickersListWidget::removeSet(uint64 setId) {
	if (setId == Data::Stickers::MegagroupSetId) {
		const auto &sets = shownSets();
		const auto i = ranges::find(sets, setId, &Set::id);
		Assert(i != end(sets));
		const auto removeLocally = i->stickers.empty()
			|| !_megagroupSet->canEditStickers();
		removeMegagroupSet(removeLocally);
	} else if (auto box = MakeConfirmRemoveSetBox(&session(), setId)) {
		checkHideWithBox(_controller->show(
			std::move(box),
			Ui::LayerOption::KeepOther));
	}
}

const Data::StickersSetsOrder &StickersListWidget::defaultSetsOrder() const {
	return _isMasks
		? session().data().stickers().maskSetsOrder()
		: session().data().stickers().setsOrder();
}

Data::StickersSetsOrder &StickersListWidget::defaultSetsOrderRef() {
	return _isMasks
		? session().data().stickers().maskSetsOrderRef()
		: session().data().stickers().setsOrderRef();
}

bool StickersListWidget::mySetsEmpty() const {
	return _mySets.empty();
}

StickersListWidget::~StickersListWidget() = default;

object_ptr<Ui::BoxContent> MakeConfirmRemoveSetBox(
		not_null<Main::Session*> session,
		uint64 setId) {
	if (setId == Data::Stickers::PremiumSetId) {
		return Ui::MakeConfirmBox({
			.text = tr::lng_stickers_remove_pack(
				tr::now,
				lt_sticker_pack,
				tr::lng_premium_stickers(tr::now)),
			.confirmed = [=](Fn<void()> &&close) {
				close();
				session->settings().setSkipPremiumStickersSet(true);
				session->saveSettingsDelayed();
				session->data().stickers().notifyUpdated(
					Data::StickersType::Stickers);
			},
			.confirmText = tr::lng_stickers_remove_pack_confirm(),
		});
	}
	const auto &sets = session->data().stickers().sets();
	const auto it = sets.find(setId);
	if (it == sets.cend()) {
		return nullptr;
	}
	const auto set = it->second.get();
	const auto text = tr::lng_stickers_remove_pack(
		tr::now,
		lt_sticker_pack,
		set->title);
	return Ui::MakeConfirmBox({
		.text = text,
		.confirmed = [=](Fn<void()> &&close) {
			close();
			const auto &sets = session->data().stickers().sets();
			const auto it = sets.find(setId);
			if (it != sets.cend()) {
				const auto set = it->second.get();
				if (set->id && set->accessHash) {
					session->api().request(MTPmessages_UninstallStickerSet(
						MTP_inputStickerSetID(
							MTP_long(set->id),
							MTP_long(set->accessHash)))
					).send();
				} else if (!set->shortName.isEmpty()) {
					session->api().request(MTPmessages_UninstallStickerSet(
						MTP_inputStickerSetShortName(
							MTP_string(set->shortName)))
					).send();
				}
				auto writeRecent = false;
				auto &recent = session->data().stickers().getRecentPack();
				for (auto i = recent.begin(); i != recent.cend();) {
					if (set->stickers.indexOf(i->first) >= 0) {
						i = recent.erase(i);
						writeRecent = true;
					} else {
						++i;
					}
				}
				set->flags &= ~SetFlag::Installed;
				set->installDate = TimeId(0);
				auto &orderRef = (set->type() == Data::StickersType::Emoji)
					? session->data().stickers().emojiSetsOrderRef()
					: (set->type() == Data::StickersType::Masks)
					? session->data().stickers().maskSetsOrderRef()
					: session->data().stickers().setsOrderRef();
				const auto removeIndex = orderRef.indexOf(setId);
				if (removeIndex >= 0) {
					orderRef.removeAt(removeIndex);
				}
				if (set->type() == Data::StickersType::Emoji) {
					session->local().writeInstalledCustomEmoji();
				} else if (set->type() == Data::StickersType::Masks) {
					session->local().writeInstalledMasks();
				} else {
					session->local().writeInstalledStickers();
				}
				if (writeRecent) {
					session->saveSettings();
				}
				session->data().stickers().notifyUpdated(set->type());
			}
		},
		.confirmText = tr::lng_stickers_remove_pack_confirm(),
	});
}

} // namespace ChatHelpers
