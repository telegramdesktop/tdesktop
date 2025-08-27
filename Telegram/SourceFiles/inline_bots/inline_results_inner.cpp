/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "inline_bots/inline_results_inner.h"

#include "api/api_common.h"
#include "chat_helpers/gifs_list_widget.h" // ChatHelpers::AddGifAction
#include "menu/menu_send.h" // SendMenu::FillSendMenu
#include "core/click_handler_types.h"
#include "data/data_document.h"
#include "data/data_file_origin.h"
#include "data/data_user.h"
#include "data/data_changes.h"
#include "data/data_chat_participant_status.h"
#include "data/data_session.h"
#include "inline_bots/bot_attach_web_view.h"
#include "inline_bots/inline_bot_result.h"
#include "inline_bots/inline_bot_layout_item.h"
#include "lang/lang_keys.h"
#include "layout/layout_position.h"
#include "mainwindow.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/effects/path_shift_gradient.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "history/view/history_view_cursor_state.h"
#include "history/history.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"

#include <QtWidgets/QApplication>

namespace InlineBots {
namespace Layout {
namespace {

constexpr auto kMinRepaintDelay = crl::time(33);
constexpr auto kMinAfterScrollDelay = crl::time(33);

} // namespace

Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _pathGradient(std::make_unique<Ui::PathShiftGradient>(
	st::windowBgRipple,
	st::windowBgOver,
	[=] { repaintItems(); }))
, _updateInlineItems([=] { updateInlineItems(); })
, _mosaic(st::emojiPanWidth - st::emojiScroll.width - st::inlineResultsLeft)
, _previewTimer([=] { showPreview(); }) {
	resize(st::emojiPanWidth - st::emojiScroll.width - st::roundRadiusSmall, st::inlineResultsMinHeight);

	setMouseTracking(true);
	setAttribute(Qt::WA_OpaquePaintEvent);

	_controller->session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		updateInlineItems();
	}, lifetime());

	controller->gifPauseLevelChanged(
	) | rpl::start_with_next([=] {
		if (!_controller->isGifPausedAtLeastFor(
				Window::GifPauseReason::InlineResults)) {
			updateInlineItems();
		}
	}, lifetime());

	_controller->session().changes().peerUpdates(
		Data::PeerUpdate::Flag::Rights
	) | rpl::filter([=](const Data::PeerUpdate &update) {
		return (update.peer.get() == _inlineQueryPeer);
	}) | rpl::start_with_next([=] {
		auto isRestricted = (_restrictedLabel != nullptr);
		if (isRestricted != isRestrictedView()) {
			auto h = countHeight();
			if (h != height()) resize(width(), h);
		}
	}, lifetime());

	sizeValue(
	) | rpl::start_with_next([=](const QSize &s) {
		_mosaic.setFullWidth(s.width());
	}, lifetime());

	_mosaic.setRightSkip(st::inlineResultsSkip);
}

void Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleBottom = visibleBottom;
	if (_visibleTop != visibleTop) {
		_visibleTop = visibleTop;
		_lastScrolledAt = crl::now();
		update();
	}
}

void Inner::checkRestrictedPeer() {
	if (_inlineQueryPeer) {
		const auto error = Data::RestrictionError(
			_inlineQueryPeer,
			ChatRestriction::SendInline);
		const auto changed = (_restrictedLabelKey != error.text);
		if (!changed) {
			return;
		}
		_restrictedLabelKey = error.text;
		if (error) {
			const auto window = _controller;
			const auto peer = _inlineQueryPeer;
			_restrictedLabel.create(
				this,
				rpl::single(error.boostsToLift
					? Ui::Text::Link(error.text)
					: TextWithEntities{ error.text }),
				st::stickersRestrictedLabel);
			const auto lifting = error.boostsToLift;
			_restrictedLabel->setClickHandlerFilter([=](auto...) {
				window->resolveBoostState(peer->asChannel(), lifting);
				return false;
			});
			_restrictedLabel->show();
			updateRestrictedLabelGeometry();
			if (_switchPmButton) {
				_switchPmButton->hide();
			}
			repaintItems();
			return;
		}
	} else {
		_restrictedLabelKey = QString();
	}
	if (_restrictedLabel) {
		_restrictedLabel.destroy();
		if (_switchPmButton) {
			_switchPmButton->show();
		}
		repaintItems();
	}
}

void Inner::updateRestrictedLabelGeometry() {
	if (!_restrictedLabel) {
		return;
	}

	auto labelWidth = width() - st::stickerPanPadding * 2;
	_restrictedLabel->resizeToWidth(labelWidth);
	_restrictedLabel->moveToLeft(
		(width() - _restrictedLabel->width()) / 2,
		st::stickerPanPadding);
}

bool Inner::isRestrictedView() {
	checkRestrictedPeer();
	return (_restrictedLabel != nullptr);
}

int Inner::countHeight() {
	if (isRestrictedView()) {
		return st::stickerPanPadding + _restrictedLabel->height() + st::stickerPanPadding;
	} else if (_mosaic.empty() && !_switchPmButton) {
		return st::stickerPanPadding + st::normalFont->height + st::stickerPanPadding;
	}
	auto result = st::stickerPanPadding;
	if (_switchPmButton) {
		result += _switchPmButton->height() + st::inlineResultsSkip;
	}
	for (auto i = 0, l = _mosaic.rowsCount(); i < l; ++i) {
		result += _mosaic.rowHeightAt(i);
	}
	return result + st::stickerPanPadding;
}

QString Inner::tooltipText() const {
	if (const auto lnk = ClickHandler::getActive()) {
		return lnk->tooltip();
	}
	return QString();
}

QPoint Inner::tooltipPos() const {
	return _lastMousePos;
}

bool Inner::tooltipWindowActive() const {
	return Ui::AppInFocus() && Ui::InFocusChain(window());
}

rpl::producer<> Inner::inlineRowsCleared() const {
	return _inlineRowsCleared.events();
}

Inner::~Inner() = default;

void Inner::resizeEvent(QResizeEvent *e) {
	updateRestrictedLabelGeometry();
}

void Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);
	QRect r = e ? e->rect() : rect();
	if (r != rect()) {
		p.setClipRect(r);
	}
	p.fillRect(r, st::emojiPanBg);

	paintInlineItems(p, r);
}

void Inner::paintInlineItems(Painter &p, const QRect &r) {
	if (_restrictedLabel) {
		return;
	}
	if (_mosaic.empty() && !_switchPmButton) {
		p.setFont(st::normalFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, 0, width(), (height() / 3) * 2 + st::normalFont->height), tr::lng_inline_bot_no_results(tr::now), style::al_center);
		return;
	}
	const auto gifPaused = _controller->isGifPausedAtLeastFor(
		Window::GifPauseReason::InlineResults);
	using namespace InlineBots::Layout;
	PaintContext context(crl::now(), false, gifPaused, false);
	context.pathGradient = _pathGradient.get();
	context.pathGradient->startFrame(0, width(), width() / 2);

	auto paintItem = [&](not_null<const ItemBase*> item, QPoint point) {
		p.translate(point.x(), point.y());
		item->paint(
			p,
			r.translated(-point),
			&context);
		p.translate(-point.x(), -point.y());
	};
	_mosaic.paint(std::move(paintItem), r);
}

void Inner::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_lastMousePos = e->globalPos();
	updateSelected();

	_pressed = _selected;
	ClickHandler::pressed();
	_previewTimer.callOnce(QApplication::startDragTime());
}

void Inner::mouseReleaseEvent(QMouseEvent *e) {
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

	using namespace InlineBots::Layout;
	const auto open = dynamic_cast<OpenFileClickHandler*>(activated.get());
	if (dynamic_cast<SendClickHandler*>(activated.get()) || open) {
		selectInlineResult(_selected, {}, !!open);
	} else {
		ActivateClickHandler(window(), activated, {
			e->button(),
			QVariant::fromValue(ClickHandlerContext{
				.sessionWindow = base::make_weak(_controller),
			})
		});
	}
}

void Inner::selectInlineResult(
		int index,
		Api::SendOptions options,
		bool open) {
	const auto item = _mosaic.maybeItemAt(index);
	if (!item) {
		return;
	}
	const auto messageSendingFrom = [&]() -> Ui::MessageSendingAnimationFrom {
		const auto document = item->getDocument()
			? item->getDocument()
			: item->getPreviewDocument();
		if (options.scheduled
			|| item->isFullLine()
			|| !document
			|| (!document->sticker() && !document->isGifv())) {
			return {};
		}
		using Type = Ui::MessageSendingAnimationFrom::Type;
		const auto type = document->sticker()
			? Type::Sticker
			: document->isGifv()
			? Type::Gif
			: Type::None;
		const auto rect = item->innerContentRect().translated(
			_mosaic.findRect(index).topLeft());
		return {
			.type = type,
			.localId = _controller->session().data().nextLocalMessageId(),
			.globalStartGeometry = mapToGlobal(rect),
			.crop = document->isGifv(),
		};
	};

	if (const auto inlineResult = item->getResult()) {
		if (inlineResult->onChoose(item)) {
			_resultSelectedCallback({
				.result = std::move(inlineResult),
				.bot = _inlineBot,
				.options = std::move(options),
				.messageSendingFrom = messageSendingFrom(),
				.open = open,
			});
		}
	}
}

void Inner::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void Inner::leaveEventHook(QEvent *e) {
	clearSelection();
	Ui::Tooltip::Hide();
}

void Inner::leaveToChildEvent(QEvent *e, QWidget *child) {
	clearSelection();
}

void Inner::enterFromChildEvent(QEvent *e, QWidget *child) {
	_lastMousePos = QCursor::pos();
	updateSelected();
}

void Inner::contextMenuEvent(QContextMenuEvent *e) {
	if (_selected < 0 || _pressed >= 0) {
		return;
	}
	auto details = _sendMenuDetails
		? _sendMenuDetails()
		: SendMenu::Details();

	// inline results don't have effects
	details.effectAllowed = false;

	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);

	const auto selected = _selected;
	const auto send = crl::guard(this, [=](Api::SendOptions options) {
		selectInlineResult(selected, options, false);
	});
	const auto show = _controller->uiShow();

	// In case we're adding items after FillSendMenu we have
	// to pass nullptr for showForEffect and attach selector later.
	// Otherwise added items widths won't be respected in menu geometry.
	SendMenu::FillSendMenu(
		_menu,
		nullptr, // showForEffect
		details,
		SendMenu::DefaultCallback(show, send));

	const auto item = _mosaic.itemAt(_selected);
	if (const auto previewDocument = item->getPreviewDocument()) {
		auto callback = [&](
				const QString &text,
				Fn<void()> &&done,
				const style::icon *icon) {
			_menu->addAction(text, std::move(done), icon);
		};
		ChatHelpers::AddGifAction(
			std::move(callback),
			_controller->uiShow(),
			previewDocument);
	}

	SendMenu::AttachSendMenuEffect(
		_menu,
		show,
		details,
		SendMenu::DefaultCallback(show, send));

	if (!_menu->empty()) {
		_menu->popup(QCursor::pos());
	}
}

void Inner::clearSelection() {
	if (_selected >= 0) {
		ClickHandler::clearActive(_mosaic.itemAt(_selected));
		setCursor(style::cur_default);
	}
	_selected = _pressed = -1;
	updateInlineItems();
}

void Inner::hideFinished() {
	clearHeavyData();
}

void Inner::clearHeavyData() {
	clearInlineRows(false);
	for (const auto &[result, layout] : _inlineLayouts) {
		layout->unloadHeavyPart();
	}
}

void Inner::inlineBotChanged() {
	refreshInlineRows(nullptr, nullptr, nullptr, true);
}

void Inner::clearInlineRows(bool resultsDeleted) {
	if (resultsDeleted) {
		_selected = _pressed = -1;
	} else {
		clearSelection();
	}
	_mosaic.clearRows(resultsDeleted);
}

ItemBase *Inner::layoutPrepareInlineResult(std::shared_ptr<Result> result) {
	const auto raw = result.get();
	auto it = _inlineLayouts.find(raw);
	if (it == _inlineLayouts.cend()) {
		if (auto layout = ItemBase::createLayout(
				this,
				std::move(result),
				_inlineWithThumb)) {
			it = _inlineLayouts.emplace(raw, std::move(layout)).first;
			it->second->initDimensions();
		} else {
			return nullptr;
		}
	}
	if (!it->second->maxWidth()) {
		return nullptr;
	}

	return it->second.get();
}

void Inner::deleteUnusedInlineLayouts() {
	if (_mosaic.empty()) { // delete all
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

void Inner::preloadImages() {
	_mosaic.forEach([](not_null<const ItemBase*> item) {
		item->preload();
	});
}

void Inner::hideInlineRowsPanel() {
	clearInlineRows(false);
}

void Inner::clearInlineRowsPanel() {
	clearInlineRows(false);
}

void Inner::refreshMosaicOffset() {
	const auto top = _switchPmButton
		? (_switchPmButton->height() + st::inlineResultsSkip)
		: 0;
	_mosaic.setPadding(st::emojiPanMargins + QMargins(0, top, 0, 0));
}

void Inner::refreshSwitchPmButton(const CacheEntry *entry) {
	if (!entry || entry->switchPmText.isEmpty()) {
		_switchPmButton.destroy();
		_switchPmStartToken.clear();
		_switchPmUrl = QByteArray();
	} else {
		if (!_switchPmButton) {
			_switchPmButton.create(this, nullptr, st::switchPmButton);
			_switchPmButton->show();
			_switchPmButton->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
			_switchPmButton->addClickHandler([=] { switchPm(); });
		}
		_switchPmButton->setText(rpl::single(entry->switchPmText));
		_switchPmStartToken = entry->switchPmStartToken;
		_switchPmUrl = entry->switchPmUrl;
		const auto buttonTop = st::stickerPanPadding;
		_switchPmButton->move(st::inlineResultsLeft - st::roundRadiusSmall, buttonTop);
		if (isRestrictedView()) {
			_switchPmButton->hide();
		}
	}
	repaintItems();
}

int Inner::refreshInlineRows(PeerData *queryPeer, UserData *bot, const CacheEntry *entry, bool resultsDeleted) {
	_inlineBot = bot;
	_inlineQueryPeer = queryPeer;
	refreshSwitchPmButton(entry);
	refreshMosaicOffset();
	auto clearResults = [&] {
		if (!entry) {
			return true;
		}
		if (entry->results.empty() && entry->switchPmText.isEmpty()) {
			return true;
		}
		return false;
	};
	auto clearResultsResult = clearResults(); // Clang workaround.
	if (clearResultsResult) {
		if (resultsDeleted) {
			clearInlineRows(true);
			deleteUnusedInlineLayouts();
		}
		_inlineRowsCleared.fire({});
		return 0;
	}

	clearSelection();

	Assert(_inlineBot != 0);

	const auto count = int(entry->results.size());
	const auto from = validateExistingInlineRows(entry->results);
	auto added = 0;

	if (count) {
		const auto resultItems = entry->results | ranges::views::slice(
			from,
			count
		) | ranges::views::transform([&](const std::shared_ptr<Result> &r) {
			return layoutPrepareInlineResult(r);
		}) | ranges::views::filter([](const ItemBase *item) {
			return item != nullptr;
		}) | ranges::to<std::vector<not_null<ItemBase*>>>;

		_mosaic.addItems(resultItems);
		added = resultItems.size();
		preloadImages();
	}

	auto h = countHeight();
	if (h != height()) resize(width(), h);
	repaintItems();

	_lastMousePos = QCursor::pos();
	updateSelected();

	return added;
}

int Inner::validateExistingInlineRows(const Results &results) {
	const auto until = _mosaic.validateExistingRows([&](
			not_null<const ItemBase*> item,
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

void Inner::inlineItemLayoutChanged(const ItemBase *layout) {
	if (_selected < 0 || !isVisible()) {
		return;
	}

	if (const auto item = _mosaic.maybeItemAt(_selected)) {
		if (layout == item) {
			updateSelected();
		}
	}
}

void Inner::inlineItemRepaint(const ItemBase *layout) {
	updateInlineItems();
}

bool Inner::inlineItemVisible(const ItemBase *layout) {
	int32 position = layout->position();
	if (position < 0 || !isVisible()) {
		return false;
	}

	const auto &[row, column] = ::Layout::IndexToPosition(position);

	auto top = st::stickerPanPadding;
	for (auto i = 0; i != row; ++i) {
		top += _mosaic.rowHeightAt(i);
	}

	return (top < _visibleBottom)
		&& (top + _mosaic.itemAt(row, column)->height() > _visibleTop);
}

Data::FileOrigin Inner::inlineItemFileOrigin() {
	return Data::FileOrigin();
}

void Inner::updateSelected() {
	if (_pressed >= 0 && !_previewShown) {
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
					_controller->widget()->showMediaPreview(
						Data::FileOrigin(),
						preview);
				} else if (const auto preview = item->getPreviewPhoto()) {
					_controller->widget()->showMediaPreview(
						Data::FileOrigin(),
						preview);
				}
			}
		}
	}
	if (ClickHandler::setActive(link, item)) {
		setCursor(link ? style::cur_pointer : style::cur_default);
		Ui::Tooltip::Hide();
	}
	if (link) {
		Ui::Tooltip::Show(1000, this);
	}
}

void Inner::showPreview() {
	if (_pressed < 0) {
		return;
	}

	if (const auto layout = _mosaic.maybeItemAt(_pressed)) {
		if (const auto previewDocument = layout->getPreviewDocument()) {
			_previewShown = _controller->widget()->showMediaPreview(
				Data::FileOrigin(),
				previewDocument);
		} else if (const auto previewPhoto = layout->getPreviewPhoto()) {
			_previewShown = _controller->widget()->showMediaPreview(
				Data::FileOrigin(),
				previewPhoto);
		}
	}
}

void Inner::updateInlineItems() {
	const auto now = crl::now();

	const auto delay = std::max(
		_lastScrolledAt + kMinAfterScrollDelay - now,
		_lastUpdatedAt + kMinRepaintDelay - now);
	if (delay <= 0) {
		repaintItems();
	} else if (!_updateInlineItems.isActive()
		|| _updateInlineItems.remainingTime() > kMinRepaintDelay) {
		_updateInlineItems.callOnce(std::max(delay, kMinRepaintDelay));
	}
}

void Inner::repaintItems(crl::time now) {
	_lastUpdatedAt = now ? now : crl::now();
	update();
}

void Inner::switchPm() {
	if (!_inlineBot || !_inlineBot->isBot()) {
		return;
	} else if (!_switchPmUrl.isEmpty()) {
		const auto bot = _inlineBot;
		_inlineBot->session().attachWebView().open({
			.bot = bot,
			.context = { .controller = _controller },
			.button = { .url = _switchPmUrl },
			.source = InlineBots::WebViewSourceSwitch(),
		});
	} else {
		_inlineBot->botInfo->startToken = _switchPmStartToken;
		_inlineBot->botInfo->inlineReturnTo
			= _controller->dialogsEntryStateCurrent();
		_controller->showPeerHistory(
			_inlineBot,
			Window::SectionShow::Way::ClearStack,
			ShowAndStartBotMsgId);
	}
}

void Inner::setSendMenuDetails(Fn<SendMenu::Details()> &&callback) {
	_sendMenuDetails = std::move(callback);
}

} // namespace Layout
} // namespace InlineBots
