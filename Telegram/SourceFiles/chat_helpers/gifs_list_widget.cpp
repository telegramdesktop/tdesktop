/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/gifs_list_widget.h"

#include "api/api_toggling_media.h" // Api::ToggleSavedGif
#include "base/const_string.h"
#include "base/invoke_queued.h"
#include "base/qt/qt_key_modifiers.h"
#include "chat_helpers/stickers_list_footer.h"
#include "data/data_photo.h"
#include "data/data_document.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_file_origin.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "data/stickers/data_stickers.h"
#include "menu/menu_send.h" // SendMenu::FillSendMenu
#include "mtproto/mtproto_config.h"
#include "core/click_handler_types.h"
#include "ui/controls/tabbed_search.h"
#include "ui/screen_reader_mode.h"
#include "ui/ui_utility.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/effects/ripple_animation.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "boxes/stickers_box.h"
#include "inline_bots/inline_bot_result.h"
#include "storage/localstorage.h"
#include "lang/lang_keys.h"
#include "layout/layout_position.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "history/view/history_view_cursor_state.h"
#include "storage/storage_account.h" // Account::writeSavedGifs
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

#include <QtWidgets/QApplication>

namespace ChatHelpers {
namespace {

constexpr auto kSearchRequestDelay = 400;
constexpr auto kMinRepaintDelay = crl::time(33);
constexpr auto kMinAfterScrollDelay = crl::time(33);

[[nodiscard]] QString JoinQuery(const std::vector<QString> &query) {
	return ranges::accumulate(query, QString(), [](QString a, QString b) {
		return a.isEmpty() ? b : (a + ' ' + b);
	});
}

} // namespace

void AddGifAction(
		Fn<void(QString, Fn<void()> &&, const style::icon*)> callback,
		std::shared_ptr<Show> show,
		not_null<DocumentData*> document,
		const style::ComposeIcons *iconsOverride) {
	if (!document->isGifv()) {
		return;
	}
	auto &data = document->owner();
	const auto index = data.stickers().savedGifs().indexOf(document);
	const auto saved = (index >= 0);
	const auto text = (saved
		? tr::lng_context_delete_gif
		: tr::lng_context_save_gif)(tr::now);
	const auto &icons = iconsOverride
		? *iconsOverride
		: st::defaultComposeIcons;
	callback(text, [=] {
		Api::ToggleSavedGif(
			show,
			document,
			Data::FileOriginSavedGifs(),
			!saved);

		const auto &data = document->owner();
		if (saved) {
			data.stickers().savedGifsRef().remove(index);
			document->session().local().writeSavedGifs();
		}
		data.stickers().notifySavedGifsUpdated();
	}, saved ? &icons.menuGifRemove : &icons.menuGifAdd);
}

GifsListWidget::GifsListWidget(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	PauseReason level)
: GifsListWidget(parent, {
	.show = controller->uiShow(),
	.paused = Window::PausedIn(controller, level),
}) {
}

GifsListWidget::GifsListWidget(
	QWidget *parent,
	GifsListDescriptor &&descriptor)
: Inner(
	parent,
	descriptor.st ? *descriptor.st : st::defaultEmojiPan,
	descriptor.show,
	descriptor.paused)
, _show(std::move(descriptor.show))
, _api(&session().mtp())
, _section(Section::Gifs)
, _updateInlineItems([=] { updateInlineItems(); })
, _mosaic(st::emojiPanWidth - st::inlineResultsLeft)
, _previewTimer([=] { showPreview(); }) {
	setMouseTracking(true);
	setAccessibleName(tr::lng_switch_gifs(tr::now));
	setAttribute(Qt::WA_OpaquePaintEvent);

	setupSearch();

	Ui::ScreenReaderModeActiveValue(
	) | rpl::filter([](bool active) {
		return !active;
	}) | rpl::on_next([=] {
		// The reader is gone, the keys are ordinary again: the focus the
		// list took for it goes back where it came from, the keyboard
		// selection is over, and so is the wait for results to land on.
		_pendingResultsFocus = std::nullopt;
		if (hasFocus()) {
			if (_focusReturn) {
				returnFocus();
			} else {
				clearFocus();
			}
		}
		_keyboardSelection = false;
		_keyboardItem = {};
	}, lifetime());

	_inlineRequestTimer.setSingleShot(true);
	connect(
		&_inlineRequestTimer,
		&QTimer::timeout,
		this,
		[=] { sendInlineRequest(); });

	session().data().stickers().savedGifsUpdated(
	) | rpl::on_next([=] {
		refreshSavedGifs();
	}, lifetime());

	session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		updateInlineItems();
	}, lifetime());

	_show->pauseChanged(
	) | rpl::on_next([=] {
		if (!paused()) {
			updateInlineItems();
		}
	}, lifetime());

	sizeValue(
	) | rpl::on_next([=](const QSize &s) {
		_mosaic.setFullWidth(s.width());
	}, lifetime());

	_mosaic.setPadding(st::gifsPadding
		+ QMargins(-st::emojiPanRadius, _search->height(), 0, 0));
	_mosaic.setRightSkip(st::inlineResultsSkip);
}

rpl::producer<FileChosen> GifsListWidget::fileChosen() const {
	return _fileChosen.events();
}

rpl::producer<PhotoChosen> GifsListWidget::photoChosen() const {
	return _photoChosen.events();
}

auto GifsListWidget::inlineResultChosen() const
-> rpl::producer<InlineChosen> {
	return _inlineResultChosen.events();
}

object_ptr<TabbedSelector::InnerFooter> GifsListWidget::createFooter() {
	Expects(_footer == nullptr);

	using FooterDescriptor = StickersListFooter::Descriptor;
	auto result = object_ptr<StickersListFooter>(FooterDescriptor{
		.session = &session(),
		.paused = pausedMethod(),
		.parent = this,
		.st = &st(),
		.features = { .stickersSettings = false },
	});
	_footer = result;
	_chosenSetId = Data::Stickers::RecentSetId;

	GifSectionsValue(
		&session()
	) | rpl::on_next([=](std::vector<GifSection> &&list) {
		_sections = std::move(list);
		refreshIcons();
	}, _footer->lifetime());

	_footer->setChosen(
	) | rpl::on_next([=](uint64 setId) {
		const auto keyboard = _footer->hasFocus();
		if (_search) {
			_search->cancel();
		}
		_chosenSetId = setId;
		refreshIcons();
		const auto i = ranges::find(_sections, setId, [](GifSection value) {
			return value.document->id;
		});
		const auto query = (i != end(_sections))
			? i->emoji->text()
			: QString();
		if (keyboard) {
			// Chosen from the keyboard: on to the first of its GIFs once
			// they are shown, see servePendingResultsFocus().
			_pendingResultsFocus = query;
		}
		searchForGifs(query);
	}, _footer->lifetime());

	return result;
}

void GifsListWidget::refreshIcons() {
	if (_footer) {
		_footer->refreshIcons(
			fillIcons(),
			_chosenSetId,
			nullptr,
			ValidateIconAnimations::None);
	}
}

std::vector<StickerIcon> GifsListWidget::fillIcons() {
	auto result = std::vector<StickerIcon>();
	result.reserve(_sections.size() + 1);
	result.emplace_back(Data::Stickers::RecentSetId);
	const auto side = StickersListFooter::IconFrameSize();
	for (const auto &section : _sections) {
		const auto s = section.document;
		const auto id = s->id;
		const auto size = s->hasThumbnail()
			? QSize(
				s->thumbnailLocation().width(),
				s->thumbnailLocation().height())
			: QSize();
		const auto pix = size.scaled(side, side, Qt::KeepAspectRatio);
		const auto owner = &s->owner();
		const auto already = _fakeSets.find(id);
		const auto set = (already != end(_fakeSets))
			? already
			: _fakeSets.emplace(
				id,
				std::make_unique<Data::StickersSet>(
					owner,
					id,
					0,
					0,
					QString(),
					QString(),
					0,
					Data::StickersSetFlag::Special,
					0)).first;
		result.emplace_back(set->second.get(), s, pix.width(), pix.height());
	}
	return result;
}

void GifsListWidget::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	const auto top = getVisibleTop();
	Inner::visibleTopBottomUpdated(visibleTop, visibleBottom);
	if (top != getVisibleTop()) {
		_lastScrolledAt = crl::now();
		update();
	}
	checkLoadMore();
}

void GifsListWidget::checkLoadMore() {
	auto visibleHeight = (getVisibleBottom() - getVisibleTop());
	if (getVisibleBottom() + visibleHeight > height()) {
		sendInlineRequest();
	}
}

int GifsListWidget::countDesiredHeight(int newWidth) {
	return _mosaic.countDesiredHeight(newWidth);
}

GifsListWidget::~GifsListWidget() {
	clearInlineRows(true);
	deleteUnusedGifLayouts();
	deleteUnusedInlineLayouts();
}

void GifsListWidget::cancelGifsSearch() {
	_search->setLoading(false);
	if (_inlineRequestId) {
		_api.request(_inlineRequestId).cancel();
		_inlineRequestId = 0;
	}
	_inlineRequestTimer.stop();
	_inlineQuery = _inlineNextQuery = _inlineNextOffset = QString();
	_inlineCache.clear();
	refreshInlineRows(nullptr, true);
}

void GifsListWidget::inlineResultsDone(const MTPmessages_BotResults &result) {
	_search->setLoading(false);
	_inlineRequestId = 0;

	auto it = _inlineCache.find(_inlineQuery);
	auto adding = (it != _inlineCache.cend());
	if (result.type() == mtpc_messages_botResults) {
		auto &d = result.c_messages_botResults();
		session().data().processUsers(d.vusers());

		auto &v = d.vresults().v;
		auto queryId = d.vquery_id().v;

		if (it == _inlineCache.cend()) {
			it = _inlineCache.emplace(
				_inlineQuery,
				std::make_unique<InlineCacheEntry>()).first;
		}
		const auto entry = it->second.get();
		entry->nextOffset = qs(d.vnext_offset().value_or_empty());
		if (const auto count = v.size()) {
			entry->results.reserve(entry->results.size() + count);
		}
		auto added = 0;
		for (const auto &res : v) {
			auto result = InlineBots::Result::Create(
				&session(),
				queryId,
				res);
			if (result) {
				++added;
				entry->results.push_back(std::move(result));
			}
		}

		if (!added) {
			entry->nextOffset = QString();
		}
	} else if (adding) {
		it->second->nextOffset = QString();
	}

	if (!showInlineRows(!adding)) {
		it->second->nextOffset = QString();
	}
	checkLoadMore();
}

void GifsListWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto clip = e->rect();
	p.fillRect(clip, st().bg);

	paintInlineItems(p, clip);
}

void GifsListWidget::paintInlineItems(Painter &p, QRect clip) {
	if (_mosaic.empty()) {
		p.setFont(st::normalFont);
		p.setPen(st::noContactsColor);
		auto text = _inlineQuery.isEmpty()
			? tr::lng_gifs_no_saved(tr::now)
			: tr::lng_inline_bot_no_results(tr::now);
		p.drawText(QRect(0, 0, width(), (height() / 3) * 2 + st::normalFont->height), text, style::al_center);
		return;
	}
	const auto gifPaused = paused();
	using namespace InlineBots::Layout;
	PaintContext context(crl::now(), false, gifPaused, false);

	auto paintItem = [&](not_null<const ItemBase*> item, QPoint point) {
		p.translate(point.x(), point.y());
		item->paint(
			p,
			clip.translated(-point),
			&context);
		p.translate(-point.x(), -point.y());
	};
	_mosaic.paint(std::move(paintItem), clip);
}

void GifsListWidget::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePos = e->globalPos();
	updateSelected();

	_pressed = _selected;
	ClickHandler::pressed();
	_previewTimer.callOnce(QApplication::startDragTime());
}

base::unique_qptr<Ui::PopupMenu> GifsListWidget::fillContextMenu(
		const SendMenu::Details &details) {
	if (_selected < 0 || _pressed >= 0) {
		return nullptr;
	}

	auto menu = base::make_unique_q<Ui::PopupMenu>(this, st().menu);
	const auto selected = _selected;
	const auto send = crl::guard(this, [=](Api::SendOptions options) {
		selectInlineResult(selected, options, true);
	});
	const auto item = _mosaic.maybeItemAt(_selected);
	const auto isInlineResult = !item->getPhoto()
		&& !item->getDocument()
		&& item->getResult();
	const auto icons = &st().icons;
	auto copyDetails = details;
	if (isInlineResult) {
		// inline results don't have effects
		copyDetails.effectAllowed = false;
	}

	// In case we're adding items after FillSendMenu we have
	// to pass nullptr for showForEffect and attach selector later.
	// Otherwise added items widths won't be respected in menu geometry.
	SendMenu::FillSendMenu(
		menu,
		nullptr, // showForMenu
		copyDetails,
		SendMenu::DefaultCallback(_show, send),
		icons);

	if (!isInlineResult && _inlineQueryPeer) {
		menu->addAction(
			tr::lng_send_gif_with_caption(tr::now),
			crl::guard(this, [=] {
				selectInlineResult(selected, {}, true, true);
			}),
			&icons->menuGifCaption);
	}

	if (const auto item = _mosaic.maybeItemAt(_selected)) {
		const auto document = item->getDocument()
			? item->getDocument() // Saved GIF.
			: item->getPreviewDocument(); // Searched GIF.
		if (document) {
			auto callback = [&](
					const QString &text,
					Fn<void()> &&done,
					const style::icon *icon) {
				menu->addAction(text, std::move(done), icon);
			};
			AddGifAction(std::move(callback), _show, document, icons);
		}
	}

	SendMenu::AttachSendMenuEffect(
		menu,
		_show,
		copyDetails,
		SendMenu::DefaultCallback(_show, send));

	return menu;
}

void GifsListWidget::mouseReleaseEvent(QMouseEvent *e) {
	_previewTimer.cancel();

	auto pressed = std::exchange(_pressed, -1);
	auto activated = ClickHandler::unpressed();

	if (_previewShown) {
		_previewShown = false;
		return;
	}

	_lastMousePos = e->globalPos();
	updateSelected();

	if (_selected < 0 || _selected != pressed || !activated) {
		return;
	}

	if (dynamic_cast<InlineBots::Layout::SendClickHandler*>(activated.get())) {
		selectInlineResult(_selected, {});
	} else {
		ActivateClickHandler(window(), activated, {
			e->button(),
			QVariant::fromValue(ClickHandlerContext{
				.show = _show,
			})
		});
	}
}

bool GifsListWidget::selectInlineResult(
		int index,
		Api::SendOptions options,
		bool forceSend,
		bool needsCaption) {
	const auto item = _mosaic.maybeItemAt(index);
	if (!item) {
		return false;
	}

	const auto messageSendingFrom = [&] {
		if (options.scheduled) {
			return Ui::MessageSendingAnimationFrom();
		}
		const auto rect = item->innerContentRect().translated(
			_mosaic.findRect(index).topLeft());
		return Ui::MessageSendingAnimationFrom{
			.type = Ui::MessageSendingAnimationFrom::Type::Gif,
			.localId = session().data().nextLocalMessageId(),
			.globalStartGeometry = mapToGlobal(rect),
			.crop = true,
		};
	};

	forceSend |= base::IsCtrlPressed();
	if (const auto photo = item->getPhoto()) {
		using Data::PhotoSize;
		const auto media = photo->activeMediaView();
		if (forceSend
			|| (media && media->image(PhotoSize::Thumbnail))
			|| (media && media->image(PhotoSize::Large))) {
			_photoChosen.fire({
				.photo = photo,
				.options = options
			});
			return true;
		} else if (!photo->loading(PhotoSize::Thumbnail)) {
			photo->load(PhotoSize::Thumbnail, Data::FileOrigin());
		}
	} else if (const auto document = item->getDocument()) {
		const auto media = document->activeMediaView();
		const auto preview = Data::VideoPreviewState(media.get());
		if (forceSend || (media && preview.loaded())) {
			_fileChosen.fire({
				.document = document,
				.options = options,
				.messageSendingFrom = messageSendingFrom(),
				.needsCaption = needsCaption,
			});
			return true;
		} else if (!preview.usingThumbnail()) {
			if (preview.loading()) {
				document->cancel();
			} else {
				document->save(
					document->stickerOrGifOrigin(),
					QString());
			}
		}
	} else if (const auto inlineResult = item->getResult()) {
		if (inlineResult->onChoose(item)) {
			options.hideViaBot = true;
			_inlineResultChosen.fire({
				.result = inlineResult,
				.bot = _searchBot,
				.options = options,
				.messageSendingFrom = messageSendingFrom(),
			});
			return true;
		}
	}
	return false;
}

void GifsListWidget::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	_keyboardSelection = false;
	_keyboardItem = {};
	updateSelected();
}

void GifsListWidget::leaveEventHook(QEvent *e) {
	// A selection made from the keyboard stays where the mouse is not.
	if (!_keyboardSelection) {
		clearSelection();
	}
}

void GifsListWidget::leaveToChildEvent(QEvent *e, QWidget *child) {
	if (!_keyboardSelection) {
		clearSelection();
	}
}

void GifsListWidget::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void GifsListWidget::clearSelection() {
	_keyboardSelection = false;
	_keyboardItem = {};
	if (_selected >= 0) {
		ClickHandler::clearActive(_mosaic.itemAt(_selected));
		setCursor(style::cur_default);
	}
	_selected = _pressed = -1;
	repaintItems();
}

TabbedSelector::InnerFooter *GifsListWidget::getFooter() const {
	return _footer;
}

void GifsListWidget::processHideFinished() {
	clearSelection();
	clearHeavyData();
	if (_footer) {
		_footer->clearHeavyData();
	}
}

void GifsListWidget::processPanelHideFinished() {
	clearHeavyData();
	if (_footer) {
		_footer->clearHeavyData();
	}
}

void GifsListWidget::clearHeavyData() {
	// Preserve panel state through visibility toggles.
	//clearInlineRows(false);
	for (const auto &[document, layout] : _gifLayouts) {
		layout->unloadHeavyPart();
	}
	for (const auto &[document, layout] : _inlineLayouts) {
		layout->unloadHeavyPart();
	}
}

void GifsListWidget::refreshSavedGifs() {
	if (_section == Section::Gifs) {
		clearInlineRows(false);

		const auto &saved = session().data().stickers().savedGifs();
		if (!saved.isEmpty()) {
			const auto layouts = ranges::views::all(
				saved
			) | ranges::views::transform([&](not_null<DocumentData*> gif) {
				return layoutPrepareSavedGif(gif);
			}) | ranges::views::filter([](const LayoutItem *item) {
				return item != nullptr;
			}) | ranges::to<std::vector<not_null<LayoutItem*>>>;

			_mosaic.addItems(layouts);
		}
		deleteUnusedGifLayouts();

		resizeToWidth(width());
		repaintItems();
	}

	if (isVisible()) {
		updateSelected();
	} else {
		preloadImages();
	}
}

void GifsListWidget::clearInlineRows(bool resultsDeleted) {
	if (resultsDeleted) {
		_selected = _pressed = -1;
	} else {
		clearSelection();
	}
	_mosaic.clearRows(resultsDeleted);
}

GifsListWidget::LayoutItem *GifsListWidget::layoutPrepareSavedGif(
		not_null<DocumentData*> document) {
	auto it = _gifLayouts.find(document);
	if (it == _gifLayouts.cend()) {
		if (auto layout = LayoutItem::createLayoutGif(this, document)) {
			it = _gifLayouts.emplace(document, std::move(layout)).first;
			it->second->initDimensions();
		} else {
			return nullptr;
		}
	}
	if (!it->second->maxWidth()) return nullptr;

	return it->second.get();
}

GifsListWidget::LayoutItem *GifsListWidget::layoutPrepareInlineResult(
		std::shared_ptr<InlineResult> result) {
	const auto raw = result.get();
	auto it = _inlineLayouts.find(raw);
	if (it == _inlineLayouts.cend()) {
		if (auto layout = LayoutItem::createLayout(
				this,
				std::move(result),
				_inlineWithThumb)) {
			it = _inlineLayouts.emplace(raw, std::move(layout)).first;
			it->second->initDimensions();
		} else {
			return nullptr;
		}
	}
	if (!it->second->maxWidth()) return nullptr;

	return it->second.get();
}

void GifsListWidget::deleteUnusedGifLayouts() {
	if (_mosaic.empty() || _section != Section::Gifs) { // delete all
		_gifLayouts.clear();
	} else {
		for (auto i = _gifLayouts.begin(); i != _gifLayouts.cend();) {
			if (i->second->position() < 0) {
				i = _gifLayouts.erase(i);
			} else {
				++i;
			}
		}
	}
}

void GifsListWidget::deleteUnusedInlineLayouts() {
	if (_mosaic.empty() || _section == Section::Gifs) { // delete all
		_inlineLayouts.clear();
	} else {
		for (auto i = _inlineLayouts.begin(); i != _inlineLayouts.cend();) {
			if (i->second->position() < 0) {
				i = _inlineLayouts.erase(i);
			} else {
				++i;
			}
		}
	}
}

void GifsListWidget::preloadImages() {
	_mosaic.forEach([](not_null<const LayoutItem*> item) {
		item->preload();
	});
}

void GifsListWidget::switchToSavedGifs() {
	const auto keyboard = takeKeyboardItem();
	clearInlineRows(false);
	_section = Section::Gifs;
	refreshSavedGifs();
	scrollTo(0);
	childrenChanged(keyboard);
	servePendingResultsFocus();
}

int GifsListWidget::refreshInlineRows(const InlineCacheEntry *entry, bool resultsDeleted) {
	if (!entry) {
		if (resultsDeleted) {
			clearInlineRows(true);
			deleteUnusedInlineLayouts();
		}
		switchToSavedGifs();
		return 0;
	}

	const auto keyboard = takeKeyboardItem();
	clearSelection();

	_section = Section::Inlines;
	const auto count = int(entry->results.size());
	const auto from = validateExistingInlineRows(entry->results);
	auto added = 0;
	if (count) {
		const auto resultLayouts = entry->results | ranges::views::slice(
			from,
			count
		) | ranges::views::transform([&](
				const std::shared_ptr<InlineBots::Result> &r) {
			return layoutPrepareInlineResult(r);
		}) | ranges::views::filter([](const LayoutItem *item) {
			return item != nullptr;
		}) | ranges::to<std::vector<not_null<LayoutItem*>>>;

		_mosaic.addItems(resultLayouts);
		added = resultLayouts.size();
		preloadImages();
	}

	resizeToWidth(width());
	repaintItems();

	_lastMousePos = QCursor::pos();
	updateSelected();
	childrenChanged(keyboard);
	servePendingResultsFocus();

	return added;
}

int GifsListWidget::validateExistingInlineRows(const InlineResults &results) {
	const auto until = _mosaic.validateExistingRows([&](
			not_null<const LayoutItem*> item,
			int untilIndex) {
		return item->getResult().get() != results[untilIndex].get();
	}, results.size());

	if (_mosaic.empty()) {
		_inlineWithThumb = false;
		for (int i = until; i < results.size(); ++i) {
			if (results.at(i)->hasThumbDisplay()) {
				_inlineWithThumb = true;
				break;
			}
		}
	}
	return until;
}

void GifsListWidget::inlineItemLayoutChanged(const InlineBots::Layout::ItemBase *layout) {
	if (_selected < 0 || !isVisible()) {
		return;
	}

	if (const auto item = _mosaic.maybeItemAt(_selected)) {
		if (layout == item) {
			updateSelected();
		}
	}
}

void GifsListWidget::inlineItemRepaint(
		const InlineBots::Layout::ItemBase *layout) {
	updateInlineItems();
}

bool GifsListWidget::inlineItemVisible(
		const InlineBots::Layout::ItemBase *layout) {
	auto position = layout->position();
	if (position < 0 || !isVisible()) {
		return false;
	}

	const auto &[row, column] = Layout::IndexToPosition(position);
	auto top = 0;
	for (auto i = 0; i != row; ++i) {
		top += _mosaic.rowHeightAt(i);
	}

	return (top < getVisibleBottom())
		&& (top + _mosaic.itemAt(row, column)->height() > getVisibleTop());
}

Data::FileOrigin GifsListWidget::inlineItemFileOrigin() {
	return _inlineQuery.isEmpty()
		? Data::FileOriginSavedGifs()
		: Data::FileOrigin();
}

void GifsListWidget::afterShown() {
	if (_search && !keepsFocusOnShow()) {
		_search->stealFocus();
	}
}

void GifsListWidget::beforeHiding() {
	_pendingResultsFocus = std::nullopt;
	if (_search) {
		_search->returnFocus();
	}
	returnFocus();
}

bool GifsListWidget::refreshInlineRows(int32 *added) {
	auto it = _inlineCache.find(_inlineQuery);
	const InlineCacheEntry *entry = nullptr;
	if (it != _inlineCache.cend()) {
		entry = it->second.get();
		_inlineNextOffset = it->second->nextOffset;
	}
	auto result = refreshInlineRows(entry, false);
	if (added) *added = result;
	return (entry != nullptr);
}

void GifsListWidget::setupSearch() {
	const auto session = &_show->session();
	_search = MakeSearch(this, st(), [=](std::vector<QString> &&query) {
		const auto accumulated = JoinQuery(query);
		if (_pendingResultsFocus && *_pendingResultsFocus != accumulated) {
			// Typed over: the results asked for are not coming any more.
			_pendingResultsFocus = std::nullopt;
		}
		_chosenSetId = accumulated.isEmpty()
			? Data::Stickers::RecentSetId
			: SearchEmojiSectionSetId();
		refreshIcons();
		searchForGifs(accumulated);
	}, session, TabbedSearchType::Emoji);

	// Enter or Down in the field, or a group chosen from the keyboard: the
	// first of the results takes the focus once the results of that query
	// are shown - right away when they are the ones shown already.
	_search->activations(
	) | rpl::on_next([=](std::vector<QString> &&query) {
		_pendingResultsFocus = JoinQuery(query);
		servePendingResultsFocus();
	}, lifetime());
	_search->escapes(
	) | rpl::on_next([=] {
		_pendingResultsFocus = std::nullopt;
	}, lifetime());
}

int32 GifsListWidget::showInlineRows(bool newResults) {
	auto added = 0;
	refreshInlineRows(&added);
	if (newResults) {
		scrollTo(0);
	}
	return added;
}

void GifsListWidget::searchForGifs(const QString &query) {
	if (query.isEmpty()) {
		cancelGifsSearch();
		return;
	}

	if (_inlineQuery != query) {
		_search->setLoading(false);
		if (_inlineRequestId) {
			_api.request(_inlineRequestId).cancel();
			_inlineRequestId = 0;
		}
		if (_inlineCache.find(query) != _inlineCache.cend()) {
			_inlineRequestTimer.stop();
			_inlineQuery = _inlineNextQuery = query;
			showInlineRows(true);
		} else {
			_inlineNextQuery = query;
			_inlineRequestTimer.start(kSearchRequestDelay);
		}
	}

	if (!_searchBot && !_searchBotRequestId) {
		const auto username = session().serverConfig().gifSearchUsername;
		_searchBotRequestId = _api.request(MTPcontacts_ResolveUsername(
			MTP_flags(0),
			MTP_string(username),
			MTP_string()
		)).done([=](const MTPcontacts_ResolvedPeer &result) {
			auto &data = result.data();
			session().data().processUsers(data.vusers());
			session().data().processChats(data.vchats());
			const auto peer = session().data().peerLoaded(
				peerFromMTP(data.vpeer()));
			if (const auto user = peer ? peer->asUser() : nullptr) {
				_searchBot = user;
			}
		}).send();
	}
}

void GifsListWidget::cancelled() {
	_cancelled.fire({});
}

rpl::producer<> GifsListWidget::cancelRequests() const {
	// The keyboard done with the list - a GIF sent or Escape pressed -
	// hides the panel the same way.
	return rpl::merge(_cancelled.events(), _hideRequests.events());
}

int GifsListWidget::accessibleCount() const {
	auto result = 0;
	for (auto row = 0, rows = _mosaic.rowsCount(); row != rows; ++row) {
		for (auto column = 0; _mosaic.maybeItemAt(row, column); ++column) {
			++result;
		}
	}
	return result;
}

int GifsListWidget::accessibleIndex(int mosaicIndex) const {
	if (mosaicIndex < 0 || !_mosaic.maybeItemAt(mosaicIndex)) {
		return -1;
	}
	const auto position = Layout::IndexToPosition(mosaicIndex);
	auto result = 0;
	for (auto row = 0; row != position.row; ++row) {
		for (auto column = 0; _mosaic.maybeItemAt(row, column); ++column) {
			++result;
		}
	}
	return result + position.column;
}

int GifsListWidget::mosaicIndexAt(int accessibleIndex) const {
	if (accessibleIndex < 0) {
		return -1;
	}
	for (auto row = 0, rows = _mosaic.rowsCount(); row != rows; ++row) {
		for (auto column = 0; _mosaic.maybeItemAt(row, column); ++column) {
			if (!accessibleIndex--) {
				return Layout::PositionToIndex(row, column);
			}
		}
	}
	return -1;
}

QAccessible::Role GifsListWidget::accessibilityRole() {
	return QAccessible::List;
}

Qt::FocusPolicy GifsListWidget::accessibilityFocusPolicy() {
	return Qt::TabFocus;
}

int GifsListWidget::accessibilityChildCount() const {
	return accessibleCount();
}

QAccessible::Role GifsListWidget::accessibilityChildRole() const {
	return QAccessible::ListItem;
}

QString GifsListWidget::accessibilityChildName(int index) const {
	// A GIF has no text of its own; the app calls one just that.
	return (mosaicIndexAt(index) >= 0) ? u"GIF"_q : QString();
}

QRect GifsListWidget::accessibilityChildRect(int index) const {
	const auto mosaicIndex = mosaicIndexAt(index);
	return (mosaicIndex >= 0)
		? myrtlrect(_mosaic.findRect(mosaicIndex))
		: QRect();
}

QAccessible::State GifsListWidget::accessibilityChildState(int index) const {
	auto state = QAccessible::State();
	if (Ui::ScreenReaderModeActive()) {
		state.focusable = true;
		state.selectable = true;
	}
	const auto mosaicIndex = mosaicIndexAt(index);
	if (mosaicIndex >= 0 && mosaicIndex == _selected) {
		state.active = true;
		// The item the keyboard is on is the selection of the list - or a
		// screen reader reports every item as "not selected".
		state.selected = true;
		if (hasFocus()) {
			state.focused = true;
		}
	}
	return state;
}

bool GifsListWidget::accessibilityChildSupportsActions(int index) const {
	return accessibilityChildIdentity(index) != 0;
}

quintptr GifsListWidget::accessibilityChildIdentity(int index) const {
	// The place in the mosaic names the item, within the generation of
	// the items it belongs to, see childrenChanged(). The tag bit keeps
	// the token non-zero.
	const auto mosaicIndex = mosaicIndexAt(index);
	return (mosaicIndex >= 0)
		? ((quintptr(_childrenGeneration) << 32)
			| (quintptr(mosaicIndex) << 1)
			| quintptr(1))
		: quintptr(0);
}

int GifsListWidget::accessibilityChildIndexByIdentity(
		quintptr identity) const {
	if (!identity || quint32(identity >> 32) != _childrenGeneration) {
		return -1;
	}
	return accessibleIndex(int((identity >> 1) & 0x7FFFFFFF));
}

void GifsListWidget::accessibilityChildSetFocus(quintptr identity) {
	crl::on_main(this, [=] {
		const auto mosaicIndex = mosaicIndexAt(
			accessibilityChildIndexByIdentity(identity));
		if (mosaicIndex < 0) {
			return;
		}
		keyboardSelect(mosaicIndex, hasFocus());
		// The keyboard focus is for a screen reader only; the action itself
		// is available regardless.
		if (!hasFocus() && Ui::ScreenReaderModeActive()) {
			_focusReturn = window()->focusWidget();
			setFocus();
		}
	});
}

void GifsListWidget::accessibilityChildActivate(quintptr identity) {
	crl::on_main(this, [=] {
		const auto mosaicIndex = mosaicIndexAt(
			accessibilityChildIndexByIdentity(identity));
		if (mosaicIndex < 0) {
			return;
		}
		keyboardSelect(mosaicIndex, false);
		activateKeyboardSelected();
	});
}

void GifsListWidget::keyboardSelect(int mosaicIndex, bool announce) {
	const auto item = _mosaic.maybeItemAt(mosaicIndex);
	if (!item) {
		return;
	}
	_keyboardSelection = true;
	_keyboardItem = KeyboardItem{
		.document = item->getDocument(),
		.result = item->getResult(),
		.index = accessibleIndex(mosaicIndex),
	};
	if (_selected != mosaicIndex) {
		if (const auto was = _mosaic.maybeItemAt(_selected)) {
			was->update();
		}
		_selected = mosaicIndex;
		_mosaic.itemAt(mosaicIndex)->update();
	}
	ensureItemVisible(mosaicIndex);
	if (announce) {
		const auto index = accessibleIndex(mosaicIndex);
		if (index >= 0) {
			accessibilityChildFocused(index);
		}
	}
}

void GifsListWidget::ensureItemVisible(int mosaicIndex) {
	const auto rect = _mosaic.findRect(mosaicIndex);
	const auto top = getVisibleTop();
	const auto bottom = getVisibleBottom();
	if (bottom <= top || rect.isEmpty()) {
		return;
	} else if (rect.y() < top) {
		scrollTo(rect.y());
	} else if (rect.y() + rect.height() > bottom) {
		scrollTo(rect.y() + rect.height() - (bottom - top));
	}
}

void GifsListWidget::keyboardMoveBy(int delta) {
	const auto count = accessibleCount();
	if (!count) {
		return;
	}
	const auto current = accessibleIndex(_selected);
	const auto index = (current < 0)
		? ((delta > 0) ? 0 : count - 1)
		: std::clamp(current + delta, 0, count - 1);
	keyboardSelect(mosaicIndexAt(index), true);
}

void GifsListWidget::keyboardMoveRows(int rows) {
	if (!_mosaic.maybeItemAt(_selected)) {
		keyboardMoveBy(rows > 0 ? 1 : -1);
		return;
	}
	// The same column one row up or down, or the last of a shorter row.
	const auto position = Layout::IndexToPosition(_selected);
	const auto row = std::clamp(
		position.row + rows,
		0,
		std::max(_mosaic.rowsCount() - 1, 0));
	auto column = position.column;
	while (column > 0 && !_mosaic.maybeItemAt(row, column)) {
		--column;
	}
	keyboardSelect(Layout::PositionToIndex(row, column), true);
}

void GifsListWidget::returnFocus() {
	// To the control the focus was taken from - by the list itself, or
	// by the search it went on from, see focusFromSearch().
	const auto was = base::take(_focusReturn);
	if (was && Ui::InFocusChain(this)) {
		was->setFocus();
	}
}

void GifsListWidget::focusFromSearch(int mosaicIndex) {
	// On from the search or the footer, for a screen reader: the place
	// to give the focus back to comes over from the search, which took
	// it on entry, unless the list has one of its own already.
	if (!Ui::ScreenReaderModeActive()) {
		return;
	}
	if (!_focusReturn && _search) {
		_focusReturn = _search->takeFocusReturn();
	}
	keyboardSelect(mosaicIndex, false);
	setFocus();
}

void GifsListWidget::servePendingResultsFocus() {
	// The results of the query the keyboard asked for are the ones shown:
	// the first of them takes the focus. Served with whatever came, so
	// that nothing waits for results that will not come.
	if (!_pendingResultsFocus || *_pendingResultsFocus != _inlineQuery) {
		return;
	}
	_pendingResultsFocus = std::nullopt;
	if (_mosaic.maybeItemAt(0, 0)) {
		focusFromSearch(Layout::PositionToIndex(0, 0));
	}
}

GifsListWidget::KeyboardItem GifsListWidget::takeKeyboardItem() {
	// Before a rebuild clears the selection: the item the keyboard is on,
	// to be found again among the new ones, see childrenChanged().
	return _keyboardSelection ? base::take(_keyboardItem) : KeyboardItem();
}

void GifsListWidget::childrenChanged(const KeyboardItem &keyboard) {
	// The items were rebuilt: an action queued against the old ones would
	// land on whatever took their place. The keyboard follows its item -
	// a saved GIF is known by its document, a searched one by its result
	// - or stays at its place among the new ones when the item is gone.
	++_childrenGeneration;
	if (keyboard.index < 0) {
		return;
	}
	auto found = -1;
	_mosaic.forEach([&](not_null<const LayoutItem*> item) {
		const auto matches = keyboard.document
			? (item->getDocument() == keyboard.document)
			: (item->getResult() == keyboard.result);
		if (found < 0 && matches) {
			found = item->position();
		}
	});
	auto index = (found >= 0) ? accessibleIndex(found) : -1;
	if (index < 0) {
		const auto count = accessibleCount();
		index = count ? std::clamp(keyboard.index, 0, count - 1) : -1;
	}
	if (index < 0) {
		return;
	}
	keyboardSelect(mosaicIndexAt(index), hasFocus() && index != keyboard.index);
}

void GifsListWidget::activateKeyboardSelected() {
	if (!_mosaic.maybeItemAt(_selected)) {
		return;
	}
	// Sent right away, as with Ctrl held: the keyboard can't see whether
	// the preview a click waits for has loaded. A searched GIF may still
	// not be chosen - its preview is loading - and the keyboard stays on
	// it then. Chosen, the panel is done: the focus goes back, unless
	// the host moved it already, and the panel hides, as on Escape.
	if (!selectInlineResult(_selected, {}, true)) {
		return;
	}
	returnFocus();
	_hideRequests.fire({});
}

void GifsListWidget::focusInEvent(QFocusEvent *e) {
	RpWidget::focusInEvent(e);
	// Land on the GIF last walked to, or the first one there is.
	const auto mosaicIndex = _mosaic.maybeItemAt(_selected)
		? _selected
		: mosaicIndexAt(0);
	if (mosaicIndex < 0) {
		return;
	}
	keyboardSelect(mosaicIndex, false);
	InvokeQueued(this, [=] {
		if (hasFocus() && _selected == mosaicIndex) {
			const auto index = accessibleIndex(mosaicIndex);
			if (index >= 0) {
				accessibilityChildFocused(index);
			}
		}
	});
}

void GifsListWidget::focusOutEvent(QFocusEvent *e) {
	RpWidget::focusOutEvent(e);
	_keyboardSelection = false;
	_keyboardItem = {};
}

void GifsListWidget::keyPressEvent(QKeyEvent *e) {
	// The keys walk the items for a screen reader only: without one the
	// list is not focusable, and a focus it kept from before the reader
	// was stopped must not keep the keys either.
	if (!Ui::ScreenReaderModeActive()) {
		RpWidget::keyPressEvent(e);
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Left || key == Qt::Key_Right) {
		const auto forward = (key == Qt::Key_Right) != rtl();
		keyboardMoveBy(forward ? 1 : -1);
	} else if (key == Qt::Key_Up || key == Qt::Key_Down) {
		keyboardMoveRows((key == Qt::Key_Down) ? 1 : -1);
	} else if (key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
		const auto rowHeight = std::max(
			_mosaic.rowsCount() ? _mosaic.rowHeightAt(0) : 0,
			1);
		const auto rowsOnPage = std::max(
			(getVisibleBottom() - getVisibleTop()) / rowHeight,
			1);
		keyboardMoveRows((key == Qt::Key_PageDown)
			? rowsOnPage
			: -rowsOnPage);
	} else if (key == Qt::Key_Home) {
		keyboardMoveBy(-accessibleCount());
	} else if (key == Qt::Key_End) {
		keyboardMoveBy(accessibleCount());
	} else if (!e->isAutoRepeat()
		&& (key == Qt::Key_Space
			|| key == Qt::Key_Return
			|| key == Qt::Key_Enter)) {
		activateKeyboardSelected();
	} else if (key == Qt::Key_Escape) {
		// An owner that hides on the request takes it from here; the key
		// itself goes on up, so that a box the list sits in closes on it.
		returnFocus();
		_hideRequests.fire({});
		RpWidget::keyPressEvent(e);
		return;
	} else {
		RpWidget::keyPressEvent(e);
		return;
	}
	e->accept();
}

void GifsListWidget::sendInlineRequest() {
	if (_inlineRequestId || !_inlineQueryPeer || _inlineNextQuery.isEmpty()) {
		return;
	}

	if (!_searchBot) {
		// Wait for the bot being resolved.
		_search->setLoading(true);
		_inlineRequestTimer.start(kSearchRequestDelay);
		return;
	}
	_inlineRequestTimer.stop();
	_inlineQuery = _inlineNextQuery;

	auto nextOffset = QString();
	auto it = _inlineCache.find(_inlineQuery);
	if (it != _inlineCache.cend()) {
		nextOffset = it->second->nextOffset;
		if (nextOffset.isEmpty()) {
			_search->setLoading(false);
			return;
		}
	}

	_search->setLoading(true);
	_inlineRequestId = _api.request(MTPmessages_GetInlineBotResults(
		MTP_flags(0),
		_searchBot->inputUser(),
		_inlineQueryPeer->input(),
		MTPInputGeoPoint(),
		MTP_string(_inlineQuery),
		MTP_string(nextOffset)
	)).done([this](const MTPmessages_BotResults &result) {
		inlineResultsDone(result);
	}).fail([this] {
		// show error?
		_search->setLoading(false);
		_inlineRequestId = 0;
	}).handleAllErrors().send();
}

void GifsListWidget::refreshRecent() {
	if (_section == Section::Gifs) {
		const auto keyboard = takeKeyboardItem();
		refreshSavedGifs();
		childrenChanged(keyboard);
		servePendingResultsFocus();
	}
}

void GifsListWidget::updateSelected() {
	if (_pressed >= 0 && !_previewShown) {
		return;
	} else if (_keyboardSelection) {
		// The selection is the keyboard's until the mouse moves.
		return;
	}

	const auto p = mapFromGlobal(_lastMousePos);
	const auto sx = rtl() ? (width() - p.x()) : p.x();
	const auto sy = p.y();
	const auto &[index, exact, relative] = _mosaic.findByPoint({ sx, sy });
	const auto selected = exact ? index : -1;
	const auto item = exact ? _mosaic.itemAt(selected).get() : nullptr;
	const auto link = exact ? item->getState(relative, {}).link : nullptr;

	if (_selected != selected) {
		if (const auto s = _mosaic.maybeItemAt(_selected)) {
			s->update();
		}
		_selected = selected;
		if (item) {
			item->update();
		}
		if (_previewShown && _selected >= 0 && _pressed != _selected) {
			_pressed = _selected;
			if (item) {
				if (const auto preview = item->getPreviewDocument()) {
					_show->showMediaPreview(
						Data::FileOriginSavedGifs(),
						preview);
				} else if (const auto preview = item->getPreviewPhoto()) {
					_show->showMediaPreview(Data::FileOrigin(), preview);
				}
			}
		}
	}
	if (ClickHandler::setActive(link, item)) {
		setCursor(link ? style::cur_pointer : style::cur_default);
	}
}

void GifsListWidget::showPreview() {
	if (_pressed < 0) {
		return;
	}
	if (const auto layout = _mosaic.maybeItemAt(_pressed)) {
		if (const auto previewDocument = layout->getPreviewDocument()) {
			_previewShown = _show->showMediaPreview(
				Data::FileOriginSavedGifs(),
				previewDocument);
		} else if (const auto previewPhoto = layout->getPreviewPhoto()) {
			_previewShown = _show->showMediaPreview(
				Data::FileOrigin(),
				previewPhoto);
		}
	}
}

void GifsListWidget::updateInlineItems() {
	const auto now = crl::now();

	const auto delay = std::max(
		_lastScrolledAt + kMinAfterScrollDelay - now,
		_lastUpdatedAt + kMinRepaintDelay - now);
	if (delay <= 0) {
		repaintItems(now);
	} else if (!_updateInlineItems.isActive()
		|| _updateInlineItems.remainingTime() > kMinRepaintDelay) {
		_updateInlineItems.callOnce(std::max(delay, kMinRepaintDelay));
	}
}

void GifsListWidget::repaintItems(crl::time now) {
	_lastUpdatedAt = now ? now : crl::now();
	update();
}

} // namespace ChatHelpers
