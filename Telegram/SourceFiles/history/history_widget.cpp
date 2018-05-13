/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_widget.h"

#include "boxes/confirm_box.h"
#include "boxes/send_files_box.h"
#include "boxes/share_box.h"
#include "boxes/edit_caption_box.h"
#include "core/file_utilities.h"
#include "ui/toast/toast.h"
#include "ui/special_buttons.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/shadow.h"
#include "ui/effects/ripple_animation.h"
#include "inline_bots/inline_bot_result.h"
#include "data/data_drafts.h"
#include "data/data_session.h"
#include "data/data_media_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_message.h"
#include "history/history_media_types.h"
#include "history/history_drag_area.h"
#include "history/history_inner_widget.h"
#include "history/history_item_components.h"
#include "history/feed/history_feed_section.h"
#include "history/view/history_view_service_message.h"
#include "history/view/history_view_element.h"
#include "profile/profile_block_group_members.h"
#include "info/info_memento.h"
#include "core/click_handler_types.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_selector.h"
#include "chat_helpers/tabbed_section.h"
#include "chat_helpers/bot_keyboard.h"
#include "chat_helpers/message_field.h"
#include "lang/lang_keys.h"
#include "application.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "passcodewidget.h"
#include "mainwindow.h"
#include "storage/localimageloader.h"
#include "storage/localstorage.h"
#include "storage/file_upload.h"
#include "storage/storage_media_prepare.h"
#include "media/media_audio.h"
#include "media/media_audio_capture.h"
#include "media/player/media_player_instance.h"
#include "apiwrap.h"
#include "history/view/history_view_top_bar_widget.h"
#include "observer_peer.h"
#include "base/qthelp_regex.h"
#include "ui/widgets/popup_menu.h"
#include "ui/text_options.h"
#include "auth_session.h"
#include "window/themes/window_theme.h"
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "window/window_slide_animation.h"
#include "window/window_peer_menu.h"
#include "inline_bots/inline_results_widget.h"
#include "chat_helpers/emoji_suggestions_widget.h"
#include "core/crash_reports.h"
#include "dialogs/dialogs_key.h"
#include "styles/style_history.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "styles/style_profile.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_info.h"

namespace {

constexpr auto kMessagesPerPageFirst = 30;
constexpr auto kMessagesPerPage = 50;
constexpr auto kPreloadHeightsCount = 3; // when 3 screens to scroll left make a preload request
constexpr auto kTabbedSelectorToggleTooltipTimeoutMs = 3000;
constexpr auto kTabbedSelectorToggleTooltipCount = 3;
constexpr auto kScrollToVoiceAfterScrolledMs = 1000;
constexpr auto kSkipRepaintWhileScrollMs = 100;
constexpr auto kShowMembersDropdownTimeoutMs = 300;
constexpr auto kDisplayEditTimeWarningMs = 300 * 1000;
constexpr auto kFullDayInMs = 86400 * 1000;
constexpr auto kCancelTypingActionTimeout = TimeMs(5000);

ApiWrap::RequestMessageDataCallback replyEditMessageDataCallback() {
	return [](ChannelData *channel, MsgId msgId) {
		if (App::main()) {
			App::main()->messageDataReceived(channel, msgId);
		}
	};
}

void ActivateWindowDelayed(not_null<Window::Controller*> controller) {
	const auto window = controller->window();
	const auto weak = make_weak(window.get());
	window->activateWindow();
	crl::on_main(window, [=] {
		window->activateWindow();
	});
}

} // namespace

ReportSpamPanel::ReportSpamPanel(QWidget *parent) : TWidget(parent),
_report(this, lang(lng_report_spam), st::reportSpamHide),
_hide(this, lang(lng_report_spam_hide), st::reportSpamHide),
_clear(this, lang(lng_profile_delete_conversation)) {
	resize(parent->width(), _hide->height() + st::lineWidth);

	connect(_report, SIGNAL(clicked()), this, SIGNAL(reportClicked()));
	connect(_hide, SIGNAL(clicked()), this, SIGNAL(hideClicked()));
	connect(_clear, SIGNAL(clicked()), this, SIGNAL(clearClicked()));

	_clear->hide();
}

void ReportSpamPanel::resizeEvent(QResizeEvent *e) {
	_report->resize(width() - (_hide->width() + st::reportSpamSeparator) * 2, _report->height());
	_report->moveToLeft(_hide->width() + st::reportSpamSeparator, 0);
	_hide->moveToRight(0, 0);
	_clear->move((width() - _clear->width()) / 2, height() - _clear->height() - ((height() - st::msgFont->height - _clear->height()) / 2));
}

void ReportSpamPanel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(QRect(0, 0, width(), height() - st::lineWidth), st::reportSpamBg);
	p.fillRect(Adaptive::OneColumn() ? 0 : st::lineWidth, height() - st::lineWidth, width() - (Adaptive::OneColumn() ? 0 : st::lineWidth), st::lineWidth, st::shadowFg);
	if (!_clear->isHidden()) {
		p.setPen(st::reportSpamFg);
		p.setFont(st::msgFont);
		p.drawText(QRect(_report->x(), (_clear->y() - st::msgFont->height) / 2, _report->width(), st::msgFont->height), lang(lng_report_spam_thanks), style::al_top);
	}
}

void ReportSpamPanel::setReported(bool reported, PeerData *onPeer) {
	if (reported) {
		_report->hide();
		_clear->setText(lang(onPeer->isChannel() ? (onPeer->isMegagroup() ? lng_profile_leave_group : lng_profile_leave_channel) : lng_profile_delete_conversation));
		_clear->show();
	} else {
		_report->show();
		_clear->hide();
	}
	update();
}

HistoryHider::HistoryHider(
	MainWidget *parent,
	MessageIdsList &&items)
: RpWidget(parent)
, _forwardItems(std::move(items))
, _send(this, langFactory(lng_forward_send), st::defaultBoxButton)
, _cancel(this, langFactory(lng_cancel), st::defaultBoxButton) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent) : RpWidget(parent)
, _sendPath(true)
, _send(this, langFactory(lng_forward_send), st::defaultBoxButton)
, _cancel(this, langFactory(lng_cancel), st::defaultBoxButton) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent, const QString &botAndQuery) : RpWidget(parent)
, _botAndQuery(botAndQuery)
, _send(this, langFactory(lng_forward_send), st::defaultBoxButton)
, _cancel(this, langFactory(lng_cancel), st::defaultBoxButton) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent, const QString &url, const QString &text) : RpWidget(parent)
, _shareUrl(url)
, _shareText(text)
, _send(this, langFactory(lng_forward_send), st::defaultBoxButton)
, _cancel(this, langFactory(lng_cancel), st::defaultBoxButton) {
	init();
}

void HistoryHider::init() {
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });
	connect(_send, SIGNAL(clicked()), this, SLOT(forward()));
	connect(_cancel, SIGNAL(clicked()), this, SLOT(startHide()));
	subscribe(Global::RefPeerChooseCancel(), [this] { startHide(); });

	_chooseWidth = st::historyForwardChooseFont->width(lang(_botAndQuery.isEmpty() ? lng_forward_choose : lng_inline_switch_choose));

	resizeEvent(0);
	_a_opacity.start([this] { update(); }, 0., 1., st::boxDuration);
}

void HistoryHider::refreshLang() {
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

bool HistoryHider::withConfirm() const {
	return _sendPath;
}

void HistoryHider::paintEvent(QPaintEvent *e) {
	Painter p(this);
	auto opacity = _a_opacity.current(getms(), _hiding ? 0. : 1.);
	if (opacity == 0.) {
		if (_hiding) {
			QTimer::singleShot(0, this, SLOT(deleteLater()));
		}
		return;
	}

	p.setOpacity(opacity);
	if (!_hiding || !_cacheForAnim.isNull() || !_offered) {
		p.fillRect(rect(), st::layerBg);
	}
	if (_cacheForAnim.isNull() || !_offered) {
		p.setFont(st::historyForwardChooseFont);
		if (_offered) {
			Ui::Shadow::paint(p, _box, width(), st::boxRoundShadow);
			App::roundRect(p, _box, st::boxBg, BoxCorners);

			p.setPen(st::boxTextFg);
			_toText.drawLeftElided(p, _box.left() + st::boxPadding.left(), _box.y() + st::boxTopMargin + st::boxPadding.top(), _toTextWidth + 2, width(), 1, style::al_left);
		} else {
			auto w = st::historyForwardChooseMargins.left() + _chooseWidth + st::historyForwardChooseMargins.right();
			auto h = st::historyForwardChooseMargins.top() + st::historyForwardChooseFont->height + st::historyForwardChooseMargins.bottom();
			App::roundRect(p, (width() - w) / 2, (height() - h) / 2, w, h, st::historyForwardChooseBg, ForwardCorners);

			p.setPen(st::historyForwardChooseFg);
			p.drawText(_box, lang(_botAndQuery.isEmpty() ? lng_forward_choose : lng_inline_switch_choose), QTextOption(style::al_center));
		}
	} else {
		p.drawPixmap(_box.left(), _box.top(), _cacheForAnim);
	}
}

void HistoryHider::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		if (_offered) {
			_offered = nullptr;
			resizeEvent(nullptr);
			update();
			App::main()->dialogsActivate();
		} else {
			startHide();
		}
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_offered) {
			forward();
		}
	}
}

void HistoryHider::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		if (!_box.contains(e->pos())) {
			startHide();
		}
	}
}

void HistoryHider::startHide() {
	if (_hiding) return;
	_hiding = true;
	if (Adaptive::OneColumn()) {
		QTimer::singleShot(0, this, SLOT(deleteLater()));
	} else {
		if (_offered) _cacheForAnim = Ui::GrabWidget(this, _box);
		if (_forwardRequest) MTP::cancel(_forwardRequest);
		_send->hide();
		_cancel->hide();
		_a_opacity.start([this] { animationCallback(); }, 1., 0., st::boxDuration);
	}
}

void HistoryHider::animationCallback() {
	update();
	if (!_a_opacity.animating() && _hiding) {
		QTimer::singleShot(0, this, SLOT(deleteLater()));
	}
}

void HistoryHider::forward() {
	if (!_hiding && _offered) {
		if (_sendPath) {
			parent()->onSendPaths(_offered->id);
		} else if (!_shareUrl.isEmpty()) {
			parent()->shareUrl(_offered, _shareUrl, _shareText);
		} else if (!_botAndQuery.isEmpty()) {
			parent()->onInlineSwitchChosen(_offered->id, _botAndQuery);
		} else {
			parent()->setForwardDraft(_offered->id, std::move(_forwardItems));
		}
	}
	emit forwarded();
}

void HistoryHider::forwardDone() {
	_forwardRequest = 0;
	startHide();
}

MainWidget *HistoryHider::parent() {
	return static_cast<MainWidget*>(parentWidget());
}

void HistoryHider::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void HistoryHider::updateControlsGeometry() {
	auto w = st::boxWidth;
	auto h = st::boxPadding.top() + st::boxPadding.bottom();
	if (_offered) {
		if (!_hiding) {
			_send->show();
			_cancel->show();
		}
		h += st::boxTopMargin + qMax(st::boxTextFont->height, st::boxLabelStyle.lineHeight) + st::boxButtonPadding.top() + _send->height() + st::boxButtonPadding.bottom();
	} else {
		h += st::historyForwardChooseFont->height;
		_send->hide();
		_cancel->hide();
	}
	_box = QRect((width() - w) / 2, (height() - h) / 2, w, h);
	_send->moveToRight(width() - (_box.x() + _box.width()) + st::boxButtonPadding.right(), _box.y() + _box.height() - st::boxButtonPadding.bottom() - _send->height());
	_cancel->moveToRight(width() - (_box.x() + _box.width()) + st::boxButtonPadding.right() + _send->width() + st::boxButtonPadding.left(), _send->y());
}

bool HistoryHider::offerPeer(PeerId peer) {
	if (!peer) {
		_offered = nullptr;
		_toText.setText(st::boxLabelStyle, QString());
		_toTextWidth = 0;
		resizeEvent(nullptr);
		return false;
	}
	_offered = App::peer(peer);
	auto phrase = QString();
	auto recipient = _offered->isUser() ? _offered->name : '\xAB' + _offered->name + '\xBB';
	if (_sendPath) {
		auto toId = _offered->id;
		_offered = nullptr;
		if (parent()->onSendPaths(toId)) {
			startHide();
		}
		return false;
	} else if (!_shareUrl.isEmpty()) {
		auto offered = base::take(_offered);
		if (parent()->shareUrl(offered, _shareUrl, _shareText)) {
			startHide();
		}
		return false;
	} else if (!_botAndQuery.isEmpty()) {
		auto toId = _offered->id;
		_offered = nullptr;
		if (parent()->onInlineSwitchChosen(toId, _botAndQuery)) {
			startHide();
		}
		return false;
	} else {
		auto toId = _offered->id;
		_offered = nullptr;
		if (parent()->setForwardDraft(toId, std::move(_forwardItems))) {
			startHide();
		}
		return false;
	}

	_toText.setText(st::boxLabelStyle, phrase, Ui::NameTextOptions());
	_toTextWidth = _toText.maxWidth();
	if (_toTextWidth > _box.width() - st::boxPadding.left() - st::boxLayerButtonPadding.right()) {
		_toTextWidth = _box.width() - st::boxPadding.left() - st::boxLayerButtonPadding.right();
	}

	resizeEvent(nullptr);
	update();
	setFocus();

	return true;
}

QString HistoryHider::offeredText() const {
	return _toText.originalText();
}

bool HistoryHider::wasOffered() const {
	return _offered != nullptr;
}

HistoryHider::~HistoryHider() {
	if (_sendPath) cSetSendPaths(QStringList());
	parent()->noHider(this);
}

HistoryWidget::HistoryWidget(
	QWidget *parent,
	not_null<Window::Controller*> controller)
: Window::AbstractSectionWidget(parent, controller)
, _fieldBarCancel(this, st::historyReplyCancel)
, _topBar(this, controller)
, _scroll(this, st::historyScroll, false)
, _historyDown(_scroll, st::historyToDown)
, _unreadMentions(_scroll, st::historyUnreadMentions)
, _fieldAutocomplete(this)
, _send(this)
, _unblock(this, lang(lng_unblock_button).toUpper(), st::historyUnblock)
, _botStart(this, lang(lng_bot_start).toUpper(), st::historyComposeButton)
, _joinChannel(this, lang(lng_profile_join_channel).toUpper(), st::historyComposeButton)
, _muteUnmute(this, lang(lng_channel_mute).toUpper(), st::historyComposeButton)
, _attachToggle(this, st::historyAttach)
, _tabbedSelectorToggle(this, st::historyAttachEmoji)
, _botKeyboardShow(this, st::historyBotKeyboardShow)
, _botKeyboardHide(this, st::historyBotKeyboardHide)
, _botCommandStart(this, st::historyBotCommandStart)
, _field(this, controller, st::historyComposeField, langFactory(lng_message_ph))
, _recordCancelWidth(st::historyRecordFont->width(lang(lng_record_cancel)))
, _a_recording(animation(this, &HistoryWidget::step_recording))
, _kbScroll(this, st::botKbScroll)
, _tabbedPanel(this, controller)
, _tabbedSelector(_tabbedPanel->getSelector())
, _attachDragState(DragState::None)
, _attachDragDocument(this)
, _attachDragPhoto(this)
, _sendActionStopTimer([this] { cancelTypingAction(); })
, _topShadow(this) {
	setAcceptDrops(true);

	subscribe(Auth().downloaderTaskFinished(), [this] { update(); });
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	_historyDown->setClickedCallback([this] { historyDownClicked(); });
	_unreadMentions->setClickedCallback([this] { showNextUnreadMention(); });
	connect(_fieldBarCancel, SIGNAL(clicked()), this, SLOT(onFieldBarCancel()));
	_send->setClickedCallback([this] { sendButtonClicked(); });
	connect(_unblock, SIGNAL(clicked()), this, SLOT(onUnblock()));
	connect(_botStart, SIGNAL(clicked()), this, SLOT(onBotStart()));
	connect(_joinChannel, SIGNAL(clicked()), this, SLOT(onJoinChannel()));
	connect(_muteUnmute, SIGNAL(clicked()), this, SLOT(onMuteUnmute()));
	connect(_field, SIGNAL(submitted(bool)), this, SLOT(onSend(bool)));
	connect(_field, SIGNAL(cancelled()), this, SLOT(onCancel()));
	connect(_field, SIGNAL(tabbed()), this, SLOT(onFieldTabbed()));
	connect(_field, SIGNAL(resized()), this, SLOT(onFieldResize()));
	connect(_field, SIGNAL(focused()), this, SLOT(onFieldFocused()));
	connect(_field, SIGNAL(changed()), this, SLOT(onTextChange()));
	connect(_field, SIGNAL(spacedReturnedPasted()), this, SLOT(onPreviewParse()));
	connect(_field, SIGNAL(linksChanged()), this, SLOT(onPreviewCheck()));
	connect(App::wnd()->windowHandle(), SIGNAL(visibleChanged(bool)), this, SLOT(onWindowVisibleChanged()));
	connect(&_scrollTimer, SIGNAL(timeout()), this, SLOT(onScrollTimer()));
	connect(_tabbedSelector, SIGNAL(emojiSelected(EmojiPtr)), _field, SLOT(onEmojiInsert(EmojiPtr)));
	connect(_tabbedSelector, SIGNAL(stickerSelected(DocumentData*)), this, SLOT(onStickerSend(DocumentData*)));
	connect(_tabbedSelector, SIGNAL(photoSelected(PhotoData*)), this, SLOT(onPhotoSend(PhotoData*)));
	connect(_tabbedSelector, SIGNAL(inlineResultSelected(InlineBots::Result*,UserData*)), this, SLOT(onInlineResultSend(InlineBots::Result*,UserData*)));
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreviewTimeout()));
	connect(Media::Capture::instance(), SIGNAL(error()), this, SLOT(onRecordError()));
	connect(Media::Capture::instance(), SIGNAL(updated(quint16,qint32)), this, SLOT(onRecordUpdate(quint16,qint32)));
	connect(Media::Capture::instance(), SIGNAL(done(QByteArray,VoiceWaveform,qint32)), this, SLOT(onRecordDone(QByteArray,VoiceWaveform,qint32)));

	_attachToggle->setClickedCallback(App::LambdaDelayed(st::historyAttach.ripple.hideDuration, this, [this] {
		chooseAttach();
	}));

	_updateHistoryItems.setSingleShot(true);
	connect(&_updateHistoryItems, SIGNAL(timeout()), this, SLOT(onUpdateHistoryItems()));

	_scrollTimer.setSingleShot(false);

	_highlightTimer.setCallback([this] { updateHighlightedMessage(); });

	_membersDropdownShowTimer.setSingleShot(true);
	connect(&_membersDropdownShowTimer, SIGNAL(timeout()), this, SLOT(onMembersDropdownShow()));

	_saveDraftTimer.setSingleShot(true);
	connect(&_saveDraftTimer, SIGNAL(timeout()), this, SLOT(onDraftSave()));
	_saveCloudDraftTimer.setSingleShot(true);
	connect(&_saveCloudDraftTimer, SIGNAL(timeout()), this, SLOT(onCloudDraftSave()));
	connect(_field->verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(onDraftSaveDelayed()));
	connect(_field, SIGNAL(cursorPositionChanged()), this, SLOT(onDraftSaveDelayed()));
	connect(_field, SIGNAL(cursorPositionChanged()), this, SLOT(onCheckFieldAutocomplete()), Qt::QueuedConnection);

	_fieldBarCancel->hide();

	_topBar->hide();
	_scroll->hide();

	_keyboard = _kbScroll->setOwnedWidget(object_ptr<BotKeyboard>(this));
	_kbScroll->hide();

	updateScrollColors();

	_historyDown->installEventFilter(this);
	_unreadMentions->installEventFilter(this);

	_fieldAutocomplete->hide();
	connect(_fieldAutocomplete, SIGNAL(mentionChosen(UserData*,FieldAutocomplete::ChooseMethod)), this, SLOT(onMentionInsert(UserData*)));
	connect(_fieldAutocomplete, SIGNAL(hashtagChosen(QString,FieldAutocomplete::ChooseMethod)), this, SLOT(onHashtagOrBotCommandInsert(QString,FieldAutocomplete::ChooseMethod)));
	connect(_fieldAutocomplete, SIGNAL(botCommandChosen(QString,FieldAutocomplete::ChooseMethod)), this, SLOT(onHashtagOrBotCommandInsert(QString,FieldAutocomplete::ChooseMethod)));
	connect(_fieldAutocomplete, SIGNAL(stickerChosen(DocumentData*,FieldAutocomplete::ChooseMethod)), this, SLOT(onStickerSend(DocumentData*)));
	connect(_fieldAutocomplete, SIGNAL(moderateKeyActivate(int,bool*)), this, SLOT(onModerateKeyActivate(int,bool*)));
	_field->installEventFilter(_fieldAutocomplete);
	_field->setInsertFromMimeDataHook([this](const QMimeData *data) {
		return confirmSendingFiles(
			data,
			CompressConfirm::Auto,
			data->text());
	});
	_emojiSuggestions.create(this, _field.data());
	_emojiSuggestions->setReplaceCallback([=](
			int from,
			int till,
			const QString &replacement) {
		_field->commmitInstantReplacement(from, till, replacement);
	});
	updateFieldSubmitSettings();

	_field->hide();
	_send->hide();
	_unblock->hide();
	_botStart->hide();
	_joinChannel->hide();
	_muteUnmute->hide();

	_send->setRecordStartCallback([this] { recordStartCallback(); });
	_send->setRecordStopCallback([this](bool active) { recordStopCallback(active); });
	_send->setRecordUpdateCallback([this](QPoint globalPos) { recordUpdateCallback(globalPos); });
	_send->setRecordAnimationCallback([this] { updateField(); });

	_attachToggle->hide();
	_tabbedSelectorToggle->hide();
	_botKeyboardShow->hide();
	_botKeyboardHide->hide();
	_botCommandStart->hide();

	_tabbedSelectorToggle->installEventFilter(_tabbedPanel);
	_tabbedSelectorToggle->setClickedCallback([this] {
		toggleTabbedSelectorMode();
	});

	connect(_botKeyboardShow, SIGNAL(clicked()), this, SLOT(onKbToggle()));
	connect(_botKeyboardHide, SIGNAL(clicked()), this, SLOT(onKbToggle()));
	connect(_botCommandStart, SIGNAL(clicked()), this, SLOT(onCmdStart()));

	_tabbedPanel->hide();
	_attachDragDocument->hide();
	_attachDragPhoto->hide();

	_topShadow->hide();

	_attachDragDocument->setDroppedCallback([this](const QMimeData *data) {
		confirmSendingFiles(data, CompressConfirm::No);
		ActivateWindowDelayed(this->controller());
	});
	_attachDragPhoto->setDroppedCallback([this](const QMimeData *data) {
		confirmSendingFiles(data, CompressConfirm::Yes);
		ActivateWindowDelayed(this->controller());
	});

	connect(&_updateEditTimeLeftDisplay, SIGNAL(timeout()), this, SLOT(updateField()));

	subscribe(Adaptive::Changed(), [this] { update(); });
	Auth().data().itemRemoved(
	) | rpl::start_with_next(
		[this](auto item) { itemRemoved(item); },
		lifetime());
	Auth().data().historyChanged(
	) | rpl::start_with_next(
		[=](auto history) { handleHistoryChange(history); },
		lifetime());
	Auth().data().viewResizeRequest(
	) | rpl::start_with_next([this](auto view) {
		if (view->data()->mainView() == view) {
			updateHistoryGeometry();
		}
	}, lifetime());
	Auth().data().itemViewRefreshRequest(
	) | rpl::start_with_next([this](auto item) {
		// While HistoryInner doesn't own item views we must refresh them
		// even if the list is not yet created / was destroyed.
		if (!_list) {
			item->refreshMainView();
		}
	}, lifetime());
	Auth().data().animationPlayInlineRequest(
	) | rpl::start_with_next([=](auto item) {
		if (const auto view = item->mainView()) {
			if (const auto media = view->media()) {
				media->playAnimation();
			}
		}
	}, lifetime());
	subscribe(Auth().data().contactsLoaded(), [this](bool) {
		if (_peer) {
			updateReportSpamStatus();
			updateControlsVisibility();
		}
	});
	subscribe(Media::Player::instance()->switchToNextNotifier(), [this](const Media::Player::Instance::Switch &pair) {
		if (pair.from.type() == AudioMsgId::Type::Voice) {
			scrollToCurrentVoiceMessage(pair.from.contextId(), pair.to);
		}
	});
	using UpdateFlag = Notify::PeerUpdate::Flag;
	auto changes = UpdateFlag::ChannelRightsChanged
		| UpdateFlag::UnreadMentionsChanged
		| UpdateFlag::MigrationChanged
		| UpdateFlag::RestrictionReasonChanged
		| UpdateFlag::ChannelPinnedChanged
		| UpdateFlag::UserIsBlocked
		| UpdateFlag::AdminsChanged
		| UpdateFlag::MembersChanged
		| UpdateFlag::UserOnlineChanged
		| UpdateFlag::NotificationsEnabled
		| UpdateFlag::ChannelAmIn;
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(changes, [this](const Notify::PeerUpdate &update) {
		if (update.peer == _peer) {
			if (update.flags & UpdateFlag::ChannelRightsChanged) {
				onPreviewCheck();
			}
			if (update.flags & UpdateFlag::UnreadMentionsChanged) {
				updateUnreadMentionsVisibility();
			}
			if (update.flags & UpdateFlag::MigrationChanged) {
				if (auto channel = _peer->migrateTo()) {
					Ui::showPeerHistory(channel, ShowAtUnreadMsgId);
					Auth().api().requestParticipantsCountDelayed(channel);
					return;
				}
			}
			if (update.flags & UpdateFlag::NotificationsEnabled) {
				updateNotifySettings();
			}
			if (update.flags & UpdateFlag::RestrictionReasonChanged) {
				auto restriction = _peer->restrictionReason();
				if (!restriction.isEmpty()) {
					this->controller()->showBackFromStack();
					Ui::show(Box<InformBox>(restriction));
					return;
				}
			}
			if (update.flags & UpdateFlag::ChannelPinnedChanged) {
				if (pinnedMsgVisibilityUpdated()) {
					updateHistoryGeometry();
					updateControlsVisibility();
					updateControlsGeometry();
					this->update();
				}
			}
			if (update.flags & (UpdateFlag::UserIsBlocked
				| UpdateFlag::AdminsChanged
				| UpdateFlag::MembersChanged
				| UpdateFlag::UserOnlineChanged
				| UpdateFlag::ChannelAmIn)) {
				handlePeerUpdate();
			}
		}
	}));
	subscribe(Auth().data().queryItemVisibility(), [this](const Data::Session::ItemVisibilityQuery &query) {
		if (_a_show.animating()
			|| _history != query.item->history()
			|| !query.item->mainView() || !isVisible()) {
			return;
		}
		if (const auto view = query.item->mainView()) {
			auto top = _list->itemTop(view);
			if (top >= 0) {
				auto scrollTop = _scroll->scrollTop();
				if (top + view->height() > scrollTop && top < scrollTop + _scroll->height()) {
					*query.isVisible = true;
				}
			}
		}
	});
	_topBar->membersShowAreaActive(
	) | rpl::start_with_next([=](bool active) {
		setMembersShowAreaActive(active);
	}, _topBar->lifetime());
	_topBar->forwardSelectionRequest(
	) | rpl::start_with_next([=] {
		forwardSelected();
	}, _topBar->lifetime());
	_topBar->deleteSelectionRequest(
	) | rpl::start_with_next([=] {
		confirmDeleteSelected();
	}, _topBar->lifetime());
	_topBar->clearSelectionRequest(
	) | rpl::start_with_next([=] {
		clearSelected();
	}, _topBar->lifetime());

	Auth().api().sendActions(
	) | rpl::start_with_next([this](const ApiWrap::SendOptions &options) {
		fastShowAtEnd(options.history);
		const auto lastKeyboardUsed = lastForceReplyReplied(FullMsgId(
			options.history->channelId(),
			options.replyTo));
		if (cancelReply(lastKeyboardUsed) && !options.clearDraft) {
			onCloudDraftSave();
		}
	}, lifetime());

	orderWidgets();
}

void HistoryWidget::scrollToCurrentVoiceMessage(FullMsgId fromId, FullMsgId toId) {
	if (getms() <= _lastUserScrolled + kScrollToVoiceAfterScrolledMs) {
		return;
	}
	if (!_list) {
		return;
	}

	auto from = App::histItemById(fromId);
	auto to = App::histItemById(toId);
	if (!from || !to) {
		return;
	}

	// If history has pending resize items, the scrollTopItem won't be updated.
	// And the scrollTop will be reset back to scrollTopItem + scrollTopOffset.
	handlePendingHistoryUpdate();

	if (const auto toView = to->mainView()) {
		auto toTop = _list->itemTop(toView);
		if (toTop >= 0 && !isItemCompletelyHidden(from)) {
			auto scrollTop = _scroll->scrollTop();
			auto scrollBottom = scrollTop + _scroll->height();
			auto toBottom = toTop + toView->height();
			if ((toTop < scrollTop && toBottom < scrollBottom) || (toTop > scrollTop && toBottom > scrollBottom)) {
				animatedScrollToItem(to->id);
			}
		}
	}
}

void HistoryWidget::animatedScrollToItem(MsgId msgId) {
	Expects(_history != nullptr);
	if (hasPendingResizedItems()) {
		updateListSize();
	}

	auto to = App::histItemById(_channel, msgId);
	if (_list->itemTop(to) < 0) {
		return;
	}

	auto scrollTo = snap(
		itemTopForHighlight(to->mainView()),
		0,
		_scroll->scrollTopMax());
	animatedScrollToY(scrollTo, to);
}

void HistoryWidget::animatedScrollToY(int scrollTo, HistoryItem *attachTo) {
	Expects(_history != nullptr);
	if (hasPendingResizedItems()) {
		updateListSize();
	}

	// Attach our scroll animation to some item.
	auto itemTop = _list->itemTop(attachTo);
	auto scrollTop = _scroll->scrollTop();
	if (itemTop < 0 && !_history->isEmpty()) {
		attachTo = _history->blocks.back()->messages.back()->data();
		itemTop = _list->itemTop(attachTo);
	}
	if (itemTop < 0 || (scrollTop == scrollTo)) {
		synteticScrollToY(scrollTo);
		return;
	}

	_scrollToAnimation.finish();
	auto maxAnimatedDelta = _scroll->height();
	auto transition = anim::sineInOut;
	if (scrollTo > scrollTop + maxAnimatedDelta) {
		scrollTop = scrollTo - maxAnimatedDelta;
		synteticScrollToY(scrollTop);
		transition = anim::easeOutCubic;
	} else if (scrollTo + maxAnimatedDelta < scrollTop) {
		scrollTop = scrollTo + maxAnimatedDelta;
		synteticScrollToY(scrollTop);
		transition = anim::easeOutCubic;
	} else {
		// In local showHistory() we forget current scroll state,
		// so we need to restore it synchronously, otherwise we may
		// jump to the bottom of history in some updateHistoryGeometry() call.
		synteticScrollToY(scrollTop);
	}
	_scrollToAnimation.start([this, itemId = attachTo->fullId()] { scrollToAnimationCallback(itemId); }, scrollTop - itemTop, scrollTo - itemTop, st::slideDuration, anim::sineInOut);
}

void HistoryWidget::scrollToAnimationCallback(FullMsgId attachToId) {
	auto itemTop = _list->itemTop(App::histItemById(attachToId));
	if (itemTop < 0) {
		_scrollToAnimation.finish();
	} else {
		synteticScrollToY(qRound(_scrollToAnimation.current()) + itemTop);
	}
	if (!_scrollToAnimation.animating()) {
		preloadHistoryByScroll();
		checkReplyReturns();
	}
}

void HistoryWidget::enqueueMessageHighlight(
		not_null<HistoryView::Element*> view) {
	if (const auto group = Auth().data().groups().find(view->data())) {
		if (const auto leader = group->items.back()->mainView()) {
			view = leader;
		}
	}
	auto enqueueMessageId = [this](MsgId universalId) {
		if (_highlightQueue.empty() && !_highlightTimer.isActive()) {
			highlightMessage(universalId);
		} else if (_highlightedMessageId != universalId
			&& !base::contains(_highlightQueue, universalId)) {
			_highlightQueue.push_back(universalId);
			checkNextHighlight();
		}
	};
	const auto item = view->data();
	if (item->history() == _history) {
		enqueueMessageId(item->id);
	} else if (item->history() == _migrated) {
		enqueueMessageId(-item->id);
	}
}

void HistoryWidget::highlightMessage(MsgId universalMessageId) {
	_highlightStart = getms();
	_highlightedMessageId = universalMessageId;
	_highlightTimer.callEach(AnimationTimerDelta);

	adjustHighlightedMessageToMigrated();
}

void HistoryWidget::adjustHighlightedMessageToMigrated() {
	if (_history
		&& _highlightTimer.isActive()
		&& _highlightedMessageId > 0
		&& _migrated
		&& !_migrated->isEmpty()
		&& _migrated->loadedAtBottom()
		&& _migrated->blocks.back()->messages.back()->data()->isGroupMigrate()
		&& _list->historyTop() != _list->historyDrawTop()) {
		auto highlighted = App::histItemById(
			_history->channelId(),
			_highlightedMessageId);
		if (highlighted && highlighted->isGroupMigrate()) {
			_highlightedMessageId = -_migrated->blocks.back()->messages.back()->data()->id;
		}
	}
}

void HistoryWidget::checkNextHighlight() {
	if (_highlightTimer.isActive()) {
		return;
	}
	auto nextHighlight = [this] {
		while (!_highlightQueue.empty()) {
			auto msgId = _highlightQueue.front();
			_highlightQueue.pop_front();
			auto item = getItemFromHistoryOrMigrated(msgId);
			if (item && item->mainView()) {
				return msgId;
			}
		}
		return 0;
	}();
	if (!nextHighlight) {
		return;
	}
	highlightMessage(nextHighlight);
}

void HistoryWidget::updateHighlightedMessage() {
	const auto item = getItemFromHistoryOrMigrated(_highlightedMessageId);
	const auto view = item ? item->mainView() : nullptr;
	if (!view) {
		return stopMessageHighlight();
	}
	auto duration = st::activeFadeInDuration + st::activeFadeOutDuration;
	if (getms() - _highlightStart > duration) {
		return stopMessageHighlight();
	}

	Auth().data().requestViewRepaint(view);
}

TimeMs HistoryWidget::highlightStartTime(not_null<const HistoryItem*> item) const {
	auto isHighlighted = [this](not_null<const HistoryItem*> item) {
		if (item->id == _highlightedMessageId) {
			return (item->history() == _history);
		} else if (item->id == -_highlightedMessageId) {
			return (item->history() == _migrated);
		}
		return false;
	};
	return (isHighlighted(item) && _highlightTimer.isActive())
		? _highlightStart
		: 0;
}

void HistoryWidget::stopMessageHighlight() {
	_highlightTimer.cancel();
	_highlightedMessageId = 0;
	checkNextHighlight();
}

void HistoryWidget::clearHighlightMessages() {
	_highlightQueue.clear();
	stopMessageHighlight();
}

int HistoryWidget::itemTopForHighlight(
		not_null<HistoryView::Element*> view) const {
	if (const auto group = Auth().data().groups().find(view->data())) {
		if (const auto leader = group->items.back()->mainView()) {
			view = leader;
		}
	}
	auto itemTop = _list->itemTop(view);
	Assert(itemTop >= 0);

	auto heightLeft = (_scroll->height() - view->height());
	if (heightLeft <= 0) {
		return itemTop;
	}
	return qMax(itemTop - (heightLeft / 2), 0);
}

bool HistoryWidget::inSelectionMode() const {
	return _list ? _list->inSelectionMode() : false;
}

void HistoryWidget::start() {
	Auth().data().stickersUpdated(
	) | rpl::start_with_next([this] {
		_tabbedSelector->refreshStickers();
		updateStickersByEmoji();
	}, lifetime());
	updateRecentStickers();
	Auth().data().notifySavedGifsUpdated();
	subscribe(Auth().api().fullPeerUpdated(), [this](PeerData *peer) {
		fullPeerUpdated(peer);
	});
}

void HistoryWidget::onMentionInsert(UserData *user) {
	QString replacement, entityTag;
	if (user->username.isEmpty()) {
		replacement = user->firstName;
		if (replacement.isEmpty()) {
			replacement = App::peerName(user);
		}
		entityTag = qsl("mention://user.")
			+ QString::number(user->bareId())
			+ '.'
			+ QString::number(user->accessHash());
	} else {
		replacement = '@' + user->username;
	}
	_field->insertTag(replacement, entityTag);
}

void HistoryWidget::onHashtagOrBotCommandInsert(
		QString str,
		FieldAutocomplete::ChooseMethod method) {
	if (!_peer) {
		return;
	}

	// Send bot command at once, if it was not inserted by pressing Tab.
	if (str.at(0) == '/' && method != FieldAutocomplete::ChooseMethod::ByTab) {
		App::sendBotCommand(_peer, nullptr, str, replyToId());
		App::main()->finishForwarding(_history);
		setFieldText(_field->getTextWithTagsPart(_field->textCursor().position()));
	} else {
		_field->insertTag(str);
	}
}

void HistoryWidget::updateInlineBotQuery() {
	UserData *bot = nullptr;
	QString inlineBotUsername;
	QString query = _field->getInlineBotQuery(&bot, &inlineBotUsername);
	if (inlineBotUsername != _inlineBotUsername) {
		_inlineBotUsername = inlineBotUsername;
		if (_inlineBotResolveRequestId) {
//			Notify::inlineBotRequesting(false);
			MTP::cancel(_inlineBotResolveRequestId);
			_inlineBotResolveRequestId = 0;
		}
		if (bot == Ui::LookingUpInlineBot) {
			_inlineBot = Ui::LookingUpInlineBot;
//			Notify::inlineBotRequesting(true);
			_inlineBotResolveRequestId = MTP::send(MTPcontacts_ResolveUsername(MTP_string(_inlineBotUsername)), rpcDone(&HistoryWidget::inlineBotResolveDone), rpcFail(&HistoryWidget::inlineBotResolveFail, _inlineBotUsername));
			return;
		}
	} else if (bot == Ui::LookingUpInlineBot) {
		if (_inlineBot == Ui::LookingUpInlineBot) {
			return;
		}
		bot = _inlineBot;
	}

	applyInlineBotQuery(bot, query);
}

void HistoryWidget::applyInlineBotQuery(UserData *bot, const QString &query) {
	if (bot) {
		if (_inlineBot != bot) {
			_inlineBot = bot;
			inlineBotChanged();
		}
		if (!_inlineResults) {
			_inlineResults.create(this, controller());
			_inlineResults->setResultSelectedCallback([this](InlineBots::Result *result, UserData *bot) {
				onInlineResultSend(result, bot);
			});
			updateControlsGeometry();
			orderWidgets();
		}
		_inlineResults->queryInlineBot(_inlineBot, _peer, query);
		if (!_fieldAutocomplete->isHidden()) {
			_fieldAutocomplete->hideAnimated();
		}
	} else {
		clearInlineBot();
	}
}

void HistoryWidget::orderWidgets() {
	if (_reportSpamPanel) {
		_reportSpamPanel->raise();
	}
	_topShadow->raise();
	if (_membersDropdown) {
		_membersDropdown->raise();
	}
	if (_inlineResults) {
		_inlineResults->raise();
	}
	if (_tabbedPanel) {
		_tabbedPanel->raise();
	}
	_emojiSuggestions->raise();
	if (_tabbedSelectorToggleTooltip) {
		_tabbedSelectorToggleTooltip->raise();
	}
	_attachDragDocument->raise();
	_attachDragPhoto->raise();
}

void HistoryWidget::setReportSpamStatus(DBIPeerReportSpamStatus status) {
	if (_reportSpamStatus == status) {
		return;
	}
	_reportSpamStatus = status;
	if (_reportSpamStatus == dbiprsShowButton || _reportSpamStatus == dbiprsReportSent) {
		Assert(_peer != nullptr);
		_reportSpamPanel.create(this);
		connect(_reportSpamPanel, SIGNAL(reportClicked()), this, SLOT(onReportSpamClicked()));
		connect(_reportSpamPanel, SIGNAL(hideClicked()), this, SLOT(onReportSpamHide()));
		connect(_reportSpamPanel, SIGNAL(clearClicked()), this, SLOT(onReportSpamClear()));
		_reportSpamPanel->setReported(_reportSpamStatus == dbiprsReportSent, _peer);
		_reportSpamPanel->show();
		orderWidgets();
		updateControlsGeometry();
	} else {
		_reportSpamPanel.destroy();
	}
}

void HistoryWidget::updateStickersByEmoji() {
	int len = 0;
	if (!_editMsgId) {
		auto &text = _field->getTextWithTags().text;
		if (auto emoji = Ui::Emoji::Find(text, &len)) {
			if (text.size() > len) {
				len = 0;
			} else {
				_fieldAutocomplete->showStickers(emoji);
			}
		}
	}
	if (!len) {
		_fieldAutocomplete->showStickers(nullptr);
	}
}

void HistoryWidget::onTextChange() {
	updateInlineBotQuery();
	updateStickersByEmoji();

	if (_history) {
		if (!_inlineBot
			&& !_editMsgId
			&& (_textUpdateEvents & TextUpdateEvent::SendTyping)) {
			updateSendAction(_history, SendAction::Type::Typing);
		}
	}

	updateSendButtonType();
	if (showRecordButton()) {
		_previewCancelled = false;
	}
	if (updateCmdStartShown()) {
		updateControlsVisibility();
		updateControlsGeometry();
	}

	_saveCloudDraftTimer.stop();
	if (!_peer || !(_textUpdateEvents & TextUpdateEvent::SaveDraft)) return;

	_saveDraftText = true;
	onDraftSave(true);
}

void HistoryWidget::onDraftSaveDelayed() {
	if (!_peer || !(_textUpdateEvents & TextUpdateEvent::SaveDraft)) return;
	if (!_field->textCursor().anchor() && !_field->textCursor().position() && !_field->verticalScrollBar()->value()) {
		if (!Local::hasDraftCursors(_peer->id)) {
			return;
		}
	}
	onDraftSave(true);
}

void HistoryWidget::onDraftSave(bool delayed) {
	if (!_peer) return;
	if (delayed) {
		auto ms = getms();
		if (!_saveDraftStart) {
			_saveDraftStart = ms;
			return _saveDraftTimer.start(SaveDraftTimeout);
		} else if (ms - _saveDraftStart < SaveDraftAnywayTimeout) {
			return _saveDraftTimer.start(SaveDraftTimeout);
		}
	}
	writeDrafts(nullptr, nullptr);
}

void HistoryWidget::saveFieldToHistoryLocalDraft() {
	if (!_history) return;

	if (_editMsgId) {
		_history->setEditDraft(std::make_unique<Data::Draft>(_field, _editMsgId, _previewCancelled, _saveEditMsgRequestId));
	} else {
		if (_replyToId || !_field->isEmpty()) {
			_history->setLocalDraft(std::make_unique<Data::Draft>(_field, _replyToId, _previewCancelled));
		} else {
			_history->clearLocalDraft();
		}
		_history->clearEditDraft();
	}
}

void HistoryWidget::onCloudDraftSave() {
	if (App::main()) {
		App::main()->saveDraftToCloud();
	}
}

void HistoryWidget::writeDrafts(Data::Draft **localDraft, Data::Draft **editDraft) {
	Data::Draft *historyLocalDraft = _history ? _history->localDraft() : nullptr;
	if (!localDraft && _editMsgId) localDraft = &historyLocalDraft;

	bool save = _peer && (_saveDraftStart > 0);
	_saveDraftStart = 0;
	_saveDraftTimer.stop();
	if (_saveDraftText) {
		if (save) {
			Local::MessageDraft storedLocalDraft, storedEditDraft;
			if (localDraft) {
				if (*localDraft) {
					storedLocalDraft = Local::MessageDraft((*localDraft)->msgId, (*localDraft)->textWithTags, (*localDraft)->previewCancelled);
				}
			} else {
				storedLocalDraft = Local::MessageDraft(_replyToId, _field->getTextWithTags(), _previewCancelled);
			}
			if (editDraft) {
				if (*editDraft) {
					storedEditDraft = Local::MessageDraft((*editDraft)->msgId, (*editDraft)->textWithTags, (*editDraft)->previewCancelled);
				}
			} else if (_editMsgId) {
				storedEditDraft = Local::MessageDraft(_editMsgId, _field->getTextWithTags(), _previewCancelled);
			}
			Local::writeDrafts(_peer->id, storedLocalDraft, storedEditDraft);
			if (_migrated) {
				Local::writeDrafts(_migrated->peer->id, Local::MessageDraft(), Local::MessageDraft());
			}
		}
		_saveDraftText = false;
	}
	if (save) {
		MessageCursor localCursor, editCursor;
		if (localDraft) {
			if (*localDraft) {
				localCursor = (*localDraft)->cursor;
			}
		} else {
			localCursor = MessageCursor(_field);
		}
		if (editDraft) {
			if (*editDraft) {
				editCursor = (*editDraft)->cursor;
			}
		} else if (_editMsgId) {
			editCursor = MessageCursor(_field);
		}
		Local::writeDraftCursors(_peer->id, localCursor, editCursor);
		if (_migrated) {
			Local::writeDraftCursors(_migrated->peer->id, MessageCursor(), MessageCursor());
		}
	}

	if (!_editMsgId && !_inlineBot) {
		_saveCloudDraftTimer.start(SaveCloudDraftIdleTimeout);
	}
}

void HistoryWidget::cancelSendAction(
		not_null<History*> history,
		SendAction::Type type) {
	auto i = _sendActionRequests.find(qMakePair(history, type));
	if (i != _sendActionRequests.cend()) {
		MTP::cancel(i.value());
		_sendActionRequests.erase(i);
	}
}

void HistoryWidget::cancelTypingAction() {
	if (_history) {
		cancelSendAction(_history, SendAction::Type::Typing);
	}
	_sendActionStopTimer.cancel();
}

void HistoryWidget::updateSendAction(
		not_null<History*> history,
		SendAction::Type type,
		int32 progress) {
	const auto peer = history->peer;
	if (peer->isSelf() || (peer->isChannel() && !peer->isMegagroup())) {
		return;
	}

	const auto doing = (progress >= 0);
	if (history->mySendActionUpdated(type, doing)) {
		cancelSendAction(history, type);
		if (doing) {
			using Type = SendAction::Type;
			MTPsendMessageAction action;
			switch (type) {
			case Type::Typing: action = MTP_sendMessageTypingAction(); break;
			case Type::RecordVideo: action = MTP_sendMessageRecordVideoAction(); break;
			case Type::UploadVideo: action = MTP_sendMessageUploadVideoAction(MTP_int(progress)); break;
			case Type::RecordVoice: action = MTP_sendMessageRecordAudioAction(); break;
			case Type::UploadVoice: action = MTP_sendMessageUploadAudioAction(MTP_int(progress)); break;
			case Type::RecordRound: action = MTP_sendMessageRecordRoundAction(); break;
			case Type::UploadRound: action = MTP_sendMessageUploadRoundAction(MTP_int(progress)); break;
			case Type::UploadPhoto: action = MTP_sendMessageUploadPhotoAction(MTP_int(progress)); break;
			case Type::UploadFile: action = MTP_sendMessageUploadDocumentAction(MTP_int(progress)); break;
			case Type::ChooseLocation: action = MTP_sendMessageGeoLocationAction(); break;
			case Type::ChooseContact: action = MTP_sendMessageChooseContactAction(); break;
			case Type::PlayGame: action = MTP_sendMessageGamePlayAction(); break;
			}
			const auto key = qMakePair(history, type);
			const auto requestId = MTP::send(
				MTPmessages_SetTyping(
					peer->input,
					action),
				rpcDone(&HistoryWidget::sendActionDone));
			_sendActionRequests.insert(key, requestId);
			if (type == Type::Typing) {
				_sendActionStopTimer.callOnce(kCancelTypingActionTimeout);
			}
		}
	}
}

void HistoryWidget::updateRecentStickers() {
	_tabbedSelector->refreshStickers();
}

void HistoryWidget::sendActionDone(const MTPBool &result, mtpRequestId req) {
	for (auto i = _sendActionRequests.begin(), e = _sendActionRequests.end(); i != e; ++i) {
		if (i.value() == req) {
			_sendActionRequests.erase(i);
			break;
		}
	}
}

void HistoryWidget::activate() {
	if (_history) {
		if (!_historyInited) {
			updateHistoryGeometry(true);
		} else if (hasPendingResizedItems()) {
			updateHistoryGeometry();
		}
	}
	if (App::wnd()) App::wnd()->setInnerFocus();
}

void HistoryWidget::setInnerFocus() {
	if (_scroll->isHidden()) {
		setFocus();
	} else if (_list) {
		if (_nonEmptySelection || (_list && _list->wasSelectedText()) || _recording || isBotStart() || isBlocked() || !_canSendMessages) {
			_list->setFocus();
		} else {
			_field->setFocus();
		}
	}
}

void HistoryWidget::onRecordError() {
	stopRecording(false);
}

void HistoryWidget::onRecordDone(
		QByteArray result,
		VoiceWaveform waveform,
		qint32 samples) {
	if (!canWriteMessage() || result.isEmpty()) return;

	ActivateWindowDelayed(controller());
	const auto duration = samples / Media::Player::kDefaultFrequency;
	auto options = ApiWrap::SendOptions(_history);
	options.replyTo = replyToId();
	Auth().api().sendVoiceMessage(result, waveform, duration, options);
}

void HistoryWidget::onRecordUpdate(quint16 level, qint32 samples) {
	if (!_recording) {
		return;
	}

	a_recordingLevel.start(level);
	_a_recording.start();
	_recordingSamples = samples;
	if (samples < 0 || samples >= Media::Player::kDefaultFrequency * AudioVoiceMsgMaxLength) {
		stopRecording(_peer && samples > 0 && _inField);
	}
	updateField();
	if (_history) {
		updateSendAction(_history, SendAction::Type::RecordVoice);
	}
}

void HistoryWidget::notify_botCommandsChanged(UserData *user) {
	if (_peer && (_peer == user || !_peer->isUser())) {
		if (_fieldAutocomplete->clearFilteredBotCommands()) {
			onCheckFieldAutocomplete();
		}
	}
}

void HistoryWidget::notify_inlineBotRequesting(bool requesting) {
	_tabbedSelectorToggle->setLoading(requesting);
}

void HistoryWidget::notify_replyMarkupUpdated(const HistoryItem *item) {
	if (_keyboard->forMsgId() == item->fullId()) {
		updateBotKeyboard(item->history(), true);
	}
}

void HistoryWidget::notify_inlineKeyboardMoved(const HistoryItem *item, int oldKeyboardTop, int newKeyboardTop) {
	if (_history == item->history() || _migrated == item->history()) {
		if (int move = _list->moveScrollFollowingInlineKeyboard(item, oldKeyboardTop, newKeyboardTop)) {
			_addToScroll = move;
		}
	}
}

bool HistoryWidget::notify_switchInlineBotButtonReceived(const QString &query, UserData *samePeerBot, MsgId samePeerReplyTo) {
	if (samePeerBot) {
		if (_history) {
			TextWithTags textWithTags = { '@' + samePeerBot->username + ' ' + query, TextWithTags::Tags() };
			MessageCursor cursor = { textWithTags.text.size(), textWithTags.text.size(), QFIXED_MAX };
			auto replyTo = _history->peer->isUser() ? 0 : samePeerReplyTo;
			_history->setLocalDraft(std::make_unique<Data::Draft>(textWithTags, replyTo, cursor, false));
			applyDraft();
			return true;
		}
	} else if (auto bot = _peer ? _peer->asUser() : nullptr) {
		const auto toPeerId = bot->botInfo
			? bot->botInfo->inlineReturnPeerId
			: PeerId(0);
		if (!toPeerId) {
			return false;
		}
		bot->botInfo->inlineReturnPeerId = 0;
		History *h = App::history(toPeerId);
		TextWithTags textWithTags = { '@' + bot->username + ' ' + query, TextWithTags::Tags() };
		MessageCursor cursor = { textWithTags.text.size(), textWithTags.text.size(), QFIXED_MAX };
		h->setLocalDraft(std::make_unique<Data::Draft>(textWithTags, 0, cursor, false));
		if (h == _history) {
			applyDraft();
		} else {
			Ui::showPeerHistory(toPeerId, ShowAtUnreadMsgId);
		}
		return true;
	}
	return false;
}

void HistoryWidget::notify_userIsBotChanged(UserData *user) {
	if (_peer && _peer == user) {
		_list->notifyIsBotChanged();
		_list->updateBotInfo();
		updateControlsVisibility();
		updateControlsGeometry();
	}
}

void HistoryWidget::notify_migrateUpdated(PeerData *peer) {
	if (_peer) {
		if (_peer == peer) {
			if (peer->migrateTo()) {
				showHistory(peer->migrateTo()->id, (_showAtMsgId > 0) ? (-_showAtMsgId) : _showAtMsgId, true);
			} else if ((_migrated ? _migrated->peer.get() : nullptr) != peer->migrateFrom()) {
				auto migrated = _history->migrateFrom();
				if (_migrated || (migrated && migrated->unreadCount() > 0)) {
					showHistory(peer->id, peer->migrateFrom() ? _showAtMsgId : ((_showAtMsgId < 0 && -_showAtMsgId < ServerMaxMsgId) ? ShowAtUnreadMsgId : _showAtMsgId), true);
				} else {
					_migrated = migrated;
					_list->notifyMigrateUpdated();
					updateHistoryGeometry();
				}
			}
		} else if (_migrated && _migrated->peer == peer && peer->migrateTo() != _peer) {
			showHistory(_peer->id, _showAtMsgId, true);
		}
	}
}

bool HistoryWidget::cmd_search() {
	if (!inFocusChain() || !_history) return false;

	App::main()->searchInChat(_history);
	return true;
}

bool HistoryWidget::cmd_next_chat() {
	if (!_history) {
		return false;
	}
	const auto next = App::main()->chatListEntryAfter(
		Dialogs::RowDescriptor(
			_history,
			FullMsgId(_history->channelId(), std::max(_showAtMsgId, 0))));
	if (const auto history = next.key.history()) {
		Ui::showPeerHistory(history, next.fullId.msg);
		return true;
	} else if (const auto feed = next.key.feed()) {
		if (const auto item = App::histItemById(next.fullId)) {
			controller()->showSection(HistoryFeed::Memento(feed, item->position()));
		} else {
			controller()->showSection(HistoryFeed::Memento(feed));
		}
	}
	return false;
}

bool HistoryWidget::cmd_previous_chat() {
	if (!_history) {
		return false;
	}
	const auto next = App::main()->chatListEntryBefore(
		Dialogs::RowDescriptor(
			_history,
			FullMsgId(_history->channelId(), std::max(_showAtMsgId, 0))));
	if (const auto history = next.key.history()) {
		Ui::showPeerHistory(history, next.fullId.msg);
		return true;
	} else if (const auto feed = next.key.feed()) {
		if (const auto item = App::histItemById(next.fullId)) {
			controller()->showSection(HistoryFeed::Memento(feed, item->position()));
		} else {
			controller()->showSection(HistoryFeed::Memento(feed));
		}
	}
	return false;
}

void HistoryWidget::saveGif(DocumentData *doc) {
	if (doc->isGifv() && Auth().data().savedGifs().indexOf(doc) != 0) {
		MTPInputDocument mtpInput = doc->mtpInput();
		if (mtpInput.type() != mtpc_inputDocumentEmpty) {
			MTP::send(MTPmessages_SaveGif(mtpInput, MTP_bool(false)), rpcDone(&HistoryWidget::saveGifDone, doc));
		}
	}
}

void HistoryWidget::saveGifDone(DocumentData *doc, const MTPBool &result) {
	if (mtpIsTrue(result)) {
		App::addSavedGif(doc);
	}
}

void HistoryWidget::clearReplyReturns() {
	_replyReturns.clear();
	_replyReturn = 0;
}

void HistoryWidget::pushReplyReturn(not_null<HistoryItem*> item) {
	if (item->history() == _history) {
		_replyReturns.push_back(item->id);
	} else if (item->history() == _migrated) {
		_replyReturns.push_back(-item->id);
	} else {
		return;
	}
	_replyReturn = item;
	updateControlsVisibility();
}

QList<MsgId> HistoryWidget::replyReturns() {
	return _replyReturns;
}

void HistoryWidget::setReplyReturns(PeerId peer, const QList<MsgId> &replyReturns) {
	if (!_peer || _peer->id != peer) return;

	_replyReturns = replyReturns;
	if (_replyReturns.isEmpty()) {
		_replyReturn = 0;
	} else if (_replyReturns.back() < 0 && -_replyReturns.back() < ServerMaxMsgId) {
		_replyReturn = App::histItemById(0, -_replyReturns.back());
	} else {
		_replyReturn = App::histItemById(_channel, _replyReturns.back());
	}
	while (!_replyReturns.isEmpty() && !_replyReturn) {
		_replyReturns.pop_back();
		if (_replyReturns.isEmpty()) {
			_replyReturn = 0;
		} else if (_replyReturns.back() < 0 && -_replyReturns.back() < ServerMaxMsgId) {
			_replyReturn = App::histItemById(0, -_replyReturns.back());
		} else {
			_replyReturn = App::histItemById(_channel, _replyReturns.back());
		}
	}
}

void HistoryWidget::calcNextReplyReturn() {
	_replyReturn = 0;
	while (!_replyReturns.isEmpty() && !_replyReturn) {
		_replyReturns.pop_back();
		if (_replyReturns.isEmpty()) {
			_replyReturn = 0;
		} else if (_replyReturns.back() < 0 && -_replyReturns.back() < ServerMaxMsgId) {
			_replyReturn = App::histItemById(0, -_replyReturns.back());
		} else {
			_replyReturn = App::histItemById(_channel, _replyReturns.back());
		}
	}
	if (!_replyReturn) {
		updateControlsVisibility();
	}
}

void HistoryWidget::fastShowAtEnd(not_null<History*> history) {
	if (_history != history) {
		return;
	}

	clearAllLoadRequests();

	setMsgId(ShowAtUnreadMsgId);
	_historyInited = false;

	if (_history->isReadyFor(_showAtMsgId)) {
		historyLoaded();
	} else {
		firstLoadMessages();
		doneShow();
	}
}

void HistoryWidget::applyDraft(bool parseLinks, Ui::FlatTextarea::UndoHistoryAction undoHistoryAction) {
	auto draft = _history ? _history->draft() : nullptr;
	auto fieldAvailable = canWriteMessage();
	if (!draft || (!_history->editDraft() && !fieldAvailable)) {
		auto fieldWillBeHiddenAfterEdit = (!fieldAvailable && _editMsgId != 0);
		clearFieldText(0, undoHistoryAction);
		_field->setFocus();
		_replyEditMsg = nullptr;
		_editMsgId = _replyToId = 0;
		if (fieldWillBeHiddenAfterEdit) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
		return;
	}

	_textUpdateEvents = 0;
	setFieldText(draft->textWithTags, 0, undoHistoryAction);
	_field->setFocus();
	draft->cursor.applyTo(_field);
	_textUpdateEvents = TextUpdateEvent::SaveDraft | TextUpdateEvent::SendTyping;
	_previewCancelled = draft->previewCancelled;
	_replyEditMsg = nullptr;
	if (auto editDraft = _history->editDraft()) {
		_editMsgId = editDraft->msgId;
		_replyToId = 0;
	} else {
		_editMsgId = 0;
		_replyToId = readyToForward() ? 0 : _history->localDraft()->msgId;
	}
	updateControlsVisibility();
	updateControlsGeometry();

	if (parseLinks) {
		onPreviewParse();
	}
	if (_editMsgId || _replyToId) {
		updateReplyEditTexts();
		if (!_replyEditMsg) {
			Auth().api().requestMessageData(
				_peer->asChannel(),
				_editMsgId ? _editMsgId : _replyToId,
				replyEditMessageDataCallback());
		}
	}
}

void HistoryWidget::applyCloudDraft(History *history) {
	if (_history == history && !_editMsgId) {
		applyDraft(true, Ui::FlatTextarea::AddToUndoHistory);

		updateControlsVisibility();
		updateControlsGeometry();
	}
}

void HistoryWidget::showHistory(const PeerId &peerId, MsgId showAtMsgId, bool reload) {
	MsgId wasMsgId = _showAtMsgId;
	History *wasHistory = _history;

	bool startBot = (showAtMsgId == ShowAndStartBotMsgId);
	if (startBot) {
		showAtMsgId = ShowAtTheEndMsgId;
	}

	clearHighlightMessages();
	if (_history) {
		if (_peer->id == peerId && !reload) {
			updateForwarding();

			bool canShowNow = _history->isReadyFor(showAtMsgId);
			if (!canShowNow) {
				delayedShowAt(showAtMsgId);
			} else {
				_history->forgetScrollState();
				if (_migrated) {
					_migrated->forgetScrollState();
				}

				clearDelayedShowAt();
				while (_replyReturn) {
					if (_replyReturn->history() == _history && _replyReturn->id == showAtMsgId) {
						calcNextReplyReturn();
					} else if (_replyReturn->history() == _migrated && -_replyReturn->id == showAtMsgId) {
						calcNextReplyReturn();
					} else {
						break;
					}
				}

				setMsgId(showAtMsgId);
				if (_historyInited) {
					countHistoryShowFrom();
					destroyUnreadBar();

					auto item = getItemFromHistoryOrMigrated(_showAtMsgId);
					animatedScrollToY(countInitialScrollTop(), item);
				} else {
					historyLoaded();
				}
			}

			_topBar->update();
			update();

			if (const auto user = _peer->asUser()) {
				if (const auto &info = user->botInfo) {
					if (startBot) {
						if (wasHistory) {
							info->inlineReturnPeerId = wasHistory->peer->id;
						}
						onBotStart();
						_history->clearLocalDraft();
						applyDraft();
						_send->finishAnimating();
					}
				}
			}
			return;
		}
		updateSendAction(_history, SendAction::Type::Typing, -1);
		cancelTypingAction();
	}

	if (!cAutoPlayGif()) {
		Auth().data().stopAutoplayAnimations();
	}
	clearReplyReturns();
	clearAllLoadRequests();

	if (_history) {
		if (Ui::InFocusChain(_list)) {
			// Removing focus from list clears selected and updates top bar.
			setFocus();
		}
		if (App::main()) {
			App::main()->saveDraftToCloud();
		}
		if (_migrated) {
			_migrated->clearLocalDraft(); // use migrated draft only once
			_migrated->clearEditDraft();
		}

		_history->showAtMsgId = _showAtMsgId;

		destroyUnreadBar();
		destroyPinnedBar();
		_membersDropdown.destroy();
		_scrollToAnimation.finish();
		_history = _migrated = nullptr;
		_list = nullptr;
		_peer = nullptr;
		_channel = NoChannel;
		_canSendMessages = false;
		_silent.destroy();
		updateBotKeyboard();
	} else {
		Assert(_list == nullptr);
	}

	App::clearMousedItems();

	_addToScroll = 0;
	_saveEditMsgRequestId = 0;
	_replyEditMsg = nullptr;
	_editMsgId = _replyToId = 0;
	_previewData = nullptr;
	_previewCache.clear();
	_fieldBarCancel->hide();

	_membersDropdownShowTimer.stop();
	_scroll->takeWidget<HistoryInner>().destroy();

	clearInlineBot();

	_showAtMsgId = showAtMsgId;
	_historyInited = false;

	if (peerId) {
		_peer = App::peer(peerId);
		_channel = peerToChannel(_peer->id);
		_canSendMessages = _peer->canWrite();
		_tabbedSelector->setCurrentPeer(_peer);
	}

	if (_peer && _peer->isChannel()) {
		_peer->asChannel()->updateFull();
		_joinChannel->setText(lang(_peer->isMegagroup()
			? lng_profile_join_group
			: lng_profile_join_channel).toUpper());
	}

	_unblockRequest = _reportSpamRequest = 0;
	if (_reportSpamSettingRequestId > 0) {
		MTP::cancel(_reportSpamSettingRequestId);
	}
	_reportSpamSettingRequestId = ReportSpamRequestNeeded;

	noSelectingScroll();
	_nonEmptySelection = false;

	if (_peer) {
		App::forgetMedia();
		Auth().data().forgetMedia();
		_serviceImageCacheSize = imageCacheSize();
		Auth().downloader().clearPriorities();

		_history = App::history(_peer);
		_migrated = _history->migrateFrom();

		_topBar->setActiveChat(_history);
		updateTopBarSelection();

		if (_channel) {
			updateNotifySettings();
			if (_peer->notifySettingsUnknown()) {
				Auth().api().requestNotifySetting(_peer);
			}
			refreshSilentToggle();
		}

		if (_showAtMsgId == ShowAtUnreadMsgId) {
			if (_history->scrollTopItem) {
				_showAtMsgId = _history->showAtMsgId;
			}
		} else {
			_history->forgetScrollState();
			if (_migrated) {
				_migrated->forgetScrollState();
			}
		}

		_scroll->hide();
		_list = _scroll->setOwnedWidget(object_ptr<HistoryInner>(this, controller(), _scroll, _history));
		_list->show();

		_updateHistoryItems.stop();

		pinnedMsgVisibilityUpdated();
		if (_history->scrollTopItem || (_migrated && _migrated->scrollTopItem) || _history->isReadyFor(_showAtMsgId)) {
			historyLoaded();
		} else {
			firstLoadMessages();
			doneShow();
		}

		handlePeerUpdate();

		Local::readDraftsWithCursors(_history);
		if (_migrated) {
			Local::readDraftsWithCursors(_migrated);
			_migrated->clearEditDraft();
			_history->takeLocalDraft(_migrated);
		}
		applyDraft(false);
		_send->finishAnimating();

		_tabbedSelector->showMegagroupSet(_peer->asMegagroup());

		updateControlsGeometry();
		if (!_previewCancelled) {
			onPreviewParse();
		}

		connect(_scroll, SIGNAL(geometryChanged()), _list, SLOT(onParentGeometryChanged()));

		if (const auto user = _peer->asUser()) {
			if (const auto &info = user->botInfo) {
				if (startBot) {
					if (wasHistory) {
						info->inlineReturnPeerId = wasHistory->peer->id;
					}
					onBotStart();
				}
			}
		}
		unreadCountChanged(_history); // set _historyDown badge.
	} else {
		_topBar->setActiveChat(Dialogs::Key());
		updateTopBarSelection();

		clearFieldText();
		_tabbedSelector->showMegagroupSet(nullptr);
		doneShow();
	}
	updateForwarding();
	updateOverStates(mapFromGlobal(QCursor::pos()));

	if (_history) {
		controller()->setActiveChatEntry({
			_history,
			FullMsgId(_history->channelId(), _showAtMsgId) });
	}
	update();

	crl::on_main(App::wnd(), [] { App::wnd()->setInnerFocus(); });
}

void HistoryWidget::clearDelayedShowAt() {
	_delayedShowAtMsgId = -1;
	if (_delayedShowAtRequest) {
		MTP::cancel(_delayedShowAtRequest);
		_delayedShowAtRequest = 0;
	}
}

void HistoryWidget::clearAllLoadRequests() {
	clearDelayedShowAt();
	if (_firstLoadRequest) MTP::cancel(_firstLoadRequest);
	if (_preloadRequest) MTP::cancel(_preloadRequest);
	if (_preloadDownRequest) MTP::cancel(_preloadDownRequest);
	_preloadRequest = _preloadDownRequest = _firstLoadRequest = 0;
}

void HistoryWidget::updateFieldSubmitSettings() {
	auto settings = Ui::FlatTextarea::SubmitSettings::Enter;
	if (_isInlineBot) {
		settings = Ui::FlatTextarea::SubmitSettings::None;
	} else if (cCtrlEnter()) {
		settings = Ui::FlatTextarea::SubmitSettings::CtrlEnter;
	}
	_field->setSubmitSettings(settings);
}

void HistoryWidget::updateNotifySettings() {
	if (!_peer || !_peer->isChannel()) return;

	_muteUnmute->setText(lang(_history->mute()
		? lng_channel_unmute
		: lng_channel_mute).toUpper());
	if (!_peer->notifySettingsUnknown()) {
		if (_silent) {
			_silent->setChecked(_peer->notifySilentPosts());
		} else if (hasSilentToggle()) {
			refreshSilentToggle();
			updateControlsGeometry();
			updateControlsVisibility();
		}
	}
}

void HistoryWidget::refreshSilentToggle() {
	if (!_silent && hasSilentToggle()) {
		_silent.create(this, _peer->asChannel());
	} else if (_silent && !hasSilentToggle()) {
		_silent.destroy();
	}
}

bool HistoryWidget::contentOverlapped(const QRect &globalRect) {
	return (_attachDragDocument->overlaps(globalRect)
			|| _attachDragPhoto->overlaps(globalRect)
			|| _fieldAutocomplete->overlaps(globalRect)
			|| (_tabbedPanel && _tabbedPanel->overlaps(globalRect))
			|| (_inlineResults && _inlineResults->overlaps(globalRect)));
}

void HistoryWidget::updateReportSpamStatus() {
	if (!_peer || (_peer->isUser() && (_peer->id == Auth().userPeerId() || isNotificationsUser(_peer->id) || isServiceUser(_peer->id) || _peer->asUser()->botInfo))) {
		setReportSpamStatus(dbiprsHidden);
		return;
	} else if (!_firstLoadRequest && _history->isEmpty()) {
		setReportSpamStatus(dbiprsNoButton);
		if (cReportSpamStatuses().contains(_peer->id)) {
			cRefReportSpamStatuses().remove(_peer->id);
			Local::writeReportSpamStatuses();
		}
		return;
	} else {
		auto i = cReportSpamStatuses().constFind(_peer->id);
		if (i != cReportSpamStatuses().cend()) {
			if (i.value() == dbiprsNoButton) {
				setReportSpamStatus(dbiprsHidden);
				if (!_peer->isUser()
					|| _peer->asUser()->contactStatus() != UserData::ContactStatus::Contact) {
					MTP::send(MTPmessages_HideReportSpam(_peer->input));
				}

				cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
				Local::writeReportSpamStatuses();
			} else {
				setReportSpamStatus(i.value());
				if (_reportSpamStatus == dbiprsShowButton) {
					requestReportSpamSetting();
				}
			}
			return;
		} else if (_peer->migrateFrom()) { // migrate report status
			i = cReportSpamStatuses().constFind(_peer->migrateFrom()->id);
			if (i != cReportSpamStatuses().cend()) {
				if (i.value() == dbiprsNoButton) {
					setReportSpamStatus(dbiprsHidden);
					if (!_peer->isUser()
						|| _peer->asUser()->contactStatus() != UserData::ContactStatus::Contact) {
						MTP::send(MTPmessages_HideReportSpam(_peer->input));
					}
				} else {
					setReportSpamStatus(i.value());
					if (_reportSpamStatus == dbiprsShowButton) {
						requestReportSpamSetting();
					}
				}
				cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
				Local::writeReportSpamStatuses();
				return;
			}
		}
	}
	auto status = dbiprsRequesting;
	if (!Auth().data().contactsLoaded().value() || _firstLoadRequest) {
		status = dbiprsUnknown;
	} else if (_peer->isUser()
		&& _peer->asUser()->contactStatus() == UserData::ContactStatus::Contact) {
		status = dbiprsHidden;
	} else {
		requestReportSpamSetting();
	}
	setReportSpamStatus(status);
	if (_reportSpamStatus == dbiprsHidden) {
		cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
		Local::writeReportSpamStatuses();
	}
}

void HistoryWidget::requestReportSpamSetting() {
	if (_reportSpamSettingRequestId >= 0 || !_peer) return;

	_reportSpamSettingRequestId = MTP::send(MTPmessages_GetPeerSettings(_peer->input), rpcDone(&HistoryWidget::reportSpamSettingDone), rpcFail(&HistoryWidget::reportSpamSettingFail));
}

void HistoryWidget::reportSpamSettingDone(const MTPPeerSettings &result, mtpRequestId req) {
	if (req != _reportSpamSettingRequestId) return;

	_reportSpamSettingRequestId = 0;
	if (result.type() == mtpc_peerSettings) {
		auto &d = result.c_peerSettings();
		auto status = d.is_report_spam() ? dbiprsShowButton : dbiprsHidden;
		if (status != _reportSpamStatus) {
			setReportSpamStatus(status);
			if (_reportSpamPanel) {
				_reportSpamPanel->setReported(false, _peer);
			}

			cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
			Local::writeReportSpamStatuses();

			updateControlsVisibility();
		}
	}
}

bool HistoryWidget::reportSpamSettingFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (req == _reportSpamSettingRequestId) {
		req = 0;
	}
	return true;
}

bool HistoryWidget::canWriteMessage() const {
	if (!_history || !_canSendMessages) return false;
	if (isBlocked() || isJoinChannel() || isMuteUnmute() || isBotStart()) return false;
	return true;
}

bool HistoryWidget::isRestrictedWrite() const {
	if (auto megagroup = _peer ? _peer->asMegagroup() : nullptr) {
		return megagroup->restricted(
			ChannelRestriction::f_send_messages);
	}
	return false;
}

void HistoryWidget::updateControlsVisibility() {
	if (!_a_show.animating()) {
		_topShadow->setVisible(_peer != nullptr);
		_topBar->setVisible(_peer != nullptr);
	}
	updateHistoryDownVisibility();
	updateUnreadMentionsVisibility();
	if (!_history || _a_show.animating()) {
		hideChildren();
		return;
	}

	if (_pinnedBar) {
		_pinnedBar->cancel->show();
		_pinnedBar->shadow->show();
	}
	if (_firstLoadRequest && !_scroll->isHidden()) {
		_scroll->hide();
	} else if (!_firstLoadRequest && _scroll->isHidden()) {
		_scroll->show();
	}
	if (_reportSpamPanel) {
		_reportSpamPanel->show();
	}
	if (!editingMessage() && (isBlocked() || isJoinChannel() || isMuteUnmute() || isBotStart())) {
		if (isBlocked()) {
			_joinChannel->hide();
			_muteUnmute->hide();
			_botStart->hide();
			if (_unblock->isHidden()) {
				_unblock->clearState();
				_unblock->show();
			}
		} else if (isJoinChannel()) {
			_unblock->hide();
			_muteUnmute->hide();
			_botStart->hide();
			if (_joinChannel->isHidden()) {
				_joinChannel->clearState();
				_joinChannel->show();
			}
		} else if (isMuteUnmute()) {
			_unblock->hide();
			_joinChannel->hide();
			_botStart->hide();
			if (_muteUnmute->isHidden()) {
				_muteUnmute->clearState();
				_muteUnmute->show();
			}
		} else if (isBotStart()) {
			_unblock->hide();
			_joinChannel->hide();
			_muteUnmute->hide();
			if (_botStart->isHidden()) {
				_botStart->clearState();
				_botStart->show();
			}
		}
		_kbShown = false;
		_fieldAutocomplete->hide();
		_send->hide();
		if (_silent) {
			_silent->hide();
		}
		_kbScroll->hide();
		_fieldBarCancel->hide();
		_attachToggle->hide();
		_tabbedSelectorToggle->hide();
		_botKeyboardShow->hide();
		_botKeyboardHide->hide();
		_botCommandStart->hide();
		if (_tabbedPanel) {
			_tabbedPanel->hide();
		}
		if (_inlineResults) {
			_inlineResults->hide();
		}
		if (!_field->isHidden()) {
			_field->hide();
			updateControlsGeometry();
			update();
		}
	} else if (editingMessage() || _canSendMessages) {
		onCheckFieldAutocomplete();
		_unblock->hide();
		_botStart->hide();
		_joinChannel->hide();
		_muteUnmute->hide();
		_send->show();
		updateSendButtonType();
		if (_recording) {
			_field->hide();
			_tabbedSelectorToggle->hide();
			_botKeyboardShow->hide();
			_botKeyboardHide->hide();
			_botCommandStart->hide();
			_attachToggle->hide();
			if (_silent) {
				_silent->hide();
			}
			if (_kbShown) {
				_kbScroll->show();
			} else {
				_kbScroll->hide();
			}
		} else {
			_field->show();
			if (_kbShown) {
				_kbScroll->show();
				_tabbedSelectorToggle->hide();
				_botKeyboardHide->show();
				_botKeyboardShow->hide();
				_botCommandStart->hide();
			} else if (_kbReplyTo) {
				_kbScroll->hide();
				_tabbedSelectorToggle->show();
				_botKeyboardHide->hide();
				_botKeyboardShow->hide();
				_botCommandStart->hide();
			} else {
				_kbScroll->hide();
				_tabbedSelectorToggle->show();
				_botKeyboardHide->hide();
				if (_keyboard->hasMarkup()) {
					_botKeyboardShow->show();
					_botCommandStart->hide();
				} else {
					_botKeyboardShow->hide();
					if (_cmdStartShown) {
						_botCommandStart->show();
					} else {
						_botCommandStart->hide();
					}
				}
			}
			_attachToggle->show();
			if (_silent) {
				_silent->show();
			}
			updateFieldPlaceholder();
		}
		if (_editMsgId || _replyToId || readyToForward() || (_previewData && _previewData->pendingTill >= 0) || _kbReplyTo) {
			if (_fieldBarCancel->isHidden()) {
				_fieldBarCancel->show();
				updateControlsGeometry();
				update();
			}
		} else {
			_fieldBarCancel->hide();
		}
	} else {
		_fieldAutocomplete->hide();
		_send->hide();
		_unblock->hide();
		_botStart->hide();
		_joinChannel->hide();
		_muteUnmute->hide();
		_attachToggle->hide();
		if (_silent) {
			_silent->hide();
		}
		_kbScroll->hide();
		_fieldBarCancel->hide();
		_attachToggle->hide();
		_tabbedSelectorToggle->hide();
		_botKeyboardShow->hide();
		_botKeyboardHide->hide();
		_botCommandStart->hide();
		if (_tabbedPanel) {
			_tabbedPanel->hide();
		}
		if (_inlineResults) {
			_inlineResults->hide();
		}
		_kbScroll->hide();
		if (!_field->isHidden()) {
			_field->hide();
			updateControlsGeometry();
			update();
		}
	}
	//checkTabbedSelectorToggleTooltip();
	updateMouseTracking();
}

void HistoryWidget::updateMouseTracking() {
	bool trackMouse = !_fieldBarCancel->isHidden() || _pinnedBar;
	setMouseTracking(trackMouse);
}

void HistoryWidget::destroyUnreadBar() {
	if (_history) _history->destroyUnreadBar();
	if (_migrated) _migrated->destroyUnreadBar();
}

void HistoryWidget::newUnreadMsg(
		not_null<History*> history,
		not_null<HistoryItem*> item) {
	if (_history == history) {
		// If we get here in non-resized state we can't rely on results of
		// doWeReadServerHistory() and mark chat as read.
		// If we receive N messages being not at bottom:
		// - on first message we set unreadcount += 1, firstUnreadMessage.
		// - on second we get wrong doWeReadServerHistory() and read both.
		Auth().data().sendHistoryChangeNotifications();

		if (_scroll->scrollTop() + 1 > _scroll->scrollTopMax()) {
			destroyUnreadBar();
		}
		if (App::wnd()->doWeReadServerHistory()) {
			if (item->mentionsMe() && item->isMediaUnread()) {
				Auth().api().markMediaRead(item);
			}
			Auth().api().readServerHistoryForce(history);
			return;
		}
	}
	Auth().notifications().schedule(history, item);
	if (history->unreadCountKnown()) {
		history->changeUnreadCount(1);
	} else {
		Auth().api().requestDialogEntry(history);
	}
}

void HistoryWidget::historyToDown(History *history) {
	history->forgetScrollState();
	if (auto migrated = App::historyLoaded(history->peer->migrateFrom())) {
		migrated->forgetScrollState();
	}
	if (history == _history) {
		synteticScrollToY(_scroll->scrollTopMax());
	}
}

void HistoryWidget::unreadCountChanged(not_null<History*> history) {
	if (history == _history || history == _migrated) {
		updateHistoryDownVisibility();
		_historyDown->setUnreadCount(_history->chatListUnreadCount());
	}
}

bool HistoryWidget::messagesFailed(const RPCError &error, mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.type() == qstr("CHANNEL_PRIVATE") || error.type() == qstr("CHANNEL_PUBLIC_GROUP_NA") || error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
		auto was = _peer;
		controller()->showBackFromStack();
		Ui::show(Box<InformBox>(lang((was && was->isMegagroup()) ? lng_group_not_accessible : lng_channel_not_accessible)));
		return true;
	}

	LOG(("RPC Error: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	if (_preloadRequest == requestId) {
		_preloadRequest = 0;
	} else if (_preloadDownRequest == requestId) {
		_preloadDownRequest = 0;
	} else if (_firstLoadRequest == requestId) {
		_firstLoadRequest = 0;
		controller()->showBackFromStack();
	} else if (_delayedShowAtRequest == requestId) {
		_delayedShowAtRequest = 0;
	}
	return true;
}

void HistoryWidget::messagesReceived(PeerData *peer, const MTPmessages_Messages &messages, mtpRequestId requestId) {
	if (!_history) {
		_preloadRequest = _preloadDownRequest = _firstLoadRequest = _delayedShowAtRequest = 0;
		return;
	}

	bool toMigrated = (peer == _peer->migrateFrom());
	if (peer != _peer && !toMigrated) {
		_preloadRequest = _preloadDownRequest = _firstLoadRequest = _delayedShowAtRequest = 0;
		return;
	}

	auto count = 0;
	const QVector<MTPMessage> emptyList, *histList = &emptyList;
	switch (messages.type()) {
	case mtpc_messages_messages: {
		auto &d(messages.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		histList = &d.vmessages.v;
		count = histList->size();
	} break;
	case mtpc_messages_messagesSlice: {
		auto &d(messages.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		histList = &d.vmessages.v;
		count = d.vcount.v;
	} break;
	case mtpc_messages_channelMessages: {
		auto &d(messages.c_messages_channelMessages());
		if (peer && peer->isChannel()) {
			peer->asChannel()->ptsReceived(d.vpts.v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (HistoryWidget::messagesReceived)"));
		}
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		histList = &d.vmessages.v;
		count = d.vcount.v;
	} break;
	case mtpc_messages_messagesNotModified: {
		LOG(("API Error: received messages.messagesNotModified! (HistoryWidget::messagesReceived)"));
	} break;
	}

	const auto ExtractFirstId = [&] {
		return histList->empty() ? -1 : idFromMessage(histList->front());
	};
	const auto ExtractLastId = [&] {
		return histList->empty() ? -1 : idFromMessage(histList->back());
	};
	const auto PeerString = [](PeerId peerId) {
		if (peerIsUser(peerId)) {
			return QString("User-%1").arg(peerToUser(peerId));
		} else if (peerIsChat(peerId)) {
			return QString("Chat-%1").arg(peerToChat(peerId));
		} else if (peerIsChannel(peerId)) {
			return QString("Channel-%1").arg(peerToChannel(peerId));
		}
		return QString("Bad-%1").arg(peerId);
	};

	if (_preloadRequest == requestId) {
		auto to = toMigrated ? _migrated : _history;
		addMessagesToFront(peer, *histList);
		_preloadRequest = 0;
		preloadHistoryIfNeeded();
		if (_reportSpamStatus == dbiprsUnknown) {
			updateReportSpamStatus();
			if (_reportSpamStatus != dbiprsUnknown) updateControlsVisibility();
		}
	} else if (_preloadDownRequest == requestId) {
		auto to = toMigrated ? _migrated : _history;
		addMessagesToBack(peer, *histList);
		_preloadDownRequest = 0;
		preloadHistoryIfNeeded();
		if (_history->loadedAtBottom() && App::wnd()) App::wnd()->checkHistoryActivation();
	} else if (_firstLoadRequest == requestId) {
		if (toMigrated) {
			_history->unloadBlocks();
		} else if (_migrated) {
			_migrated->unloadBlocks();
		}
		addMessagesToFront(peer, *histList);
		_firstLoadRequest = 0;
		if (_history->loadedAtTop() && _history->isEmpty() && count > 0) {
			firstLoadMessages();
			return;
		}

		historyLoaded();
	} else if (_delayedShowAtRequest == requestId) {
		if (toMigrated) {
			_history->unloadBlocks();
		} else if (_migrated) {
			_migrated->unloadBlocks();
		}

		_delayedShowAtRequest = 0;
		_history->getReadyFor(_delayedShowAtMsgId);
		if (_history->isEmpty()) {
			if (_preloadRequest) MTP::cancel(_preloadRequest);
			if (_preloadDownRequest) MTP::cancel(_preloadDownRequest);
			if (_firstLoadRequest) MTP::cancel(_firstLoadRequest);
			_preloadRequest = _preloadDownRequest = 0;
			_firstLoadRequest = -1; // hack - don't updateListSize yet
			addMessagesToFront(peer, *histList);
			_firstLoadRequest = 0;
			if (_history->loadedAtTop()
				&& _history->isEmpty()
				&& count > 0) {
				firstLoadMessages();
				return;
			}
		}
		while (_replyReturn) {
			if (_replyReturn->history() == _history
				&& _replyReturn->id == _delayedShowAtMsgId) {
				calcNextReplyReturn();
			} else if (_replyReturn->history() == _migrated
				&& -_replyReturn->id == _delayedShowAtMsgId) {
				calcNextReplyReturn();
			} else {
				break;
			}
		}

		setMsgId(_delayedShowAtMsgId);

		_historyInited = false;
		historyLoaded();
	}
}

void HistoryWidget::historyLoaded() {
	countHistoryShowFrom();
	destroyUnreadBar();
	doneShow();
}

void HistoryWidget::windowShown() {
	updateControlsGeometry();
}

bool HistoryWidget::doWeReadServerHistory() const {
	if (!_history || !_list) return true;
	if (_firstLoadRequest || _a_show.animating()) return false;
	if (_history->loadedAtBottom()) {
		int scrollTop = _scroll->scrollTop();
		if (scrollTop + 1 > _scroll->scrollTopMax()) return true;

		if (const auto unread = firstUnreadMessage()) {
			const auto scrollBottom = scrollTop + _scroll->height();
			if (scrollBottom > _list->itemTop(unread)) {
				return true;
			}
		}
	}
	if (_history->hasNotFreezedUnreadBar()
		|| (_migrated && _migrated->hasNotFreezedUnreadBar())) {
		return true;
	}
	return false;
}

bool HistoryWidget::doWeReadMentions() const {
	if (!_history || !_list) return true;
	if (_firstLoadRequest || _a_show.animating()) return false;
	return true;
}

void HistoryWidget::firstLoadMessages() {
	if (!_history || _firstLoadRequest) return;

	auto from = _peer;
	auto offsetId = 0;
	auto offset = 0;
	auto loadCount = kMessagesPerPage;
	if (_showAtMsgId == ShowAtUnreadMsgId) {
		if (const auto around = _migrated ? _migrated->loadAroundId() : 0) {
			_history->getReadyFor(_showAtMsgId);
			from = _migrated->peer;
			offset = -loadCount / 2;
			offsetId = around;
		} else if (const auto around = _history->loadAroundId()) {
			_history->getReadyFor(_showAtMsgId);
			offset = -loadCount / 2;
			offsetId = around;
		} else {
			_history->getReadyFor(ShowAtTheEndMsgId);
		}
	} else if (_showAtMsgId == ShowAtTheEndMsgId) {
		_history->getReadyFor(_showAtMsgId);
		loadCount = kMessagesPerPageFirst;
	} else if (_showAtMsgId > 0) {
		_history->getReadyFor(_showAtMsgId);
		offset = -loadCount / 2;
		offsetId = _showAtMsgId;
	} else if (_showAtMsgId < 0 && _history->isChannel()) {
		if (_showAtMsgId < 0 && -_showAtMsgId < ServerMaxMsgId && _migrated) {
			_history->getReadyFor(_showAtMsgId);
			from = _migrated->peer;
			offset = -loadCount / 2;
			offsetId = -_showAtMsgId;
		} else if (_showAtMsgId == SwitchAtTopMsgId) {
			_history->getReadyFor(_showAtMsgId);
		}
	}

	auto offsetDate = 0;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;

	_firstLoadRequest = MTP::send(
		MTPmessages_GetHistory(
			from->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(offset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)),
		rpcDone(&HistoryWidget::messagesReceived, from),
		rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::loadMessages() {
	if (!_history || _preloadRequest) return;

	if (_history->isEmpty() && _migrated && _migrated->isEmpty()) {
		return firstLoadMessages();
	}

	auto loadMigrated = _migrated && (_history->isEmpty() || _history->loadedAtTop() || (!_migrated->isEmpty() && !_migrated->loadedAtBottom()));
	auto from = loadMigrated ? _migrated : _history;
	if (from->loadedAtTop()) {
		return;
	}

	auto offsetId = from->minMsgId();
	auto addOffset = 0;
	auto loadCount = offsetId
		? kMessagesPerPage
		: kMessagesPerPageFirst;
	auto offsetDate = 0;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;

	_preloadRequest = MTP::send(
		MTPmessages_GetHistory(
			from->peer->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(addOffset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)),
		rpcDone(&HistoryWidget::messagesReceived, from->peer.get()),
		rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::loadMessagesDown() {
	if (!_history || _preloadDownRequest) return;

	if (_history->isEmpty() && _migrated && _migrated->isEmpty()) {
		return firstLoadMessages();
	}

	auto loadMigrated = _migrated && !(_migrated->isEmpty() || _migrated->loadedAtBottom() || (!_history->isEmpty() && !_history->loadedAtTop()));
	auto from = loadMigrated ? _migrated : _history;
	if (from->loadedAtBottom()) {
		return;
	}

	auto loadCount = kMessagesPerPage;
	auto addOffset = -loadCount;
	auto offsetId = from->maxMsgId();
	if (!offsetId) {
		if (loadMigrated || !_migrated) return;
		++offsetId;
		++addOffset;
	}
	auto offsetDate = 0;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;

	_preloadDownRequest = MTP::send(
		MTPmessages_GetHistory(
			from->peer->input,
			MTP_int(offsetId + 1),
			MTP_int(offsetDate),
			MTP_int(addOffset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)),
		rpcDone(&HistoryWidget::messagesReceived, from->peer.get()),
		rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::delayedShowAt(MsgId showAtMsgId) {
	if (!_history || (_delayedShowAtRequest && _delayedShowAtMsgId == showAtMsgId)) return;

	clearDelayedShowAt();
	_delayedShowAtMsgId = showAtMsgId;

	auto from = _peer;
	auto offsetId = 0;
	auto offset = 0;
	auto loadCount = kMessagesPerPage;
	if (_delayedShowAtMsgId == ShowAtUnreadMsgId) {
		if (const auto around = _migrated ? _migrated->loadAroundId() : 0) {
			from = _migrated->peer;
			offset = -loadCount / 2;
			offsetId = around;
		} else if (const auto around = _history->loadAroundId()) {
			offset = -loadCount / 2;
			offsetId = around;
		} else {
			loadCount = kMessagesPerPageFirst;
		}
	} else if (_delayedShowAtMsgId == ShowAtTheEndMsgId) {
		loadCount = kMessagesPerPageFirst;
	} else if (_delayedShowAtMsgId > 0) {
		offset = -loadCount / 2;
		offsetId = _delayedShowAtMsgId;
	} else if (_delayedShowAtMsgId < 0 && _history->isChannel()) {
		if (_delayedShowAtMsgId < 0 && -_delayedShowAtMsgId < ServerMaxMsgId && _migrated) {
			from = _migrated->peer;
			offset = -loadCount / 2;
			offsetId = -_delayedShowAtMsgId;
		}
	}
	auto offsetDate = 0;
	auto maxId = 0;
	auto minId = 0;
	auto historyHash = 0;

	_delayedShowAtRequest = MTP::send(
		MTPmessages_GetHistory(
			from->input,
			MTP_int(offsetId),
			MTP_int(offsetDate),
			MTP_int(offset),
			MTP_int(loadCount),
			MTP_int(maxId),
			MTP_int(minId),
			MTP_int(historyHash)),
		rpcDone(&HistoryWidget::messagesReceived, from),
		rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::onScroll() {
	App::checkImageCacheSize();
	preloadHistoryIfNeeded();
	visibleAreaUpdated();
	if (!_synteticScrollEvent) {
		_lastUserScrolled = getms();
	}
}

bool HistoryWidget::isItemCompletelyHidden(HistoryItem *item) const {
	const auto view = item ? item->mainView() : nullptr;
	if (!view) {
		return true;
	}
	auto top = _list ? _list->itemTop(item) : -2;
	if (top < 0) {
		return true;
	}

	auto bottom = top + view->height();
	auto scrollTop = _scroll->scrollTop();
	auto scrollBottom = scrollTop + _scroll->height();
	return (top >= scrollBottom || bottom <= scrollTop);
}

void HistoryWidget::visibleAreaUpdated() {
	if (_list && !_scroll->isHidden()) {
		auto scrollTop = _scroll->scrollTop();
		auto scrollBottom = scrollTop + _scroll->height();
		_list->visibleAreaUpdated(scrollTop, scrollBottom);
		if (_history->loadedAtBottom() && (_history->unreadCount() > 0 || (_migrated && _migrated->unreadCount() > 0))) {
			const auto unread = firstUnreadMessage();
			const auto unreadVisible = unread
				&& (scrollBottom > _list->itemTop(unread));
			const auto atBottom = (scrollTop >= _scroll->scrollTopMax());
			if ((unreadVisible || atBottom)
				 && App::wnd()->doWeReadServerHistory()) {
				Auth().api().readServerHistory(_history);
			}
		}
		controller()->floatPlayerAreaUpdated().notify(true);
	}
}

void HistoryWidget::preloadHistoryIfNeeded() {
	if (_firstLoadRequest || _scroll->isHidden() || !_peer) {
		return;
	}

	updateHistoryDownVisibility();
	if (!_scrollToAnimation.animating()) {
		preloadHistoryByScroll();
		checkReplyReturns();
	}

	auto scrollTop = _scroll->scrollTop();
	if (scrollTop != _lastScrollTop) {
		_lastScrolled = getms();
		_lastScrollTop = scrollTop;
	}
}

void HistoryWidget::preloadHistoryByScroll() {
	if (_firstLoadRequest || _scroll->isHidden() || !_peer) {
		return;
	}

	auto scrollTop = _scroll->scrollTop();
	auto scrollTopMax = _scroll->scrollTopMax();
	auto scrollHeight = _scroll->height();
	if (scrollTop + kPreloadHeightsCount * scrollHeight >= scrollTopMax) {
		loadMessagesDown();
	}
	if (scrollTop <= kPreloadHeightsCount * scrollHeight) {
		loadMessages();
	}
}

void HistoryWidget::checkReplyReturns() {
	if (_firstLoadRequest || _scroll->isHidden() || !_peer) {
		return;
	}
	auto scrollTop = _scroll->scrollTop();
	auto scrollTopMax = _scroll->scrollTopMax();
	auto scrollHeight = _scroll->height();
	while (_replyReturn) {
		auto below = (!_replyReturn->mainView() && _replyReturn->history() == _history && !_history->isEmpty() && _replyReturn->id < _history->blocks.back()->messages.back()->data()->id);
		if (!below) {
			below = (!_replyReturn->mainView() && _replyReturn->history() == _migrated && !_history->isEmpty());
		}
		if (!below) {
			below = (!_replyReturn->mainView() && _migrated && _replyReturn->history() == _migrated && !_migrated->isEmpty() && _replyReturn->id < _migrated->blocks.back()->messages.back()->data()->id);
		}
		if (!below && _replyReturn->mainView()) {
			below = (scrollTop >= scrollTopMax) || (_list->itemTop(_replyReturn) < scrollTop + scrollHeight / 2);
		}
		if (below) {
			calcNextReplyReturn();
		} else {
			break;
		}
	}
}

void HistoryWidget::onInlineBotCancel() {
	auto &textWithTags = _field->getTextWithTags();
	if (textWithTags.text.size() > _inlineBotUsername.size() + 2) {
		setFieldText({ '@' + _inlineBotUsername + ' ', TextWithTags::Tags() }, TextUpdateEvent::SaveDraft, Ui::FlatTextarea::AddToUndoHistory);
	} else {
		clearFieldText(TextUpdateEvent::SaveDraft, Ui::FlatTextarea::AddToUndoHistory);
	}
}

void HistoryWidget::onWindowVisibleChanged() {
	QTimer::singleShot(0, this, SLOT(preloadHistoryIfNeeded()));
}

void HistoryWidget::historyDownClicked() {
	if (_replyReturn && _replyReturn->history() == _history) {
		showHistory(_peer->id, _replyReturn->id);
	} else if (_replyReturn && _replyReturn->history() == _migrated) {
		showHistory(_peer->id, -_replyReturn->id);
	} else if (_peer) {
		showHistory(_peer->id, ShowAtUnreadMsgId);
	}
}

void HistoryWidget::showNextUnreadMention() {
	showHistory(_peer->id, _history->getMinLoadedUnreadMention());
}

void HistoryWidget::saveEditMsg() {
	if (_saveEditMsgRequestId) return;

	WebPageId webPageId = _previewCancelled ? CancelledWebPageId : ((_previewData && _previewData->pendingTill >= 0) ? _previewData->id : 0);

	auto &textWithTags = _field->getTextWithTags();
	auto prepareFlags = Ui::ItemTextOptions(_history, App::self()).flags;
	auto sending = TextWithEntities();
	auto left = TextWithEntities { textWithTags.text, ConvertTextTagsToEntities(textWithTags.tags) };
	TextUtilities::PrepareForSending(left, prepareFlags);

	if (!TextUtilities::CutPart(sending, left, MaxMessageSize)) {
		_field->selectAll();
		_field->setFocus();
		return;
	} else if (!left.text.isEmpty()) {
		Ui::show(Box<InformBox>(lang(lng_edit_too_long)));
		return;
	}

	auto sendFlags = MTPmessages_EditMessage::Flag::f_message | 0;
	if (webPageId == CancelledWebPageId) {
		sendFlags |= MTPmessages_EditMessage::Flag::f_no_webpage;
	}
	auto localEntities = TextUtilities::EntitiesToMTP(sending.entities);
	auto sentEntities = TextUtilities::EntitiesToMTP(sending.entities, TextUtilities::ConvertOption::SkipLocal);
	if (!sentEntities.v.isEmpty()) {
		sendFlags |= MTPmessages_EditMessage::Flag::f_entities;
	}

	_saveEditMsgRequestId = MTP::send(
		MTPmessages_EditMessage(
			MTP_flags(sendFlags),
			_history->peer->input,
			MTP_int(_editMsgId),
			MTP_string(sending.text),
			MTPnullMarkup,
			sentEntities,
			MTP_inputGeoPointEmpty()),
		rpcDone(&HistoryWidget::saveEditMsgDone, _history),
		rpcFail(&HistoryWidget::saveEditMsgFail, _history));
}

void HistoryWidget::saveEditMsgDone(History *history, const MTPUpdates &updates, mtpRequestId req) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
	if (req == _saveEditMsgRequestId) {
		_saveEditMsgRequestId = 0;
		cancelEdit();
	}
	if (auto editDraft = history->editDraft()) {
		if (editDraft->saveRequestId == req) {
			history->clearEditDraft();
			if (App::main()) App::main()->writeDrafts(history);
		}
	}
}

bool HistoryWidget::saveEditMsgFail(History *history, const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;
	if (req == _saveEditMsgRequestId) {
		_saveEditMsgRequestId = 0;
	}
	if (auto editDraft = history->editDraft()) {
		if (editDraft->saveRequestId == req) {
			editDraft->saveRequestId = 0;
		}
	}

	QString err = error.type();
	if (err == qstr("MESSAGE_ID_INVALID") || err == qstr("CHAT_ADMIN_REQUIRED") || err == qstr("MESSAGE_EDIT_TIME_EXPIRED")) {
		Ui::show(Box<InformBox>(lang(lng_edit_error)));
	} else if (err == qstr("MESSAGE_NOT_MODIFIED")) {
		cancelEdit();
	} else if (err == qstr("MESSAGE_EMPTY")) {
		_field->selectAll();
		_field->setFocus();
	} else {
		Ui::show(Box<InformBox>(lang(lng_edit_error)));
	}
	update();
	return true;
}

void HistoryWidget::hideSelectorControlsAnimated() {
	_fieldAutocomplete->hideAnimated();
	if (_tabbedPanel) {
		_tabbedPanel->hideAnimated();
	}
	if (_inlineResults) {
		_inlineResults->hideAnimated();
	}
}

void HistoryWidget::onSend(bool ctrlShiftEnter) {
	if (!_history) return;

	if (_editMsgId) {
		saveEditMsg();
		return;
	}

	WebPageId webPageId = _previewCancelled ? CancelledWebPageId : ((_previewData && _previewData->pendingTill >= 0) ? _previewData->id : 0);

	auto message = MainWidget::MessageToSend(_history);
	message.textWithTags = _field->getTextWithTags();
	message.replyTo = replyToId();
	message.webPageId = webPageId;
	App::main()->sendMessage(message);

	clearFieldText();
	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	hideSelectorControlsAnimated();

	if (_previewData && _previewData->pendingTill) previewCancel();
	_field->setFocus();

	if (!_keyboard->hasMarkup() && _keyboard->forceReply() && !_kbReplyTo) {
		onKbToggle();
	}
}

void HistoryWidget::onUnblock() {
	if (_unblockRequest) return;
	if (!_peer || !_peer->isUser() || !_peer->asUser()->isBlocked()) {
		updateControlsVisibility();
		return;
	}

	_unblockRequest = MTP::send(MTPcontacts_Unblock(_peer->asUser()->inputUser), rpcDone(&HistoryWidget::unblockDone, _peer), rpcFail(&HistoryWidget::unblockFail));
}

void HistoryWidget::unblockDone(PeerData *peer, const MTPBool &result, mtpRequestId req) {
	if (!peer->isUser()) return;
	if (_unblockRequest == req) _unblockRequest = 0;
	peer->asUser()->setBlockStatus(UserData::BlockStatus::NotBlocked);
}

bool HistoryWidget::unblockFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_unblockRequest == req) _unblockRequest = 0;
	return false;
}

void HistoryWidget::blockDone(PeerData *peer, const MTPBool &result) {
	if (!peer->isUser()) return;

	peer->asUser()->setBlockStatus(UserData::BlockStatus::Blocked);
}

void HistoryWidget::onBotStart() {
	if (!_peer || !_peer->isUser() || !_peer->asUser()->botInfo || !_canSendMessages) {
		updateControlsVisibility();
		return;
	}

	QString token = _peer->asUser()->botInfo->startToken;
	if (token.isEmpty()) {
		sendBotCommand(_peer, _peer->asUser(), qsl("/start"), 0);
	} else {
		uint64 randomId = rand_value<uint64>();
		MTP::send(MTPmessages_StartBot(_peer->asUser()->inputUser, MTP_inputPeerEmpty(), MTP_long(randomId), MTP_string(token)), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::addParticipantFail, { _peer->asUser(), (PeerData*)nullptr }));

		_peer->asUser()->botInfo->startToken = QString();
		if (_keyboard->hasMarkup()) {
			if (_keyboard->singleUse() && _keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId) && _history->lastKeyboardUsed) {
				_history->lastKeyboardHiddenId = _history->lastKeyboardId;
			}
			if (!kbWasHidden()) _kbShown = _keyboard->hasMarkup();
		}
	}
	updateControlsVisibility();
	updateControlsGeometry();
}

void HistoryWidget::onJoinChannel() {
	if (!_peer || !_peer->isChannel() || !isJoinChannel()) {
		updateControlsVisibility();
		return;
	}
	Auth().api().joinChannel(_peer->asChannel());
}

void HistoryWidget::onMuteUnmute() {
	const auto muteState = _history->mute()
		? Data::NotifySettings::MuteChange::Unmute
		: Data::NotifySettings::MuteChange::Mute;
	App::main()->updateNotifySettings(_peer, muteState);
}

void HistoryWidget::onBroadcastSilentChange() {
	updateFieldPlaceholder();
}

History *HistoryWidget::history() const {
	return _history;
}

PeerData *HistoryWidget::peer() const {
	return _peer;
}

// Sometimes _showAtMsgId is set directly.
void HistoryWidget::setMsgId(MsgId showAtMsgId) {
	if (_showAtMsgId != showAtMsgId) {
		auto wasMsgId = _showAtMsgId;
		_showAtMsgId = showAtMsgId;
		if (_history) {
			controller()->setActiveChatEntry({
				_history,
				FullMsgId(_history->channelId(), _showAtMsgId) });
		}
	}
}

MsgId HistoryWidget::msgId() const {
	return _showAtMsgId;
}

void HistoryWidget::showAnimated(
		Window::SlideDirection direction,
		const Window::SectionSlideParams &params) {
	_showDirection = direction;

	_a_show.finish();

	_cacheUnder = params.oldContentCache;
	show();
	_topBar->finishAnimating();
	historyDownAnimationFinish();
	unreadMentionsAnimationFinish();
	_topShadow->setVisible(params.withTopBarShadow ? false : true);
	_cacheOver = App::main()->grabForShowAnimation(params);

	hideChildren();
	if (params.withTopBarShadow) _topShadow->show();

	if (_showDirection == Window::SlideDirection::FromLeft) {
		std::swap(_cacheUnder, _cacheOver);
	}
	_a_show.start([this] { animationCallback(); }, 0., 1., st::slideDuration, Window::SlideAnimation::transition());
	if (_history) {
		_topBar->show();
		_topBar->setAnimatingMode(true);
	}

	activate();
}

void HistoryWidget::animationCallback() {
	update();
	if (!_a_show.animating()) {
		historyDownAnimationFinish();
		unreadMentionsAnimationFinish();
		_cacheUnder = _cacheOver = QPixmap();
		doneShow();
	}
}

void HistoryWidget::doneShow() {
	_topBar->setAnimatingMode(false);
	updateReportSpamStatus();
	updateBotKeyboard();
	updateControlsVisibility();
	if (!_historyInited) {
		updateHistoryGeometry(true);
	} else if (hasPendingResizedItems()) {
		updateHistoryGeometry();
	}
	preloadHistoryIfNeeded();
	if (App::wnd()) {
		App::wnd()->checkHistoryActivation();
		App::wnd()->setInnerFocus();
	}
}

void HistoryWidget::finishAnimating() {
	if (!_a_show.animating()) return;
	_a_show.finish();
	_topShadow->setVisible(_peer != nullptr);
	_topBar->setVisible(_peer != nullptr);
	historyDownAnimationFinish();
	unreadMentionsAnimationFinish();
}

void HistoryWidget::historyDownAnimationFinish() {
	_historyDownShown.finish();
	updateHistoryDownPosition();
}

void HistoryWidget::unreadMentionsAnimationFinish() {
	_unreadMentionsShown.finish();
	updateUnreadMentionsPosition();
}

void HistoryWidget::step_recording(float64 ms, bool timer) {
	float64 dt = ms / AudioVoiceMsgUpdateView;
	if (dt >= 1) {
		_a_recording.stop();
		a_recordingLevel.finish();
	} else {
		a_recordingLevel.update(dt, anim::linear);
	}
	if (timer) update(_attachToggle->geometry());
}

void HistoryWidget::chooseAttach() {
	if (!_peer || !_peer->canWrite()) return;
	if (auto megagroup = _peer->asMegagroup()) {
		if (megagroup->restricted(ChannelRestriction::f_send_media)) {
			Ui::show(Box<InformBox>(lang(lng_restricted_send_media)));
			return;
		}
	}

	auto filter = FileDialog::AllFilesFilter() + qsl(";;Image files (*") + cImgExtensions().join(qsl(" *")) + qsl(")");

	FileDialog::GetOpenPaths(lang(lng_choose_files), filter, base::lambda_guarded(this, [this](FileDialog::OpenResult &&result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!result.remoteContent.isEmpty()) {
			auto animated = false;
			auto image = App::readImage(
				result.remoteContent,
				nullptr,
				false,
				&animated);
			if (!image.isNull() && !animated) {
				confirmSendingFiles(
					std::move(image),
					std::move(result.remoteContent),
					CompressConfirm::Auto);
			} else {
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			auto list = Storage::PrepareMediaList(
				result.paths,
				st::sendMediaPreviewSize);
			if (list.allFilesForCompress || list.albumIsPossible) {
				confirmSendingFiles(std::move(list), CompressConfirm::Auto);
			} else if (!showSendingFilesError(list)) {
				uploadFiles(std::move(list), SendMediaType::File);
			}
		}
	}));
}

void HistoryWidget::sendButtonClicked() {
	auto type = _send->type();
	if (type == Ui::SendButton::Type::Cancel) {
		onInlineBotCancel();
	} else if (type != Ui::SendButton::Type::Record) {
		onSend();
	}
}

void HistoryWidget::dragEnterEvent(QDragEnterEvent *e) {
	if (!_history || !_canSendMessages) return;

	_attachDragState = Storage::ComputeMimeDataState(e->mimeData());
	updateDragAreas();

	if (_attachDragState != DragState::None) {
		e->setDropAction(Qt::IgnoreAction);
		e->accept();
	}
}

void HistoryWidget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_attachDragState != DragState::None || !_attachDragPhoto->isHidden() || !_attachDragDocument->isHidden()) {
		_attachDragState = DragState::None;
		updateDragAreas();
	}
}

void HistoryWidget::leaveEventHook(QEvent *e) {
	if (_attachDragState != DragState::None || !_attachDragPhoto->isHidden() || !_attachDragDocument->isHidden()) {
		_attachDragState = DragState::None;
		updateDragAreas();
	}
	if (hasMouseTracking()) mouseMoveEvent(0);
}

void HistoryWidget::mouseMoveEvent(QMouseEvent *e) {
	auto pos = e ? e->pos() : mapFromGlobal(QCursor::pos());
	updateOverStates(pos);
}

void HistoryWidget::updateOverStates(QPoint pos) {
	auto inField = pos.y() >= (_scroll->y() + _scroll->height()) && pos.y() < height() && pos.x() >= 0 && pos.x() < width();
	auto inReplyEditForward = QRect(st::historyReplySkip, _field->y() - st::historySendPadding - st::historyReplyHeight, width() - st::historyReplySkip - _fieldBarCancel->width(), st::historyReplyHeight).contains(pos) && (_editMsgId || replyToId() || readyToForward());
	auto inPinnedMsg = QRect(0, _topBar->bottomNoMargins(), width(), st::historyReplyHeight).contains(pos) && _pinnedBar;
	auto inClickable = inReplyEditForward || inPinnedMsg;
	if (inField != _inField && _recording) {
		_inField = inField;
		_send->setRecordActive(_inField);
	}
	_inReplyEditForward = inReplyEditForward;
	_inPinnedMsg = inPinnedMsg;
	if (inClickable != _inClickable) {
		_inClickable = inClickable;
		setCursor(_inClickable ? style::cur_pointer : style::cur_default);
	}
}

void HistoryWidget::leaveToChildEvent(QEvent *e, QWidget *child) { // e -- from enterEvent() of child TWidget
	if (hasMouseTracking()) {
		updateOverStates(mapFromGlobal(QCursor::pos()));
	}
}

void HistoryWidget::recordStartCallback() {
	if (!Media::Capture::instance()->available()) {
		return;
	}
	if (auto megagroup = _peer ? _peer->asMegagroup() : nullptr) {
		if (megagroup->restricted(ChannelRestriction::f_send_media)) {
			Ui::show(Box<InformBox>(lang(lng_restricted_send_media)));
			return;
		}
	}

	emit Media::Capture::instance()->start();

	_recording = _inField = true;
	updateControlsVisibility();
	activate();

	updateField();

	_send->setRecordActive(true);
}

void HistoryWidget::recordStopCallback(bool active) {
	stopRecording(_peer && active);
}

void HistoryWidget::recordUpdateCallback(QPoint globalPos) {
	updateOverStates(mapFromGlobal(globalPos));
}

void HistoryWidget::mouseReleaseEvent(QMouseEvent *e) {
	if (_replyForwardPressed) {
		_replyForwardPressed = false;
		update(0, _field->y() - st::historySendPadding - st::historyReplyHeight, width(), st::historyReplyHeight);
	}
	if (_attachDragState != DragState::None || !_attachDragPhoto->isHidden() || !_attachDragDocument->isHidden()) {
		_attachDragState = DragState::None;
		updateDragAreas();
	}
	if (_recording) {
		stopRecording(_peer && _inField);
	}
}

void HistoryWidget::stopRecording(bool send) {
	emit Media::Capture::instance()->stop(send);

	a_recordingLevel = anim::value();
	_a_recording.stop();

	_recording = false;
	_recordingSamples = 0;
	if (_history) {
		updateSendAction(_history, SendAction::Type::RecordVoice, -1);
	}

	updateControlsVisibility();
	activate();

	updateField();
	_send->setRecordActive(false);
}

void HistoryWidget::sendBotCommand(PeerData *peer, UserData *bot, const QString &cmd, MsgId replyTo) { // replyTo != 0 from ReplyKeyboardMarkup, == 0 from cmd links
	if (!_peer || _peer != peer) return;

	bool lastKeyboardUsed = (_keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)) && (_keyboard->forMsgId() == FullMsgId(_channel, replyTo));

	QString toSend = cmd;
	if (bot && (!bot->isUser() || !bot->asUser()->botInfo)) {
		bot = nullptr;
	}
	QString username = bot ? bot->asUser()->username : QString();
	int32 botStatus = _peer->isChat() ? _peer->asChat()->botStatus : (_peer->isMegagroup() ? _peer->asChannel()->mgInfo->botStatus : -1);
	if (!replyTo && toSend.indexOf('@') < 2 && !username.isEmpty() && (botStatus == 0 || botStatus == 2)) {
		toSend += '@' + username;
	}

	auto message = MainWidget::MessageToSend(_history);
	message.textWithTags = { toSend, TextWithTags::Tags() };
	message.replyTo = replyTo
		? ((!_peer->isUser()/* && (botStatus == 0 || botStatus == 2)*/)
			? replyTo
			: replyToId())
		: 0;
	App::main()->sendMessage(message);
	if (replyTo) {
		if (_replyToId == replyTo) {
			cancelReply();
			onCloudDraftSave();
		}
		if (_keyboard->singleUse() && _keyboard->hasMarkup() && lastKeyboardUsed) {
			if (_kbShown) onKbToggle(false);
			_history->lastKeyboardUsed = true;
		}
	}

	_field->setFocus();
}

void HistoryWidget::hideSingleUseKeyboard(PeerData *peer, MsgId replyTo) {
	if (!_peer || _peer != peer) return;

	bool lastKeyboardUsed = (_keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)) && (_keyboard->forMsgId() == FullMsgId(_channel, replyTo));
	if (replyTo) {
		if (_replyToId == replyTo) {
			cancelReply();
			onCloudDraftSave();
		}
		if (_keyboard->singleUse() && _keyboard->hasMarkup() && lastKeyboardUsed) {
			if (_kbShown) onKbToggle(false);
			_history->lastKeyboardUsed = true;
		}
	}
}

void HistoryWidget::app_sendBotCallback(
		not_null<const HistoryMessageMarkupButton*> button,
		not_null<const HistoryItem*> msg,
		int row,
		int column) {
	if (msg->id < 0 || _peer != msg->history()->peer) {
		return;
	}

	bool lastKeyboardUsed = (_keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)) && (_keyboard->forMsgId() == FullMsgId(_channel, msg->id));

	auto bot = msg->getMessageBot();

	using ButtonType = HistoryMessageMarkupButton::Type;
	BotCallbackInfo info = {
		bot,
		msg->fullId(),
		row,
		column,
		(button->type == ButtonType::Game)
	};
	auto flags = MTPmessages_GetBotCallbackAnswer::Flags(0);
	QByteArray sendData;
	if (info.game) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_game;
	} else if (button->type == ButtonType::Callback) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_data;
		sendData = button->data;
	}
	button->requestId = MTP::send(
		MTPmessages_GetBotCallbackAnswer(
			MTP_flags(flags),
			_peer->input,
			MTP_int(msg->id),
			MTP_bytes(sendData)),
		rpcDone(&HistoryWidget::botCallbackDone, info),
		rpcFail(&HistoryWidget::botCallbackFail, info));
	Auth().data().requestItemRepaint(msg);

	if (_replyToId == msg->id) {
		cancelReply();
	}
	if (_keyboard->singleUse() && _keyboard->hasMarkup() && lastKeyboardUsed) {
		if (_kbShown) onKbToggle(false);
		_history->lastKeyboardUsed = true;
	}
}

void HistoryWidget::botCallbackDone(
		BotCallbackInfo info,
		const MTPmessages_BotCallbackAnswer &answer,
		mtpRequestId req) {
	auto item = App::histItemById(info.msgId);
	if (item) {
		if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (info.row < markup->rows.size()
				&& info.col < markup->rows[info.row].size()) {
				if (markup->rows[info.row][info.col].requestId == req) {
					markup->rows[info.row][info.col].requestId = 0;
					Auth().data().requestItemRepaint(item);
				}
			}
		}
	}
	if (answer.type() == mtpc_messages_botCallbackAnswer) {
		auto &answerData = answer.c_messages_botCallbackAnswer();
		if (answerData.has_message()) {
			if (answerData.is_alert()) {
				Ui::show(Box<InformBox>(qs(answerData.vmessage)));
			} else {
				Ui::Toast::Show(qs(answerData.vmessage));
			}
		} else if (answerData.has_url()) {
			auto url = qs(answerData.vurl);
			if (info.game) {
				url = AppendShareGameScoreUrl(url, info.msgId);
				BotGameUrlClickHandler(info.bot, url).onClick(Qt::LeftButton);
				if (item) {
					updateSendAction(item->history(), SendAction::Type::PlayGame);
				}
			} else {
				UrlClickHandler(url).onClick(Qt::LeftButton);
			}
		}
	}
}

bool HistoryWidget::botCallbackFail(
		BotCallbackInfo info,
		const RPCError &error,
		mtpRequestId req) {
	// show error?
	if (const auto item = App::histItemById(info.msgId)) {
		if (const auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (info.row < markup->rows.size()
				&& info.col < markup->rows[info.row].size()) {
				if (markup->rows[info.row][info.col].requestId == req) {
					markup->rows[info.row][info.col].requestId = 0;
					Auth().data().requestItemRepaint(item);
				}
			}
		}
	}
	return true;
}

bool HistoryWidget::insertBotCommand(const QString &cmd) {
	if (!canWriteMessage()) return false;

	auto insertingInlineBot = !cmd.isEmpty() && (cmd.at(0) == '@');
	auto toInsert = cmd;
	if (!toInsert.isEmpty() && !insertingInlineBot) {
		auto bot = _peer->isUser()
			? _peer
			: (App::hoveredLinkItem()
				? App::hoveredLinkItem()->data()->fromOriginal().get()
				: nullptr);
		if (bot && (!bot->isUser() || !bot->asUser()->botInfo)) {
			bot = nullptr;
		}
		auto username = bot ? bot->asUser()->username : QString();
		auto botStatus = _peer->isChat() ? _peer->asChat()->botStatus : (_peer->isMegagroup() ? _peer->asChannel()->mgInfo->botStatus : -1);
		if (toInsert.indexOf('@') < 0 && !username.isEmpty() && (botStatus == 0 || botStatus == 2)) {
			toInsert += '@' + username;
		}
	}
	toInsert += ' ';

	if (!insertingInlineBot) {
		auto &textWithTags = _field->getTextWithTags();
		TextWithTags textWithTagsToSet;
		QRegularExpressionMatch m = QRegularExpression(qsl("^/[A-Za-z_0-9]{0,64}(@[A-Za-z_0-9]{0,32})?(\\s|$)")).match(textWithTags.text);
		if (m.hasMatch()) {
			textWithTagsToSet = _field->getTextWithTagsPart(m.capturedLength());
		} else {
			textWithTagsToSet = textWithTags;
		}
		textWithTagsToSet.text = toInsert + textWithTagsToSet.text;
		for (auto &tag : textWithTagsToSet.tags) {
			tag.offset += toInsert.size();
		}
		_field->setTextWithTags(textWithTagsToSet);

		QTextCursor cur(_field->textCursor());
		cur.movePosition(QTextCursor::End);
		_field->setTextCursor(cur);
	} else {
		setFieldText({ toInsert, TextWithTags::Tags() }, TextUpdateEvent::SaveDraft, Ui::FlatTextarea::AddToUndoHistory);
		_field->setFocus();
		return true;
	}
	return false;
}

bool HistoryWidget::eventFilter(QObject *obj, QEvent *e) {
	if ((obj == _historyDown || obj == _unreadMentions) && e->type() == QEvent::Wheel) {
		return _scroll->viewportEvent(e);
	}
	return TWidget::eventFilter(obj, e);
}

bool HistoryWidget::wheelEventFromFloatPlayer(QEvent *e) {
	return _scroll->viewportEvent(e);
}

QRect HistoryWidget::rectForFloatPlayer() const {
	return mapToGlobal(_scroll->geometry());
}

void HistoryWidget::updateDragAreas() {
	_field->setAcceptDrops(_attachDragState == DragState::None);
	updateControlsGeometry();

	switch (_attachDragState) {
	case DragState::None:
		_attachDragDocument->otherLeave();
		_attachDragPhoto->otherLeave();
	break;
	case DragState::Files:
		_attachDragDocument->setText(lang(lng_drag_files_here), lang(lng_drag_to_send_files));
		_attachDragDocument->otherEnter();
		_attachDragPhoto->hideFast();
	break;
	case DragState::PhotoFiles:
		_attachDragDocument->setText(lang(lng_drag_images_here), lang(lng_drag_to_send_no_compression));
		_attachDragPhoto->setText(lang(lng_drag_photos_here), lang(lng_drag_to_send_quick));
		_attachDragDocument->otherEnter();
		_attachDragPhoto->otherEnter();
	break;
	case DragState::Image:
		_attachDragPhoto->setText(lang(lng_drag_images_here), lang(lng_drag_to_send_quick));
		_attachDragDocument->hideFast();
		_attachDragPhoto->otherEnter();
	break;
	};
}

bool HistoryWidget::readyToForward() const {
	return _canSendMessages && !_toForward.empty();
}

bool HistoryWidget::hasSilentToggle() const {
	return _peer
		&& _peer->isChannel()
		&& !_peer->isMegagroup()
		&& _peer->canWrite()
		&& !_peer->notifySettingsUnknown();
}

void HistoryWidget::inlineBotResolveDone(const MTPcontacts_ResolvedPeer &result) {
	_inlineBotResolveRequestId = 0;
//	Notify::inlineBotRequesting(false);
	UserData *resolvedBot = nullptr;
	if (result.type() == mtpc_contacts_resolvedPeer) {
		const auto &d(result.c_contacts_resolvedPeer());
		resolvedBot = App::feedUsers(d.vusers);
		if (resolvedBot) {
			if (!resolvedBot->botInfo || resolvedBot->botInfo->inlinePlaceholder.isEmpty()) {
				resolvedBot = nullptr;
			}
		}
		App::feedChats(d.vchats);
	}

	UserData *bot = nullptr;
	QString inlineBotUsername;
	auto query = _field->getInlineBotQuery(&bot, &inlineBotUsername);
	if (inlineBotUsername == _inlineBotUsername) {
		if (bot == Ui::LookingUpInlineBot) {
			bot = resolvedBot;
		}
	} else {
		bot = nullptr;
	}
	if (bot) {
		applyInlineBotQuery(bot, query);
	} else {
		clearInlineBot();
	}
}

bool HistoryWidget::inlineBotResolveFail(QString name, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_inlineBotResolveRequestId = 0;
//	Notify::inlineBotRequesting(false);
	if (name == _inlineBotUsername) {
		clearInlineBot();
	}
	return true;
}

bool HistoryWidget::isBotStart() const {
	const auto user = _peer ? _peer->asUser() : nullptr;
	if (!user
		|| !user->botInfo
		|| !_canSendMessages) {
		return false;
	} else if (!user->botInfo->startToken.isEmpty()) {
		return true;
	} else if (_history->isEmpty() && !_history->lastMessage()) {
		return true;
	}
	return false;
}

bool HistoryWidget::isBlocked() const {
	return _peer && _peer->isUser() && _peer->asUser()->isBlocked();
}

bool HistoryWidget::isJoinChannel() const {
	return _peer && _peer->isChannel() && !_peer->asChannel()->amIn();
}

bool HistoryWidget::isMuteUnmute() const {
	return _peer && _peer->isChannel() && _peer->asChannel()->isBroadcast() && !_peer->asChannel()->canPublish();
}

bool HistoryWidget::showRecordButton() const {
	return Media::Capture::instance()->available() && !_field->hasSendText() && !readyToForward() && !_editMsgId;
}

bool HistoryWidget::showInlineBotCancel() const {
	return _inlineBot && (_inlineBot != Ui::LookingUpInlineBot);
}

void HistoryWidget::updateSendButtonType() {
	auto type = [this] {
		using Type = Ui::SendButton::Type;
		if (_editMsgId) {
			return Type::Save;
		} else if (_isInlineBot) {
			return Type::Cancel;
		} else if (showRecordButton()) {
			return Type::Record;
		}
		return Type::Send;
	};
	_send->setType(type());
}

bool HistoryWidget::updateCmdStartShown() {
	bool cmdStartShown = false;
	if (_history && _peer && ((_peer->isChat() && _peer->asChat()->botStatus > 0) || (_peer->isMegagroup() && _peer->asChannel()->mgInfo->botStatus > 0) || (_peer->isUser() && _peer->asUser()->botInfo))) {
		if (!isBotStart() && !isBlocked() && !_keyboard->hasMarkup() && !_keyboard->forceReply()) {
			if (!_field->hasSendText()) {
				cmdStartShown = true;
			}
		}
	}
	if (_cmdStartShown != cmdStartShown) {
		_cmdStartShown = cmdStartShown;
		return true;
	}
	return false;
}

bool HistoryWidget::kbWasHidden() const {
	return _history && (_keyboard->forMsgId() == FullMsgId(_history->channelId(), _history->lastKeyboardHiddenId));
}

void HistoryWidget::dropEvent(QDropEvent *e) {
	_attachDragState = DragState::None;
	updateDragAreas();
	e->acceptProposedAction();
}

void HistoryWidget::onKbToggle(bool manual) {
	auto fieldEnabled = canWriteMessage() && !_a_show.animating();
	if (_kbShown || _kbReplyTo) {
		_botKeyboardHide->hide();
		if (_kbShown) {
			if (fieldEnabled) {
				_botKeyboardShow->show();
			}
			if (manual && _history) {
				_history->lastKeyboardHiddenId = _keyboard->forMsgId().msg;
			}

			_kbScroll->hide();
			_kbShown = false;

			_field->setMaxHeight(st::historyComposeFieldMaxHeight);

			_kbReplyTo = 0;
			if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_editMsgId && !_replyToId) {
				_fieldBarCancel->hide();
				updateMouseTracking();
			}
		} else {
			if (_history) {
				_history->clearLastKeyboard();
			} else {
				updateBotKeyboard();
			}
		}
	} else if (!_keyboard->hasMarkup() && _keyboard->forceReply()) {
		_botKeyboardHide->hide();
		_botKeyboardShow->hide();
		if (fieldEnabled) {
			_botCommandStart->show();
		}
		_kbScroll->hide();
		_kbShown = false;

		_field->setMaxHeight(st::historyComposeFieldMaxHeight);

		_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard->forceReply()) ? App::histItemById(_keyboard->forMsgId()) : 0;
		if (_kbReplyTo && !_editMsgId && !_replyToId && fieldEnabled) {
			updateReplyToName();
			_replyEditMsgText.setText(
				st::messageTextStyle,
				TextUtilities::Clean(_kbReplyTo->inReplyText()),
				Ui::DialogTextOptions());
			_fieldBarCancel->show();
			updateMouseTracking();
		}
		if (manual && _history) {
			_history->lastKeyboardHiddenId = 0;
		}
	} else if (fieldEnabled) {
		_botKeyboardHide->show();
		_botKeyboardShow->hide();
		_kbScroll->show();
		_kbShown = true;

		int32 maxh = qMin(_keyboard->height(), st::historyComposeFieldMaxHeight - (st::historyComposeFieldMaxHeight / 2));
		_field->setMaxHeight(st::historyComposeFieldMaxHeight - maxh);

		_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard->forceReply()) ? App::histItemById(_keyboard->forMsgId()) : 0;
		if (_kbReplyTo && !_editMsgId && !_replyToId) {
			updateReplyToName();
			_replyEditMsgText.setText(
				st::messageTextStyle,
				TextUtilities::Clean(_kbReplyTo->inReplyText()),
				Ui::DialogTextOptions());
			_fieldBarCancel->show();
			updateMouseTracking();
		}
		if (manual && _history) {
			_history->lastKeyboardHiddenId = 0;
		}
	}
	updateControlsGeometry();
	if (_botKeyboardHide->isHidden() && canWriteMessage() && !_a_show.animating()) {
		_tabbedSelectorToggle->show();
	} else {
		_tabbedSelectorToggle->hide();
	}
	updateField();
}

void HistoryWidget::onCmdStart() {
	setFieldText({ qsl("/"), TextWithTags::Tags() }, 0, Ui::FlatTextarea::AddToUndoHistory);
}

void HistoryWidget::setMembersShowAreaActive(bool active) {
	if (!active) {
		_membersDropdownShowTimer.stop();
	}
	if (active && _peer && (_peer->isChat() || _peer->isMegagroup())) {
		if (_membersDropdown) {
			_membersDropdown->otherEnter();
		} else if (!_membersDropdownShowTimer.isActive()) {
			_membersDropdownShowTimer.start(kShowMembersDropdownTimeoutMs);
		}
	} else if (_membersDropdown) {
		_membersDropdown->otherLeave();
	}
}

void HistoryWidget::onMembersDropdownShow() {
	if (!_membersDropdown) {
		_membersDropdown.create(this, st::membersInnerDropdown);
		_membersDropdown->setOwnedWidget(object_ptr<Profile::GroupMembersWidget>(this, _peer, Profile::GroupMembersWidget::TitleVisibility::Hidden, st::membersInnerItem));
		_membersDropdown->resizeToWidth(st::membersInnerWidth);

		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
		_membersDropdown->moveToLeft(0, _topBar->height());
		_membersDropdown->setHiddenCallback([this] { _membersDropdown.destroyDelayed(); });
	}
	_membersDropdown->otherEnter();
}

void HistoryWidget::onModerateKeyActivate(int index, bool *outHandled) {
	*outHandled = _keyboard->isHidden() ? false : _keyboard->moderateKeyActivate(index);
}

void HistoryWidget::pushTabbedSelectorToThirdSection(
		const Window::SectionShow &params) {
	if (!_history || !_tabbedPanel) {
		return;
	} else if (!_canSendMessages) {
		Auth().settings().setTabbedReplacedWithInfo(true);
		controller()->showPeerInfo(_peer, params.withThirdColumn());
		return;
	}
	Auth().settings().setTabbedReplacedWithInfo(false);
	_tabbedSelectorToggle->setColorOverrides(
		&st::historyAttachEmojiActive,
		&st::historyRecordVoiceFgActive,
		&st::historyRecordVoiceRippleBgActive);
	auto destroyingPanel = std::move(_tabbedPanel);
	auto memento = ChatHelpers::TabbedMemento(
		destroyingPanel->takeSelector(),
		base::lambda_guarded(this, [this](
				object_ptr<TabbedSelector> selector) {
			returnTabbedSelector(std::move(selector));
		}));
	controller()->resizeForThirdSection();
	controller()->showSection(std::move(memento), params.withThirdColumn());
	destroyingPanel.destroy();
}

void HistoryWidget::toggleTabbedSelectorMode() {
	if (_tabbedPanel) {
		if (controller()->canShowThirdSection()
			&& !Adaptive::OneColumn()) {
			Auth().settings().setTabbedSelectorSectionEnabled(true);
			Auth().saveSettingsDelayed();
			pushTabbedSelectorToThirdSection(
				Window::SectionShow::Way::ClearStack);
		} else {
			_tabbedPanel->toggleAnimated();
		}
	} else {
		controller()->closeThirdSection();
	}
}

void HistoryWidget::returnTabbedSelector(
		object_ptr<TabbedSelector> selector) {
	_tabbedPanel.create(
		this,
		controller(),
		std::move(selector));
	_tabbedPanel->hide();
	_tabbedSelectorToggle->installEventFilter(_tabbedPanel);
	_tabbedSelectorToggle->setColorOverrides(nullptr, nullptr, nullptr);
	_tabbedSelectorToggleTooltipShown = false;
	moveFieldControls();
}

void HistoryWidget::recountChatWidth() {
	auto layout = (width() < st::adaptiveChatWideWidth)
		? Adaptive::ChatLayout::Normal
		: Adaptive::ChatLayout::Wide;
	if (layout != Global::AdaptiveChatLayout()) {
		Global::SetAdaptiveChatLayout(layout);
		Adaptive::Changed().notify(true);
	}
}

void HistoryWidget::moveFieldControls() {
	auto keyboardHeight = 0;
	auto bottom = height();
	auto maxKeyboardHeight = st::historyComposeFieldMaxHeight - _field->height();
	_keyboard->resizeToWidth(width(), maxKeyboardHeight);
	if (_kbShown) {
		keyboardHeight = qMin(_keyboard->height(), maxKeyboardHeight);
		bottom -= keyboardHeight;
		_kbScroll->setGeometryToLeft(0, bottom, width(), keyboardHeight);
	}

// _attachToggle --------- _inlineResults -------------------------------------- _tabbedPanel --------- _fieldBarCancel
// (_attachDocument|_attachPhoto) _field (_silent|_cmdStart|_kbShow) (_kbHide|_tabbedSelectorToggle) [_broadcast] _send
// (_botStart|_unblock|_joinChannel|_muteUnmute)

	auto buttonsBottom = bottom - _attachToggle->height();
	auto left = 0;
	_attachToggle->moveToLeft(left, buttonsBottom); left += _attachToggle->width();
	_field->moveToLeft(left, bottom - _field->height() - st::historySendPadding);
	auto right = st::historySendRight;
	_send->moveToRight(right, buttonsBottom); right += _send->width();
	_tabbedSelectorToggle->moveToRight(right, buttonsBottom);
	updateTabbedSelectorToggleTooltipGeometry();
	_botKeyboardHide->moveToRight(right, buttonsBottom); right += _botKeyboardHide->width();
	_botKeyboardShow->moveToRight(right, buttonsBottom);
	_botCommandStart->moveToRight(right, buttonsBottom);
	if (_silent) {
		_silent->moveToRight(right, buttonsBottom);
	}

	_fieldBarCancel->moveToRight(0, _field->y() - st::historySendPadding - _fieldBarCancel->height());
	if (_inlineResults) {
		_inlineResults->moveBottom(_field->y() - st::historySendPadding);
	}
	if (_tabbedPanel) {
		_tabbedPanel->moveBottom(buttonsBottom);
	}

	auto fullWidthButtonRect = myrtlrect(
		0,
		bottom - _botStart->height(),
		width(),
		_botStart->height());
	_botStart->setGeometry(fullWidthButtonRect);
	_unblock->setGeometry(fullWidthButtonRect);
	_joinChannel->setGeometry(fullWidthButtonRect);
	_muteUnmute->setGeometry(fullWidthButtonRect);
}

void HistoryWidget::updateTabbedSelectorToggleTooltipGeometry() {
	if (_tabbedSelectorToggleTooltip) {
		auto toggle = _tabbedSelectorToggle->geometry();
		auto margin = st::historyAttachEmojiTooltipDelta;
		auto margins = QMargins(margin, margin, margin, margin);
		_tabbedSelectorToggleTooltip->pointAt(toggle.marginsRemoved(margins));
	}
}

void HistoryWidget::updateFieldSize() {
	auto kbShowShown = _history && !_kbShown && _keyboard->hasMarkup();
	auto fieldWidth = width() - _attachToggle->width() - st::historySendRight;
	fieldWidth -= _send->width();
	fieldWidth -= _tabbedSelectorToggle->width();
	if (kbShowShown) fieldWidth -= _botKeyboardShow->width();
	if (_cmdStartShown) fieldWidth -= _botCommandStart->width();
	if (_silent) fieldWidth -= _silent->width();

	if (_field->width() != fieldWidth) {
		_field->resize(fieldWidth, _field->height());
	} else {
		moveFieldControls();
	}
}

void HistoryWidget::clearInlineBot() {
	if (_inlineBot) {
		_inlineBot = nullptr;
		inlineBotChanged();
		_field->finishPlaceholder();
	}
	if (_inlineResults) {
		_inlineResults->clearInlineBot();
	}
	onCheckFieldAutocomplete();
}

void HistoryWidget::inlineBotChanged() {
	bool isInlineBot = showInlineBotCancel();
	if (_isInlineBot != isInlineBot) {
		_isInlineBot = isInlineBot;
		updateFieldPlaceholder();
		updateFieldSubmitSettings();
		updateControlsVisibility();
	}
}

void HistoryWidget::onFieldResize() {
	moveFieldControls();
	updateHistoryGeometry();
	updateField();
}

void HistoryWidget::onFieldFocused() {
	if (_list) {
		_list->clearSelected(true);
	}
}

void HistoryWidget::onCheckFieldAutocomplete() {
	if (!_history || _a_show.animating()) return;

	auto start = false;
	auto isInlineBot = _inlineBot && (_inlineBot != Ui::LookingUpInlineBot);
	auto query = isInlineBot ? QString() : _field->getMentionHashtagBotCommandPart(start);
	if (!query.isEmpty()) {
		if (query.at(0) == '#' && cRecentWriteHashtags().isEmpty() && cRecentSearchHashtags().isEmpty()) Local::readRecentHashtagsAndBots();
		if (query.at(0) == '@' && cRecentInlineBots().isEmpty()) Local::readRecentHashtagsAndBots();
		if (query.at(0) == '/' && _peer->isUser() && !_peer->asUser()->botInfo) return;
	}
	_fieldAutocomplete->showFiltered(_peer, query, start);
}

void HistoryWidget::updateFieldPlaceholder() {
	if (_editMsgId) {
		_field->setPlaceholder(langFactory(lng_edit_message_text));
	} else {
		if (_inlineBot && _inlineBot != Ui::LookingUpInlineBot) {
			auto text = _inlineBot->botInfo->inlinePlaceholder.mid(1);
			_field->setPlaceholder([text] { return text; }, _inlineBot->username.size() + 2);
		} else {
			const auto peer = _history ? _history->peer.get() : nullptr;
			_field->setPlaceholder(langFactory(
				(peer && peer->isChannel() && !peer->isMegagroup())
				? (peer->notifySilentPosts()
					? lng_broadcast_silent_ph
					: lng_broadcast_ph)
				: lng_message_ph));
		}
	}
	updateSendButtonType();
}

bool HistoryWidget::showSendingFilesError(
		const Storage::PreparedList &list) const {
	const auto text = [&] {
		if (const auto megagroup = _peer ? _peer->asMegagroup() : nullptr) {
			if (megagroup->restricted(ChannelRestriction::f_send_media)) {
				return lang(lng_restricted_send_media);
			}
		}
		if (!canWriteMessage()) {
			return lang(lng_forward_send_files_cant);
		}
		using Error = Storage::PreparedList::Error;
		switch (list.error) {
		case Error::None: return QString();
		case Error::EmptyFile:
		case Error::Directory:
		case Error::NonLocalUrl: return lng_send_image_empty(
			lt_name,
			list.errorData);
		case Error::TooLargeFile: return lng_send_image_too_large(
			lt_name,
			list.errorData);
		}
		return lang(lng_forward_send_files_cant);
	}();
	if (text.isEmpty()) {
		return false;
	}

	Ui::show(Box<InformBox>(text));
	return true;
}

bool HistoryWidget::confirmSendingFiles(const QStringList &files) {
	return confirmSendingFiles(files, CompressConfirm::Auto);
}

bool HistoryWidget::confirmSendingFiles(const QMimeData *data) {
	return confirmSendingFiles(data, CompressConfirm::Auto);
}

bool HistoryWidget::confirmSendingFiles(
		const QStringList &files,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	return confirmSendingFiles(
		Storage::PrepareMediaList(files, st::sendMediaPreviewSize),
		compressed,
		insertTextOnCancel);
}

bool HistoryWidget::confirmSendingFiles(
		Storage::PreparedList &&list,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	if (showSendingFilesError(list)) {
		return false;
	}

	const auto noCompressOption = (list.files.size() > 1)
		&& !list.allFilesForCompress
		&& !list.albumIsPossible;
	const auto boxCompressConfirm = noCompressOption
		? CompressConfirm::None
		: compressed;

	auto box = Box<SendFilesBox>(std::move(list), boxCompressConfirm);
	box->setConfirmedCallback(base::lambda_guarded(this, [=](
			Storage::PreparedList &&list,
			SendFilesWay way,
			const QString &caption,
			bool ctrlShiftEnter) {
		if (showSendingFilesError(list)) {
			return;
		}
		const auto type = (way == SendFilesWay::Files)
			? SendMediaType::File
			: SendMediaType::Photo;
		const auto album = (way == SendFilesWay::Album)
			? std::make_shared<SendingAlbum>()
			: nullptr;
		uploadFilesAfterConfirmation(
			std::move(list),
			type,
			caption,
			replyToId(),
			album);
	}));
	if (!insertTextOnCancel.isEmpty()) {
		box->setCancelledCallback(base::lambda_guarded(this, [=] {
			_field->textCursor().insertText(insertTextOnCancel);
		}));
	}

	ActivateWindowDelayed(controller());
	Ui::show(std::move(box));
	return true;
}

bool HistoryWidget::confirmSendingFiles(
		QImage &&image,
		QByteArray &&content,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	if (image.isNull()) {
		return false;
	}

	auto list = Storage::PrepareMediaFromImage(
		std::move(image),
		std::move(content),
		st::sendMediaPreviewSize);
	return confirmSendingFiles(
		std::move(list),
		compressed,
		insertTextOnCancel);
}

bool HistoryWidget::confirmSendingFiles(
		const QMimeData *data,
		CompressConfirm compressed,
		const QString &insertTextOnCancel) {
	if (!canWriteMessage()) {
		return false;
	}

	const auto hasImage = data->hasImage();

	if (const auto urls = data->urls(); !urls.empty()) {
		auto list = Storage::PrepareMediaList(
			urls,
			st::sendMediaPreviewSize);
		if (list.error != Storage::PreparedList::Error::NonLocalUrl) {
			if (list.error == Storage::PreparedList::Error::None
				|| !hasImage) {
				const auto emptyTextOnCancel = QString();
				confirmSendingFiles(
					std::move(list),
					compressed,
					emptyTextOnCancel);
				return true;
			}
		}
	}

	if (hasImage) {
		auto image = qvariant_cast<QImage>(data->imageData());
		if (!image.isNull()) {
			confirmSendingFiles(
				std::move(image),
				QByteArray(),
				compressed,
				insertTextOnCancel);
			return true;
		}
	}
	return false;
}

void HistoryWidget::uploadFiles(
		Storage::PreparedList &&list,
		SendMediaType type) {
	ActivateWindowDelayed(controller());

	auto caption = QString();
	uploadFilesAfterConfirmation(
		std::move(list),
		type,
		caption,
		replyToId());
}

void HistoryWidget::uploadFilesAfterConfirmation(
		Storage::PreparedList &&list,
		SendMediaType type,
		QString caption,
		MsgId replyTo,
		std::shared_ptr<SendingAlbum> album) {
	Assert(canWriteMessage());

	auto options = ApiWrap::SendOptions(_history);
	options.replyTo = replyTo;
	Auth().api().sendFiles(
		std::move(list),
		type,
		caption,
		album,
		options);
}

void HistoryWidget::uploadFile(
		const QByteArray &fileContent,
		SendMediaType type) {
	if (!canWriteMessage()) return;

	auto options = ApiWrap::SendOptions(_history);
	options.replyTo = replyToId();
	Auth().api().sendFile(fileContent, type, options);
}

void HistoryWidget::sendFileConfirmed(
		const std::shared_ptr<FileLoadResult> &file) {
	const auto channelId = peerToChannel(file->to.peer);
	const auto lastKeyboardUsed = lastForceReplyReplied(FullMsgId(
		channelId,
		file->to.replyTo));

	const auto newId = FullMsgId(channelId, clientMsgId());
	const auto groupId = file->album ? file->album->groupId : uint64(0);
	if (file->album) {
		const auto proj = [](const SendingAlbum::Item &item) {
			return item.taskId;
		};
		const auto it = ranges::find(file->album->items, file->taskId, proj);
		Assert(it != file->album->items.end());

		it->msgId = newId;
	}

	connect(&Auth().uploader(), SIGNAL(photoReady(const FullMsgId&,bool,const MTPInputFile&)), this, SLOT(onPhotoUploaded(const FullMsgId&,bool,const MTPInputFile&)), Qt::UniqueConnection);
	connect(&Auth().uploader(), SIGNAL(documentReady(const FullMsgId&,bool,const MTPInputFile&)), this, SLOT(onDocumentUploaded(const FullMsgId&,bool,const MTPInputFile&)), Qt::UniqueConnection);
	connect(&Auth().uploader(), SIGNAL(thumbDocumentReady(const FullMsgId&,bool,const MTPInputFile&,const MTPInputFile&)), this, SLOT(onThumbDocumentUploaded(const FullMsgId&,bool,const MTPInputFile&, const MTPInputFile&)), Qt::UniqueConnection);
	connect(&Auth().uploader(), SIGNAL(photoProgress(const FullMsgId&)), this, SLOT(onPhotoProgress(const FullMsgId&)), Qt::UniqueConnection);
	connect(&Auth().uploader(), SIGNAL(documentProgress(const FullMsgId&)), this, SLOT(onDocumentProgress(const FullMsgId&)), Qt::UniqueConnection);
	connect(&Auth().uploader(), SIGNAL(photoFailed(const FullMsgId&)), this, SLOT(onPhotoFailed(const FullMsgId&)), Qt::UniqueConnection);
	connect(&Auth().uploader(), SIGNAL(documentFailed(const FullMsgId&)), this, SLOT(onDocumentFailed(const FullMsgId&)), Qt::UniqueConnection);

	Auth().uploader().upload(newId, file);

	const auto history = App::history(file->to.peer);
	const auto peer = history->peer;

	auto options = ApiWrap::SendOptions(history);
	options.clearDraft = false;
	options.replyTo = file->to.replyTo;
	options.generateLocal = true;
	Auth().api().sendAction(options);

	auto flags = NewMessageFlags(peer) | MTPDmessage::Flag::f_media;
	if (file->to.replyTo) flags |= MTPDmessage::Flag::f_reply_to_msg_id;
	bool channelPost = peer->isChannel() && !peer->isMegagroup();
	bool silentPost = channelPost && file->to.silent;
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (!channelPost) {
		flags |= MTPDmessage::Flag::f_from_id;
	} else if (peer->asChannel()->addsSignature()) {
		flags |= MTPDmessage::Flag::f_post_author;
	}
	if (silentPost) {
		flags |= MTPDmessage::Flag::f_silent;
	}
	if (groupId) {
		flags |= MTPDmessage::Flag::f_grouped_id;
	}
	auto messageFromId = channelPost ? 0 : Auth().userId();
	auto messagePostAuthor = channelPost ? (Auth().user()->firstName + ' ' + Auth().user()->lastName) : QString();
	if (file->type == SendMediaType::Photo) {
		auto photoFlags = MTPDmessageMediaPhoto::Flag::f_photo | 0;
		auto photo = MTP_messageMediaPhoto(
			MTP_flags(photoFlags),
			file->photo,
			MTPint());
		history->addNewMessage(
			MTP_message(
				MTP_flags(flags),
				MTP_int(newId.msg),
				MTP_int(messageFromId),
				peerToMTP(file->to.peer),
				MTPnullFwdHeader,
				MTPint(),
				MTP_int(file->to.replyTo),
				MTP_int(unixtime()),
				MTP_string(file->caption),
				photo,
				MTPnullMarkup,
				MTPnullEntities, // #TODO caption entities
				MTP_int(1),
				MTPint(),
				MTP_string(messagePostAuthor),
				MTP_long(groupId)),
			NewMessageUnread);
	} else if (file->type == SendMediaType::File) {
		auto documentFlags = MTPDmessageMediaDocument::Flag::f_document | 0;
		auto document = MTP_messageMediaDocument(
			MTP_flags(documentFlags),
			file->document,
			MTPint());
		history->addNewMessage(
			MTP_message(
				MTP_flags(flags),
				MTP_int(newId.msg),
				MTP_int(messageFromId),
				peerToMTP(file->to.peer),
				MTPnullFwdHeader,
				MTPint(),
				MTP_int(file->to.replyTo),
				MTP_int(unixtime()),
				MTP_string(file->caption),
				document,
				MTPnullMarkup,
				MTPnullEntities, // #TODO caption entities
				MTP_int(1),
				MTPint(),
				MTP_string(messagePostAuthor),
				MTP_long(groupId)),
			NewMessageUnread);
	} else if (file->type == SendMediaType::Audio) {
		if (!peer->isChannel()) {
			flags |= MTPDmessage::Flag::f_media_unread;
		}
		auto documentFlags = MTPDmessageMediaDocument::Flag::f_document | 0;
		auto document = MTP_messageMediaDocument(
			MTP_flags(documentFlags),
			file->document,
			MTPint());
		history->addNewMessage(
			MTP_message(
				MTP_flags(flags),
				MTP_int(newId.msg),
				MTP_int(messageFromId),
				peerToMTP(file->to.peer),
				MTPnullFwdHeader,
				MTPint(),
				MTP_int(file->to.replyTo),
				MTP_int(unixtime()),
				MTP_string(file->caption),
				document,
				MTPnullMarkup,
				MTPnullEntities, // #TODO caption entities
				MTP_int(1),
				MTPint(),
				MTP_string(messagePostAuthor),
				MTP_long(groupId)),
			NewMessageUnread);
	}

	Auth().data().sendHistoryChangeNotifications();
	if (_peer && file->to.peer == _peer->id) {
		App::main()->historyToDown(_history);
	}
	App::main()->dialogsToUp();
}

void HistoryWidget::onPhotoUploaded(
		const FullMsgId &newId,
		bool silent,
		const MTPInputFile &file) {
	Auth().api().sendUploadedPhoto(newId, file, silent);
}

void HistoryWidget::onDocumentUploaded(
		const FullMsgId &newId,
		bool silent,
		const MTPInputFile &file) {
	Auth().api().sendUploadedDocument(newId, file, base::none, silent);
}

void HistoryWidget::onThumbDocumentUploaded(
		const FullMsgId &newId,
		bool silent,
		const MTPInputFile &file,
		const MTPInputFile &thumb) {
	Auth().api().sendUploadedDocument(newId, file, thumb, silent);
}

void HistoryWidget::onPhotoProgress(const FullMsgId &newId) {
	if (const auto item = App::histItemById(newId)) {
		const auto photo = item->media()
			? item->media()->photo()
			: nullptr;
		updateSendAction(item->history(), SendAction::Type::UploadPhoto, 0);
		Auth().data().requestItemRepaint(item);
	}
}

void HistoryWidget::onDocumentProgress(const FullMsgId &newId) {
	if (const auto item = App::histItemById(newId)) {
		const auto media = item->media();
		const auto document = media ? media->document() : nullptr;
		const auto sendAction = (document && document->isVoiceMessage())
			? SendAction::Type::UploadVoice
			: SendAction::Type::UploadFile;
		const auto progress = (document && document->uploading())
			? document->uploadingData->offset
			: 0;
		updateSendAction(
			item->history(),
			sendAction,
			progress);
		Auth().data().requestItemRepaint(item);
	}
}

void HistoryWidget::onPhotoFailed(const FullMsgId &newId) {
	if (const auto item = App::histItemById(newId)) {
		updateSendAction(
			item->history(),
			SendAction::Type::UploadPhoto,
			-1);
		Auth().data().requestItemRepaint(item);
	}
}

void HistoryWidget::onDocumentFailed(const FullMsgId &newId) {
	if (const auto item = App::histItemById(newId)) {
		const auto media = item->media();
		const auto document = media ? media->document() : nullptr;
		const auto sendAction = (document && document->isVoiceMessage())
			? SendAction::Type::UploadVoice
			: SendAction::Type::UploadFile;
		updateSendAction(item->history(), sendAction, -1);
		Auth().data().requestItemRepaint(item);
	}
}

void HistoryWidget::onReportSpamClicked() {
	auto text = lang(_peer->isUser() ? lng_report_spam_sure : ((_peer->isChat() || _peer->isMegagroup()) ? lng_report_spam_sure_group : lng_report_spam_sure_channel));
	Ui::show(Box<ConfirmBox>(text, lang(lng_report_spam_ok), st::attentionBoxButton, base::lambda_guarded(this, [this, peer = _peer] {
		if (_reportSpamRequest) return;

		Ui::hideLayer();
		if (auto user = peer->asUser()) {
			MTP::send(MTPcontacts_Block(user->inputUser), rpcDone(&HistoryWidget::blockDone, peer), RPCFailHandlerPtr(), 0, 5);
		}
		_reportSpamRequest = MTP::send(MTPmessages_ReportSpam(peer->input), rpcDone(&HistoryWidget::reportSpamDone, peer), rpcFail(&HistoryWidget::reportSpamFail));
	})));
}

void HistoryWidget::reportSpamDone(PeerData *peer, const MTPBool &result, mtpRequestId req) {
	Expects(peer != nullptr);
	if (req == _reportSpamRequest) {
		_reportSpamRequest = 0;
	}
	cRefReportSpamStatuses().insert(peer->id, dbiprsReportSent);
	Local::writeReportSpamStatuses();
	if (_peer == peer) {
		setReportSpamStatus(dbiprsReportSent);
		if (_reportSpamPanel) {
			_reportSpamPanel->setReported(_reportSpamStatus == dbiprsReportSent, peer);
		}
	}
}

bool HistoryWidget::reportSpamFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (req == _reportSpamRequest) {
		_reportSpamRequest = 0;
	}
	return false;
}

void HistoryWidget::onReportSpamHide() {
	if (_peer) {
		cRefReportSpamStatuses().insert(_peer->id, dbiprsHidden);
		Local::writeReportSpamStatuses();

		MTP::send(MTPmessages_HideReportSpam(_peer->input));
	}
	setReportSpamStatus(dbiprsHidden);
	updateControlsVisibility();
}

void HistoryWidget::onReportSpamClear() {
	Expects(_peer != nullptr);
	InvokeQueued(App::main(), [peer = _peer] {
		if (peer->isUser()) {
			App::main()->deleteConversation(peer);
		} else if (auto chat = peer->asChat()) {
			App::main()->deleteAndExit(chat);
		} else if (auto channel = peer->asChannel()) {
			if (channel->migrateFrom()) {
				App::main()->deleteConversation(channel->migrateFrom());
			}
			Auth().api().leaveChannel(channel);
		}
	});

	// Invalidates _peer.
	controller()->showBackFromStack();
}

void HistoryWidget::handleHistoryChange(not_null<const History*> history) {
	if (_list && (_history == history || _migrated == history)) {
		handlePendingHistoryUpdate();
		updateBotKeyboard();
		if (!_scroll->isHidden()) {
			const auto unblock = isBlocked();
			const auto botStart = isBotStart();
			const auto joinChannel = isJoinChannel();
			const auto muteUnmute = isMuteUnmute();
			const auto update = false
				|| (_unblock->isHidden() == unblock)
				|| (!unblock && _botStart->isHidden() == botStart)
				|| (!unblock
					&& !botStart
					&& _joinChannel->isHidden() == joinChannel)
				|| (!unblock
					&& !botStart
					&& !joinChannel
					&& _muteUnmute->isHidden() == muteUnmute);
			if (update) {
				updateControlsVisibility();
				updateControlsGeometry();
			}
		}
	}
}

void HistoryWidget::grapWithoutTopBarShadow() {
	grabStart();
	_topShadow->hide();
}

void HistoryWidget::grabFinish() {
	_inGrab = false;
	updateControlsGeometry();
	_topShadow->show();
}

bool HistoryWidget::skipItemRepaint() {
	auto ms = getms();
	if (_lastScrolled + kSkipRepaintWhileScrollMs <= ms) {
		return false;
	}
	_updateHistoryItems.start(
		_lastScrolled + kSkipRepaintWhileScrollMs - ms);
	return true;
}

void HistoryWidget::onUpdateHistoryItems() {
	if (!_list) return;

	auto ms = getms();
	if (_lastScrolled + kSkipRepaintWhileScrollMs <= ms) {
		_list->update();
	} else {
		_updateHistoryItems.start(_lastScrolled + kSkipRepaintWhileScrollMs - ms);
	}
}

PeerData *HistoryWidget::ui_getPeerForMouseAction() {
	return _peer;
}

void HistoryWidget::handlePendingHistoryUpdate() {
	if (hasPendingResizedItems() || _updateHistoryGeometryRequired) {
		updateHistoryGeometry();
		_list->update();
	}
}

void HistoryWidget::resizeEvent(QResizeEvent *e) {
	//updateTabbedSelectorSectionShown();
	recountChatWidth();
	updateControlsGeometry();
}

void HistoryWidget::updateControlsGeometry() {
	_topBar->resizeToWidth(width());
	_topBar->moveToLeft(0, 0);

	moveFieldControls();

	auto scrollAreaTop = _topBar->bottomNoMargins();
	if (_pinnedBar) {
		_pinnedBar->cancel->moveToLeft(width() - _pinnedBar->cancel->width(), scrollAreaTop);
		scrollAreaTop += st::historyReplyHeight;
		_pinnedBar->shadow->setGeometryToLeft(0, scrollAreaTop, width(), st::lineWidth);
	}
	if (_scroll->y() != scrollAreaTop) {
		_scroll->moveToLeft(0, scrollAreaTop);
		_fieldAutocomplete->setBoundings(_scroll->geometry());
	}
	if (_reportSpamPanel) {
		_reportSpamPanel->setGeometryToLeft(0, _scroll->y(), width(), _reportSpamPanel->height());
	}

	updateHistoryGeometry(false, false, { ScrollChangeAdd, App::main() ? App::main()->contentScrollAddToY() : 0 });

	updateFieldSize();

	updateHistoryDownPosition();

	if (_membersDropdown) {
		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
	}

	switch (_attachDragState) {
	case DragState::Files:
		_attachDragDocument->resize(width() - st::dragMargin.left() - st::dragMargin.right(), height() - st::dragMargin.top() - st::dragMargin.bottom());
		_attachDragDocument->move(st::dragMargin.left(), st::dragMargin.top());
	break;
	case DragState::PhotoFiles:
		_attachDragDocument->resize(width() - st::dragMargin.left() - st::dragMargin.right(), (height() - st::dragMargin.top() - st::dragMargin.bottom()) / 2);
		_attachDragDocument->move(st::dragMargin.left(), st::dragMargin.top());
		_attachDragPhoto->resize(_attachDragDocument->width(), _attachDragDocument->height());
		_attachDragPhoto->move(st::dragMargin.left(), height() - _attachDragPhoto->height() - st::dragMargin.bottom());
	break;
	case DragState::Image:
		_attachDragPhoto->resize(width() - st::dragMargin.left() - st::dragMargin.right(), height() - st::dragMargin.top() - st::dragMargin.bottom());
		_attachDragPhoto->move(st::dragMargin.left(), st::dragMargin.top());
	break;
	}

	auto topShadowLeft = (Adaptive::OneColumn() || _inGrab) ? 0 : st::lineWidth;
	auto topShadowRight = (Adaptive::ThreeColumn() && !_inGrab && _peer) ? st::lineWidth : 0;
	_topShadow->setGeometryToLeft(
		topShadowLeft,
		_topBar->bottomNoMargins(),
		width() - topShadowLeft - topShadowRight,
		st::lineWidth);
}

void HistoryWidget::itemRemoved(not_null<const HistoryItem*> item) {
	if (item == _replyEditMsg) {
		if (_editMsgId) {
			cancelEdit();
		} else {
			cancelReply();
		}
	}
	while (item == _replyReturn) {
		calcNextReplyReturn();
	}
	if (_pinnedBar && item->id == _pinnedBar->msgId) {
		pinnedMsgVisibilityUpdated();
	}
	if (_kbReplyTo && item == _kbReplyTo) {
		onKbToggle();
		_kbReplyTo = 0;
	}
	auto found = ranges::find(_toForward, item);
	if (found != _toForward.end()) {
		_toForward.erase(found);
		updateForwardingTexts();
		if (_toForward.empty()) {
			updateControlsVisibility();
			updateControlsGeometry();
		}
	}
}

void HistoryWidget::itemEdited(HistoryItem *item) {
	if (item == _replyEditMsg) {
		updateReplyEditTexts(true);
	}
	if (_pinnedBar && item->id == _pinnedBar->msgId) {
		updatePinnedBar(true);
	}
}

void HistoryWidget::updateScrollColors() {
	_scroll->updateBars();
}

MsgId HistoryWidget::replyToId() const {
	return _replyToId ? _replyToId : (_kbReplyTo ? _kbReplyTo->id : 0);
}

int HistoryWidget::countInitialScrollTop() {
	auto result = ScrollMax;
	if (_history->scrollTopItem || (_migrated && _migrated->scrollTopItem)) {
		result = _list->historyScrollTop();
	} else if (_showAtMsgId && (_showAtMsgId > 0 || -_showAtMsgId < ServerMaxMsgId)) {
		auto item = getItemFromHistoryOrMigrated(_showAtMsgId);
		auto itemTop = _list->itemTop(item);
		if (itemTop < 0) {
			setMsgId(0);
			return countInitialScrollTop();
		} else {
			const auto view = item->mainView();
			Assert(view != nullptr);

			result = itemTopForHighlight(view);
			enqueueMessageHighlight(view);
		}
	} else if (const auto top = unreadBarTop()) {
		result = *top;
	} else {
		return countAutomaticScrollTop();
	}
	return qMin(result, _scroll->scrollTopMax());
}

int HistoryWidget::countAutomaticScrollTop() {
	auto result = ScrollMax;
	if (const auto unread = firstUnreadMessage()) {
		result = _list->itemTop(unread);
		const auto possibleUnreadBarTop = _scroll->scrollTopMax()
			+ HistoryView::UnreadBar::height()
			- HistoryView::UnreadBar::marginTop();
		if (result < possibleUnreadBarTop) {
			const auto history = unread->data()->history();
			history->addUnreadBar();
			if (hasPendingResizedItems()) {
				updateListSize();
			}
			if (history->unreadBar() != nullptr) {
				setMsgId(ShowAtUnreadMsgId);
				result = countInitialScrollTop();
				App::wnd()->checkHistoryActivation();
				return result;
			}
		}
	}
	return qMin(result, _scroll->scrollTopMax());
}

void HistoryWidget::updateHistoryGeometry(bool initial, bool loadedDown, const ScrollChange &change) {
	if (!_history || (initial && _historyInited) || (!initial && !_historyInited)) {
		return;
	}
	if (_firstLoadRequest || _a_show.animating()) {
		return; // scrollTopMax etc are not working after recountHistoryGeometry()
	}

	auto newScrollHeight = height() - _topBar->height();
	if (!editingMessage() && (isBlocked() || isBotStart() || isJoinChannel() || isMuteUnmute())) {
		newScrollHeight -= _unblock->height();
	} else {
		if (editingMessage() || _canSendMessages) {
			newScrollHeight -= (_field->height() + 2 * st::historySendPadding);
		} else if (isRestrictedWrite()) {
			newScrollHeight -= _unblock->height();
		}
		if (_editMsgId || replyToId() || readyToForward() || (_previewData && _previewData->pendingTill >= 0)) {
			newScrollHeight -= st::historyReplyHeight;
		}
		if (_kbShown) {
			newScrollHeight -= _kbScroll->height();
		}
	}
	if (_pinnedBar) {
		newScrollHeight -= st::historyReplyHeight;
	}
	auto wasScrollTop = _scroll->scrollTop();
	auto wasScrollTopMax = _scroll->scrollTopMax();
	auto wasAtBottom = wasScrollTop + 1 > wasScrollTopMax;
	auto needResize = (_scroll->width() != width()) || (_scroll->height() != newScrollHeight);
	if (needResize) {
		_scroll->resize(width(), newScrollHeight);
		// on initial updateListSize we didn't put the _scroll->scrollTop correctly yet
		// so visibleAreaUpdated() call will erase it with the new (undefined) value
		if (!initial) {
			visibleAreaUpdated();
		}

		_fieldAutocomplete->setBoundings(_scroll->geometry());
		if (!_historyDownShown.animating()) {
			// _historyDown is a child widget of _scroll, not me.
			_historyDown->moveToRight(st::historyToDownPosition.x(), _scroll->height() - _historyDown->height() - st::historyToDownPosition.y());
			if (!_unreadMentionsShown.animating()) {
				// _unreadMentions is a child widget of _scroll, not me.
				auto additionalSkip = _historyDownIsShown ? (_historyDown->height() + st::historyUnreadMentionsSkip) : 0;
				_unreadMentions->moveToRight(st::historyToDownPosition.x(), _scroll->height() - additionalSkip - st::historyToDownPosition.y());
			}
		}

		controller()->floatPlayerAreaUpdated().notify(true);
	}

	updateListSize();
	_updateHistoryGeometryRequired = false;

	if ((!initial && !wasAtBottom)
		|| (loadedDown
			&& (!_history->firstUnreadMessage()
				|| _history->unreadBar()
				|| _history->loadedAtBottom())
			&& (!_migrated
				|| !_migrated->firstUnreadMessage()
				|| _migrated->unreadBar()
				|| _history->loadedAtBottom()))) {
		const auto historyScrollTop = _list->historyScrollTop();
		if (!wasAtBottom && historyScrollTop == ScrollMax) {
			// History scroll top was not inited yet.
			// If we're showing locally unread messages, we get here
			// from destroyUnreadBar() before we have time to scroll
			// to good initial position, like top of an unread bar.
			return;
		}
		auto toY = qMin(_list->historyScrollTop(), _scroll->scrollTopMax());
		if (change.type == ScrollChangeAdd) {
			toY += change.value;
		} else if (change.type == ScrollChangeNoJumpToBottom) {
			toY = wasScrollTop;
		} else if (_addToScroll) {
			toY += _addToScroll;
			_addToScroll = 0;
		}
		toY = snap(toY, 0, _scroll->scrollTopMax());
		if (_scroll->scrollTop() == toY) {
			visibleAreaUpdated();
		} else {
			synteticScrollToY(toY);
		}
		return;
	}

	if (initial) {
		_historyInited = true;
		_scrollToAnimation.finish();
	}
	auto newScrollTop = initial
		? countInitialScrollTop()
		: countAutomaticScrollTop();
	if (_scroll->scrollTop() == newScrollTop) {
		visibleAreaUpdated();
	} else {
		synteticScrollToY(newScrollTop);
	}
}

void HistoryWidget::updateListSize() {
	_list->recountHistoryGeometry();
	auto washidden = _scroll->isHidden();
	if (washidden) {
		_scroll->show();
	}
	_list->updateSize();
	if (washidden) {
		_scroll->hide();
	}
	_updateHistoryGeometryRequired = true;
}

bool HistoryWidget::hasPendingResizedItems() const {
	return (_history && _history->hasPendingResizedItems())
		|| (_migrated && _migrated->hasPendingResizedItems());
}

base::optional<int> HistoryWidget::unreadBarTop() const {
	auto getUnreadBar = [this]() -> HistoryView::Element* {
		if (const auto bar = _migrated ? _migrated->unreadBar() : nullptr) {
			return bar;
		} else if (const auto bar = _history->unreadBar()) {
			return bar;
		}
		return nullptr;
	};
	if (const auto bar = getUnreadBar()) {
		const auto result = _list->itemTop(bar)
			+ HistoryView::UnreadBar::marginTop();
		if (bar->Has<HistoryView::DateBadge>()) {
			return result + bar->Get<HistoryView::DateBadge>()->height();
		}
		return result;
	}
	return base::none;
}

HistoryView::Element *HistoryWidget::firstUnreadMessage() const {
	if (_migrated) {
		if (const auto result = _migrated->firstUnreadMessage()) {
			return result;
		}
	}
	return _history ? _history->firstUnreadMessage() : nullptr;
}

void HistoryWidget::addMessagesToFront(PeerData *peer, const QVector<MTPMessage> &messages) {
	_list->messagesReceived(peer, messages);
	if (!_firstLoadRequest) {
		updateHistoryGeometry();
		adjustHighlightedMessageToMigrated();
		updateBotKeyboard();
	}
}

void HistoryWidget::addMessagesToBack(PeerData *peer, const QVector<MTPMessage> &messages) {
	_list->messagesReceivedDown(peer, messages);
	if (!_firstLoadRequest) {
		updateHistoryGeometry(false, true, { ScrollChangeNoJumpToBottom, 0 });
	}
}

void HistoryWidget::countHistoryShowFrom() {
	if (_migrated
		&& _showAtMsgId == ShowAtUnreadMsgId
		&& _migrated->unreadCount()) {
		_migrated->calculateFirstUnreadMessage();
	}
	if ((_migrated && _migrated->firstUnreadMessage())
		|| (_showAtMsgId != ShowAtUnreadMsgId)
		|| !_history->unreadCount()) {
		_history->unsetFirstUnreadMessage();
	} else {
		_history->calculateFirstUnreadMessage();
	}
}

void HistoryWidget::updateBotKeyboard(History *h, bool force) {
	if (h && h != _history && h != _migrated) {
		return;
	}

	bool changed = false;
	bool wasVisible = _kbShown || _kbReplyTo;
	if ((_replyToId && !_replyEditMsg) || _editMsgId || !_history) {
		changed = _keyboard->updateMarkup(nullptr, force);
	} else if (_replyToId && _replyEditMsg) {
		changed = _keyboard->updateMarkup(_replyEditMsg, force);
	} else {
		HistoryItem *keyboardItem = _history->lastKeyboardId ? App::histItemById(_channel, _history->lastKeyboardId) : nullptr;
		changed = _keyboard->updateMarkup(keyboardItem, force);
	}
	updateCmdStartShown();
	if (!changed) return;

	bool hasMarkup = _keyboard->hasMarkup(), forceReply = _keyboard->forceReply() && (!_replyToId || !_replyEditMsg);
	if (hasMarkup || forceReply) {
		if (_keyboard->singleUse() && _keyboard->hasMarkup() && _keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId) && _history->lastKeyboardUsed) {
			_history->lastKeyboardHiddenId = _history->lastKeyboardId;
		}
		if (!isBotStart() && !isBlocked() && _canSendMessages && (wasVisible || (_replyToId && _replyEditMsg) || (!_field->hasSendText() && !kbWasHidden()))) {
			if (!_a_show.animating()) {
				if (hasMarkup) {
					_kbScroll->show();
					_tabbedSelectorToggle->hide();
					_botKeyboardHide->show();
				} else {
					_kbScroll->hide();
					_tabbedSelectorToggle->show();
					_botKeyboardHide->hide();
				}
				_botKeyboardShow->hide();
				_botCommandStart->hide();
			}
			int32 maxh = hasMarkup ? qMin(_keyboard->height(), st::historyComposeFieldMaxHeight - (st::historyComposeFieldMaxHeight / 2)) : 0;
			_field->setMaxHeight(st::historyComposeFieldMaxHeight - maxh);
			_kbShown = hasMarkup;
			_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard->forceReply()) ? App::histItemById(_keyboard->forMsgId()) : 0;
			if (_kbReplyTo && !_replyToId) {
				updateReplyToName();
				_replyEditMsgText.setText(
					st::messageTextStyle,
					TextUtilities::Clean(_kbReplyTo->inReplyText()),
					Ui::DialogTextOptions());
				_fieldBarCancel->show();
				updateMouseTracking();
			}
		} else {
			if (!_a_show.animating()) {
				_kbScroll->hide();
				_tabbedSelectorToggle->show();
				_botKeyboardHide->hide();
				_botKeyboardShow->show();
				_botCommandStart->hide();
			}
			_field->setMaxHeight(st::historyComposeFieldMaxHeight);
			_kbShown = false;
			_kbReplyTo = 0;
			if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId) {
				_fieldBarCancel->hide();
				updateMouseTracking();
			}
		}
	} else {
		if (!_scroll->isHidden()) {
			_kbScroll->hide();
			_tabbedSelectorToggle->show();
			_botKeyboardHide->hide();
			_botKeyboardShow->hide();
			_botCommandStart->show();
		}
		_field->setMaxHeight(st::historyComposeFieldMaxHeight);
		_kbShown = false;
		_kbReplyTo = 0;
		if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId && !_editMsgId) {
			_fieldBarCancel->hide();
			updateMouseTracking();
		}
	}
	updateControlsGeometry();
	update();
}

void HistoryWidget::updateHistoryDownPosition() {
	// _historyDown is a child widget of _scroll, not me.
	auto top = anim::interpolate(0, _historyDown->height() + st::historyToDownPosition.y(), _historyDownShown.current(_historyDownIsShown ? 1. : 0.));
	_historyDown->moveToRight(st::historyToDownPosition.x(), _scroll->height() - top);
	auto shouldBeHidden = !_historyDownIsShown && !_historyDownShown.animating();
	if (shouldBeHidden != _historyDown->isHidden()) {
		_historyDown->setVisible(!shouldBeHidden);
	}
	updateUnreadMentionsPosition();
}

void HistoryWidget::updateHistoryDownVisibility() {
	if (_a_show.animating()) return;

	auto haveUnreadBelowBottom = [&](History *history) {
		if (!_list || !history || history->unreadCount() <= 0) {
			return false;
		}
		const auto unread = history->firstUnreadMessage();
		if (!unread) {
			return false;
		}
		const auto top = _list->itemTop(unread);
		return (top >= _scroll->scrollTop() + _scroll->height());
	};
	auto historyDownIsVisible = [&] {
		if (!_list || _firstLoadRequest) {
			return false;
		}
		if (!_history->loadedAtBottom() || _replyReturn) {
			return true;
		}
		const auto top = _scroll->scrollTop() + st::historyToDownShownAfter;
		if (top < _scroll->scrollTopMax()) {
			return true;
		}
		if (haveUnreadBelowBottom(_history)
			|| haveUnreadBelowBottom(_migrated)) {
			return true;
		}
		return false;
	};
	auto historyDownIsShown = historyDownIsVisible();
	if (_historyDownIsShown != historyDownIsShown) {
		_historyDownIsShown = historyDownIsShown;
		_historyDownShown.start([this] { updateHistoryDownPosition(); }, _historyDownIsShown ? 0. : 1., _historyDownIsShown ? 1. : 0., st::historyToDownDuration);
	}
}

void HistoryWidget::updateUnreadMentionsPosition() {
	// _unreadMentions is a child widget of _scroll, not me.
	auto right = anim::interpolate(-_unreadMentions->width(), st::historyToDownPosition.x(), _unreadMentionsShown.current(_unreadMentionsIsShown ? 1. : 0.));
	auto shift = anim::interpolate(0, _historyDown->height() + st::historyUnreadMentionsSkip, _historyDownShown.current(_historyDownIsShown ? 1. : 0.));
	auto top = _scroll->height() - _unreadMentions->height() - st::historyToDownPosition.y() - shift;
	_unreadMentions->moveToRight(right, top);
	auto shouldBeHidden = !_unreadMentionsIsShown && !_unreadMentionsShown.animating();
	if (shouldBeHidden != _unreadMentions->isHidden()) {
		_unreadMentions->setVisible(!shouldBeHidden);
	}
}

void HistoryWidget::updateUnreadMentionsVisibility() {
	if (_a_show.animating()) return;

	auto showUnreadMentions = _peer && (_peer->isChat() || _peer->isMegagroup());
	if (showUnreadMentions) {
		Auth().api().preloadEnoughUnreadMentions(_history);
	}
	auto unreadMentionsIsVisible = [this, showUnreadMentions] {
		if (!showUnreadMentions || _firstLoadRequest) {
			return false;
		}
		return (_history->getUnreadMentionsLoadedCount() > 0);
	};
	auto unreadMentionsIsShown = unreadMentionsIsVisible();
	if (unreadMentionsIsShown) {
		_unreadMentions->setUnreadCount(_history->getUnreadMentionsCount());
	}
	if (_unreadMentionsIsShown != unreadMentionsIsShown) {
		_unreadMentionsIsShown = unreadMentionsIsShown;
		_unreadMentionsShown.start([this] { updateUnreadMentionsPosition(); }, _unreadMentionsIsShown ? 0. : 1., _unreadMentionsIsShown ? 1. : 0., st::historyToDownDuration);
	}
}

void HistoryWidget::mousePressEvent(QMouseEvent *e) {
	_replyForwardPressed = QRect(0, _field->y() - st::historySendPadding - st::historyReplyHeight, st::historyReplySkip, st::historyReplyHeight).contains(e->pos());
	if (_replyForwardPressed && !_fieldBarCancel->isHidden()) {
		updateField();
	} else if (_inReplyEditForward) {
		if (readyToForward()) {
			const auto items = std::move(_toForward);
			App::main()->cancelForwarding(_history);
			Window::ShowForwardMessagesBox(ranges::view::all(
				items
			) | ranges::view::transform([](not_null<HistoryItem*> item) {
				return item->fullId();
			}) | ranges::to_vector);
		} else {
			Ui::showPeerHistory(_peer, _editMsgId ? _editMsgId : replyToId());
		}
	} else if (_inPinnedMsg) {
		Assert(_pinnedBar != nullptr);
		Ui::showPeerHistory(_peer, _pinnedBar->msgId);
	}
}

void HistoryWidget::keyPressEvent(QKeyEvent *e) {
	if (!_history) return;

	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Back) {
		controller()->showBackFromStack();
		emit cancelled();
	} else if (e->key() == Qt::Key_PageDown) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_PageUp) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_Down) {
		if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			_scroll->keyPressEvent(e);
		} else if ((e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier)) == Qt::ControlModifier) {
			replyToNextMessage();
		}
	} else if (e->key() == Qt::Key_Up) {
		if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			const auto item = _history
				? _history->lastSentMessage()
				: nullptr;
			if (item
				&& item->allowsEdit(unixtime())
				&& _field->isEmpty()
				&& !_editMsgId
				&& !_replyToId) {
				editMessage(item);
				return;
			}
			_scroll->keyPressEvent(e);
		} else if ((e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier)) == Qt::ControlModifier) {
			replyToPreviousMessage();
		}
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		onListEnterPressed();
	} else {
		e->ignore();
	}
}

void HistoryWidget::replyToPreviousMessage() {
	if (!_history || _editMsgId) {
		return;
	}
	const auto fullId = FullMsgId(
		_history->channelId(),
		_replyToId);
	if (const auto item = App::histItemById(fullId)) {
		if (const auto view = item->mainView()) {
			if (const auto previousView = view->previousInBlocks()) {
				const auto previous = previousView->data();
				Ui::showPeerHistoryAtItem(previous);
				replyToMessage(previous);
			}
		}
	} else if (const auto previous = _history->lastMessage()) {
		Ui::showPeerHistoryAtItem(previous);
		replyToMessage(previous);
	}
}

void HistoryWidget::replyToNextMessage() {
	if (!_history || _editMsgId) {
		return;
	}
	const auto fullId = FullMsgId(
		_history->channelId(),
		_replyToId);
	if (const auto item = App::histItemById(fullId)) {
		if (const auto view = item->mainView()) {
			if (const auto nextView = view->nextInBlocks()) {
				const auto next = nextView->data();
				Ui::showPeerHistoryAtItem(next);
				replyToMessage(next);
			}
		}
	}
}

void HistoryWidget::onFieldTabbed() {
	if (!_fieldAutocomplete->isHidden()) {
		_fieldAutocomplete->chooseSelected(FieldAutocomplete::ChooseMethod::ByTab);
	}
}

bool HistoryWidget::onStickerSend(DocumentData *sticker) {
	if (auto megagroup = _peer ? _peer->asMegagroup() : nullptr) {
		if (megagroup->restricted(ChannelRestriction::f_send_stickers)) {
			Ui::show(
				Box<InformBox>(lang(lng_restricted_send_stickers)),
				LayerOption::KeepOther);
			return false;
		}
	}
	return sendExistingDocument(sticker, TextWithEntities());
}

void HistoryWidget::onPhotoSend(PhotoData *photo) {
	if (auto megagroup = _peer ? _peer->asMegagroup() : nullptr) {
		if (megagroup->restricted(ChannelRestriction::f_send_media)) {
			Ui::show(
				Box<InformBox>(lang(lng_restricted_send_media)),
				LayerOption::KeepOther);
			return;
		}
	}
	sendExistingPhoto(photo, TextWithEntities());
}

void HistoryWidget::onInlineResultSend(
		InlineBots::Result *result,
		UserData *bot) {
	if (!_peer || !_peer->canWrite() || !result) {
		return;
	}

	auto errorText = result->getErrorOnSend(_history);
	if (!errorText.isEmpty()) {
		Ui::show(Box<InformBox>(errorText));
		return;
	}

	auto options = ApiWrap::SendOptions(_history);
	options.clearDraft = true;
	options.replyTo = replyToId();
	options.generateLocal = true;
	Auth().api().sendAction(options);

	uint64 randomId = rand_value<uint64>();
	FullMsgId newId(_channel, clientMsgId());

	auto flags = NewMessageFlags(_peer) | MTPDmessage::Flag::f_media;
	auto sendFlags = MTPmessages_SendInlineBotResult::Flag::f_clear_draft | 0;
	if (options.replyTo) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendInlineBotResult::Flag::f_reply_to_msg_id;
	}
	bool channelPost = _peer->isChannel() && !_peer->isMegagroup();
	bool silentPost = channelPost && _peer->notifySilentPosts();
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (!channelPost) {
		flags |= MTPDmessage::Flag::f_from_id;
	} else if (_peer->asChannel()->addsSignature()) {
		flags |= MTPDmessage::Flag::f_post_author;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendInlineBotResult::Flag::f_silent;
	}
	if (bot) {
		flags |= MTPDmessage::Flag::f_via_bot_id;
	}

	auto messageFromId = channelPost ? 0 : Auth().userId();
	auto messagePostAuthor = channelPost ? (Auth().user()->firstName + ' ' + Auth().user()->lastName) : QString();
	MTPint messageDate = MTP_int(unixtime());
	UserId messageViaBotId = bot ? peerToUser(bot->id) : 0;
	MsgId messageId = newId.msg;

	result->addToHistory(
		_history,
		flags,
		messageId,
		messageFromId,
		messageDate,
		messageViaBotId,
		options.replyTo,
		messagePostAuthor);

	_history->sendRequestId = MTP::send(
		MTPmessages_SendInlineBotResult(
			MTP_flags(sendFlags),
			_peer->input,
			MTP_int(options.replyTo),
			MTP_long(randomId),
			MTP_long(result->getQueryId()),
			MTP_string(result->getId())),
		App::main()->rpcDone(&MainWidget::sentUpdatesReceived),
		App::main()->rpcFail(&MainWidget::sendMessageFail),
		0,
		0,
		_history->sendRequestId);
	App::main()->finishForwarding(_history);

	App::historyRegRandom(randomId, newId);

	clearFieldText();
	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	RecentInlineBots &bots(cRefRecentInlineBots());
	int32 index = bots.indexOf(bot);
	if (index) {
		if (index > 0) {
			bots.removeAt(index);
		} else if (bots.size() >= RecentInlineBotsLimit) {
			bots.resize(RecentInlineBotsLimit - 1);
		}
		bots.push_front(bot);
		Local::writeRecentHashtagsAndBots();
	}

	hideSelectorControlsAnimated();

	_field->setFocus();
}

HistoryWidget::PinnedBar::PinnedBar(MsgId msgId, HistoryWidget *parent)
: msgId(msgId)
, cancel(parent, st::historyReplyCancel)
, shadow(parent) {
}

HistoryWidget::PinnedBar::~PinnedBar() {
	cancel.destroyDelayed();
	shadow.destroyDelayed();
}

void HistoryWidget::updatePinnedBar(bool force) {
	update();
	if (!_pinnedBar) {
		return;
	}
	if (!force) {
		if (_pinnedBar->msg) {
			return;
		}
	}

	Assert(_history != nullptr);
	if (!_pinnedBar->msg) {
		_pinnedBar->msg = App::histItemById(_history->channelId(), _pinnedBar->msgId);
	}
	if (_pinnedBar->msg) {
		_pinnedBar->text.setText(
			st::messageTextStyle,
			TextUtilities::Clean(_pinnedBar->msg->notificationText()),
			Ui::DialogTextOptions());
		update();
	} else if (force) {
		if (auto channel = _peer ? _peer->asChannel() : nullptr) {
			channel->clearPinnedMessage();
		}
		destroyPinnedBar();
		updateControlsGeometry();
	}
}

bool HistoryWidget::pinnedMsgVisibilityUpdated() {
	auto result = false;
	auto pinnedId = [&] {
		if (auto channel = _peer ? _peer->asChannel() : nullptr) {
			return channel->pinnedMessageId();
		}
		return 0;
	}();
	if (pinnedId && !_peer->asChannel()->canPinMessages()) {
		auto it = Global::HiddenPinnedMessages().constFind(_peer->id);
		if (it != Global::HiddenPinnedMessages().cend()) {
			if (it.value() == pinnedId) {
				pinnedId = 0;
			} else {
				Global::RefHiddenPinnedMessages().remove(_peer->id);
				Local::writeUserSettings();
			}
		}
	}
	if (pinnedId) {
		if (!_pinnedBar) {
			_pinnedBar = std::make_unique<PinnedBar>(pinnedId, this);
			if (_a_show.animating()) {
				_pinnedBar->cancel->hide();
				_pinnedBar->shadow->hide();
			} else {
				_pinnedBar->cancel->show();
				_pinnedBar->shadow->show();
			}
			connect(_pinnedBar->cancel, SIGNAL(clicked()), this, SLOT(onPinnedHide()));
			orderWidgets();

			updatePinnedBar();
			result = true;

			const auto barTop = unreadBarTop();
			if (!barTop || _scroll->scrollTop() != *barTop) {
				synteticScrollToY(_scroll->scrollTop() + st::historyReplyHeight);
			}
		} else if (_pinnedBar->msgId != pinnedId) {
			_pinnedBar->msgId = pinnedId;
			_pinnedBar->msg = nullptr;
			_pinnedBar->text.clear();
			updatePinnedBar();
		}
		if (!_pinnedBar->msg) {
			Auth().api().requestMessageData(
				_peer->asChannel(),
				_pinnedBar->msgId,
				replyEditMessageDataCallback());
		}
	} else if (_pinnedBar) {
		destroyPinnedBar();
		result = true;
		const auto barTop = unreadBarTop();
		if (!barTop || _scroll->scrollTop() != *barTop) {
			synteticScrollToY(_scroll->scrollTop() - st::historyReplyHeight);
		}
		updateControlsGeometry();
	}
	return result;
}

void HistoryWidget::destroyPinnedBar() {
	_pinnedBar.reset();
	_inPinnedMsg = false;
}

bool HistoryWidget::sendExistingDocument(
		DocumentData *doc,
		TextWithEntities caption) {
	if (!_peer || !_peer->canWrite() || !doc) {
		return false;
	}

	MTPInputDocument mtpInput = doc->mtpInput();
	if (mtpInput.type() == mtpc_inputDocumentEmpty) {
		return false;
	}

	auto options = ApiWrap::SendOptions(_history);
	options.clearDraft = false;
	options.replyTo = replyToId();
	options.generateLocal = true;
	Auth().api().sendAction(options);

	uint64 randomId = rand_value<uint64>();
	FullMsgId newId(_channel, clientMsgId());

	auto flags = NewMessageFlags(_peer) | MTPDmessage::Flag::f_media;
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (options.replyTo) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	bool channelPost = _peer->isChannel() && !_peer->isMegagroup();
	bool silentPost = channelPost && _peer->notifySilentPosts();
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (!channelPost) {
		flags |= MTPDmessage::Flag::f_from_id;
	} else if (_peer->asChannel()->addsSignature()) {
		flags |= MTPDmessage::Flag::f_post_author;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	auto messageFromId = channelPost ? 0 : Auth().userId();
	auto messagePostAuthor = channelPost ? (Auth().user()->firstName + ' ' + Auth().user()->lastName) : QString();

	TextUtilities::Trim(caption);
	auto sentEntities = TextUtilities::EntitiesToMTP(
		caption.entities,
		TextUtilities::ConvertOption::SkipLocal);
	if (!sentEntities.v.isEmpty()) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_entities;
	}

	_history->addNewDocument(
		newId.msg,
		flags,
		0,
		options.replyTo,
		unixtime(),
		messageFromId,
		messagePostAuthor,
		doc,
		caption,
		MTPnullMarkup);
	_history->sendRequestId = MTP::send(
		MTPmessages_SendMedia(
			MTP_flags(sendFlags),
			_peer->input,
			MTP_int(options.replyTo),
			MTP_inputMediaDocument(
				MTP_flags(0),
				mtpInput,
				MTPint()),
			MTP_string(caption.text),
			MTP_long(randomId),
			MTPnullMarkup,
			sentEntities),
		App::main()->rpcDone(&MainWidget::sentUpdatesReceived),
		App::main()->rpcFail(&MainWidget::sendMessageFail),
		0,
		0,
		_history->sendRequestId);
	App::main()->finishForwarding(_history);

	if (doc->sticker()) App::main()->incrementSticker(doc);

	App::historyRegRandom(randomId, newId);

	if (_fieldAutocomplete->stickersShown()) {
		clearFieldText();
		//_saveDraftText = true;
		//_saveDraftStart = getms();
		//onDraftSave();
		onCloudDraftSave(); // won't be needed if SendInlineBotResult will clear the cloud draft
	}

	hideSelectorControlsAnimated();

	_field->setFocus();
	return true;
}

void HistoryWidget::sendExistingPhoto(
		PhotoData *photo,
		TextWithEntities caption) {
	if (!_peer || !_peer->canWrite() || !photo) {
		return;
	}

	auto options = ApiWrap::SendOptions(_history);
	options.clearDraft = false;
	options.replyTo = replyToId();
	options.generateLocal = true;
	Auth().api().sendAction(options);

	uint64 randomId = rand_value<uint64>();
	FullMsgId newId(_channel, clientMsgId());

	auto flags = NewMessageFlags(_peer) | MTPDmessage::Flag::f_media;
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (options.replyTo) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	bool channelPost = _peer->isChannel() && !_peer->isMegagroup();
	bool silentPost = channelPost && _peer->notifySilentPosts();
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (!channelPost) {
		flags |= MTPDmessage::Flag::f_from_id;
	} else if (_peer->asChannel()->addsSignature()) {
		flags |= MTPDmessage::Flag::f_post_author;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	auto messageFromId = channelPost ? 0 : Auth().userId();
	auto messagePostAuthor = channelPost ? (Auth().user()->firstName + ' ' + Auth().user()->lastName) : QString();

	TextUtilities::Trim(caption);
	auto sentEntities = TextUtilities::EntitiesToMTP(
		caption.entities,
		TextUtilities::ConvertOption::SkipLocal);
	if (!sentEntities.v.isEmpty()) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_entities;
	}

	_history->addNewPhoto(
		newId.msg,
		flags,
		0,
		options.replyTo,
		unixtime(),
		messageFromId,
		messagePostAuthor,
		photo,
		caption,
		MTPnullMarkup);

	_history->sendRequestId = MTP::send(
		MTPmessages_SendMedia(
			MTP_flags(sendFlags),
			_peer->input,
			MTP_int(options.replyTo),
			MTP_inputMediaPhoto(
				MTP_flags(0),
				MTP_inputPhoto(MTP_long(photo->id), MTP_long(photo->access)),
				MTPint()),
			MTP_string(caption.text),
			MTP_long(randomId),
			MTPnullMarkup,
			sentEntities),
		App::main()->rpcDone(&MainWidget::sentUpdatesReceived),
		App::main()->rpcFail(&MainWidget::sendMessageFail),
		0,
		0,
		_history->sendRequestId);
	App::main()->finishForwarding(_history);

	App::historyRegRandom(randomId, newId);

	hideSelectorControlsAnimated();

	_field->setFocus();
}

void HistoryWidget::setFieldText(const TextWithTags &textWithTags, TextUpdateEvents events, Ui::FlatTextarea::UndoHistoryAction undoHistoryAction) {
	_textUpdateEvents = events;
	_field->setTextWithTags(textWithTags, undoHistoryAction);
	_field->moveCursor(QTextCursor::End);
	_textUpdateEvents = TextUpdateEvent::SaveDraft | TextUpdateEvent::SendTyping;

	_previewCancelled = false;
	_previewData = nullptr;
	if (_previewRequest) {
		MTP::cancel(_previewRequest);
		_previewRequest = 0;
	}
	_previewLinks.clear();
}

void HistoryWidget::replyToMessage(FullMsgId itemId) {
	if (const auto item = App::histItemById(itemId)) {
		replyToMessage(item);
	}
}

void HistoryWidget::replyToMessage(not_null<HistoryItem*> item) {
	if (!IsServerMsgId(item->id) || !_canSendMessages) {
		return;
	}
	if (item->history() == _migrated) {
		if (item->isGroupMigrate()
			&& !_history->isEmpty()
			&& _history->blocks.front()->messages.front()->data()->isGroupMigrate()
			&& _history != _migrated) {
			replyToMessage(_history->blocks.front()->messages.front()->data());
		} else {
			if (item->serviceMsg()) {
				Ui::show(Box<InformBox>(lang(lng_reply_cant)));
			} else {
				const auto itemId = item->fullId();
				Ui::show(Box<ConfirmBox>(lang(lng_reply_cant_forward), lang(lng_selected_forward), base::lambda_guarded(this, [=] {
					App::main()->setForwardDraft(
						_peer->id,
						{ 1, itemId });
				})));
			}
		}
		return;
	}

	App::main()->cancelForwarding(_history);

	if (_editMsgId) {
		if (auto localDraft = _history->localDraft()) {
			localDraft->msgId = item->id;
		} else {
			_history->setLocalDraft(std::make_unique<Data::Draft>(
				TextWithTags(),
				item->id,
				MessageCursor(),
				false));
		}
	} else {
		_replyEditMsg = item;
		_replyToId = item->id;
		_replyEditMsgText.setText(
			st::messageTextStyle,
			TextUtilities::Clean(_replyEditMsg->inReplyText()),
			Ui::DialogTextOptions());

		updateBotKeyboard();

		if (!_field->isHidden()) _fieldBarCancel->show();
		updateMouseTracking();
		updateReplyToName();
		updateControlsGeometry();
		updateField();
	}

	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	_field->setFocus();
}

void HistoryWidget::editMessage(FullMsgId itemId) {
	if (const auto item = App::histItemById(itemId)) {
		editMessage(item);
	}
}

void HistoryWidget::editMessage(not_null<HistoryItem*> item) {
	if (const auto media = item->media()) {
		if (media->allowsEditCaption()) {
			Ui::show(Box<EditCaptionBox>(item));
			return;
		}
	}

	if (_recording) {
		// Just fix some strange inconsistency.
		_send->clearState();
	}
	if (!_editMsgId) {
		if (_replyToId || !_field->isEmpty()) {
			_history->setLocalDraft(std::make_unique<Data::Draft>(_field, _replyToId, _previewCancelled));
		} else {
			_history->clearLocalDraft();
		}
	}

	const auto original = item->originalText();
	const auto editData = TextWithTags {
		TextUtilities::ApplyEntities(original),
		ConvertEntitiesToTextTags(original.entities)
	};
	const auto cursor = MessageCursor {
		editData.text.size(),
		editData.text.size(),
		QFIXED_MAX
	};
	_history->setEditDraft(std::make_unique<Data::Draft>(
		editData,
		item->id,
		cursor,
		false));
	applyDraft(false);

	_previewData = nullptr;
	if (const auto media = item->media()) {
		if (const auto page = media->webpage()) {
			_previewData = page;
			updatePreview();
		}
	}
	if (!_previewData) {
		onPreviewParse();
	}

	updateBotKeyboard();

	if (!_field->isHidden()) _fieldBarCancel->show();
	updateFieldPlaceholder();
	updateMouseTracking();
	updateReplyToName();
	updateControlsGeometry();
	updateField();

	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	_field->setFocus();
}

void HistoryWidget::pinMessage(FullMsgId itemId) {
	if (const auto item = App::histItemById(itemId)) {
		if (item->canPin()) {
			const auto channel = item->history()->peer->asChannel();
			Assert(channel != nullptr);
			Ui::show(Box<PinMessageBox>(channel, item->id));
		}
	}
}

void HistoryWidget::unpinMessage(FullMsgId itemId) {
	const auto channel = _peer ? _peer->asChannel() : nullptr;
	if (!channel) {
		return;
	}

	Ui::show(Box<ConfirmBox>(lang(lng_pinned_unpin_sure), lang(lng_pinned_unpin), base::lambda_guarded(this, [=] {
		channel->clearPinnedMessage();

		Ui::hideLayer();
		MTP::send(
			MTPchannels_UpdatePinnedMessage(
				MTP_flags(0),
				channel->inputChannel,
				MTP_int(0)),
			rpcDone(&HistoryWidget::unpinDone));
	})));
}

void HistoryWidget::unpinDone(const MTPUpdates &updates) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
}

void HistoryWidget::onPinnedHide() {
	const auto channel = _peer ? _peer->asChannel() : nullptr;
	const auto pinnedId = channel ? channel->pinnedMessageId() : MsgId(0);
	if (!pinnedId) {
		if (pinnedMsgVisibilityUpdated()) {
			updateControlsGeometry();
			update();
		}
		return;
	}

	if (channel->canPinMessages()) {
		unpinMessage(FullMsgId(peerToChannel(channel->id), pinnedId));
	} else {
		Global::RefHiddenPinnedMessages().insert(channel->id, pinnedId);
		Local::writeUserSettings();
		if (pinnedMsgVisibilityUpdated()) {
			updateControlsGeometry();
			update();
		}
	}
}

void HistoryWidget::copyPostLink(FullMsgId itemId) {
	if (const auto item = App::histItemById(itemId)) {
		if (item->hasDirectLink()) {
			QApplication::clipboard()->setText(item->directLink());
		}
	}
}

bool HistoryWidget::lastForceReplyReplied(const FullMsgId &replyTo) const {
	if (replyTo.channel != _channel) {
		return false;
	}
	return _keyboard->forceReply()
		&& _keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)
		&& _keyboard->forMsgId().msg == replyTo.msg;
}

bool HistoryWidget::lastForceReplyReplied() const {
	return _keyboard->forceReply()
		&& _keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)
		&& _keyboard->forMsgId().msg == replyToId();
}

bool HistoryWidget::cancelReply(bool lastKeyboardUsed) {
	bool wasReply = false;
	if (_replyToId) {
		wasReply = true;

		_replyEditMsg = nullptr;
		_replyToId = 0;
		mouseMoveEvent(0);
		if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_kbReplyTo) {
			_fieldBarCancel->hide();
			updateMouseTracking();
		}

		updateBotKeyboard();

		updateControlsGeometry();
		update();
	} else if (auto localDraft = (_history ? _history->localDraft() : nullptr)) {
		if (localDraft->msgId) {
			if (localDraft->textWithTags.text.isEmpty()) {
				_history->clearLocalDraft();
			} else {
				localDraft->msgId = 0;
			}
		}
	}
	if (wasReply) {
		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();
	}
	if (!_editMsgId
		&& _keyboard->singleUse()
		&& _keyboard->forceReply()
		&& lastKeyboardUsed) {
		if (_kbReplyTo) {
			onKbToggle(false);
		}
	}
	return wasReply;
}

void HistoryWidget::cancelReplyAfterMediaSend(bool lastKeyboardUsed) {
	if (cancelReply(lastKeyboardUsed)) {
		onCloudDraftSave();
	}
}

int HistoryWidget::countMembersDropdownHeightMax() const {
	int result = height() - st::membersInnerDropdown.padding.top() - st::membersInnerDropdown.padding.bottom();
	result -= _tabbedSelectorToggle->height();
	accumulate_min(result, st::membersInnerHeightMax);
	return result;
}

void HistoryWidget::cancelEdit() {
	if (!_editMsgId) return;

	_replyEditMsg = nullptr;
	_editMsgId = 0;
	_history->clearEditDraft();
	applyDraft();

	if (_saveEditMsgRequestId) {
		MTP::cancel(_saveEditMsgRequestId);
		_saveEditMsgRequestId = 0;
	}

	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	mouseMoveEvent(nullptr);
	if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !replyToId()) {
		_fieldBarCancel->hide();
		updateMouseTracking();
	}

	auto old = _textUpdateEvents;
	_textUpdateEvents = 0;
	onTextChange();
	_textUpdateEvents = old;

	if (!canWriteMessage()) {
		updateControlsVisibility();
	}
	updateBotKeyboard();
	updateFieldPlaceholder();

	updateControlsGeometry();
	update();
}

void HistoryWidget::onFieldBarCancel() {
	Ui::hideLayer();
	_replyForwardPressed = false;
	if (_previewData && _previewData->pendingTill >= 0) {
		_previewCancelled = true;
		previewCancel();

		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();
	} else if (_editMsgId) {
		cancelEdit();
	} else if (readyToForward()) {
		App::main()->cancelForwarding(_history);
	} else if (_replyToId) {
		cancelReply();
	} else if (_kbReplyTo) {
		onKbToggle();
	}
}

void HistoryWidget::previewCancel() {
	MTP::cancel(base::take(_previewRequest));
	_previewData = nullptr;
	_previewLinks.clear();
	updatePreview();
	if (!_editMsgId && !_replyToId && !readyToForward() && !_kbReplyTo) {
		_fieldBarCancel->hide();
		updateMouseTracking();
	}
}

void HistoryWidget::onPreviewParse() {
	if (_previewCancelled) return;
	_field->parseLinks();
}

void HistoryWidget::onPreviewCheck() {
	auto previewRestricted = [this] {
		if (auto megagroup = _peer ? _peer->asMegagroup() : nullptr) {
			if (megagroup->restricted(ChannelRestriction::f_embed_links)) {
				return true;
			}
		}
		return false;
	};
	if (_previewCancelled || previewRestricted()) {
		MTP::cancel(base::take(_previewRequest));
		_previewData = nullptr;
		_previewLinks.clear();
		update();
		return;
	}
	auto linksList = _field->linksList();
	auto newLinks = linksList.join(' ');
	if (newLinks != _previewLinks) {
		MTP::cancel(base::take(_previewRequest));
		_previewLinks = newLinks;
		if (_previewLinks.isEmpty()) {
			if (_previewData && _previewData->pendingTill >= 0) previewCancel();
		} else {
			PreviewCache::const_iterator i = _previewCache.constFind(_previewLinks);
			if (i == _previewCache.cend()) {
				_previewRequest = MTP::send(
					MTPmessages_GetWebPagePreview(
						MTP_flags(0),
						MTP_string(_previewLinks),
						MTPnullEntities),
					rpcDone(&HistoryWidget::gotPreview, _previewLinks));
			} else if (i.value()) {
				_previewData = Auth().data().webpage(i.value());
				updatePreview();
			} else {
				if (_previewData && _previewData->pendingTill >= 0) previewCancel();
			}
		}
	}
}

void HistoryWidget::onPreviewTimeout() {
	if (_previewData
		&& (_previewData->pendingTill > 0)
		&& !_previewLinks.isEmpty()) {
		_previewRequest = MTP::send(
			MTPmessages_GetWebPagePreview(
				MTP_flags(0),
				MTP_string(_previewLinks),
				MTPnullEntities),
			rpcDone(&HistoryWidget::gotPreview, _previewLinks));
	}
}

void HistoryWidget::gotPreview(QString links, const MTPMessageMedia &result, mtpRequestId req) {
	if (req == _previewRequest) {
		_previewRequest = 0;
	}
	if (result.type() == mtpc_messageMediaWebPage) {
		const auto &data = result.c_messageMediaWebPage().vwebpage;
		const auto page = Auth().data().webpage(data);
		_previewCache.insert(links, page->id);
		if (page->pendingTill > 0 && page->pendingTill <= unixtime()) {
			page->pendingTill = -1;
		}
		if (links == _previewLinks && !_previewCancelled) {
			_previewData = (page->id && page->pendingTill >= 0)
				? page.get()
				: nullptr;
			updatePreview();
		}
		Auth().data().sendWebPageGameNotifications();
	} else if (result.type() == mtpc_messageMediaEmpty) {
		_previewCache.insert(links, 0);
		if (links == _previewLinks && !_previewCancelled) {
			_previewData = 0;
			updatePreview();
		}
	}
}

void HistoryWidget::updatePreview() {
	_previewTimer.stop();
	if (_previewData && _previewData->pendingTill >= 0) {
		_fieldBarCancel->show();
		updateMouseTracking();
		if (_previewData->pendingTill) {
			_previewTitle.setText(
				st::msgNameStyle,
				lang(lng_preview_loading),
				Ui::NameTextOptions());
#ifndef OS_MAC_OLD
			auto linkText = _previewLinks.splitRef(' ').at(0).toString();
#else // OS_MAC_OLD
			auto linkText = _previewLinks.split(' ').at(0);
#endif // OS_MAC_OLD
			_previewDescription.setText(
				st::messageTextStyle,
				TextUtilities::Clean(linkText),
				Ui::DialogTextOptions());

			int32 t = (_previewData->pendingTill - unixtime()) * 1000;
			if (t <= 0) t = 1;
			_previewTimer.start(t);
		} else {
			QString title, desc;
			if (_previewData->siteName.isEmpty()) {
				if (_previewData->title.isEmpty()) {
					if (_previewData->description.text.isEmpty()) {
						title = _previewData->author;
						desc = ((_previewData->document && !_previewData->document->filename().isEmpty()) ? _previewData->document->filename() : _previewData->url);
					} else {
						title = _previewData->description.text;
						desc = _previewData->author.isEmpty() ? ((_previewData->document && !_previewData->document->filename().isEmpty()) ? _previewData->document->filename() : _previewData->url) : _previewData->author;
					}
				} else {
					title = _previewData->title;
					desc = _previewData->description.text.isEmpty() ? (_previewData->author.isEmpty() ? ((_previewData->document && !_previewData->document->filename().isEmpty()) ? _previewData->document->filename() : _previewData->url) : _previewData->author) : _previewData->description.text;
				}
			} else {
				title = _previewData->siteName;
				desc = _previewData->title.isEmpty() ? (_previewData->description.text.isEmpty() ? (_previewData->author.isEmpty() ? ((_previewData->document && !_previewData->document->filename().isEmpty()) ? _previewData->document->filename() : _previewData->url) : _previewData->author) : _previewData->description.text) : _previewData->title;
			}
			if (title.isEmpty()) {
				if (_previewData->document) {
					title = lang(lng_attach_file);
				} else if (_previewData->photo) {
					title = lang(lng_attach_photo);
				}
			}
			_previewTitle.setText(
				st::msgNameStyle,
				title,
				Ui::NameTextOptions());
			_previewDescription.setText(
				st::messageTextStyle,
				TextUtilities::Clean(desc),
				Ui::DialogTextOptions());
		}
	} else if (!readyToForward() && !replyToId() && !_editMsgId) {
		_fieldBarCancel->hide();
		updateMouseTracking();
	}
	updateControlsGeometry();
	update();
}

void HistoryWidget::onCancel() {
	if (_isInlineBot) {
		onInlineBotCancel();
	} else if (_editMsgId) {
		auto original = _replyEditMsg ? _replyEditMsg->originalText() : TextWithEntities();
		auto editData = TextWithTags { TextUtilities::ApplyEntities(original), ConvertEntitiesToTextTags(original.entities) };
		if (_replyEditMsg && editData != _field->getTextWithTags()) {
			Ui::show(Box<ConfirmBox>(
				lang(lng_cancel_edit_post_sure),
				lang(lng_cancel_edit_post_yes),
				lang(lng_cancel_edit_post_no),
				base::lambda_guarded(this, [this] {
				onFieldBarCancel();
			})));
		} else {
			onFieldBarCancel();
		}
	} else if (!_fieldAutocomplete->isHidden()) {
		_fieldAutocomplete->hideAnimated();
	} else if (_replyToId && _field->getTextWithTags().text.isEmpty()) {
		cancelReply();
	} else {
		controller()->showBackFromStack();
		emit cancelled();
	}
}

void HistoryWidget::fullPeerUpdated(PeerData *peer) {
	auto refresh = false;
	if (_list && peer == _peer) {
		auto newCanSendMessages = _peer->canWrite();
		if (newCanSendMessages != _canSendMessages) {
			_canSendMessages = newCanSendMessages;
			if (!_canSendMessages) {
				cancelReply();
			}
			refreshSilentToggle();
			refresh = true;
		}
		onCheckFieldAutocomplete();
		updateReportSpamStatus();
		_list->updateBotInfo();

		handlePeerUpdate();
	}
	if (updateCmdStartShown()) {
		refresh = true;
	} else if (!_scroll->isHidden() && _unblock->isHidden() == isBlocked()) {
		refresh = true;
	}
	if (refresh) {
		updateControlsVisibility();
		updateControlsGeometry();
	}
}

void HistoryWidget::handlePeerUpdate() {
	bool resize = false;
	updateHistoryGeometry();
	if (_peer->isChannel()) updateReportSpamStatus();
	if (_peer->isChat() && _peer->asChat()->noParticipantInfo()) {
		Auth().api().requestFullPeer(_peer);
	} else if (_peer->isUser() && (_peer->asUser()->blockStatus() == UserData::BlockStatus::Unknown || _peer->asUser()->callsStatus() == UserData::CallsStatus::Unknown)) {
		Auth().api().requestFullPeer(_peer);
	} else if (auto channel = _peer->asMegagroup()) {
		if (!channel->mgInfo->botStatus) {
			Auth().api().requestBots(channel);
		}
		if (channel->mgInfo->admins.empty()) {
			Auth().api().requestAdmins(channel);
		}
	}
	if (!_a_show.animating()) {
		if (_unblock->isHidden() == isBlocked() || (!isBlocked() && _joinChannel->isHidden() == isJoinChannel())) {
			resize = true;
		}
		bool newCanSendMessages = _peer->canWrite();
		if (newCanSendMessages != _canSendMessages) {
			_canSendMessages = newCanSendMessages;
			if (!_canSendMessages) {
				cancelReply();
			}
			refreshSilentToggle();
			resize = true;
		}
		updateControlsVisibility();
		if (resize) {
			updateControlsGeometry();
		}
	}
}

void HistoryWidget::forwardSelected() {
	if (!_list) {
		return;
	}
	const auto weak = make_weak(this);
	Window::ShowForwardMessagesBox(getSelectedItems(), [=] {
		if (const auto strong = weak.data()) {
			strong->clearSelected();
		}
	});
}

void HistoryWidget::confirmDeleteSelected() {
	if (!_list) return;

	auto items = _list->getSelectedItems();
	if (items.empty()) {
		return;
	}
	const auto weak = make_weak(this);
	const auto box = Ui::show(Box<DeleteMessagesBox>(std::move(items)));
	box->setDeleteConfirmedCallback([=] {
		if (const auto strong = weak.data()) {
			strong->clearSelected();
		}
	});
}

void HistoryWidget::onListEscapePressed() {
	if (_nonEmptySelection && _list) {
		clearSelected();
	} else {
		onCancel();
	}
}

void HistoryWidget::onListEnterPressed() {
	if (!_botStart->isHidden()) {
		onBotStart();
	}
}

void HistoryWidget::clearSelected() {
	if (_list) {
		_list->clearSelected();
	}
}

HistoryItem *HistoryWidget::getItemFromHistoryOrMigrated(MsgId genericMsgId) const {
	if (genericMsgId < 0 && -genericMsgId < ServerMaxMsgId && _migrated) {
		return App::histItemById(_migrated->channelId(), -genericMsgId);
	}
	return App::histItemById(_channel, genericMsgId);
}

MessageIdsList HistoryWidget::getSelectedItems() const {
	return _list ? _list->getSelectedItems() : MessageIdsList();
}

void HistoryWidget::updateTopBarSelection() {
	if (!_list) {
		_topBar->showSelected(HistoryView::TopBarWidget::SelectedState {});
		return;
	}

	auto selectedState = _list->getSelectionState();
	_nonEmptySelection = (selectedState.count > 0) || selectedState.textSelected;
	_topBar->showSelected(selectedState);
	updateControlsVisibility();
	updateHistoryGeometry();
	if (!Ui::isLayerShown() && !App::passcoded()) {
		if (_nonEmptySelection || (_list && _list->wasSelectedText()) || _recording || isBotStart() || isBlocked() || !_canSendMessages) {
			_list->setFocus();
		} else {
			_field->setFocus();
		}
	}
	_topBar->update();
	update();
}

void HistoryWidget::messageDataReceived(ChannelData *channel, MsgId msgId) {
	if (!_peer || _peer->asChannel() != channel || !msgId) return;
	if (_editMsgId == msgId || _replyToId == msgId) {
		updateReplyEditTexts(true);
	}
	if (_pinnedBar && _pinnedBar->msgId == msgId) {
		updatePinnedBar(true);
	}
}

void HistoryWidget::updateReplyEditTexts(bool force) {
	if (!force) {
		if (_replyEditMsg || (!_editMsgId && !_replyToId)) {
			return;
		}
	}
	if (!_replyEditMsg) {
		_replyEditMsg = App::histItemById(_channel, _editMsgId ? _editMsgId : _replyToId);
	}
	if (_replyEditMsg) {
		_replyEditMsgText.setText(
			st::messageTextStyle,
			TextUtilities::Clean(_replyEditMsg->inReplyText()),
			Ui::DialogTextOptions());

		updateBotKeyboard();

		if (!_field->isHidden() || _recording) {
			_fieldBarCancel->show();
			updateMouseTracking();
		}
		updateReplyToName();
		updateField();
	} else if (force) {
		if (_editMsgId) {
			cancelEdit();
		} else {
			cancelReply();
		}
	}
}

void HistoryWidget::updateForwarding() {
	if (_history) {
		_toForward = _history->validateForwardDraft();
		updateForwardingTexts();
	} else {
		_toForward.clear();
	}
	updateControlsVisibility();
	updateControlsGeometry();
}

void HistoryWidget::updateForwardingTexts() {
	int32 version = 0;
	QString from, text;
	if (const auto count = int(_toForward.size())) {
		QMap<PeerData*, bool> fromUsersMap;
		QVector<PeerData*> fromUsers;
		fromUsers.reserve(_toForward.size());
		for (const auto item : _toForward) {
			const auto from = item->senderOriginal();
			if (!fromUsersMap.contains(from)) {
				fromUsersMap.insert(from, true);
				fromUsers.push_back(from);
			}
			version += from->nameVersion;
		}
		if (fromUsers.size() > 2) {
			from = lng_forwarding_from(lt_count, fromUsers.size() - 1, lt_user, fromUsers.at(0)->shortName());
		} else if (fromUsers.size() < 2) {
			from = fromUsers.at(0)->name;
		} else {
			from = lng_forwarding_from_two(lt_user, fromUsers.at(0)->shortName(), lt_second_user, fromUsers.at(1)->shortName());
		}

		if (count < 2) {
			text = _toForward.front()->inReplyText();
		} else {
			text = lng_forward_messages(lt_count, count);
		}
	}
	_toForwardFrom.setText(st::msgNameStyle, from, Ui::NameTextOptions());
	_toForwardText.setText(
		st::messageTextStyle,
		TextUtilities::Clean(text),
		Ui::DialogTextOptions());
	_toForwardNameVersion = version;
}

void HistoryWidget::checkForwardingInfo() {
	if (!_toForward.empty()) {
		auto version = 0;
		for (const auto item : _toForward) {
			version += item->senderOriginal()->nameVersion;
		}
		if (version != _toForwardNameVersion) {
			updateForwardingTexts();
		}
	}
}

void HistoryWidget::updateReplyToName() {
	if (_editMsgId) return;
	if (!_replyEditMsg && (_replyToId || !_kbReplyTo)) return;
	_replyToName.setText(
		st::msgNameStyle,
		App::peerName((_replyEditMsg ? _replyEditMsg : _kbReplyTo)->author()),
		Ui::NameTextOptions());
	_replyToNameVersion = (_replyEditMsg ? _replyEditMsg : _kbReplyTo)->author()->nameVersion;
}

void HistoryWidget::updateField() {
	auto fieldAreaTop = _scroll->y() + _scroll->height();
	rtlupdate(0, fieldAreaTop, width(), height() - fieldAreaTop);
}

void HistoryWidget::drawField(Painter &p, const QRect &rect) {
	auto backy = _field->y() - st::historySendPadding;
	auto backh = _field->height() + 2 * st::historySendPadding;
	auto hasForward = readyToForward();
	auto drawMsgText = (_editMsgId || _replyToId) ? _replyEditMsg : _kbReplyTo;
	if (_editMsgId || _replyToId || (!hasForward && _kbReplyTo)) {
		if (!_editMsgId && drawMsgText && drawMsgText->author()->nameVersion > _replyToNameVersion) {
			updateReplyToName();
		}
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	} else if (hasForward) {
		checkForwardingInfo();
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	} else if (_previewData && _previewData->pendingTill >= 0) {
		backy -= st::historyReplyHeight;
		backh += st::historyReplyHeight;
	}
	auto drawWebPagePreview = (_previewData && _previewData->pendingTill >= 0) && !_replyForwardPressed;
	p.fillRect(myrtlrect(0, backy, width(), backh), st::historyReplyBg);
	if (_editMsgId || _replyToId || (!hasForward && _kbReplyTo)) {
		auto replyLeft = st::historyReplySkip;
		(_editMsgId ? st::historyEditIcon : st::historyReplyIcon).paint(p, st::historyReplyIconPosition + QPoint(0, backy), width());
		if (!drawWebPagePreview) {
			if (drawMsgText) {
				if (drawMsgText->media() && drawMsgText->media()->hasReplyPreview()) {
					auto replyPreview = drawMsgText->media()->replyPreview();
					if (!replyPreview->isNull()) {
						auto to = QRect(replyLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
						p.drawPixmap(to.x(), to.y(), replyPreview->pixSingle(replyPreview->width() / cIntRetinaFactor(), replyPreview->height() / cIntRetinaFactor(), to.width(), to.height(), ImageRoundRadius::Small));
					}
					replyLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
				}
				p.setPen(st::historyReplyNameFg);
				if (_editMsgId) {
					paintEditHeader(p, rect, replyLeft, backy);
				} else {
					_replyToName.drawElided(p, replyLeft, backy + st::msgReplyPadding.top(), width() - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
				}
				p.setPen(!drawMsgText->toHistoryMessage() ? st::historyComposeAreaFgService : st::historyComposeAreaFg);
				_replyEditMsgText.drawElided(p, replyLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
			} else {
				p.setFont(st::msgDateFont);
				p.setPen(st::historyComposeAreaFgService);
				p.drawText(replyLeft, backy + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->elided(lang(lng_profile_loading), width() - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right()));
			}
		}
	} else if (hasForward) {
		auto forwardLeft = st::historyReplySkip;
		st::historyForwardIcon.paint(p, st::historyReplyIconPosition + QPoint(0, backy), width());
		if (!drawWebPagePreview) {
			const auto firstItem = _toForward.front();
			const auto firstMedia = firstItem->media();
			const auto serviceColor = (_toForward.size() > 1)
				|| (firstMedia != nullptr)
				|| firstItem->serviceMsg();
			const auto preview = (_toForward.size() < 2 && firstMedia && firstMedia->hasReplyPreview())
				? firstMedia->replyPreview()
				: ImagePtr();
			if (!preview->isNull()) {
				auto to = QRect(forwardLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				if (preview->width() == preview->height()) {
					p.drawPixmap(to.x(), to.y(), preview->pix());
				} else {
					auto from = (preview->width() > preview->height()) ? QRect((preview->width() - preview->height()) / 2, 0, preview->height(), preview->height()) : QRect(0, (preview->height() - preview->width()) / 2, preview->width(), preview->width());
					p.drawPixmap(to, preview->pix(), from);
				}
				forwardLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
			}
			p.setPen(st::historyReplyNameFg);
			_toForwardFrom.drawElided(p, forwardLeft, backy + st::msgReplyPadding.top(), width() - forwardLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
			p.setPen(serviceColor ? st::historyComposeAreaFgService : st::historyComposeAreaFg);
			_toForwardText.drawElided(p, forwardLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - forwardLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
		}
	}
	if (drawWebPagePreview) {
		auto previewLeft = st::historyReplySkip + st::webPageLeft;
		p.fillRect(st::historyReplySkip, backy + st::msgReplyPadding.top(), st::webPageBar, st::msgReplyBarSize.height(), st::msgInReplyBarColor);
		if ((_previewData->photo && !_previewData->photo->thumb->isNull()) || (_previewData->document && !_previewData->document->thumb->isNull())) {
			auto replyPreview = _previewData->photo ? _previewData->photo->makeReplyPreview() : _previewData->document->makeReplyPreview();
			if (!replyPreview->isNull()) {
				auto to = QRect(previewLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				if (replyPreview->width() == replyPreview->height()) {
					p.drawPixmap(to.x(), to.y(), replyPreview->pix());
				} else {
					auto from = (replyPreview->width() > replyPreview->height()) ? QRect((replyPreview->width() - replyPreview->height()) / 2, 0, replyPreview->height(), replyPreview->height()) : QRect(0, (replyPreview->height() - replyPreview->width()) / 2, replyPreview->width(), replyPreview->width());
					p.drawPixmap(to, replyPreview->pix(), from);
				}
			}
			previewLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
		}
		p.setPen(st::historyReplyNameFg);
		_previewTitle.drawElided(p, previewLeft, backy + st::msgReplyPadding.top(), width() - previewLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
		p.setPen(st::historyComposeAreaFg);
		_previewDescription.drawElided(p, previewLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - previewLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
	}
}

void HistoryWidget::drawRestrictedWrite(Painter &p) {
	auto rect = myrtlrect(0, height() - _unblock->height(), width(), _unblock->height());
	p.fillRect(rect, st::historyReplyBg);

	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(rect.marginsRemoved(QMargins(st::historySendPadding, 0, st::historySendPadding, 0)), lang(lng_restricted_send_message), style::al_center);
}

void HistoryWidget::paintEditHeader(Painter &p, const QRect &rect, int left, int top) const {
	if (!rect.intersects(myrtlrect(left, top, width() - left, st::normalFont->height))) {
		return;
	}

	p.setFont(st::msgServiceNameFont);
	p.drawTextLeft(left, top + st::msgReplyPadding.top(), width(), lang(lng_edit_message));

	if (!_replyEditMsg || _replyEditMsg->history()->peer->isSelf()) return;

	QString editTimeLeftText;
	int updateIn = -1;
	auto timeSinceMessage = ItemDateTime(_replyEditMsg).msecsTo(QDateTime::currentDateTime());
	auto editTimeLeft = (Global::EditTimeLimit() * 1000LL) - timeSinceMessage;
	if (editTimeLeft < 2) {
		editTimeLeftText = qsl("0:00");
	} else if (editTimeLeft > kDisplayEditTimeWarningMs) {
		updateIn = static_cast<int>(qMin(editTimeLeft - kDisplayEditTimeWarningMs, qint64(kFullDayInMs)));
	} else {
		updateIn = static_cast<int>(editTimeLeft % 1000);
		if (!updateIn) {
			updateIn = 1000;
		}
		++updateIn;

		editTimeLeft = (editTimeLeft - 1) / 1000; // seconds
		editTimeLeftText = qsl("%1:%2").arg(editTimeLeft / 60).arg(editTimeLeft % 60, 2, 10, QChar('0'));
	}

	// Restart timer only if we are sure that we've painted the whole timer.
	if (rect.contains(myrtlrect(left, top, width() - left, st::normalFont->height)) && updateIn > 0) {
		_updateEditTimeLeftDisplay.start(updateIn);
	}

	if (!editTimeLeftText.isEmpty()) {
		p.setFont(st::normalFont);
		p.setPen(st::historyComposeAreaFgService);
		p.drawText(left + st::msgServiceNameFont->width(lang(lng_edit_message)) + st::normalFont->spacew, top + st::msgReplyPadding.top() + st::msgServiceNameFont->ascent, editTimeLeftText);
	}
}

void HistoryWidget::drawRecording(Painter &p, float64 recordActive) {
	p.setPen(Qt::NoPen);
	p.setBrush(st::historyRecordSignalColor);

	auto delta = qMin(a_recordingLevel.current() / 0x4000, 1.);
	auto d = 2 * qRound(st::historyRecordSignalMin + (delta * (st::historyRecordSignalMax - st::historyRecordSignalMin)));
	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(_attachToggle->x() + (_tabbedSelectorToggle->width() - d) / 2, _attachToggle->y() + (_attachToggle->height() - d) / 2, d, d);
	}

	auto duration = formatDurationText(_recordingSamples / Media::Player::kDefaultFrequency);
	p.setFont(st::historyRecordFont);

	p.setPen(st::historyRecordDurationFg);
	p.drawText(_attachToggle->x() + _tabbedSelectorToggle->width(), _attachToggle->y() + st::historyRecordTextTop + st::historyRecordFont->ascent, duration);

	int32 left = _attachToggle->x() + _tabbedSelectorToggle->width() + st::historyRecordFont->width(duration) + ((_send->width() - st::historyRecordVoice.width()) / 2);
	int32 right = width() - _send->width();

	p.setPen(anim::pen(st::historyRecordCancel, st::historyRecordCancelActive, 1. - recordActive));
	p.drawText(left + (right - left - _recordCancelWidth) / 2, _attachToggle->y() + st::historyRecordTextTop + st::historyRecordFont->ascent, lang(lng_record_cancel));
}

void HistoryWidget::drawPinnedBar(Painter &p) {
	Expects(_pinnedBar != nullptr);

	auto top = _topBar->bottomNoMargins();
	Text *from = 0, *text = 0;
	bool serviceColor = false, hasForward = readyToForward();
	ImagePtr preview;
	p.fillRect(myrtlrect(0, top, width(), st::historyReplyHeight), st::historyPinnedBg);

	top += st::msgReplyPadding.top();
	QRect rbar(myrtlrect(st::msgReplyBarSkip + st::msgReplyBarPos.x(), top + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height()));
	p.fillRect(rbar, st::msgInReplyBarColor);

	int32 left = st::msgReplyBarSkip + st::msgReplyBarSkip;
	if (_pinnedBar->msg) {
		if (_pinnedBar->msg->media() && _pinnedBar->msg->media()->hasReplyPreview()) {
			ImagePtr replyPreview = _pinnedBar->msg->media()->replyPreview();
			if (!replyPreview->isNull()) {
				QRect to(left, top, st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				p.drawPixmap(to.x(), to.y(), replyPreview->pixSingle(replyPreview->width() / cIntRetinaFactor(), replyPreview->height() / cIntRetinaFactor(), to.width(), to.height(), ImageRoundRadius::Small));
			}
			left += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
		}
		p.setPen(st::historyReplyNameFg);
		p.setFont(st::msgServiceNameFont);
		p.drawText(left, top + st::msgServiceNameFont->ascent, lang(lng_pinned_message));

		p.setPen(!_pinnedBar->msg->toHistoryMessage() ? st::historyComposeAreaFgService : st::historyComposeAreaFg);
		_pinnedBar->text.drawElided(p, left, top + st::msgServiceNameFont->height, width() - left - _pinnedBar->cancel->width() - st::msgReplyPadding.right());
	} else {
		p.setFont(st::msgDateFont);
		p.setPen(st::historyComposeAreaFgService);
		p.drawText(left, top + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->elided(lang(lng_profile_loading), width() - left - _pinnedBar->cancel->width() - st::msgReplyPadding.right()));
	}
}

bool HistoryWidget::paintShowAnimationFrame(TimeMs ms) {
	auto progress = _a_show.current(ms, 1.);
	if (!_a_show.animating()) {
		return false;
	}

	Painter p(this);
	auto animationWidth = width();
	auto retina = cIntRetinaFactor();
	auto fromLeft = (_showDirection == Window::SlideDirection::FromLeft);
	auto coordUnder = fromLeft ? anim::interpolate(-st::slideShift, 0, progress) : anim::interpolate(0, -st::slideShift, progress);
	auto coordOver = fromLeft ? anim::interpolate(0, animationWidth, progress) : anim::interpolate(animationWidth, 0, progress);
	auto shadow = fromLeft ? (1. - progress) : progress;
	if (coordOver > 0) {
		p.drawPixmap(QRect(0, 0, coordOver, height()), _cacheUnder, QRect(-coordUnder * retina, 0, coordOver * retina, height() * retina));
		p.setOpacity(shadow);
		p.fillRect(0, 0, coordOver, height(), st::slideFadeOutBg);
		p.setOpacity(1);
	}
	p.drawPixmap(QRect(coordOver, 0, _cacheOver.width() / retina, height()), _cacheOver, QRect(0, 0, _cacheOver.width(), height() * retina));
	p.setOpacity(shadow);
	st::slideShadow.fill(p, QRect(coordOver - st::slideShadow.width(), 0, st::slideShadow.width(), height()));
	return true;
}

void HistoryWidget::paintEvent(QPaintEvent *e) {
	auto ms = getms();
	_historyDownShown.step(ms);
	_unreadMentionsShown.step(ms);
	if (paintShowAnimationFrame(ms)) {
		return;
	}
	if (Ui::skipPaintEvent(this, e)) {
		return;
	}
	if (hasPendingResizedItems()) {
		updateListSize();
	}

	Window::SectionWidget::PaintBackground(this, e);

	Painter p(this);
	const auto clip = e->rect();
	if (_list) {
		if (!_field->isHidden() || _recording) {
			drawField(p, clip);
			if (!_send->isHidden() && _recording) {
				drawRecording(p, _send->recordActiveRatio());
			}
		} else if (isRestrictedWrite()) {
			drawRestrictedWrite(p);
		}
		if (_pinnedBar && !_pinnedBar->cancel->isHidden()) {
			drawPinnedBar(p);
		}
		if (_scroll->isHidden()) {
			p.setClipRect(_scroll->geometry());
			HistoryView::paintEmpty(p, width(), height() - _field->height() - 2 * st::historySendPadding);
		}
	} else {
		const auto w = st::msgServiceFont->width(lang(lng_willbe_history))
			+ st::msgPadding.left()
			+ st::msgPadding.right();
		const auto h = st::msgServiceFont->height
			+ st::msgServicePadding.top()
			+ st::msgServicePadding.bottom();
		const auto tr = QRect(
			(width() - w) / 2,
			st::msgServiceMargin.top() + (height()
				- _field->height()
				- 2 * st::historySendPadding
				- h
				- st::msgServiceMargin.top()
				- st::msgServiceMargin.bottom()) / 2,
			w,
			h);
		HistoryView::ServiceMessagePainter::paintBubble(p, tr.x(), tr.y(), tr.width(), tr.height());

		p.setPen(st::msgServiceFg);
		p.setFont(st::msgServiceFont->f);
		p.drawTextLeft(tr.left() + st::msgPadding.left(), tr.top() + st::msgServicePadding.top(), width(), lang(lng_willbe_history));
	}
}

QRect HistoryWidget::historyRect() const {
	return _scroll->geometry();
}

void HistoryWidget::destroyData() {
	showHistory(0, 0);
}

QPoint HistoryWidget::clampMousePosition(QPoint point) {
	if (point.x() < 0) {
		point.setX(0);
	} else if (point.x() >= _scroll->width()) {
		point.setX(_scroll->width() - 1);
	}
	if (point.y() < _scroll->scrollTop()) {
		point.setY(_scroll->scrollTop());
	} else if (point.y() >= _scroll->scrollTop() + _scroll->height()) {
		point.setY(_scroll->scrollTop() + _scroll->height() - 1);
	}
	return point;
}

void HistoryWidget::onScrollTimer() {
	auto d = (_scrollDelta > 0) ? qMin(_scrollDelta * 3 / 20 + 1, int32(MaxScrollSpeed)) : qMax(_scrollDelta * 3 / 20 - 1, -int32(MaxScrollSpeed));
	_scroll->scrollToY(_scroll->scrollTop() + d);
}

void HistoryWidget::checkSelectingScroll(QPoint point) {
	if (point.y() < _scroll->scrollTop()) {
		_scrollDelta = point.y() - _scroll->scrollTop();
	} else if (point.y() >= _scroll->scrollTop() + _scroll->height()) {
		_scrollDelta = point.y() - _scroll->scrollTop() - _scroll->height() + 1;
	} else {
		_scrollDelta = 0;
	}
	if (_scrollDelta) {
		_scrollTimer.start(15);
	} else {
		_scrollTimer.stop();
	}
}

void HistoryWidget::noSelectingScroll() {
	_scrollTimer.stop();
}

bool HistoryWidget::touchScroll(const QPoint &delta) {
	int32 scTop = _scroll->scrollTop(), scMax = _scroll->scrollTopMax(), scNew = snap(scTop - delta.y(), 0, scMax);
	if (scNew == scTop) return false;

	_scroll->scrollToY(scNew);
	return true;
}

void HistoryWidget::synteticScrollToY(int y) {
	_synteticScrollEvent = true;
	if (_scroll->scrollTop() == y) {
		visibleAreaUpdated();
	} else {
		_scroll->scrollToY(y);
	}
	_synteticScrollEvent = false;
}

HistoryWidget::~HistoryWidget() = default;
