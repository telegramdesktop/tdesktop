/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/history_view_add_poll_option.h"

#include "history/view/history_view_element.h"
#include "history/view/history_view_element_overlay.h"
#include "history/view/controls/history_view_webpage_processor.h"
#include "history/view/media/history_view_media.h"
#include "history/view/media/menu/history_view_poll_menu.h"
#include "api/api_polls.h"
#include "apiwrap.h"
#include "base/event_filter.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "core/file_utilities.h"
#include "core/ui_integration.h"
#include "data/data_peer.h"
#include "data/data_poll.h"
#include "data/data_user.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "data/stickers/data_custom_emoji.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "poll/poll_link_box.h"
#include "poll/poll_link_thumbnail.h"
#include "poll/poll_media_upload.h"
#include "ui/chat/attach/attach_prepare.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/chat/chat_style.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/popup_menu.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_polls.h"

namespace HistoryView {
namespace {

class AddPollOptionWidget final : public Ui::RpWidget {
public:
	AddPollOptionWidget(
		not_null<QWidget*> parent,
		not_null<PollData*> poll,
		FullMsgId itemId,
		not_null<Window::SessionController*> controller);

	void setPlaceholderColorOverride(const style::color &color);
	void setIconColorOverride(QColor color);
	void updatePosition(QPoint topLeft, int width);
	void triggerSubmit();

	[[nodiscard]] rpl::producer<> submitted() const;
	[[nodiscard]] rpl::producer<> cancelled() const;

private:
	void setupField();
	void setupEmojiPanel();
	void setupAttach();
	void showStickerPanel();
	void subscribeToPollUpdates();
	void chooseLink();
	void resolveLink(QString url);
	void applyResolvedWebPage(not_null<WebPageData*> page);
	void subscribeToWebPageUpdates(not_null<WebPageData*> page);
	void clearMedia();
	[[nodiscard]] static QString mapErrorToText(const QString &error);

	const not_null<PollData*> _poll;
	const FullMsgId _itemId;
	const not_null<Window::SessionController*> _controller;
	const not_null<Main::Session*> _session;

	Ui::InputField *_field = nullptr;
	Ui::IconButton *_emoji = nullptr;
	PollMediaUpload::PollMediaButton *_attach = nullptr;
	base::unique_qptr<ChatHelpers::TabbedPanel> _emojiPanel;
	base::unique_qptr<ChatHelpers::TabbedPanel> _stickerPanel;
	base::unique_qptr<Ui::PopupMenu> _mediaMenu;
	std::unique_ptr<PollMediaUpload::PollMediaUploader> _uploader;
	std::shared_ptr<PollMediaUpload::PollMediaState> _mediaState;
	std::shared_ptr<HistoryView::Controls::WebpageResolver> _webpageResolver;
	rpl::lifetime _webpageLifetime;

	rpl::event_stream<> _submittedEvents;
	rpl::event_stream<> _cancelledEvents;

};

AddPollOptionWidget::AddPollOptionWidget(
	not_null<QWidget*> parent,
	not_null<PollData*> poll,
	FullMsgId itemId,
	not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _poll(poll)
, _itemId(itemId)
, _controller(controller)
, _session(&controller->session())
, _mediaState(std::make_shared<PollMediaUpload::PollMediaState>())
, _webpageResolver(
	std::make_shared<HistoryView::Controls::WebpageResolver>(_session)) {
	const auto item = _session->data().message(_itemId);
	const auto peer = item ? item->history()->peer.get() : nullptr;
	if (peer) {
		_uploader = std::make_unique<PollMediaUpload::PollMediaUploader>(
			PollMediaUpload::PollMediaUploader::Args{
				.session = _session,
				.peer = peer,
				.showError = [=](const QString &text) {
					Ui::Toast::Show(parentWidget(), text);
				},
			});
	}
	setupField();
	setupEmojiPanel();
	setupAttach();
	subscribeToPollUpdates();
}

void AddPollOptionWidget::setupField() {
	_field = Ui::CreateChild<Ui::InputField>(
		this,
		st::historyPollAddOptionField,
		Ui::InputField::Mode::NoNewlines,
		tr::lng_polls_add_option_placeholder());

	_emoji = Ui::CreateChild<Ui::IconButton>(
		this,
		st::historyPollAddOptionEmoji);

	_attach = Ui::CreateChild<PollMediaUpload::PollMediaButton>(
		this,
		st::historyPollAddOptionAttach,
		_mediaState);

	_field->setMaxLength(100);
	_field->setCustomTextContext(Core::TextContext({
		.session = _session,
	}));

	const auto field = _field;
	const auto emoji = _emoji;
	const auto attach = _attach;
	sizeValue(
	) | rpl::on_next([field, emoji, attach](QSize size) {
		field->setGeometry(0, 0, size.width(), size.height());
		const auto bsize = st::historyPollAddOptionButtonSize;
		const auto by = (size.height() - bsize) / 2;
		emoji->moveToLeft(st::historyPollAddOptionEmojiLeft, by);
		attach->moveToRight(0, by, size.width());
	}, _field->lifetime());

	_field->submits(
	) | rpl::on_next([=] {
		triggerSubmit();
	}, _field->lifetime());

	base::install_event_filter(_field, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::KeyPress) {
			if (static_cast<QKeyEvent*>(event.get())->key() == Qt::Key_Escape) {
				if (_emojiPanel && !_emojiPanel->isHidden()) {
					_emojiPanel->hideAnimated();
				} else {
					_cancelledEvents.fire({});
				}
				return base::EventFilterResult::Cancel;
			}
		}
		return base::EventFilterResult::Continue;
	});

	_field->setFocusFast();
}

void AddPollOptionWidget::setupEmojiPanel() {
	using Selector = ChatHelpers::TabbedSelector;

	const auto parent = parentWidget();
	_emojiPanel = base::make_unique_q<ChatHelpers::TabbedPanel>(
		parent,
		_controller,
		object_ptr<Selector>(
			nullptr,
			_controller->uiShow(),
			Window::GifPauseReason::Layer,
			Selector::Mode::EmojiOnly));

	const auto panel = _emojiPanel.get();
	panel->setDesiredHeightValues(
		1.,
		st::emojiPanMinHeight / 2,
		st::emojiPanMinHeight);
	panel->hide();
	panel->selector()->setCurrentPeer(_session->user().get());

	_emoji->installEventFilter(panel);
	_emoji->addClickHandler([=] {
		const auto button = QRect(
			_emoji->mapTo(parent, QPoint()),
			_emoji->size());
		const auto isDropDown = button.y() < parent->height() / 2;
		panel->setDropDown(isDropDown);
		if (isDropDown) {
			panel->moveTopRight(
				button.y() + button.height(),
				button.x() + button.width());
		} else {
			panel->moveBottomRight(
				button.y(),
				button.x() + button.width());
		}
		panel->toggleAnimated();
	});

	panel->selector()->emojiChosen(
	) | rpl::on_next([=](ChatHelpers::EmojiChosen data) {
		Ui::InsertEmojiAtCursor(_field->textCursor(), data.emoji);
	}, panel->lifetime());

	panel->selector()->customEmojiChosen(
	) | rpl::on_next([=](ChatHelpers::FileChosen data) {
		Data::InsertCustomEmoji(_field, data.document);
	}, panel->lifetime());
}

void AddPollOptionWidget::setupAttach() {
	if (!_uploader) {
		return;
	}
	_attach->addClickHandler([=] {
		if (ShowPollMediaPreview(_controller, _mediaState, {
			.choosePhotoOrVideo = [=] {
				_uploader->choosePhotoOrVideo(this, _mediaState);
			},
			.chooseDocument = [=] {
				_uploader->chooseDocument(this, _mediaState);
			},
			.chooseSticker = [=] { showStickerPanel(); },
			.editPhoto = [=](Ui::PreparedList list) {
				_uploader->applyPreparedPhotoList(
					_mediaState,
					std::move(list));
			},
			.remove = [=] { clearMedia(); },
		})) {
			return;
		}
		_mediaMenu = base::make_unique_q<Ui::PopupMenu>(
			_attach,
			st::popupMenuWithIcons);
		_mediaMenu->setForcedOrigin(
			Ui::PanelAnimation::Origin::TopRight);
		_mediaMenu->addAction(
			tr::lng_attach_photo_or_video(tr::now),
			[=] { _uploader->choosePhotoOrVideo(this, _mediaState); },
			&st::menuIconPhoto);
		_mediaMenu->addAction(
			tr::lng_attach_file(tr::now),
			[=] { _uploader->chooseDocument(this, _mediaState); },
			&st::menuIconFile);
		_mediaMenu->addAction(
			tr::lng_chat_intro_choose_sticker(tr::now),
			[=] { showStickerPanel(); },
			&st::menuIconStickers);
		_mediaMenu->addAction(
			tr::lng_polls_create_option_link(tr::now),
			[=] { chooseLink(); },
			&st::menuIconLink);
		_mediaMenu->popup(QCursor::pos());
	});
	_uploader->installDropToField(_field, _mediaState, false);
}

void AddPollOptionWidget::chooseLink() {
	const auto initial = _mediaState->media.url;
	const auto callback = crl::guard(this, [=](QString url) {
		auto pollMedia = PollMedia();
		pollMedia.url = url;
		_uploader->setMedia(
			_mediaState,
			pollMedia,
			Poll::MakeLinkThumbnail(),
			false);
		resolveLink(url);
	});
	const auto box = _controller->show(Box(
		Poll::AddPollOptionLinkBox,
		initial,
		callback));
	if (const auto raw = box.get()) {
		raw->boxClosing(
		) | rpl::on_next(crl::guard(this, [this] {
			_field->setFocus();
		}), raw->lifetime());
	}
}

void AddPollOptionWidget::applyResolvedWebPage(
		not_null<WebPageData*> page) {
	auto pollMedia = PollMedia();
	pollMedia.webpage = page;
	pollMedia.url = page->url.isEmpty()
		? _mediaState->media.url
		: page->url;
	auto thumb = page->photo
		? Ui::MakePhotoThumbnailCenterCrop(page->photo, FullMsgId())
		: Poll::MakeLinkThumbnail();
	const auto rounded = (page->photo != nullptr);
	_uploader->setMedia(_mediaState, pollMedia, std::move(thumb), rounded);
}

void AddPollOptionWidget::subscribeToWebPageUpdates(
		not_null<WebPageData*> page) {
	_session->data().webPageUpdates(
	) | rpl::filter([=](not_null<WebPageData*> updated) {
		return (updated == page) && (_mediaState->media.webpage == page);
	}) | rpl::on_next([=] {
		applyResolvedWebPage(page);
	}, _webpageLifetime);
}

void AddPollOptionWidget::resolveLink(QString url) {
	_webpageLifetime.destroy();
	const auto token = _mediaState->token;
	const auto apply = [=](const QString &resolvedUrl) {
		if (_mediaState->token != token || resolvedUrl != url) {
			return;
		}
		const auto cached = _webpageResolver->lookup(url);
		if (!cached || !*cached) {
			return;
		}
		const auto page = *cached;
		applyResolvedWebPage(page);
		subscribeToWebPageUpdates(page);
	};
	if (const auto cached = _webpageResolver->lookup(url)) {
		if (*cached) {
			applyResolvedWebPage(*cached);
			subscribeToWebPageUpdates(*cached);
		}
		return;
	}
	_webpageResolver->resolved(
	) | rpl::filter([=](const QString &resolvedUrl) {
		return (resolvedUrl == url) && (_mediaState->token == token);
	}) | rpl::take(1) | rpl::on_next(apply, _webpageLifetime);
	_webpageResolver->request(url);
}

void AddPollOptionWidget::clearMedia() {
	_webpageLifetime.destroy();
	_uploader->clearMedia(_mediaState);
}

void AddPollOptionWidget::showStickerPanel() {
	const auto parent = parentWidget();
	if (!_stickerPanel) {
		_stickerPanel = CreatePollStickerPanel(parent, _controller);
		_stickerPanel->selector()->fileChosen(
		) | rpl::on_next([=](ChatHelpers::FileChosen data) {
			if (Window::ShowSendPremiumError(
					_controller,
					data.document)) {
				return;
			}
			_uploader->setMedia(
				_mediaState,
				PollMedia{ .document = data.document },
				Ui::MakeEmojiThumbnail(
					&_session->data(),
					Data::SerializeCustomEmojiId(data.document)),
				false);
			_stickerPanel->hideAnimated();
		}, _stickerPanel->lifetime());
	}
	const auto panel = _stickerPanel.get();
	const auto button = QRect(
		_attach->mapTo(parent, QPoint()),
		_attach->size());
	const auto isDropDown = button.y() < parent->height() / 2;
	panel->setDropDown(isDropDown);
	if (isDropDown) {
		panel->moveTopRight(
			button.y() + button.height(),
			button.x() + button.width());
	} else {
		panel->moveBottomRight(
			button.y(),
			button.x() + button.width());
	}
	panel->toggleAnimated();
}

void AddPollOptionWidget::subscribeToPollUpdates() {
	_session->data().pollUpdates(
	) | rpl::filter([=](not_null<PollData*> poll) {
		return (poll == _poll);
	}) | rpl::on_next([=](not_null<PollData*> poll) {
		if (poll->closed()
			|| (int(poll->answers.size())
				>= _session->appConfig().pollOptionsLimit())) {
			_cancelledEvents.fire({});
		}
	}, lifetime());

	_session->data().itemRemoved(
	) | rpl::filter([=](not_null<const HistoryItem*> item) {
		return (item->fullId() == _itemId);
	}) | rpl::on_next([=] {
		_cancelledEvents.fire({});
	}, lifetime());
}

void AddPollOptionWidget::triggerSubmit() {
	const auto textWithTags = _field->getTextWithAppliedMarkdown();
	auto fullText = TextWithEntities{
		textWithTags.text,
		TextUtilities::ConvertTextTagsToEntities(textWithTags.tags),
	};
	TextUtilities::Trim(fullText);
	if (fullText.text.isEmpty()) {
		return;
	}
	if (int(_poll->answers.size())
		>= _session->appConfig().pollOptionsLimit()) {
		Ui::Toast::Show(
			parentWidget(),
			tr::lng_polls_max_options_reached(tr::now));
		return;
	}

	_field->setEnabled(false);

	const auto media = _mediaState ? _mediaState->media : PollMedia();
	_session->api().polls().addAnswer(
		_itemId,
		fullText,
		media,
		[=] { _submittedEvents.fire({}); },
		[=](QString error) {
			_field->setEnabled(true);
			Ui::Toast::Show(
				parentWidget(),
				mapErrorToText(error));
		});
}

QString AddPollOptionWidget::mapErrorToText(const QString &error) {
	if (error == u"POLL_ANSWERS_TOO_MUCH"_q) {
		return tr::lng_polls_max_options_reached(tr::now);
	} else if (error == u"POLL_ANSWER_DUPLICATE"_q) {
		return tr::lng_polls_add_option_duplicate(tr::now);
	} else if (error.startsWith(u"POLL"_q) && error.contains(u"CLOSE"_q)) {
		return tr::lng_polls_add_option_closed(tr::now);
	}
	return tr::lng_polls_add_option_error(tr::now);
}

void AddPollOptionWidget::updatePosition(QPoint topLeft, int w) {
	setGeometry(
		topLeft.x(),
		topLeft.y() + st::historyPollAddOptionTop,
		w,
		st::historyPollAddOptionField.heightMin);
}

rpl::producer<> AddPollOptionWidget::submitted() const {
	return _submittedEvents.events();
}

rpl::producer<> AddPollOptionWidget::cancelled() const {
	return _cancelledEvents.events();
}

void AddPollOptionWidget::setPlaceholderColorOverride(
		const style::color &color) {
	_field->setPlaceholderColorOverride(color);
}

void AddPollOptionWidget::setIconColorOverride(QColor color) {
	_emoji->setIconColorOverride(color);
	_attach->setIconColorOverride(color);
	_attach->setRippleColorOverride(anim::with_alpha(color, 0.15));
}

} // namespace

void ShowAddPollOptionOverlay(
		ElementOverlayHost &host,
		not_null<QWidget*> parent,
		not_null<Element*> view,
		not_null<PollData*> poll,
		FullMsgId context,
		not_null<Window::SessionController*> controller,
		not_null<const Ui::ChatStyle*> st) {
	auto widget = base::make_unique_q<AddPollOptionWidget>(
		parent,
		poll,
		context,
		controller);
	const auto raw = widget.get();
	const auto &msgSt = st->messageStyle(view->hasOutLayout(), false);
	raw->setPlaceholderColorOverride(msgSt.msgDateFg);
	raw->setIconColorOverride(msgSt.msgDateFg->c);
	host.show(
		view,
		context,
		std::move(widget),
		rpl::merge(raw->submitted(), raw->cancelled()),
		[raw](not_null<Element*> v, int top) {
			const auto media = v->media();
			if (!media) {
				return false;
			}
			const auto mediaPos = v->mediaTopLeft();
			const auto innerWidth = v->innerGeometry().width()
				- st::msgPadding.left()
				- st::msgPadding.right();
			const auto rect = media->addOptionRect(innerWidth);
			raw->updatePosition(
				QPoint(
					mediaPos.x() + rect.x(),
					top + mediaPos.y() + rect.y()),
				rect.width());
			return true;
		},
		[](not_null<Element*> v, bool active) {
			if (const auto media = v->media()) {
				media->setAddOptionActive(active);
			}
		},
		[raw] { raw->triggerSubmit(); });
}

} // namespace HistoryView
