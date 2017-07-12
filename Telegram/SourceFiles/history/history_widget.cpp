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
#include "history/history_widget.h"

#include "styles/style_history.h"
#include "styles/style_dialogs.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "styles/style_profile.h"
#include "styles/style_chat_helpers.h"
#include "boxes/confirm_box.h"
#include "boxes/send_files_box.h"
#include "boxes/share_box.h"
#include "core/file_utilities.h"
#include "ui/toast/toast.h"
#include "ui/special_buttons.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/inner_dropdown.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/labels.h"
#include "ui/effects/ripple_animation.h"
#include "inline_bots/inline_bot_result.h"
#include "data/data_drafts.h"
#include "history/history_message.h"
#include "history/history_service_layout.h"
#include "history/history_media_types.h"
#include "history/history_drag_area.h"
#include "history/history_inner_widget.h"
#include "profile/profile_block_group_members.h"
#include "core/click_handler_types.h"
#include "chat_helpers/tabbed_panel.h"
#include "chat_helpers/tabbed_section.h"
#include "chat_helpers/tabbed_selector.h"
#include "chat_helpers/bot_keyboard.h"
#include "chat_helpers/message_field.h"
#include "lang/lang_keys.h"
#include "application.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "passcodewidget.h"
#include "mainwindow.h"
#include "storage/file_upload.h"
#include "media/media_audio.h"
#include "media/media_audio_capture.h"
#include "media/player/media_player_instance.h"
#include "storage/localstorage.h"
#include "apiwrap.h"
#include "window/top_bar_widget.h"
#include "window/themes/window_theme.h"
#include "observer_peer.h"
#include "base/qthelp_regex.h"
#include "ui/widgets/popup_menu.h"
#include "platform/platform_file_utilities.h"
#include "auth_session.h"
#include "window/notifications_manager.h"
#include "window/window_controller.h"
#include "inline_bots/inline_results_widget.h"

namespace {

constexpr auto kStickersUpdateTimeout = 3600000; // update not more than once in an hour
constexpr auto kSaveTabbedSelectorSectionTimeoutMs = 1000;
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

ApiWrap::RequestMessageDataCallback replyEditMessageDataCallback() {
	return [](ChannelData *channel, MsgId msgId) {
		if (App::main()) {
			App::main()->messageDataReceived(channel, msgId);
		}
	};
}

MTPVector<MTPDocumentAttribute> composeDocumentAttributes(DocumentData *document) {
	QVector<MTPDocumentAttribute> attributes(1, MTP_documentAttributeFilename(MTP_string(document->name)));
	if (document->dimensions.width() > 0 && document->dimensions.height() > 0) {
		int32 duration = document->duration();
		if (duration >= 0) {
			auto flags = MTPDdocumentAttributeVideo::Flags(0);
			if (document->isRoundVideo()) {
				flags |= MTPDdocumentAttributeVideo::Flag::f_round_message;
			}
			attributes.push_back(MTP_documentAttributeVideo(MTP_flags(flags), MTP_int(duration), MTP_int(document->dimensions.width()), MTP_int(document->dimensions.height())));
		} else {
			attributes.push_back(MTP_documentAttributeImageSize(MTP_int(document->dimensions.width()), MTP_int(document->dimensions.height())));
		}
	}
	if (document->type == AnimatedDocument) {
		attributes.push_back(MTP_documentAttributeAnimated());
	} else if (document->type == StickerDocument && document->sticker()) {
		attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(document->sticker()->alt), document->sticker()->set, MTPMaskCoords()));
	} else if (document->type == SongDocument && document->song()) {
		auto flags = MTPDdocumentAttributeAudio::Flag::f_title | MTPDdocumentAttributeAudio::Flag::f_performer;
		attributes.push_back(MTP_documentAttributeAudio(MTP_flags(flags), MTP_int(document->song()->duration), MTP_string(document->song()->title), MTP_string(document->song()->performer), MTPstring()));
	} else if (document->type == VoiceDocument && document->voice()) {
		auto flags = MTPDdocumentAttributeAudio::Flag::f_voice | MTPDdocumentAttributeAudio::Flag::f_waveform;
		attributes.push_back(MTP_documentAttributeAudio(MTP_flags(flags), MTP_int(document->voice()->duration), MTPstring(), MTPstring(), MTP_bytes(documentWaveformEncode5bit(document->voice()->waveform))));
	}
	return MTP_vector<MTPDocumentAttribute>(attributes);
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

HistoryHider::HistoryHider(MainWidget *parent, const SelectedItemSet &items) : TWidget(parent)
, _forwardItems(items)
, _send(this, langFactory(lng_forward_send), st::defaultBoxButton)
, _cancel(this, langFactory(lng_cancel), st::defaultBoxButton) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent, UserData *sharedContact) : TWidget(parent)
, _sharedContact(sharedContact)
, _send(this, langFactory(lng_forward_send), st::defaultBoxButton)
, _cancel(this, langFactory(lng_cancel), st::defaultBoxButton) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent) : TWidget(parent)
, _sendPath(true)
, _send(this, langFactory(lng_forward_send), st::defaultBoxButton)
, _cancel(this, langFactory(lng_cancel), st::defaultBoxButton) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent, const QString &botAndQuery) : TWidget(parent)
, _botAndQuery(botAndQuery)
, _send(this, langFactory(lng_forward_send), st::defaultBoxButton)
, _cancel(this, langFactory(lng_cancel), st::defaultBoxButton) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent, const QString &url, const QString &text) : TWidget(parent)
, _shareUrl(url)
, _shareText(text)
, _send(this, langFactory(lng_forward_send), st::defaultBoxButton)
, _cancel(this, langFactory(lng_cancel), st::defaultBoxButton) {
	init();
}

void HistoryHider::init() {
	subscribe(Lang::Current().updated(), [this] { refreshLang(); });
	if (!_forwardItems.empty()) {
		subscribe(Global::RefItemRemoved(), [this](HistoryItem *item) {
			for (auto i = _forwardItems.begin(); i != _forwardItems.end(); ++i) {
				if (i->get() == item) {
					i = _forwardItems.erase(i);
					break;
				}
			}
			if (_forwardItems.empty()) {
				startHide();
			}
		});
	}
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
	return _sharedContact || _sendPath;
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
		if (_offered) _cacheForAnim = myGrab(this, _box);
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
		if (_sharedContact) {
			parent()->onShareContact(_offered->id, _sharedContact);
		} else if (_sendPath) {
			parent()->onSendPaths(_offered->id);
		} else if (!_shareUrl.isEmpty()) {
			parent()->onShareUrl(_offered->id, _shareUrl, _shareText);
		} else if (!_botAndQuery.isEmpty()) {
			parent()->onInlineSwitchChosen(_offered->id, _botAndQuery);
		} else {
			parent()->setForwardDraft(_offered->id, _forwardItems);
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
	if (_sharedContact) {
		if (!_offered->canWrite()) {
			Ui::show(Box<InformBox>(lang(lng_forward_share_cant)));
			_offered = nullptr;
			_toText.setText(st::boxLabelStyle, QString());
			_toTextWidth = 0;
			resizeEvent(nullptr);
			return false;
		}
		phrase = lng_forward_share_contact(lt_recipient, recipient);
	} else if (_sendPath) {
		auto toId = _offered->id;
		_offered = nullptr;
		if (parent()->onSendPaths(toId)) {
			startHide();
		}
		return false;
	} else if (!_shareUrl.isEmpty()) {
		auto toId = _offered->id;
		_offered = nullptr;
		if (parent()->onShareUrl(toId, _shareUrl, _shareText)) {
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
		if (parent()->setForwardDraft(toId, _forwardItems)) {
			startHide();
		}
		return false;
	}

	_toText.setText(st::boxLabelStyle, phrase, _textNameOptions);
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

class SilentToggle : public Ui::IconButton, public Ui::AbstractTooltipShower {
public:
	SilentToggle(QWidget *parent);

	void setChecked(bool checked);
	bool checked() const {
		return _checked;
	}

	// AbstractTooltipShower interface
	QString tooltipText() const override;
	QPoint tooltipPos() const override;

protected:
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	bool _checked = false;

};

SilentToggle::SilentToggle(QWidget *parent) : IconButton(parent, st::historySilentToggle) {
	setMouseTracking(true);
}

void SilentToggle::mouseMoveEvent(QMouseEvent *e) {
	IconButton::mouseMoveEvent(e);
	if (rect().contains(e->pos())) {
		Ui::Tooltip::Show(1000, this);
	} else {
		Ui::Tooltip::Hide();
	}
}

void SilentToggle::setChecked(bool checked) {
	if (_checked != checked) {
		_checked = checked;
		setIconOverride(_checked ? &st::historySilentToggleOn : nullptr, _checked ? &st::historySilentToggleOnOver : nullptr);
	}
}

void SilentToggle::leaveEventHook(QEvent *e) {
	IconButton::leaveEventHook(e);
	Ui::Tooltip::Hide();
}

void SilentToggle::mouseReleaseEvent(QMouseEvent *e) {
	setChecked(!_checked);
	IconButton::mouseReleaseEvent(e);
	Ui::Tooltip::Show(0, this);
	auto p = App::main() ? App::main()->peer() : nullptr;
	if (p && p->isChannel() && p->notify != UnknownNotifySettings) {
		App::main()->updateNotifySetting(p, NotifySettingDontChange, _checked ? SilentNotifiesSetSilent : SilentNotifiesSetNotify);
	}
}

QString SilentToggle::tooltipText() const {
	return lang(_checked ? lng_wont_be_notified : lng_will_be_notified);
}

QPoint SilentToggle::tooltipPos() const {
	return QCursor::pos();
}

HistoryWidget::HistoryWidget(QWidget *parent, gsl::not_null<Window::Controller*> controller) : Window::AbstractSectionWidget(parent, controller)
, _fieldBarCancel(this, st::historyReplyCancel)
, _topBar(this, controller)
, _scroll(this, st::historyScroll, false)
, _historyDown(_scroll, st::historyToDown)
, _fieldAutocomplete(this)
, _send(this)
, _unblock(this, lang(lng_unblock_button).toUpper(), st::historyUnblock)
, _botStart(this, lang(lng_bot_start).toUpper(), st::historyComposeButton)
, _joinChannel(this, lang(lng_channel_join).toUpper(), st::historyComposeButton)
, _muteUnmute(this, lang(lng_channel_mute).toUpper(), st::historyComposeButton)
, _attachToggle(this, st::historyAttach)
, _tabbedSelectorToggle(this, st::historyAttachEmoji)
, _botKeyboardShow(this, st::historyBotKeyboardShow)
, _botKeyboardHide(this, st::historyBotKeyboardHide)
, _botCommandStart(this, st::historyBotCommandStart)
, _silent(this)
, _field(this, controller, st::historyComposeField, langFactory(lng_message_ph))
, _recordCancelWidth(st::historyRecordFont->width(lang(lng_record_cancel)))
, _a_recording(animation(this, &HistoryWidget::step_recording))
, _kbScroll(this, st::botKbScroll)
, _tabbedPanel(this, controller)
, _tabbedSelector(_tabbedPanel->getSelector())
, _attachDragDocument(this)
, _attachDragPhoto(this)
, _fileLoader(this, FileLoaderQueueStopTimeout)
, _topShadow(this, st::shadowFg) {
	setAcceptDrops(true);

	subscribe(AuthSession::CurrentDownloaderTaskFinished(), [this] { update(); });
	connect(_topBar, &Window::TopBarWidget::clicked, this, [this] { topBarClick(); });
	connect(_scroll, SIGNAL(scrolled()), this, SLOT(onScroll()));
	connect(_historyDown, SIGNAL(clicked()), this, SLOT(onHistoryToEnd()));
	connect(_fieldBarCancel, SIGNAL(clicked()), this, SLOT(onFieldBarCancel()));
	_send->setClickedCallback([this] { sendButtonClicked(); });
	connect(_unblock, SIGNAL(clicked()), this, SLOT(onUnblock()));
	connect(_botStart, SIGNAL(clicked()), this, SLOT(onBotStart()));
	connect(_joinChannel, SIGNAL(clicked()), this, SLOT(onJoinChannel()));
	connect(_muteUnmute, SIGNAL(clicked()), this, SLOT(onMuteUnmute()));
	connect(_silent, SIGNAL(clicked()), this, SLOT(onBroadcastSilentChange()));
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
	connect(_tabbedSelector, SIGNAL(updateStickers()), this, SLOT(updateStickers()));
	connect(&_sendActionStopTimer, SIGNAL(timeout()), this, SLOT(onCancelSendAction()));
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

	_sendActionStopTimer.setSingleShot(true);

	_animActiveTimer.setSingleShot(false);
	connect(&_animActiveTimer, SIGNAL(timeout()), this, SLOT(onAnimActiveStep()));

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

	_fieldAutocomplete->hide();
	connect(_fieldAutocomplete, SIGNAL(mentionChosen(UserData*,FieldAutocomplete::ChooseMethod)), this, SLOT(onMentionInsert(UserData*)));
	connect(_fieldAutocomplete, SIGNAL(hashtagChosen(QString,FieldAutocomplete::ChooseMethod)), this, SLOT(onHashtagOrBotCommandInsert(QString,FieldAutocomplete::ChooseMethod)));
	connect(_fieldAutocomplete, SIGNAL(botCommandChosen(QString,FieldAutocomplete::ChooseMethod)), this, SLOT(onHashtagOrBotCommandInsert(QString,FieldAutocomplete::ChooseMethod)));
	connect(_fieldAutocomplete, SIGNAL(stickerChosen(DocumentData*,FieldAutocomplete::ChooseMethod)), this, SLOT(onStickerSend(DocumentData*)));
	connect(_fieldAutocomplete, SIGNAL(moderateKeyActivate(int,bool*)), this, SLOT(onModerateKeyActivate(int,bool*)));
	_field->installEventFilter(_fieldAutocomplete);
	_field->setInsertFromMimeDataHook([this](const QMimeData *data) {
		return confirmSendingFiles(data, CompressConfirm::Auto, data->text());
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
	_silent->hide();
	_botCommandStart->hide();

	_tabbedSelectorToggle->installEventFilter(_tabbedPanel);
	_tabbedSelectorToggle->setClickedCallback([this] { toggleTabbedSelectorMode(); });

	connect(_botKeyboardShow, SIGNAL(clicked()), this, SLOT(onKbToggle()));
	connect(_botKeyboardHide, SIGNAL(clicked()), this, SLOT(onKbToggle()));
	connect(_botCommandStart, SIGNAL(clicked()), this, SLOT(onCmdStart()));

	_tabbedPanel->hide();
	_attachDragDocument->hide();
	_attachDragPhoto->hide();

	_topShadow->hide();

	_attachDragDocument->setDroppedCallback([this](const QMimeData *data) { confirmSendingFiles(data, CompressConfirm::No); });
	_attachDragPhoto->setDroppedCallback([this](const QMimeData *data) { confirmSendingFiles(data, CompressConfirm::Yes); });

	connect(&_updateEditTimeLeftDisplay, SIGNAL(timeout()), this, SLOT(updateField()));

	subscribe(Adaptive::Changed(), [this] { update(); });
	subscribe(Global::RefItemRemoved(), [this](HistoryItem *item) {
		itemRemoved(item);
	});
	subscribe(AuthSession::Current().data().contactsLoaded(), [this](bool) {
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
	subscribe(Notify::PeerUpdated(), Notify::PeerUpdatedHandler(Notify::PeerUpdate::Flag::ChannelRightsChanged, [this](const Notify::PeerUpdate &update) {
		if (update.peer == _peer) {
			onPreviewCheck();
		}
	}));
	subscribe(controller->window()->widgetGrabbed(), [this] {
		// Qt bug workaround: QWidget::render() for an arbitrary widget calls
		// sendPendingMoveAndResizeEvents(true, true) for the whole window,
		// which does something like:
		//
		// setAttribute(Qt::WA_UpdatesDisabled);
		// sendEvent(QResizeEvent);
		// setAttribute(Qt::WA_UpdatesDisabled, false);
		//
		// So if we create TabbedSection widget in HistoryWidget::resizeEvent()
		// it will get an enabled Qt::WA_UpdatesDisabled from its parent and it
		// will never be rendered, because no one will ever remove that attribute.
		//
		// So we force HistoryWidget::resizeEvent() here, without WA_UpdatesDisabled.
		myEnsureResized(this);
	});
	subscribe(AuthSession::Current().data().pendingHistoryResize(), [this] { handlePendingHistoryUpdate(); });
	subscribe(AuthSession::Current().data().queryItemVisibility(), [this](const AuthSessionData::ItemVisibilityQuery &query) {
		if (_a_show.animating() || _history != query.item->history() || query.item->detached() || !isVisible()) {
			return;
		}
		auto top = _list->itemTop(query.item);
		if (top >= 0) {
			auto scrollTop = _scroll->scrollTop();
			if (top + query.item->height() > scrollTop && top < scrollTop + _scroll->height()) {
				*query.isVisible = true;
			}
		}
	});

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

	auto toTop = _list->itemTop(to);
	if (toTop >= 0 && !isItemCompletelyHidden(from)) {
		auto scrollTop = _scroll->scrollTop();
		auto scrollBottom = scrollTop + _scroll->height();
		auto toBottom = toTop + to->height();
		if ((toTop < scrollTop && toBottom < scrollBottom) || (toTop > scrollTop && toBottom > scrollBottom)) {
			animatedScrollToItem(to->id);
		}
	}
}

void HistoryWidget::animatedScrollToItem(MsgId msgId) {
	Expects(_history != nullptr);

	auto to = App::histItemById(_channel, msgId);
	if (_list->itemTop(to) < 0) {
		return;
	}

	auto scrollTo = snap(itemTopForHighlight(to), 0, _scroll->scrollTopMax());
	animatedScrollToY(scrollTo, to);
}

void HistoryWidget::animatedScrollToY(int scrollTo, HistoryItem *attachTo) {
	Expects(_history != nullptr);

	// Attach our scroll animation to some item.
	auto itemTop = _list->itemTop(attachTo);
	auto scrollTop = _scroll->scrollTop();
	if (itemTop < 0 && !_history->isEmpty()) {
		attachTo = _history->blocks.back()->items.back();
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

void HistoryWidget::highlightMessage(HistoryItem *context) {
	Expects(_list != nullptr);

	_animActiveStart = getms();
	_animActiveTimer.start(AnimationTimerDelta);
	_activeAnimMsgId = _showAtMsgId;
	if (context
		&& context->history() == _history
		&& context->isGroupMigrate()
		&& _migrated
		&& !_migrated->isEmpty()
		&& _migrated->loadedAtBottom()
		&& _migrated->blocks.back()->items.back()->isGroupMigrate()
		&& _list->historyTop() != _list->historyDrawTop()) {
		_activeAnimMsgId = -_migrated->blocks.back()->items.back()->id;
	}
}

int HistoryWidget::itemTopForHighlight(gsl::not_null<HistoryItem*> item) const {
	auto itemTop = _list->itemTop(item);
	t_assert(itemTop >= 0);

	auto heightLeft = (_scroll->height() - item->height());
	if (heightLeft <= 0) {
		return itemTop;
	}
	return qMax(itemTop - (heightLeft / 2), 0);
}

void HistoryWidget::start() {
	connect(App::main(), SIGNAL(stickersUpdated()), this, SLOT(onStickersUpdated()));
	updateRecentStickers();
	AuthSession::Current().data().savedGifsUpdated().notify();
	subscribe(App::api()->fullPeerUpdated(), [this](PeerData *peer) {
		fullPeerUpdated(peer);
	});
}

void HistoryWidget::onStickersUpdated() {
	_tabbedSelector->refreshStickers();
	updateStickersByEmoji();
}

void HistoryWidget::onMentionInsert(UserData *user) {
	QString replacement, entityTag;
	if (user->username.isEmpty()) {
		replacement = user->firstName;
		if (replacement.isEmpty()) {
			replacement = App::peerName(user);
		}
		entityTag = qsl("mention://user.") + QString::number(user->bareId()) + '.' + QString::number(user->access);
	} else {
		replacement = '@' + user->username;
	}
	_field->insertTag(replacement, entityTag);
}

void HistoryWidget::onHashtagOrBotCommandInsert(QString str, FieldAutocomplete::ChooseMethod method) {
	// Send bot command at once, if it was not inserted by pressing Tab.
	if (str.at(0) == '/' && method != FieldAutocomplete::ChooseMethod::ByTab) {
		App::sendBotCommand(_peer, nullptr, str);
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
	if (_rightShadow) {
		_rightShadow->raise();
	}
	if (_membersDropdown) {
		_membersDropdown->raise();
	}
	if (_inlineResults) {
		_inlineResults->raise();
	}
	if (_tabbedPanel) {
		_tabbedPanel->raise();
	}
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
		t_assert(_peer != nullptr);
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

	if (_peer && (!_peer->isChannel() || _peer->isMegagroup())) {
		if (!_inlineBot && !_editMsgId && (_textUpdateEvents.testFlag(TextUpdateEvent::SendTyping))) {
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
	if (!_peer || !(_textUpdateEvents.testFlag(TextUpdateEvent::SaveDraft))) return;

	_saveDraftText = true;
	onDraftSave(true);
}

void HistoryWidget::onDraftSaveDelayed() {
	if (!_peer || !(_textUpdateEvents.testFlag(TextUpdateEvent::SaveDraft))) return;
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

void HistoryWidget::cancelSendAction(History *history, SendAction::Type type) {
	auto i = _sendActionRequests.find(qMakePair(history, type));
	if (i != _sendActionRequests.cend()) {
		MTP::cancel(i.value());
		_sendActionRequests.erase(i);
	}
}

void HistoryWidget::onCancelSendAction() {
	cancelSendAction(_history, SendAction::Type::Typing);
}

void HistoryWidget::updateSendAction(History *history, SendAction::Type type, int32 progress) {
	if (!history) return;

	auto doing = (progress >= 0);
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
			_sendActionRequests.insert(qMakePair(history, type), MTP::send(MTPmessages_SetTyping(history->peer->input, action), rpcDone(&HistoryWidget::sendActionDone)));
			if (type == Type::Typing) _sendActionStopTimer.start(5000);
		}
	}
}

void HistoryWidget::updateRecentStickers() {
	_tabbedSelector->refreshStickers();
}

void HistoryWidget::stickersInstalled(uint64 setId) {
	if (_tabbedPanel) {
		_tabbedPanel->stickersInstalled(setId);
	} else if (_tabbedSection) {
		_tabbedSection->stickersInstalled(setId);
	}
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

void HistoryWidget::onRecordDone(QByteArray result, VoiceWaveform waveform, qint32 samples) {
	if (!canWriteMessage() || result.isEmpty()) return;

	App::wnd()->activateWindow();
	auto duration = samples / Media::Player::kDefaultFrequency;
	auto to = FileLoadTo(_peer->id, _silent->checked(), replyToId());
	auto caption = QString();
	_fileLoader.addTask(MakeShared<FileLoadTask>(result, duration, waveform, to, caption));
	cancelReplyAfterMediaSend(lastForceReplyReplied());
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
	if (_peer && (!_peer->isChannel() || _peer->isMegagroup())) {
		updateSendAction(_history, SendAction::Type::RecordVoice);
	}
}

void HistoryWidget::updateStickers() {
	auto now = getms(true);
	if (!Global::LastStickersUpdate() || now >= Global::LastStickersUpdate() + kStickersUpdateTimeout) {
		if (!_stickersUpdateRequest) {
			_stickersUpdateRequest = MTP::send(MTPmessages_GetAllStickers(MTP_int(Local::countStickersHash(true))), rpcDone(&HistoryWidget::stickersGot), rpcFail(&HistoryWidget::stickersFailed));
		}
	}
	if (!Global::LastRecentStickersUpdate() || now >= Global::LastRecentStickersUpdate() + kStickersUpdateTimeout) {
		if (!_recentStickersUpdateRequest) {
			_recentStickersUpdateRequest = MTP::send(MTPmessages_GetRecentStickers(MTP_flags(0), MTP_int(Local::countRecentStickersHash())), rpcDone(&HistoryWidget::recentStickersGot), rpcFail(&HistoryWidget::recentStickersFailed));
		}
	}
	if (!Global::LastFeaturedStickersUpdate() || now >= Global::LastFeaturedStickersUpdate() + kStickersUpdateTimeout) {
		if (!_featuredStickersUpdateRequest) {
			_featuredStickersUpdateRequest = MTP::send(MTPmessages_GetFeaturedStickers(MTP_int(Local::countFeaturedStickersHash())), rpcDone(&HistoryWidget::featuredStickersGot), rpcFail(&HistoryWidget::featuredStickersFailed));
		}
	}
	if (!cLastSavedGifsUpdate() || now >= cLastSavedGifsUpdate() + kStickersUpdateTimeout) {
		if (!_savedGifsUpdateRequest) {
			_savedGifsUpdateRequest = MTP::send(MTPmessages_GetSavedGifs(MTP_int(Local::countSavedGifsHash())), rpcDone(&HistoryWidget::savedGifsGot), rpcFail(&HistoryWidget::savedGifsFailed));
		}
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
		PeerId toPeerId = bot->botInfo ? bot->botInfo->inlineReturnPeerId : 0;
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
			} else if ((_migrated ? _migrated->peer : 0) != peer->migrateFrom()) {
				History *migrated = peer->migrateFrom() ? App::history(peer->migrateFrom()->id) : 0;
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
	if (!inFocusChain() || !_peer) return false;

	App::main()->searchInPeer(_peer);
	return true;
}

bool HistoryWidget::cmd_next_chat() {
	PeerData *p = 0;
	MsgId m = 0;
	App::main()->peerAfter(_peer, qMax(_showAtMsgId, 0), p, m);
	if (p) {
		Ui::showPeerHistory(p, m);
		return true;
	}
	return false;
}

bool HistoryWidget::cmd_previous_chat() {
	PeerData *p = 0;
	MsgId m = 0;
	App::main()->peerBefore(_peer, qMax(_showAtMsgId, 0), p, m);
	if (p) {
		Ui::showPeerHistory(p, m);
		return true;
	}
	return false;
}

void HistoryWidget::stickersGot(const MTPmessages_AllStickers &stickers) {
	Global::SetLastStickersUpdate(getms(true));
	_stickersUpdateRequest = 0;

	if (stickers.type() != mtpc_messages_allStickers) return;
	auto &d = stickers.c_messages_allStickers();

	auto &d_sets = d.vsets.v;

	auto &setsOrder = Global::RefStickerSetsOrder();
	setsOrder.clear();

	auto &sets = Global::RefStickerSets();
	QMap<uint64, uint64> setsToRequest;
	for (auto &set : sets) {
		if (!(set.flags & MTPDstickerSet::Flag::f_archived)) {
			set.flags &= ~MTPDstickerSet::Flag::f_installed; // mark for removing
		}
	}
	for_const (auto &setData, d_sets) {
		if (setData.type() == mtpc_stickerSet) {
			auto set = Stickers::feedSet(setData.c_stickerSet());
			if (!(set->flags & MTPDstickerSet::Flag::f_archived) || (set->flags & MTPDstickerSet::Flag::f_official)) {
				setsOrder.push_back(set->id);
				if (set->stickers.isEmpty() || (set->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
					setsToRequest.insert(set->id, set->access);
				}
			}
		}
	}
	bool writeRecent = false;
	RecentStickerPack &recent(cGetRecentStickers());
	for (Stickers::Sets::iterator it = sets.begin(), e = sets.end(); it != e;) {
		bool installed = (it->flags & MTPDstickerSet::Flag::f_installed);
		bool featured = (it->flags & MTPDstickerSet_ClientFlag::f_featured);
		bool special = (it->flags & MTPDstickerSet_ClientFlag::f_special);
		bool archived = (it->flags & MTPDstickerSet::Flag::f_archived);
		if (!installed) { // remove not mine sets from recent stickers
			for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
				if (it->stickers.indexOf(i->first) >= 0) {
					i = recent.erase(i);
					writeRecent = true;
				} else {
					++i;
				}
			}
		}
		if (installed || featured || special || archived) {
			++it;
		} else {
			it = sets.erase(it);
		}
	}

	if (Local::countStickersHash() != d.vhash.v) {
		LOG(("API Error: received stickers hash %1 while counted hash is %2").arg(d.vhash.v).arg(Local::countStickersHash()));
	}

	if (!setsToRequest.isEmpty() && App::api()) {
		for (QMap<uint64, uint64>::const_iterator i = setsToRequest.cbegin(), e = setsToRequest.cend(); i != e; ++i) {
			App::api()->scheduleStickerSetRequest(i.key(), i.value());
		}
		App::api()->requestStickerSets();
	}

	Local::writeInstalledStickers();
	if (writeRecent) Local::writeUserSettings();

	if (App::main()) emit App::main()->stickersUpdated();
}

bool HistoryWidget::stickersFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("App Fail: Failed to get stickers!"));

	Global::SetLastStickersUpdate(getms(true));
	_stickersUpdateRequest = 0;
	return true;
}

void HistoryWidget::recentStickersGot(const MTPmessages_RecentStickers &stickers) {
	Global::SetLastRecentStickersUpdate(getms(true));
	_recentStickersUpdateRequest = 0;

	if (stickers.type() != mtpc_messages_recentStickers) return;
	auto &d = stickers.c_messages_recentStickers();

	auto &sets = Global::RefStickerSets();
	auto it = sets.find(Stickers::CloudRecentSetId);

	auto &d_docs = d.vstickers.v;
	if (d_docs.isEmpty()) {
		if (it != sets.cend()) {
			sets.erase(it);
		}
	} else {
		if (it == sets.cend()) {
			it = sets.insert(Stickers::CloudRecentSetId, Stickers::Set(Stickers::CloudRecentSetId, 0, lang(lng_recent_stickers), QString(), 0, 0, qFlags(MTPDstickerSet_ClientFlag::f_special)));
		} else {
			it->title = lang(lng_recent_stickers);
		}
		it->hash = d.vhash.v;

		auto custom = sets.find(Stickers::CustomSetId);

		StickerPack pack;
		pack.reserve(d_docs.size());
		for (int32 i = 0, l = d_docs.size(); i != l; ++i) {
			DocumentData *doc = App::feedDocument(d_docs.at(i));
			if (!doc || !doc->sticker()) continue;

			pack.push_back(doc);
			if (custom != sets.cend()) {
				int32 index = custom->stickers.indexOf(doc);
				if (index >= 0) {
					custom->stickers.removeAt(index);
				}
			}
		}
		if (custom != sets.cend() && custom->stickers.isEmpty()) {
			sets.erase(custom);
			custom = sets.end();
		}

		bool writeRecent = false;
		RecentStickerPack &recent(cGetRecentStickers());
		for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
			if (it->stickers.indexOf(i->first) >= 0 && pack.indexOf(i->first) < 0) {
				i = recent.erase(i);
				writeRecent = true;
			} else {
				++i;
			}
		}

		if (pack.isEmpty()) {
			sets.erase(it);
		} else {
			it->stickers = pack;
			it->emoji.clear();
		}

		if (writeRecent) {
			Local::writeUserSettings();
		}
	}

	if (Local::countRecentStickersHash() != d.vhash.v) {
		LOG(("API Error: received stickers hash %1 while counted hash is %2").arg(d.vhash.v).arg(Local::countRecentStickersHash()));
	}

	Local::writeRecentStickers();

	if (App::main()) emit App::main()->stickersUpdated();
}

bool HistoryWidget::recentStickersFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("App Fail: Failed to get recent stickers!"));

	Global::SetLastRecentStickersUpdate(getms(true));
	_recentStickersUpdateRequest = 0;
	return true;
}

void HistoryWidget::featuredStickersGot(const MTPmessages_FeaturedStickers &stickers) {
	Global::SetLastFeaturedStickersUpdate(getms(true));
	_featuredStickersUpdateRequest = 0;

	if (stickers.type() != mtpc_messages_featuredStickers) return;
	auto &d = stickers.c_messages_featuredStickers();

	OrderedSet<uint64> unread;
	for_const (auto &unreadSetId, d.vunread.v) {
		unread.insert(unreadSetId.v);
	}

	auto &d_sets = d.vsets.v;

	auto &setsOrder = Global::RefFeaturedStickerSetsOrder();
	setsOrder.clear();

	auto &sets = Global::RefStickerSets();
	QMap<uint64, uint64> setsToRequest;
	for (auto &set : sets) {
		set.flags &= ~MTPDstickerSet_ClientFlag::f_featured; // mark for removing
	}
	for (int i = 0, l = d_sets.size(); i != l; ++i) {
		auto &setData = d_sets[i];
		const MTPDstickerSet *set = nullptr;
		switch (setData.type()) {
		case mtpc_stickerSetCovered: {
			auto &d = setData.c_stickerSetCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				set = &d.vset.c_stickerSet();
			}
		} break;
		case mtpc_stickerSetMultiCovered: {
			auto &d = setData.c_stickerSetMultiCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				set = &d.vset.c_stickerSet();
			}
		} break;
		}

		if (set) {
			auto it = sets.find(set->vid.v);
			auto title = stickerSetTitle(*set);
			if (it == sets.cend()) {
				auto setClientFlags = MTPDstickerSet_ClientFlag::f_featured | MTPDstickerSet_ClientFlag::f_not_loaded;
				if (unread.contains(set->vid.v)) {
					setClientFlags |= MTPDstickerSet_ClientFlag::f_unread;
				}
				it = sets.insert(set->vid.v, Stickers::Set(set->vid.v, set->vaccess_hash.v, title, qs(set->vshort_name), set->vcount.v, set->vhash.v, set->vflags.v | setClientFlags));
			} else {
				it->access = set->vaccess_hash.v;
				it->title = title;
				it->shortName = qs(set->vshort_name);
				auto clientFlags = it->flags & (MTPDstickerSet_ClientFlag::f_featured | MTPDstickerSet_ClientFlag::f_unread | MTPDstickerSet_ClientFlag::f_not_loaded | MTPDstickerSet_ClientFlag::f_special);
				it->flags = set->vflags.v | clientFlags;
				it->flags |= MTPDstickerSet_ClientFlag::f_featured;
				if (unread.contains(it->id)) {
					it->flags |= MTPDstickerSet_ClientFlag::f_unread;
				} else {
					it->flags &= ~MTPDstickerSet_ClientFlag::f_unread;
				}
				if (it->count != set->vcount.v || it->hash != set->vhash.v || it->emoji.isEmpty()) {
					it->count = set->vcount.v;
					it->hash = set->vhash.v;
					it->flags |= MTPDstickerSet_ClientFlag::f_not_loaded; // need to request this set
				}
			}
			setsOrder.push_back(set->vid.v);
			if (it->stickers.isEmpty() || (it->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
				setsToRequest.insert(set->vid.v, set->vaccess_hash.v);
			}
		}
	}

	int unreadCount = 0;
	for (auto it = sets.begin(), e = sets.end(); it != e;) {
		bool installed = (it->flags & MTPDstickerSet::Flag::f_installed);
		bool featured = (it->flags & MTPDstickerSet_ClientFlag::f_featured);
		bool special = (it->flags & MTPDstickerSet_ClientFlag::f_special);
		bool archived = (it->flags & MTPDstickerSet::Flag::f_archived);
		if (installed || featured || special || archived) {
			if (featured && (it->flags & MTPDstickerSet_ClientFlag::f_unread)) {
				++unreadCount;
			}
			++it;
		} else {
			it = sets.erase(it);
		}
	}
	if (Global::FeaturedStickerSetsUnreadCount() != unreadCount) {
		Global::SetFeaturedStickerSetsUnreadCount(unreadCount);
		Global::RefFeaturedStickerSetsUnreadCountChanged().notify();
	}

	if (Local::countFeaturedStickersHash() != d.vhash.v) {
		LOG(("API Error: received featured stickers hash %1 while counted hash is %2").arg(d.vhash.v).arg(Local::countFeaturedStickersHash()));
	}

	if (!setsToRequest.isEmpty() && App::api()) {
		for (QMap<uint64, uint64>::const_iterator i = setsToRequest.cbegin(), e = setsToRequest.cend(); i != e; ++i) {
			App::api()->scheduleStickerSetRequest(i.key(), i.value());
		}
		App::api()->requestStickerSets();
	}

	Local::writeFeaturedStickers();

	if (App::main()) emit App::main()->stickersUpdated();
}

bool HistoryWidget::featuredStickersFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("App Fail: Failed to get featured stickers!"));

	Global::SetLastFeaturedStickersUpdate(getms(true));
	_featuredStickersUpdateRequest = 0;
	return true;
}

void HistoryWidget::savedGifsGot(const MTPmessages_SavedGifs &gifs) {
	cSetLastSavedGifsUpdate(getms(true));
	_savedGifsUpdateRequest = 0;

	if (gifs.type() != mtpc_messages_savedGifs) return;
	auto &d = gifs.c_messages_savedGifs();

	auto &gifsList = d.vgifs.v;

	auto &saved = cRefSavedGifs();
	saved.clear();

	saved.reserve(gifsList.size());
	for (auto &gif : gifsList) {
		auto document = App::feedDocument(gif);
		if (!document || !document->isGifv()) {
			LOG(("API Error: bad document returned in HistoryWidget::savedGifsGot!"));
			continue;
		}

		saved.push_back(document);
	}
	if (Local::countSavedGifsHash() != d.vhash.v) {
		LOG(("API Error: received saved gifs hash %1 while counted hash is %2").arg(d.vhash.v).arg(Local::countSavedGifsHash()));
	}

	Local::writeSavedGifs();

	AuthSession::Current().data().savedGifsUpdated().notify();
}

void HistoryWidget::saveGif(DocumentData *doc) {
	if (doc->isGifv() && cSavedGifs().indexOf(doc) != 0) {
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

bool HistoryWidget::savedGifsFailed(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	LOG(("App Fail: Failed to get saved gifs!"));

	cSetLastSavedGifsUpdate(getms(true));
	_savedGifsUpdateRequest = 0;
	return true;
}

void HistoryWidget::clearReplyReturns() {
	_replyReturns.clear();
	_replyReturn = 0;
}

void HistoryWidget::pushReplyReturn(HistoryItem *item) {
	if (!item) return;
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
	if (!_replyReturn) updateControlsVisibility();
}

void HistoryWidget::fastShowAtEnd(History *h) {
	if (h == _history) {
		h->getReadyFor(ShowAtTheEndMsgId);

		clearAllLoadRequests();

		setMsgId(ShowAtUnreadMsgId);
		_historyInited = false;

		if (h->isReadyFor(_showAtMsgId)) {
			historyLoaded();
		} else {
			firstLoadMessages();
			doneShow();
		}
	} else if (h) {
		h->getReadyFor(ShowAtTheEndMsgId);
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
		if (!_replyEditMsg && App::api()) {
			App::api()->requestMessageData(_peer->asChannel(), _editMsgId ? _editMsgId : _replyToId, replyEditMessageDataCallback());
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

	if (_history) {
		if (_peer->id == peerId && !reload) {
			updateForwarding();

			bool canShowNow = _history->isReadyFor(showAtMsgId);
			if (!canShowNow) {
				delayedShowAt(showAtMsgId);

				App::main()->dlgUpdated(wasHistory ? wasHistory->peer : nullptr, wasMsgId);
				emit historyShown(_history, _showAtMsgId);
			} else {
				_history->forgetScrollState();
				if (_migrated) {
					_migrated->forgetScrollState();
				}

				clearDelayedShowAt();
				if (_replyReturn) {
					if (_replyReturn->history() == _history && _replyReturn->id == showAtMsgId) {
						calcNextReplyReturn();
					} else if (_replyReturn->history() == _migrated && -_replyReturn->id == showAtMsgId) {
						calcNextReplyReturn();
					}
				}

				setMsgId(showAtMsgId);
				if (_historyInited) {
					countHistoryShowFrom();
					destroyUnreadBar();

					auto item = getItemFromHistoryOrMigrated(_showAtMsgId);
					animatedScrollToY(countInitialScrollTop(), item);
					highlightMessage(item);
				} else {
					historyLoaded();
				}
			}

			_topBar->update();
			update();

			if (startBot && _peer->isUser() && _peer->asUser()->botInfo) {
				if (wasHistory) _peer->asUser()->botInfo->inlineReturnPeerId = wasHistory->peer->id;
				onBotStart();
				_history->clearLocalDraft();
				applyDraft();
				_send->finishAnimation();
			}
			return;
		}
		updateSendAction(_history, SendAction::Type::Typing, -1);
	}

	if (!cAutoPlayGif()) {
		App::stopGifItems();
	}
	clearReplyReturns();

	clearAllLoadRequests();

	if (_history) {
		if (App::main()) App::main()->saveDraftToCloud();
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
		_peer = nullptr;
		_channel = NoChannel;
		_canSendMessages = false;
		updateBotKeyboard();
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
	_scroll->takeWidget<HistoryInner>().destroyDelayed();
	_list = nullptr;

	clearInlineBot();

	_showAtMsgId = showAtMsgId;
	_historyInited = false;

	if (peerId) {
		_peer = App::peer(peerId);
		_channel = peerToChannel(_peer->id);
		_canSendMessages = canSendMessages(_peer);
		_tabbedSelector->setCurrentPeer(_peer);
	}
	updateTopBarSelection();

	if (_peer && _peer->isChannel()) {
		_peer->asChannel()->updateFull();
		_joinChannel->setText(lang(_peer->isMegagroup() ? lng_group_invite_join : lng_channel_join).toUpper());
	}

	_unblockRequest = _reportSpamRequest = 0;
	if (_reportSpamSettingRequestId > 0) {
		MTP::cancel(_reportSpamSettingRequestId);
	}
	_reportSpamSettingRequestId = ReportSpamRequestNeeded;

	_titlePeerText = QString();
	_titlePeerTextWidth = 0;

	noSelectingScroll();
	_nonEmptySelection = false;
	_topBar->showSelected(Window::TopBarWidget::SelectedState {});

	App::hoveredItem(nullptr);
	App::pressedItem(nullptr);
	App::hoveredLinkItem(nullptr);
	App::pressedLinkItem(nullptr);
	App::contextItem(nullptr);
	App::mousedItem(nullptr);

	if (_peer) {
		App::forgetMedia();
		_serviceImageCacheSize = imageCacheSize();
		AuthSession::Current().downloader().clearPriorities();

		_history = App::history(_peer->id);
		_migrated = _peer->migrateFrom() ? App::history(_peer->migrateFrom()->id) : 0;

		if (_channel) {
			updateNotifySettings();
			if (_peer->notify == UnknownNotifySettings) {
				App::api()->requestNotifySetting(_peer);
			}
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

		emit App::main()->peerUpdated(_peer);

		Local::readDraftsWithCursors(_history);
		if (_migrated) {
			Local::readDraftsWithCursors(_migrated);
			_migrated->clearEditDraft();
			_history->takeLocalDraft(_migrated);
		}
		applyDraft(false);
		_send->finishAnimation();

		updateControlsGeometry();
		if (!_previewCancelled) {
			onPreviewParse();
		}

		connect(_scroll, SIGNAL(geometryChanged()), _list, SLOT(onParentGeometryChanged()));

		if (startBot && _peer->isUser() && _peer->asUser()->botInfo) {
			if (wasHistory) _peer->asUser()->botInfo->inlineReturnPeerId = wasHistory->peer->id;
			onBotStart();
		}
		unreadCountChanged(_history); // set _historyDown badge.
	} else {
		clearFieldText();
		doneShow();
	}
	updateForwarding();
	updateOverStates(mapFromGlobal(QCursor::pos()));

	if (App::wnd()) QTimer::singleShot(0, App::wnd(), SLOT(setInnerFocus()));

	App::main()->dlgUpdated(wasHistory ? wasHistory->peer : nullptr, wasMsgId);
	emit historyShown(_history, _showAtMsgId);

	controller()->historyPeerChanged().notify(_peer, true);
	update();
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

	_muteUnmute->setText(lang(_history->mute() ? lng_channel_unmute : lng_channel_mute).toUpper());
	if (_peer->notify != UnknownNotifySettings) {
		_silent->setChecked(_peer->notify != EmptyNotifySettings && (_peer->notify->flags & MTPDpeerNotifySettings::Flag::f_silent));
		if (_silent->isHidden() && hasSilentToggle()) {
			updateControlsVisibility();
		}
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
	if (!_peer || (_peer->isUser() && (_peer->id == AuthSession::CurrentUserPeerId() || isNotificationsUser(_peer->id) || isServiceUser(_peer->id) || _peer->asUser()->botInfo))) {
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
				if (!_peer->isUser() || _peer->asUser()->contact < 1) {
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
					if (!_peer->isUser() || _peer->asUser()->contact < 1) {
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
	if (!AuthSession::Current().data().contactsLoaded().value() || _firstLoadRequest) {
		status = dbiprsUnknown;
	} else if (_peer->isUser() && _peer->asUser()->contact > 0) {
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
		return megagroup->restrictedRights().is_send_messages();
	}
	return false;
}

void HistoryWidget::updateControlsVisibility() {
	if (!_a_show.animating()) {
		_topShadow->setVisible(_peer != nullptr);
		_topBar->setVisible(_peer != nullptr);
	}
	updateHistoryDownVisibility();
	if (!_history || _a_show.animating()) {
		if (_tabbedSection && !_tabbedSection->isHidden()) {
			_tabbedSection->beforeHiding();
		}
		hideChildren();
		return;
	}

	if (_tabbedSection) {
		if (_tabbedSection->isHidden()) {
			_tabbedSection->show();
			_tabbedSection->afterShown();
		}
		_rightShadow->show();
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
		_silent->hide();
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
			_silent->hide();
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
			if (hasSilentToggle()) {
				_silent->show();
			} else {
				_silent->hide();
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
		_silent->hide();
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
	checkTabbedSelectorToggleTooltip();
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

void HistoryWidget::newUnreadMsg(History *history, HistoryItem *item) {
	if (_history == history) {
		if (_scroll->scrollTop() + 1 > _scroll->scrollTopMax()) {
			destroyUnreadBar();
		}
		if (App::wnd()->doWeReadServerHistory()) {
			historyWasRead(ReadServerHistoryChecks::ForceRequest);
			return;
		}
	}
	AuthSession::Current().notifications().schedule(history, item);
	history->setUnreadCount(history->unreadCount() + 1);
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

void HistoryWidget::historyWasRead(ReadServerHistoryChecks checks) {
	App::main()->readServerHistory(_history, checks);
	if (_migrated) {
		App::main()->readServerHistory(_migrated, ReadServerHistoryChecks::OnlyIfUnread);
	}
}

void HistoryWidget::unreadCountChanged(History *history) {
	if (history == _history || history == _migrated) {
		updateHistoryDownVisibility();
		if (_historyDown) {
			_historyDown->setUnreadCount(_history->unreadCount() + (_migrated ? _migrated->unreadCount() : 0));
		}
	}
}

bool HistoryWidget::messagesFailed(const RPCError &error, mtpRequestId requestId) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (error.type() == qstr("CHANNEL_PRIVATE") || error.type() == qstr("CHANNEL_PUBLIC_GROUP_NA") || error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
		auto was = _peer;
		App::main()->showBackFromStack();
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
		App::main()->showBackFromStack();
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

	int32 count = 0;
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
	}

	if (_preloadRequest == requestId) {
		addMessagesToFront(peer, *histList);
		_preloadRequest = 0;
		preloadHistoryIfNeeded();
		if (_reportSpamStatus == dbiprsUnknown) {
			updateReportSpamStatus();
			if (_reportSpamStatus != dbiprsUnknown) updateControlsVisibility();
		}
	} else if (_preloadDownRequest == requestId) {
		addMessagesToBack(peer, *histList);
		_preloadDownRequest = 0;
		preloadHistoryIfNeeded();
		if (_history->loadedAtBottom() && App::wnd()) App::wnd()->checkHistoryActivation();
	} else if (_firstLoadRequest == requestId) {
		if (toMigrated) {
			_history->clear(true);
		} else if (_migrated) {
			_migrated->clear(true);
		}
		addMessagesToFront(peer, *histList);
		_firstLoadRequest = 0;
		if (_history->loadedAtTop()) {
			if (_history->unreadCount() > count) {
				_history->setUnreadCount(count);
			}
			if (_history->isEmpty() && count > 0) {
				firstLoadMessages();
				return;
			}
		}

		historyLoaded();
	} else if (_delayedShowAtRequest == requestId) {
		if (toMigrated) {
			_history->clear(true);
		} else if (_migrated) {
			_migrated->clear(true);
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
			if (_history->loadedAtTop()) {
				if (_history->unreadCount() > count) {
					_history->setUnreadCount(count);
				}
				if (_history->isEmpty() && count > 0) {
					firstLoadMessages();
					return;
				}
			}
		}
		if (_replyReturn) {
			if (_replyReturn->history() == _history && _replyReturn->id == _delayedShowAtMsgId) {
				calcNextReplyReturn();
			} else if (_replyReturn->history() == _migrated && -_replyReturn->id == _delayedShowAtMsgId) {
				calcNextReplyReturn();
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

		auto showFrom = (_migrated && _migrated->showFrom) ? _migrated->showFrom : (_history ? _history->showFrom : nullptr);
		if (showFrom && !showFrom->detached()) {
			int scrollBottom = scrollTop + _scroll->height();
			if (scrollBottom > _list->itemTop(showFrom)) return true;
		}
	}
	if (historyHasNotFreezedUnreadBar(_history)) {
		return true;
	}
	if (historyHasNotFreezedUnreadBar(_migrated)) {
		return true;
	}
	return false;
}

bool HistoryWidget::historyHasNotFreezedUnreadBar(History *history) const {
	if (history && history->showFrom && !history->showFrom->detached() && history->unreadBar) {
		if (auto unreadBar = history->unreadBar->Get<HistoryMessageUnreadBar>()) {
			return !unreadBar->_freezed;
		}
	}
	return false;
}

void HistoryWidget::firstLoadMessages() {
	if (!_history || _firstLoadRequest) return;

	auto from = _peer;
	auto offset_id = 0;
	auto offset = 0;
	auto loadCount = kMessagesPerPage;
	if (_showAtMsgId == ShowAtUnreadMsgId) {
		if (_migrated && _migrated->unreadCount()) {
			_history->getReadyFor(_showAtMsgId);
			from = _migrated->peer;
			offset = -loadCount / 2;
			offset_id = _migrated->inboxReadBefore;
		} else if (_history->unreadCount()) {
			_history->getReadyFor(_showAtMsgId);
			offset = -loadCount / 2;
			offset_id = _history->inboxReadBefore;
		} else {
			_history->getReadyFor(ShowAtTheEndMsgId);
		}
	} else if (_showAtMsgId == ShowAtTheEndMsgId) {
		_history->getReadyFor(_showAtMsgId);
		loadCount = kMessagesPerPageFirst;
	} else if (_showAtMsgId > 0) {
		_history->getReadyFor(_showAtMsgId);
		offset = -loadCount / 2;
		offset_id = _showAtMsgId;
	} else if (_showAtMsgId < 0 && _history->isChannel()) {
		if (_showAtMsgId < 0 && -_showAtMsgId < ServerMaxMsgId && _migrated) {
			_history->getReadyFor(_showAtMsgId);
			from = _migrated->peer;
			offset = -loadCount / 2;
			offset_id = -_showAtMsgId;
		} else if (_showAtMsgId == SwitchAtTopMsgId) {
			_history->getReadyFor(_showAtMsgId);
		}
	}

	_firstLoadRequest = MTP::send(MTPmessages_GetHistory(from->input, MTP_int(offset_id), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from), rpcFail(&HistoryWidget::messagesFailed));
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

	auto offset_id = from->minMsgId();
	auto offset = 0;
	auto loadCount = offset_id ? kMessagesPerPage : kMessagesPerPageFirst;

	_preloadRequest = MTP::send(MTPmessages_GetHistory(from->peer->input, MTP_int(offset_id), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from->peer), rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::loadMessagesDown() {
	if (!_history || _preloadDownRequest) return;

	if (_history->isEmpty() && _migrated && _migrated->isEmpty()) {
		return firstLoadMessages();
	}

	bool loadMigrated = _migrated && !(_migrated->isEmpty() || _migrated->loadedAtBottom() || (!_history->isEmpty() && !_history->loadedAtTop()));
	History *from = loadMigrated ? _migrated : _history;
	if (from->loadedAtBottom()) {
		return;
	}

	auto loadCount = kMessagesPerPage;
	auto offset = -loadCount;
	auto offset_id = from->maxMsgId();
	if (!offset_id) {
		if (loadMigrated || !_migrated) return;
		++offset_id;
		++offset;
	}

	_preloadDownRequest = MTP::send(MTPmessages_GetHistory(from->peer->input, MTP_int(offset_id + 1), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from->peer), rpcFail(&HistoryWidget::messagesFailed));
}

void HistoryWidget::delayedShowAt(MsgId showAtMsgId) {
	if (!_history || (_delayedShowAtRequest && _delayedShowAtMsgId == showAtMsgId)) return;

	clearDelayedShowAt();
	_delayedShowAtMsgId = showAtMsgId;

	auto from = _peer;
	auto offset_id = 0;
	auto offset = 0;
	auto loadCount = kMessagesPerPage;
	if (_delayedShowAtMsgId == ShowAtUnreadMsgId) {
		if (_migrated && _migrated->unreadCount()) {
			from = _migrated->peer;
			offset = -loadCount / 2;
			offset_id = _migrated->inboxReadBefore;
		} else if (_history->unreadCount()) {
			offset = -loadCount / 2;
			offset_id = _history->inboxReadBefore;
		} else {
			loadCount = kMessagesPerPageFirst;
		}
	} else if (_delayedShowAtMsgId == ShowAtTheEndMsgId) {
		loadCount = kMessagesPerPageFirst;
	} else if (_delayedShowAtMsgId > 0) {
		offset = -loadCount / 2;
		offset_id = _delayedShowAtMsgId;
	} else if (_delayedShowAtMsgId < 0 && _history->isChannel()) {
		if (_delayedShowAtMsgId < 0 && -_delayedShowAtMsgId < ServerMaxMsgId && _migrated) {
			from = _migrated->peer;
			offset = -loadCount / 2;
			offset_id = -_delayedShowAtMsgId;
		}
	}

	_delayedShowAtRequest = MTP::send(MTPmessages_GetHistory(from->input, MTP_int(offset_id), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from), rpcFail(&HistoryWidget::messagesFailed));
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
	auto top = _list ? _list->itemTop(item) : -2;
	if (top < 0) {
		return true;
	}

	auto bottom = top + item->height();
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
			auto showFrom = (_migrated && _migrated->showFrom) ? _migrated->showFrom : (_history ? _history->showFrom : nullptr);
			if (showFrom && !showFrom->detached() && scrollBottom > _list->itemTop(showFrom) && App::wnd()->doWeReadServerHistory()) {
				historyWasRead(ReadServerHistoryChecks::OnlyIfUnread);
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
		auto below = (_replyReturn->detached() && _replyReturn->history() == _history && !_history->isEmpty() && _replyReturn->id < _history->blocks.back()->items.back()->id);
		if (!below) {
			below = (_replyReturn->detached() && _replyReturn->history() == _migrated && !_history->isEmpty());
		}
		if (!below) {
			below = (_replyReturn->detached() && _migrated && _replyReturn->history() == _migrated && !_migrated->isEmpty() && _replyReturn->id < _migrated->blocks.back()->items.back()->id);
		}
		if (!below && !_replyReturn->detached()) {
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

void HistoryWidget::onHistoryToEnd() {
	if (_replyReturn && _replyReturn->history() == _history) {
		showHistory(_peer->id, _replyReturn->id);
	} else if (_replyReturn && _replyReturn->history() == _migrated) {
		showHistory(_peer->id, -_replyReturn->id);
	} else if (_peer) {
		showHistory(_peer->id, ShowAtUnreadMsgId);
	}
}

void HistoryWidget::saveEditMsg() {
	if (_saveEditMsgRequestId) return;

	WebPageId webPageId = _previewCancelled ? CancelledWebPageId : ((_previewData && _previewData->pendingTill >= 0) ? _previewData->id : 0);

	auto &textWithTags = _field->getTextWithTags();
	auto prepareFlags = itemTextOptions(_history, App::self()).flags;
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

	auto sendFlags = qFlags(MTPmessages_EditMessage::Flag::f_message);
	if (webPageId == CancelledWebPageId) {
		sendFlags |= MTPmessages_EditMessage::Flag::f_no_webpage;
	}
	auto localEntities = TextUtilities::EntitiesToMTP(sending.entities);
	auto sentEntities = TextUtilities::EntitiesToMTP(sending.entities, TextUtilities::ConvertOption::SkipLocal);
	if (!sentEntities.v.isEmpty()) {
		sendFlags |= MTPmessages_EditMessage::Flag::f_entities;
	}
	_saveEditMsgRequestId = MTP::send(MTPmessages_EditMessage(MTP_flags(sendFlags), _history->peer->input, MTP_int(_editMsgId), MTP_string(sending.text), MTPnullMarkup, sentEntities), rpcDone(&HistoryWidget::saveEditMsgDone, _history), rpcFail(&HistoryWidget::saveEditMsgFail, _history));
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

void HistoryWidget::onSend(bool ctrlShiftEnter, MsgId replyTo) {
	if (!_history) return;

	if (_editMsgId) {
		saveEditMsg();
		return;
	}

	bool lastKeyboardUsed = lastForceReplyReplied(FullMsgId(_channel, replyTo));

	WebPageId webPageId = _previewCancelled ? CancelledWebPageId : ((_previewData && _previewData->pendingTill >= 0) ? _previewData->id : 0);

	MainWidget::MessageToSend message;
	message.history = _history;
	message.textWithTags = _field->getTextWithTags();
	message.replyTo = replyTo;
	message.silent = _silent->checked();
	message.webPageId = webPageId;
	App::main()->sendMessage(message);

	clearFieldText();
	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	hideSelectorControlsAnimated();

	if (replyTo < 0) cancelReply(lastKeyboardUsed);
	if (_previewData && _previewData->pendingTill) previewCancel();
	_field->setFocus();

	if (!_keyboard->hasMarkup() && _keyboard->forceReply() && !_kbReplyTo) onKbToggle();
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
	emit App::main()->peerUpdated(peer);
}

bool HistoryWidget::unblockFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_unblockRequest == req) _unblockRequest = 0;
	return false;
}

void HistoryWidget::blockDone(PeerData *peer, const MTPBool &result) {
	if (!peer->isUser()) return;

	peer->asUser()->setBlockStatus(UserData::BlockStatus::Blocked);
	emit App::main()->peerUpdated(peer);
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
	if (_unblockRequest) return;
	if (!_peer || !_peer->isChannel() || !isJoinChannel()) {
		updateControlsVisibility();
		return;
	}

	_unblockRequest = MTP::send(MTPchannels_JoinChannel(_peer->asChannel()->inputChannel), rpcDone(&HistoryWidget::joinDone), rpcFail(&HistoryWidget::joinFail));
}

void HistoryWidget::joinDone(const MTPUpdates &result, mtpRequestId req) {
	if (_unblockRequest == req) _unblockRequest = 0;
	if (App::main()) App::main()->sentUpdatesReceived(result);
}

bool HistoryWidget::joinFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	if (_unblockRequest == req) _unblockRequest = 0;
	if (error.type() == qstr("CHANNEL_PRIVATE") || error.type() == qstr("CHANNEL_PUBLIC_GROUP_NA") || error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
		Ui::show(Box<InformBox>(lang((_peer && _peer->isMegagroup()) ? lng_group_not_accessible : lng_channel_not_accessible)));
		return true;
	} else if (error.type() == qstr("CHANNELS_TOO_MUCH")) {
		Ui::show(Box<InformBox>(lang(lng_join_channel_error)));
	}

	return false;
}

void HistoryWidget::onMuteUnmute() {
	App::main()->updateNotifySetting(_peer, _history->mute() ? NotifySettingSetNotify : NotifySettingSetMuted);
}

void HistoryWidget::onBroadcastSilentChange() {
	updateFieldPlaceholder();
}

void HistoryWidget::onShareContact(const PeerId &peer, UserData *contact) {
	auto phone = contact->phone();
	if (phone.isEmpty()) phone = App::phoneFromSharedContact(peerToUser(contact->id));
	if (!contact || phone.isEmpty()) return;

	Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
	if (!_history) return;

	shareContact(peer, phone, contact->firstName, contact->lastName, replyToId(), peerToUser(contact->id));
}

void HistoryWidget::shareContact(const PeerId &peer, const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, int32 userId) {
	auto history = App::history(peer);

	uint64 randomId = rand_value<uint64>();
	FullMsgId newId(peerToChannel(peer), clientMsgId());

	App::main()->readServerHistory(history);
	fastShowAtEnd(history);

	auto p = App::peer(peer);
	auto flags = NewMessageFlags(p) | MTPDmessage::Flag::f_media; // unread, out

	bool lastKeyboardUsed = lastForceReplyReplied(FullMsgId(peerToChannel(peer), replyTo));

	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (replyTo) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}

	bool channelPost = p->isChannel() && !p->isMegagroup();
	bool silentPost = channelPost && _silent->checked();
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (!channelPost) {
		flags |= MTPDmessage::Flag::f_from_id;
	} else if (p->asChannel()->addsSignature()) {
		flags |= MTPDmessage::Flag::f_post_author;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
	}
	auto messageFromId = channelPost ? 0 : AuthSession::CurrentUserId();
	auto messagePostAuthor = channelPost ? (AuthSession::CurrentUser()->firstName + ' ' + AuthSession::CurrentUser()->lastName) : QString();
	history->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(newId.msg), MTP_int(messageFromId), peerToMTP(peer), MTPnullFwdHeader, MTPint(), MTP_int(replyToId()), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaContact(MTP_string(phone), MTP_string(fname), MTP_string(lname), MTP_int(userId)), MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint(), MTP_string(messagePostAuthor)), NewMessageUnread);
	history->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), p->input, MTP_int(replyTo), MTP_inputMediaContact(MTP_string(phone), MTP_string(fname), MTP_string(lname)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, history->sendRequestId);

	App::historyRegRandom(randomId, newId);

	App::main()->finishForwarding(history, _silent->checked());
	cancelReplyAfterMediaSend(lastKeyboardUsed);
}

History *HistoryWidget::history() const {
	return _history;
}

PeerData *HistoryWidget::peer() const {
	return _peer;
}

void HistoryWidget::setMsgId(MsgId showAtMsgId) { // sometimes _showAtMsgId is set directly
	if (_showAtMsgId != showAtMsgId) {
		auto wasMsgId = _showAtMsgId;
		_showAtMsgId = showAtMsgId;
		App::main()->dlgUpdated(_history ? _history->peer : nullptr, wasMsgId);
		emit historyShown(_history, _showAtMsgId);
	}
}

MsgId HistoryWidget::msgId() const {
	return _showAtMsgId;
}

void HistoryWidget::showAnimated(Window::SlideDirection direction, const Window::SectionSlideParams &params) {
	_showDirection = direction;

	_a_show.finish();

	_cacheUnder = params.oldContentCache;
	show();
	_topBar->updateControlsVisibility();
	historyDownAnimationFinish();
	_topShadow->setVisible(params.withTopBarShadow ? false : true);
	_cacheOver = App::main()->grabForShowAnimation(params);

	if (_tabbedSection && !_tabbedSection->isHidden()) {
		_tabbedSection->beforeHiding();
	}
	hideChildren();
	if (params.withTopBarShadow) _topShadow->show();
	if (params.withTabbedSection && _tabbedSection) {
		_tabbedSection->show();
		_tabbedSection->afterShown();
	}

	if (_showDirection == Window::SlideDirection::FromLeft) {
		std::swap(_cacheUnder, _cacheOver);
	}
	_a_show.start([this] { animationCallback(); }, 0., 1., st::slideDuration, Window::SlideAnimation::transition());
	if (_history) {
		_backAnimationButton.create(this);
		_backAnimationButton->setClickedCallback([this] { topBarClick(); });
		_backAnimationButton->setGeometry(_topBar->geometry());
		_backAnimationButton->show();
	}

	activate();
}

void HistoryWidget::animationCallback() {
	update();
	if (!_a_show.animating()) {
		historyDownAnimationFinish();
		_cacheUnder = _cacheOver = QPixmap();
		doneShow();
	}
}

void HistoryWidget::doneShow() {
	_topBar->animationFinished();
	_backAnimationButton.destroy();
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

void HistoryWidget::finishAnimation() {
	if (!_a_show.animating()) return;
	_a_show.finish();
	_topShadow->setVisible(_peer != nullptr);
	_topBar->setVisible(_peer != nullptr);
	historyDownAnimationFinish();
}

void HistoryWidget::historyDownAnimationFinish() {
	_historyDownShown.finish();
	updateHistoryDownPosition();
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
		if (megagroup->restrictedRights().is_send_media()) {
			Ui::show(Box<InformBox>(lang(lng_restricted_send_media)));
			return;
		}
	}

	auto filter = FileDialog::AllFilesFilter() + qsl(";;Image files (*") + cImgExtensions().join(qsl(" *")) + qsl(")");

	FileDialog::GetOpenPaths(lang(lng_choose_files), filter, base::lambda_guarded(this, [this](const FileDialog::OpenResult &result) {
		if (result.paths.isEmpty() && result.remoteContent.isEmpty()) {
			return;
		}

		if (!result.remoteContent.isEmpty()) {
			auto animated = false;
			auto image = App::readImage(result.remoteContent, nullptr, false, &animated);
			if (!image.isNull() && !animated) {
				confirmSendingFiles(image, result.remoteContent);
			} else {
				uploadFile(result.remoteContent, SendMediaType::File);
			}
		} else {
			auto lists = getSendingFilesLists(result.paths);
			if (lists.allFilesForCompress) {
				confirmSendingFiles(lists);
			} else {
				validateSendingFiles(lists, [this](const QStringList &files) {
					uploadFiles(files, SendMediaType::File);
					return true;
				});
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

	_attachDrag = getDragState(e->mimeData());
	updateDragAreas();

	if (_attachDrag) {
		e->setDropAction(Qt::IgnoreAction);
		e->accept();
	}
}

void HistoryWidget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_attachDrag != DragStateNone || !_attachDragPhoto->isHidden() || !_attachDragDocument->isHidden()) {
		_attachDrag = DragStateNone;
		updateDragAreas();
	}
}

void HistoryWidget::leaveEventHook(QEvent *e) {
	if (_attachDrag != DragStateNone || !_attachDragPhoto->isHidden() || !_attachDragDocument->isHidden()) {
		_attachDrag = DragStateNone;
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
		if (megagroup->restrictedRights().is_send_media()) {
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
	if (_attachDrag != DragStateNone || !_attachDragPhoto->isHidden() || !_attachDragDocument->isHidden()) {
		_attachDrag = DragStateNone;
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
	if (_peer && (!_peer->isChannel() || _peer->isMegagroup())) {
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

	MainWidget::MessageToSend message;
	message.history = _history;
	message.textWithTags = { toSend, TextWithTags::Tags() };
	message.replyTo = replyTo ? ((!_peer->isUser()/* && (botStatus == 0 || botStatus == 2)*/) ? replyTo : -1) : 0;
	message.silent = false;
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

void HistoryWidget::app_sendBotCallback(const HistoryMessageReplyMarkup::Button *button, gsl::not_null<const HistoryItem*> msg, int row, int col) {
	if (msg->id < 0 || _peer != msg->history()->peer) {
		return;
	}

	bool lastKeyboardUsed = (_keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)) && (_keyboard->forMsgId() == FullMsgId(_channel, msg->id));

	auto bot = msg->getMessageBot();

	using ButtonType = HistoryMessageReplyMarkup::Button::Type;
	BotCallbackInfo info = { bot, msg->fullId(), row, col, (button->type == ButtonType::Game) };
	auto flags = MTPmessages_GetBotCallbackAnswer::Flags(0);
	QByteArray sendData;
	if (info.game) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_game;
	} else if (button->type == ButtonType::Callback) {
		flags |= MTPmessages_GetBotCallbackAnswer::Flag::f_data;
		sendData = button->data;
	}
	button->requestId = MTP::send(MTPmessages_GetBotCallbackAnswer(MTP_flags(flags), _peer->input, MTP_int(msg->id), MTP_bytes(sendData)), rpcDone(&HistoryWidget::botCallbackDone, info), rpcFail(&HistoryWidget::botCallbackFail, info));
	Ui::repaintHistoryItem(msg);

	if (_replyToId == msg->id) {
		cancelReply();
	}
	if (_keyboard->singleUse() && _keyboard->hasMarkup() && lastKeyboardUsed) {
		if (_kbShown) onKbToggle(false);
		_history->lastKeyboardUsed = true;
	}
}

void HistoryWidget::botCallbackDone(BotCallbackInfo info, const MTPmessages_BotCallbackAnswer &answer, mtpRequestId req) {
	auto item = App::histItemById(info.msgId);
	if (item) {
		if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (info.row < markup->rows.size() && info.col < markup->rows.at(info.row).size()) {
				if (markup->rows.at(info.row).at(info.col).requestId == req) {
					markup->rows.at(info.row).at(info.col).requestId = 0;
					Ui::repaintHistoryItem(item);
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
				if (item && (!item->history()->peer->isChannel() || item->history()->peer->isMegagroup())) {
					updateSendAction(item->history(), SendAction::Type::PlayGame);
				}
			} else {
				UrlClickHandler(url).onClick(Qt::LeftButton);
			}
		}
	}
}

bool HistoryWidget::botCallbackFail(BotCallbackInfo info, const RPCError &error, mtpRequestId req) {
	// show error?
	if (auto item = App::histItemById(info.msgId)) {
		if (auto markup = item->Get<HistoryMessageReplyMarkup>()) {
			if (info.row < markup->rows.size() && info.col < markup->rows.at(info.row).size()) {
				if (markup->rows.at(info.row).at(info.col).requestId == req) {
					markup->rows.at(info.row).at(info.col).requestId = 0;
					Ui::repaintHistoryItem(item);
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
		auto bot = _peer->isUser() ? _peer : (App::hoveredLinkItem() ? App::hoveredLinkItem()->fromOriginal() : nullptr);
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
	if (obj == _historyDown && e->type() == QEvent::Wheel) {
		return _scroll->viewportEvent(e);
	}
	return TWidget::eventFilter(obj, e);
}

bool HistoryWidget::wheelEventFromFloatPlayer(QEvent *e, Window::Column myColumn, Window::Column playerColumn) {
	if (playerColumn == Window::Column::Third && _tabbedSection) {
		auto tabbedColumn = (myColumn == Window::Column::First) ? Window::Column::Second : Window::Column::Third;
		return _tabbedSection->wheelEventFromFloatPlayer(e, tabbedColumn, playerColumn);
	}
	return _scroll->viewportEvent(e);
}

QRect HistoryWidget::rectForFloatPlayer(Window::Column myColumn, Window::Column playerColumn) {
	if (playerColumn == Window::Column::Third && _tabbedSection) {
		auto tabbedColumn = (myColumn == Window::Column::First) ? Window::Column::Second : Window::Column::Third;
		return _tabbedSection->rectForFloatPlayer(tabbedColumn, playerColumn);
	}
	return mapToGlobal(_scroll->geometry());
}

DragState HistoryWidget::getDragState(const QMimeData *d) {
	if (!d
		|| d->hasFormat(qsl("application/x-td-forward-selected"))
		|| d->hasFormat(qsl("application/x-td-forward-pressed"))
		|| d->hasFormat(qsl("application/x-td-forward-pressed-link"))) return DragStateNone;

	if (d->hasImage()) return DragStateImage;

	QString uriListFormat(qsl("text/uri-list"));
	if (!d->hasFormat(uriListFormat)) return DragStateNone;

	QStringList imgExtensions(cImgExtensions()), files;

	const QList<QUrl> &urls(d->urls());
	if (urls.isEmpty()) return DragStateNone;

	bool allAreSmallImages = true;
	for (QList<QUrl>::const_iterator i = urls.cbegin(), en = urls.cend(); i != en; ++i) {
		if (!i->isLocalFile()) return DragStateNone;

		auto file = Platform::File::UrlToLocal(*i);

		QFileInfo info(file);
		if (info.isDir()) return DragStateNone;

		quint64 s = info.size();
		if (s > App::kFileSizeLimit) {
			return DragStateNone;
		}
		if (allAreSmallImages) {
			if (s > App::kImageSizeLimit) {
				allAreSmallImages = false;
			} else {
				bool foundImageExtension = false;
				for (QStringList::const_iterator j = imgExtensions.cbegin(), end = imgExtensions.cend(); j != end; ++j) {
					if (file.right(j->size()).toLower() == (*j).toLower()) {
						foundImageExtension = true;
						break;
					}
				}
				if (!foundImageExtension) {
					allAreSmallImages = false;
				}
			}
		}
	}
	return allAreSmallImages ? DragStatePhotoFiles : DragStateFiles;
}

void HistoryWidget::updateDragAreas() {
	_field->setAcceptDrops(!_attachDrag);
	updateControlsGeometry();

	switch (_attachDrag) {
	case DragStateNone:
		_attachDragDocument->otherLeave();
		_attachDragPhoto->otherLeave();
	break;
	case DragStateFiles:
		_attachDragDocument->setText(lang(lng_drag_files_here), lang(lng_drag_to_send_files));
		_attachDragDocument->otherEnter();
		_attachDragPhoto->hideFast();
	break;
	case DragStatePhotoFiles:
		_attachDragDocument->setText(lang(lng_drag_images_here), lang(lng_drag_to_send_no_compression));
		_attachDragPhoto->setText(lang(lng_drag_photos_here), lang(lng_drag_to_send_quick));
		_attachDragDocument->otherEnter();
		_attachDragPhoto->otherEnter();
	break;
	case DragStateImage:
		_attachDragPhoto->setText(lang(lng_drag_images_here), lang(lng_drag_to_send_quick));
		_attachDragDocument->hideFast();
		_attachDragPhoto->otherEnter();
	break;
	};
}

bool HistoryWidget::canSendMessages(PeerData *peer) const {
	return peer && peer->canWrite();
}

bool HistoryWidget::readyToForward() const {
	return _canSendMessages && !_toForward.isEmpty();
}

bool HistoryWidget::hasSilentToggle() const {
	return _peer && _peer->isChannel() && !_peer->isMegagroup() && _peer->asChannel()->canPublish() && _peer->notify != UnknownNotifySettings;
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
	if (!_peer || !_peer->isUser() || !_peer->asUser()->botInfo || !_canSendMessages) return false;
	return !_peer->asUser()->botInfo->startToken.isEmpty() || (_history->isEmpty() && !_history->lastMsg);
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
	_attachDrag = DragStateNone;
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
			_replyEditMsgText.setText(st::messageTextStyle, TextUtilities::Clean(_kbReplyTo->inReplyText()), _textDlgOptions);
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
			_replyEditMsgText.setText(st::messageTextStyle, TextUtilities::Clean(_kbReplyTo->inReplyText()), _textDlgOptions);
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

void HistoryWidget::forwardMessage() {
	auto item = App::contextItem();
	if (!item || item->id < 0 || item->serviceMsg()) return;

	auto items = SelectedItemSet();
	items.insert(item->id, item);
	App::main()->showForwardLayer(items);
}

void HistoryWidget::selectMessage() {
	auto item = App::contextItem();
	if (!item || item->id < 0 || item->serviceMsg()) return;

	if (_list) _list->selectItem(item);
}

bool HistoryWidget::paintTopBar(Painter &p, int decreaseWidth, TimeMs ms) {
	if (!_history) return false;

	auto increaseLeft = (Adaptive::OneColumn() || !App::main()->stackIsEmpty()) ? (st::topBarArrowPadding.left() - st::topBarArrowPadding.right()) : 0;
	auto nameleft = st::topBarArrowPadding.right() + increaseLeft;
	auto nametop = st::topBarArrowPadding.top();
	auto statustop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	auto namewidth = _chatWidth - decreaseWidth - nameleft - st::topBarArrowPadding.right();
	p.setFont(st::dialogsTextFont);
	if (!_history->paintSendAction(p, nameleft, statustop, namewidth, width(), st::historyStatusFgTyping, ms)) {
		p.setPen(_titlePeerTextOnline ? st::historyStatusFgActive : st::historyStatusFg);
		p.drawText(nameleft, statustop + st::dialogsTextFont->ascent, _titlePeerText);
	}

	p.setPen(st::dialogsNameFg);
	_peer->dialogName().drawElided(p, nameleft, nametop, namewidth);

	if (Adaptive::OneColumn() || !App::main()->stackIsEmpty()) {
		st::topBarBackward.paint(p, (st::topBarArrowPadding.left() - st::topBarBackward.width()) / 2, (st::topBarHeight - st::topBarBackward.height()) / 2, width());
	}
	return true;
}

QRect HistoryWidget::getMembersShowAreaGeometry() const {
	int increaseLeft = (Adaptive::OneColumn() || !App::main()->stackIsEmpty()) ? (st::topBarArrowPadding.left() - st::topBarArrowPadding.right()) : 0;
	int membersTextLeft = st::topBarArrowPadding.right() + increaseLeft;
	int membersTextTop = st::topBarHeight - st::topBarArrowPadding.bottom() - st::dialogsTextFont->height;
	int membersTextWidth = _titlePeerTextWidth;
	int membersTextHeight = st::topBarHeight - membersTextTop;

	return myrtlrect(membersTextLeft, membersTextTop, membersTextWidth, membersTextHeight);
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

void HistoryWidget::topBarClick() {
	if (Adaptive::OneColumn() || !App::main()->stackIsEmpty()) {
		App::main()->showBackFromStack();
	} else {
		if (_history) Ui::showPeerProfile(_peer);
	}
}

void HistoryWidget::updateTabbedSelectorSectionShown() {
	auto tabbedSelectorSectionEnabled = AuthSession::Current().data().tabbedSelectorSectionEnabled();
	auto useTabbedSection = tabbedSelectorSectionEnabled && (width() >= minimalWidthForTabbedSelectorSection());
	if (_tabbedSectionUsed == useTabbedSection) {
		return;
	}
	_tabbedSectionUsed = useTabbedSection;

	// Use a separate bool flag instead of just (_tabbedSection != nullptr), because
	// _tabbedPanel->takeSelector() calls QWidget::render(), which calls
	// sendPendingMoveAndResizeEvents() for all widgets in the window, which can lead
	// to a new HistoryWidget::resizeEvent() call and an infinite recursion here.
	if (_tabbedSectionUsed) {
		_tabbedSection.create(this, controller(), _tabbedPanel->takeSelector());
		_tabbedSection->setCancelledCallback([this] { setInnerFocus(); });
		_tabbedSelectorToggle->setColorOverrides(&st::historyAttachEmojiActive, &st::historyRecordVoiceFgActive, &st::historyRecordVoiceRippleBgActive);
		_rightShadow.create(this, st::shadowFg);
		auto destroyingPanel = std::move(_tabbedPanel);
		updateControlsVisibility();
	} else {
		_tabbedPanel.create(this, controller(), _tabbedSection->takeSelector());
		_tabbedPanel->hide();
		_tabbedSelectorToggle->installEventFilter(_tabbedPanel);
		_tabbedSection.destroy();
		_tabbedSelectorToggle->setColorOverrides(nullptr, nullptr, nullptr);
		_rightShadow.destroy();
		_tabbedSelectorToggleTooltipShown = false;
	}
	checkTabbedSelectorToggleTooltip();
	orderWidgets();
}

void HistoryWidget::checkTabbedSelectorToggleTooltip() {
	if (_tabbedSection && !_tabbedSection->isHidden() && !_tabbedSelectorToggle->isHidden()) {
		if (!_tabbedSelectorToggleTooltipShown) {
			auto shownCount = AuthSession::Current().data().tabbedSelectorSectionTooltipShown();
			if (shownCount < kTabbedSelectorToggleTooltipCount) {
				_tabbedSelectorToggleTooltipShown = true;
				_tabbedSelectorToggleTooltip.create(this, object_ptr<Ui::FlatLabel>(this, lang(lng_emoji_hide_panel), Ui::FlatLabel::InitType::Simple, st::defaultImportantTooltipLabel), st::defaultImportantTooltip);
				_tabbedSelectorToggleTooltip->setHiddenCallback([this] {
					_tabbedSelectorToggleTooltip.destroy();
				});
				InvokeQueued(_tabbedSelectorToggleTooltip, [this, shownCount] {
					AuthSession::Current().data().setTabbedSelectorSectionTooltipShown(shownCount + 1);
					AuthSession::Current().saveDataDelayed(kTabbedSelectorToggleTooltipTimeoutMs);

					updateTabbedSelectorToggleTooltipGeometry();
					_tabbedSelectorToggleTooltip->hideAfter(kTabbedSelectorToggleTooltipTimeoutMs);
					_tabbedSelectorToggleTooltip->toggleAnimated(true);
				});
			}
		}
	} else {
		_tabbedSelectorToggleTooltip.destroy();
	}
}

int HistoryWidget::tabbedSelectorSectionWidth() const {
	return st::emojiPanWidth;
}

int HistoryWidget::minimalWidthForTabbedSelectorSection() const {
	return st::windowMinWidth + tabbedSelectorSectionWidth();
}

bool HistoryWidget::willSwitchToTabbedSelectorWithWidth(int newWidth) const {
	if (!AuthSession::Current().data().tabbedSelectorSectionEnabled()) {
		return false;
	} else if (_tabbedSectionUsed) {
		return false;
	}
	return (newWidth >= minimalWidthForTabbedSelectorSection());
}

void HistoryWidget::toggleTabbedSelectorMode() {
	if (_tabbedSection) {
		AuthSession::Current().data().setTabbedSelectorSectionEnabled(false);
		AuthSession::Current().saveDataDelayed(kSaveTabbedSelectorSectionTimeoutMs);
		updateTabbedSelectorSectionShown();
		recountChatWidth();
		updateControlsGeometry();
	} else if (controller()->canProvideChatWidth(minimalWidthForTabbedSelectorSection())) {
		if (!AuthSession::Current().data().tabbedSelectorSectionEnabled()) {
			AuthSession::Current().data().setTabbedSelectorSectionEnabled(true);
			AuthSession::Current().saveDataDelayed(kSaveTabbedSelectorSectionTimeoutMs);
		}
		controller()->provideChatWidth(minimalWidthForTabbedSelectorSection());
		updateTabbedSelectorSectionShown();
		recountChatWidth();
		updateControlsGeometry();
	} else {
		t_assert(_tabbedPanel != nullptr);
		_tabbedPanel->toggleAnimated();
	}
}

void HistoryWidget::recountChatWidth() {
	_chatWidth = width();
	if (_tabbedSection) {
		_chatWidth -= _tabbedSection->width();
	}
	auto layout = (_chatWidth < st::adaptiveChatWideWidth) ? Adaptive::ChatLayout::Normal : Adaptive::ChatLayout::Wide;
	if (layout != Global::AdaptiveChatLayout()) {
		Global::SetAdaptiveChatLayout(layout);
		Adaptive::Changed().notify(true);
	}
}

void HistoryWidget::updateOnlineDisplay() {
	if (!_history) return;

	QString text;
	int32 t = unixtime();
	bool titlePeerTextOnline = false;
	if (auto user = _peer->asUser()) {
		text = App::onlineText(user, t);
		titlePeerTextOnline = App::onlineColorUse(user, t);
	} else if (_peer->isChat()) {
		auto chat = _peer->asChat();
		if (!chat->amIn()) {
			text = lang(lng_chat_status_unaccessible);
		} else if (chat->participants.isEmpty()) {
			if (!_titlePeerText.isEmpty()) {
				text = _titlePeerText;
			} else if (chat->count <= 0) {
				text = lang(lng_group_status);
			} else {
				text = lng_chat_status_members(lt_count, chat->count);
			}
		} else {
			auto online = 0;
			auto onlyMe = true;
			for (auto i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
				if (i.key()->onlineTill > t) {
					++online;
					if (onlyMe && i.key() != App::self()) onlyMe = false;
				}
			}
			if (online > 0 && !onlyMe) {
				auto membersCount = lng_chat_status_members(lt_count, chat->participants.size());
				auto onlineCount = lng_chat_status_online(lt_count, online);
				text = lng_chat_status_members_online(lt_members_count, membersCount, lt_online_count, onlineCount);
			} else if (chat->participants.size() > 0) {
				text = lng_chat_status_members(lt_count, chat->participants.size());
			} else {
				text = lang(lng_group_status);
			}
		}
	} else if (_peer->isChannel()) {
		if (_peer->isMegagroup() && _peer->asChannel()->membersCount() > 0 && _peer->asChannel()->membersCount() <= Global::ChatSizeMax()) {
			if (_peer->asChannel()->mgInfo->lastParticipants.size() < _peer->asChannel()->membersCount() || _peer->asChannel()->lastParticipantsCountOutdated()) {
				if (App::api()) App::api()->requestLastParticipants(_peer->asChannel());
			}
			auto online = 0;
			bool onlyMe = true;
			for (auto i = _peer->asChannel()->mgInfo->lastParticipants.cbegin(), e = _peer->asChannel()->mgInfo->lastParticipants.cend(); i != e; ++i) {
				if ((*i)->onlineTill > t) {
					++online;
					if (onlyMe && (*i) != App::self()) onlyMe = false;
				}
			}
			if (online && !onlyMe) {
				auto membersCount = lng_chat_status_members(lt_count, _peer->asChannel()->membersCount());
				auto onlineCount = lng_chat_status_online(lt_count, online);
				text = lng_chat_status_members_online(lt_members_count, membersCount, lt_online_count, onlineCount);
			} else if (_peer->asChannel()->membersCount() > 0) {
				text = lng_chat_status_members(lt_count, _peer->asChannel()->membersCount());
			} else {
				text = lang(lng_group_status);
			}
		} else if (_peer->asChannel()->membersCount() > 0) {
			text = lng_chat_status_members(lt_count, _peer->asChannel()->membersCount());
		} else {
			text = lang(_peer->isMegagroup() ? lng_group_status : lng_channel_status);
		}
	}
	if (_titlePeerText != text) {
		_titlePeerText = text;
		_titlePeerTextOnline = titlePeerTextOnline;
		_titlePeerTextWidth = st::dialogsTextFont->width(_titlePeerText);
		if (App::main()) {
			_topBar->updateMembersShowArea();
			_topBar->update();
		}
	}
	updateOnlineDisplayTimer();
}

void HistoryWidget::updateOnlineDisplayTimer() {
	if (!_history) return;

	int32 t = unixtime(), minIn = 86400;
	if (_peer->isUser()) {
		minIn = App::onlineWillChangeIn(_peer->asUser(), t);
	} else if (_peer->isChat()) {
		ChatData *chat = _peer->asChat();
		if (chat->participants.isEmpty()) return;

		for (auto i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
			int32 onlineWillChangeIn = App::onlineWillChangeIn(i.key(), t);
			if (onlineWillChangeIn < minIn) {
				minIn = onlineWillChangeIn;
			}
		}
	} else if (_peer->isChannel()) {
	}
	App::main()->updateOnlineDisplayIn(minIn * 1000);
}

void HistoryWidget::moveFieldControls() {
	auto keyboardHeight = 0;
	auto bottom = height();
	auto maxKeyboardHeight = st::historyComposeFieldMaxHeight - _field->height();
	_keyboard->resizeToWidth(_chatWidth, maxKeyboardHeight);
	if (_kbShown) {
		keyboardHeight = qMin(_keyboard->height(), maxKeyboardHeight);
		bottom -= keyboardHeight;
		_kbScroll->setGeometryToLeft(0, bottom, _chatWidth, keyboardHeight);
	}

// _attachToggle --------- _inlineResults -------------------------------------- _tabbedPanel --------- _fieldBarCancel
// (_attachDocument|_attachPhoto) _field (_silent|_cmdStart|_kbShow) (_kbHide|_tabbedSelectorToggle) [_broadcast] _send
// (_botStart|_unblock|_joinChannel|_muteUnmute)

	auto buttonsBottom = bottom - _attachToggle->height();
	auto left = 0;
	_attachToggle->moveToLeft(left, buttonsBottom); left += _attachToggle->width();
	_field->moveToLeft(left, bottom - _field->height() - st::historySendPadding);
	auto right = (width() - _chatWidth) + st::historySendRight;
	_send->moveToRight(right, buttonsBottom); right += _send->width();
	_tabbedSelectorToggle->moveToRight(right, buttonsBottom);
	updateTabbedSelectorToggleTooltipGeometry();
	_botKeyboardHide->moveToRight(right, buttonsBottom); right += _botKeyboardHide->width();
	_botKeyboardShow->moveToRight(right, buttonsBottom);
	_botCommandStart->moveToRight(right, buttonsBottom);
	_silent->moveToRight(right, buttonsBottom);

	_fieldBarCancel->moveToRight(width() - _chatWidth, _field->y() - st::historySendPadding - _fieldBarCancel->height());
	if (_inlineResults) {
		_inlineResults->moveBottom(_field->y() - st::historySendPadding);
	}
	if (_tabbedPanel) {
		_tabbedPanel->moveBottom(buttonsBottom);
	}

	auto fullWidthButtonRect = myrtlrect(0, bottom - _botStart->height(), _chatWidth, _botStart->height());
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
	auto fieldWidth = _chatWidth - _attachToggle->width() - st::historySendRight;
	fieldWidth -= _send->width();
	fieldWidth -= _tabbedSelectorToggle->width();
	if (kbShowShown) fieldWidth -= _botKeyboardShow->width();
	if (_cmdStartShown) fieldWidth -= _botCommandStart->width();
	if (hasSilentToggle()) fieldWidth -= _silent->width();

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
	if (_list) _list->clearSelectedItems(true);
}

void HistoryWidget::onCheckFieldAutocomplete() {
	if (!_history || _a_show.animating()) return;

	bool start = false;
	bool isInlineBot = _inlineBot && (_inlineBot != Ui::LookingUpInlineBot);
	QString query = isInlineBot ? QString() : _field->getMentionHashtagBotCommandPart(start);
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
			_field->setPlaceholder(langFactory((_history && _history->isChannel() && !_history->isMegagroup()) ? (_silent->checked() ? lng_broadcast_silent_ph : lng_broadcast_ph) : lng_message_ph));
		}
	}
	updateSendButtonType();
}

template <typename SendCallback>
bool HistoryWidget::showSendFilesBox(object_ptr<SendFilesBox> box, const QString &insertTextOnCancel, const QString *addedComment, SendCallback callback) {
	App::wnd()->activateWindow();

	auto withComment = (addedComment != nullptr);
	box->setConfirmedCallback(base::lambda_guarded(this, [this, withComment, sendCallback = std::move(callback)](const QStringList &files, const QImage &image, std::unique_ptr<FileLoadTask::MediaInformation> information, bool compressed, const QString &caption, bool ctrlShiftEnter) {
		if (!canWriteMessage()) return;

		auto replyTo = replyToId();
		if (withComment) {
			onSend(ctrlShiftEnter, replyTo);
		}
		sendCallback(files, image, std::move(information), compressed, caption, replyTo);
	}));

	if (withComment) {
		auto was = _field->getTextWithTags();
		setFieldText({ *addedComment, TextWithTags::Tags() });
		box->setCancelledCallback(base::lambda_guarded(this, [this, was] {
			setFieldText(was);
		}));
	} else if (!insertTextOnCancel.isEmpty()) {
		box->setCancelledCallback(base::lambda_guarded(this, [this, insertTextOnCancel] {
			_field->textCursor().insertText(insertTextOnCancel);
		}));
	}

	Ui::show(std::move(box));
	return true;
}

template <typename Callback>
bool HistoryWidget::validateSendingFiles(const SendingFilesLists &lists, Callback callback) {
	if (!canWriteMessage()) return false;

	App::wnd()->activateWindow();
	if (!lists.nonLocalUrls.isEmpty()) {
		Ui::show(Box<InformBox>(lng_send_image_non_local(lt_name, lists.nonLocalUrls.front().toDisplayString())));
	} else if (!lists.emptyFiles.isEmpty()) {
		Ui::show(Box<InformBox>(lng_send_image_empty(lt_name, lists.emptyFiles.front())));
	} else if (!lists.tooLargeFiles.isEmpty()) {
		Ui::show(Box<InformBox>(lng_send_image_too_large(lt_name, lists.tooLargeFiles.front())));
	} else if (!lists.filesToSend.isEmpty()) {
		return callback(lists.filesToSend);
	}
	return false;
}

bool HistoryWidget::confirmSendingFiles(const QList<QUrl> &files, CompressConfirm compressed, const QString *addedComment) {
	return confirmSendingFiles(getSendingFilesLists(files), compressed, addedComment);
}

bool HistoryWidget::confirmSendingFiles(const QStringList &files, CompressConfirm compressed, const QString *addedComment) {
	return confirmSendingFiles(getSendingFilesLists(files), compressed, addedComment);
}

bool HistoryWidget::confirmSendingFiles(const SendingFilesLists &lists, CompressConfirm compressed, const QString *addedComment) {
	if (auto megagroup = _peer ? _peer->asMegagroup() : nullptr) {
		if (megagroup->restrictedRights().is_send_media()) {
			Ui::show(Box<InformBox>(lang(lng_restricted_send_media)));
			return false;
		}
	}
	return validateSendingFiles(lists, [this, &lists, compressed, addedComment](const QStringList &files) {
		auto insertTextOnCancel = QString();
		auto sendCallback = [this](const QStringList &files, const QImage &image, std::unique_ptr<FileLoadTask::MediaInformation> information, bool compressed, const QString &caption, MsgId replyTo) {
			auto type = compressed ? SendMediaType::Photo : SendMediaType::File;
			uploadFilesAfterConfirmation(files, QByteArray(), image, std::move(information), type, caption);
		};
		auto boxCompressConfirm = compressed;
		if (files.size() > 1 && !lists.allFilesForCompress) {
			boxCompressConfirm = CompressConfirm::None;
		}
		auto box = Box<SendFilesBox>(files, boxCompressConfirm);
		return showSendFilesBox(std::move(box), insertTextOnCancel, addedComment, std::move(sendCallback));
	});
}

bool HistoryWidget::confirmSendingFiles(const QImage &image, const QByteArray &content, CompressConfirm compressed, const QString &insertTextOnCancel) {
	if (!canWriteMessage() || image.isNull()) return false;

	App::wnd()->activateWindow();
	auto sendCallback = [this, content](const QStringList &files, const QImage &image, std::unique_ptr<FileLoadTask::MediaInformation> information, bool compressed, const QString &caption, MsgId replyTo) {
		auto type = compressed ? SendMediaType::Photo : SendMediaType::File;
		uploadFilesAfterConfirmation(files, content, image, std::move(information), type, caption);
	};
	auto box = Box<SendFilesBox>(image, compressed);
	return showSendFilesBox(std::move(box), insertTextOnCancel, nullptr, std::move(sendCallback));
}

bool HistoryWidget::confirmSendingFiles(const QMimeData *data, CompressConfirm compressed, const QString &insertTextOnCancel) {
	if (!canWriteMessage()) {
		return false;
	}

	auto urls = data->urls();
	if (!urls.isEmpty()) {
		for_const (auto &url, urls) {
			if (url.isLocalFile()) {
				confirmSendingFiles(urls, compressed);
				return true;
			}
		}
	}
	if (data->hasImage()) {
		auto image = qvariant_cast<QImage>(data->imageData());
		if (!image.isNull()) {
			confirmSendingFiles(image, QByteArray(), compressed, insertTextOnCancel);
			return true;
		}
	}
	return false;
}

bool HistoryWidget::confirmShareContact(const QString &phone, const QString &fname, const QString &lname, const QString *addedComment) {
	if (!canWriteMessage()) return false;

	auto box = Box<SendFilesBox>(phone, fname, lname);
	auto sendCallback = [this, phone, fname, lname](const QStringList &files, const QImage &image, std::unique_ptr<FileLoadTask::MediaInformation> information, bool compressed, const QString &caption, MsgId replyTo) {
		shareContact(_peer->id, phone, fname, lname, replyTo);
	};
	auto insertTextOnCancel = QString();
	return showSendFilesBox(std::move(box), insertTextOnCancel, addedComment, std::move(sendCallback));
}

HistoryWidget::SendingFilesLists HistoryWidget::getSendingFilesLists(const QList<QUrl> &files) {
	auto result = SendingFilesLists();
	for_const (auto &url, files) {
		if (!url.isLocalFile()) {
			result.nonLocalUrls.push_back(url);
		} else {
			auto filepath = Platform::File::UrlToLocal(url);
			getSendingLocalFileInfo(result, filepath);
		}
	}
	return result;
}

HistoryWidget::SendingFilesLists HistoryWidget::getSendingFilesLists(const QStringList &files) {
	auto result = SendingFilesLists();
	for_const (auto &filepath, files) {
		getSendingLocalFileInfo(result, filepath);
	}
	return result;
}

void HistoryWidget::getSendingLocalFileInfo(SendingFilesLists &result, const QString &filepath) {
	auto hasExtensionForCompress = [](const QString &filepath) {
		for_const (auto extension, cExtensionsForCompress()) {
			if (filepath.right(extension.size()).compare(extension, Qt::CaseInsensitive) == 0) {
				return true;
			}
		}
		return false;
	};
	auto fileinfo = QFileInfo(filepath);
	if (fileinfo.isDir()) {
		result.directories.push_back(filepath);
	} else {
		auto filesize = fileinfo.size();
		if (filesize <= 0) {
			result.emptyFiles.push_back(filepath);
		} else if (filesize > App::kFileSizeLimit) {
			result.tooLargeFiles.push_back(filepath);
		} else {
			result.filesToSend.push_back(filepath);
			if (result.allFilesForCompress) {
				if (filesize > App::kImageSizeLimit || !hasExtensionForCompress(filepath)) {
					result.allFilesForCompress = false;
				}
			}
		}
	}
}

void HistoryWidget::uploadFiles(const QStringList &files, SendMediaType type) {
	if (!canWriteMessage()) return;

	auto caption = QString();
	uploadFilesAfterConfirmation(files, QByteArray(), QImage(), std::unique_ptr<FileLoadTask::MediaInformation>(), type, caption);
}

void HistoryWidget::uploadFilesAfterConfirmation(const QStringList &files, const QByteArray &content, const QImage &image, std::unique_ptr<FileLoadTask::MediaInformation> information, SendMediaType type, QString caption) {
	t_assert(canWriteMessage());

	auto to = FileLoadTo(_peer->id, _silent->checked(), replyToId());
	if (files.size() > 1 && !caption.isEmpty()) {
		MainWidget::MessageToSend message;
		message.history = _history;
		message.textWithTags = { caption, TextWithTags::Tags() };
		message.replyTo = to.replyTo;
		message.silent = to.silent;
		message.clearDraft = false;
		App::main()->sendMessage(message);
		caption = QString();
	}
	auto tasks = TasksList();
	tasks.reserve(files.size());
	for_const (auto &filepath, files) {
		if (filepath.isEmpty() && (!image.isNull() || !content.isNull())) {
			tasks.push_back(MakeShared<FileLoadTask>(content, image, type, to, caption));
		} else {
			tasks.push_back(MakeShared<FileLoadTask>(filepath, std::move(information), type, to, caption));
		}
	}
	_fileLoader.addTasks(tasks);

	cancelReplyAfterMediaSend(lastForceReplyReplied());
}

void HistoryWidget::uploadFile(const QByteArray &fileContent, SendMediaType type) {
	if (!canWriteMessage()) return;

	auto to = FileLoadTo(_peer->id, _silent->checked(), replyToId());
	auto caption = QString();
	_fileLoader.addTask(MakeShared<FileLoadTask>(fileContent, QImage(), type, to, caption));

	cancelReplyAfterMediaSend(lastForceReplyReplied());
}

void HistoryWidget::sendFileConfirmed(const FileLoadResultPtr &file) {
	bool lastKeyboardUsed = lastForceReplyReplied(FullMsgId(peerToChannel(file->to.peer), file->to.replyTo));

	FullMsgId newId(peerToChannel(file->to.peer), clientMsgId());

	connect(App::uploader(), SIGNAL(photoReady(const FullMsgId&,bool,const MTPInputFile&)), this, SLOT(onPhotoUploaded(const FullMsgId&,bool,const MTPInputFile&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentReady(const FullMsgId&,bool,const MTPInputFile&)), this, SLOT(onDocumentUploaded(const FullMsgId&,bool,const MTPInputFile&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(thumbDocumentReady(const FullMsgId&,bool,const MTPInputFile&,const MTPInputFile&)), this, SLOT(onThumbDocumentUploaded(const FullMsgId&,bool,const MTPInputFile&, const MTPInputFile&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(photoProgress(const FullMsgId&)), this, SLOT(onPhotoProgress(const FullMsgId&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentProgress(const FullMsgId&)), this, SLOT(onDocumentProgress(const FullMsgId&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(photoFailed(const FullMsgId&)), this, SLOT(onPhotoFailed(const FullMsgId&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentFailed(const FullMsgId&)), this, SLOT(onDocumentFailed(const FullMsgId&)), Qt::UniqueConnection);

	App::uploader()->upload(newId, file);

	History *h = App::history(file->to.peer);

	fastShowAtEnd(h);

	auto flags = NewMessageFlags(h->peer) | MTPDmessage::Flag::f_media; // unread, out
	if (file->to.replyTo) flags |= MTPDmessage::Flag::f_reply_to_msg_id;
	bool channelPost = h->peer->isChannel() && !h->peer->isMegagroup();
	bool silentPost = channelPost && file->to.silent;
	if (channelPost) {
		flags |= MTPDmessage::Flag::f_views;
		flags |= MTPDmessage::Flag::f_post;
	}
	if (!channelPost) {
		flags |= MTPDmessage::Flag::f_from_id;
	} else if (h->peer->asChannel()->addsSignature()) {
		flags |= MTPDmessage::Flag::f_post_author;
	}
	if (silentPost) {
		flags |= MTPDmessage::Flag::f_silent;
	}
	auto messageFromId = channelPost ? 0 : AuthSession::CurrentUserId();
	auto messagePostAuthor = channelPost ? (AuthSession::CurrentUser()->firstName + ' ' + AuthSession::CurrentUser()->lastName) : QString();
	if (file->type == SendMediaType::Photo) {
		auto photoFlags = qFlags(MTPDmessageMediaPhoto::Flag::f_photo);
		if (!file->caption.isEmpty()) {
			photoFlags |= MTPDmessageMediaPhoto::Flag::f_caption;
		}
		auto photo = MTP_messageMediaPhoto(MTP_flags(photoFlags), file->photo, MTP_string(file->caption), MTPint());
		h->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(newId.msg), MTP_int(messageFromId), peerToMTP(file->to.peer), MTPnullFwdHeader, MTPint(), MTP_int(file->to.replyTo), MTP_int(unixtime()), MTP_string(""), photo, MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint(), MTP_string(messagePostAuthor)), NewMessageUnread);
	} else if (file->type == SendMediaType::File) {
		auto documentFlags = qFlags(MTPDmessageMediaDocument::Flag::f_document);
		if (!file->caption.isEmpty()) {
			documentFlags |= MTPDmessageMediaDocument::Flag::f_caption;
		}
		auto document = MTP_messageMediaDocument(MTP_flags(documentFlags), file->document, MTP_string(file->caption), MTPint());
		h->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(newId.msg), MTP_int(messageFromId), peerToMTP(file->to.peer), MTPnullFwdHeader, MTPint(), MTP_int(file->to.replyTo), MTP_int(unixtime()), MTP_string(""), document, MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint(), MTP_string(messagePostAuthor)), NewMessageUnread);
	} else if (file->type == SendMediaType::Audio) {
		if (!h->peer->isChannel()) {
			flags |= MTPDmessage::Flag::f_media_unread;
		}
		auto documentFlags = qFlags(MTPDmessageMediaDocument::Flag::f_document);
		if (!file->caption.isEmpty()) {
			documentFlags |= MTPDmessageMediaDocument::Flag::f_caption;
		}
		auto document = MTP_messageMediaDocument(MTP_flags(documentFlags), file->document, MTP_string(file->caption), MTPint());
		h->addNewMessage(MTP_message(MTP_flags(flags), MTP_int(newId.msg), MTP_int(messageFromId), peerToMTP(file->to.peer), MTPnullFwdHeader, MTPint(), MTP_int(file->to.replyTo), MTP_int(unixtime()), MTP_string(""), document, MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint(), MTP_string(messagePostAuthor)), NewMessageUnread);
	}

	if (_peer && file->to.peer == _peer->id) {
		App::main()->historyToDown(_history);
	}
	App::main()->dialogsToUp();
	peerMessagesUpdated(file->to.peer);

	cancelReplyAfterMediaSend(lastKeyboardUsed);
}

void HistoryWidget::onPhotoUploaded(const FullMsgId &newId, bool silent, const MTPInputFile &file) {
	if (auto item = App::histItemById(newId)) {
		uint64 randomId = rand_value<uint64>();
		App::historyRegRandom(randomId, newId);
		History *hist = item->history();
		MsgId replyTo = item->replyToId();
		auto sendFlags = MTPmessages_SendMedia::Flags(0);
		if (replyTo) {
			sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
		}

		bool channelPost = hist->peer->isChannel() && !hist->peer->isMegagroup();
		bool silentPost = channelPost && silent;
		if (silentPost) {
			sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
		}
		auto caption = item->getMedia() ? item->getMedia()->getCaption() : TextWithEntities();
		auto media = MTP_inputMediaUploadedPhoto(MTP_flags(0), file, MTP_string(caption.text), MTPVector<MTPInputDocument>(), MTP_int(0));
		hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), item->history()->peer->input, MTP_int(replyTo), media, MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
	}
}

void HistoryWidget::onDocumentUploaded(const FullMsgId &newId, bool silent, const MTPInputFile &file) {
	if (auto item = dynamic_cast<HistoryMessage*>(App::histItemById(newId))) {
		auto media = item->getMedia();
		if (auto document = media ? media->getDocument() : nullptr) {
			auto randomId = rand_value<uint64>();
			App::historyRegRandom(randomId, newId);
			auto hist = item->history();
			auto replyTo = item->replyToId();
			auto sendFlags = MTPmessages_SendMedia::Flags(0);
			if (replyTo) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
			}

			bool channelPost = hist->peer->isChannel() && !hist->peer->isMegagroup();
			bool silentPost = channelPost && silent;
			if (silentPost) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
			}
			auto caption = item->getMedia() ? item->getMedia()->getCaption() : TextWithEntities();
			auto media = MTP_inputMediaUploadedDocument(MTP_flags(0), file, MTPInputFile(), MTP_string(document->mime), composeDocumentAttributes(document), MTP_string(caption.text), MTPVector<MTPInputDocument>(), MTP_int(0));
			hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), item->history()->peer->input, MTP_int(replyTo), media, MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
		}
	}
}

void HistoryWidget::onThumbDocumentUploaded(const FullMsgId &newId, bool silent, const MTPInputFile &file, const MTPInputFile &thumb) {
	if (auto item = dynamic_cast<HistoryMessage*>(App::histItemById(newId))) {
		auto media = item->getMedia();
		if (auto document = media ? media->getDocument() : nullptr) {
			auto randomId = rand_value<uint64>();
			App::historyRegRandom(randomId, newId);
			auto hist = item->history();
			auto replyTo = item->replyToId();
			auto sendFlags = MTPmessages_SendMedia::Flags(0);
			if (replyTo) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
			}

			bool channelPost = hist->peer->isChannel() && !hist->peer->isMegagroup();
			bool silentPost = channelPost && silent;
			if (silentPost) {
				sendFlags |= MTPmessages_SendMedia::Flag::f_silent;
			}
			auto caption = media ? media->getCaption() : TextWithEntities();
			auto media = MTP_inputMediaUploadedDocument(MTP_flags(MTPDinputMediaUploadedDocument::Flag::f_thumb), file, thumb, MTP_string(document->mime), composeDocumentAttributes(document), MTP_string(caption.text), MTPVector<MTPInputDocument>(), MTP_int(0));
			hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), item->history()->peer->input, MTP_int(replyTo), media, MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
		}
	}
}

void HistoryWidget::onPhotoProgress(const FullMsgId &newId) {
	if (auto item = App::histItemById(newId)) {
		auto photo = (item->getMedia() && item->getMedia()->type() == MediaTypePhoto) ? static_cast<HistoryPhoto*>(item->getMedia())->photo() : nullptr;
		if (!item->isPost()) {
			updateSendAction(item->history(), SendAction::Type::UploadPhoto, 0);
		}
		Ui::repaintHistoryItem(item);
	}
}

void HistoryWidget::onDocumentProgress(const FullMsgId &newId) {
	if (auto item = App::histItemById(newId)) {
		auto media = item->getMedia();
		auto document = media ? media->getDocument() : nullptr;
		if (!item->isPost()) {
			updateSendAction(item->history(), (document && document->voice()) ? SendAction::Type::UploadVoice : SendAction::Type::UploadFile, document ? document->uploadOffset : 0);
		}
		Ui::repaintHistoryItem(item);
	}
}

void HistoryWidget::onPhotoFailed(const FullMsgId &newId) {
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		if (!item->isPost()) {
			updateSendAction(item->history(), SendAction::Type::UploadPhoto, -1);
		}
//		Ui::repaintHistoryItem(item);
	}
}

void HistoryWidget::onDocumentFailed(const FullMsgId &newId) {
	if (auto item = App::histItemById(newId)) {
		auto media = item->getMedia();
		auto document = media ? media->getDocument() : nullptr;
		if (!item->isPost()) {
			updateSendAction(item->history(), (document && document->voice()) ? SendAction::Type::UploadVoice : SendAction::Type::UploadFile, -1);
		}
		Ui::repaintHistoryItem(item);
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
			MTP::send(MTPmessages_DeleteChatUser(chat->inputChat, App::self()->inputUser), App::main()->rpcDone(&MainWidget::deleteHistoryAfterLeave, peer), App::main()->rpcFail(&MainWidget::leaveChatFailed, peer));
		} else if (auto channel = peer->asChannel()) {
			if (channel->migrateFrom()) {
				App::main()->deleteConversation(channel->migrateFrom());
			}
			MTP::send(MTPchannels_LeaveChannel(channel->inputChannel), App::main()->rpcDone(&MainWidget::sentUpdatesReceived));
		}
	});

	// Invalidates _peer.
	App::main()->showBackFromStack();
}

void HistoryWidget::peerMessagesUpdated(PeerId peer) {
	if (_peer && _list && peer == _peer->id) {
		updateHistoryGeometry();
		updateBotKeyboard();
		if (!_scroll->isHidden()) {
			bool unblock = isBlocked(), botStart = isBotStart(), joinChannel = isJoinChannel(), muteUnmute = isMuteUnmute();
			bool upd = (_unblock->isHidden() == unblock);
			if (!upd && !unblock) upd = (_botStart->isHidden() == botStart);
			if (!upd && !unblock && !botStart) upd = (_joinChannel->isHidden() == joinChannel);
			if (!upd && !unblock && !botStart && !joinChannel) upd = (_muteUnmute->isHidden() == muteUnmute);
			if (upd) {
				updateControlsVisibility();
				updateControlsGeometry();
			}
		}
	}
}

void HistoryWidget::peerMessagesUpdated() {
	if (_list) peerMessagesUpdated(_peer->id);
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

void HistoryWidget::ui_repaintHistoryItem(gsl::not_null<const HistoryItem*> item) {
	if (_peer && _list && (item->history() == _history || (_migrated && item->history() == _migrated))) {
		auto ms = getms();
		if (_lastScrolled + kSkipRepaintWhileScrollMs <= ms) {
			_list->repaintItem(item);
		} else {
			_updateHistoryItems.start(_lastScrolled + kSkipRepaintWhileScrollMs - ms);
		}
	}
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

void HistoryWidget::notify_historyItemLayoutChanged(const HistoryItem *item) {
	if (_peer && _list && (item == App::mousedItem() || item == App::hoveredItem() || item == App::hoveredLinkItem())) {
		_list->onUpdateSelected();
	}
}

void HistoryWidget::handlePendingHistoryUpdate() {
	if (hasPendingResizedItems() || _updateHistoryGeometryRequired) {
		if (_list) {
			updateHistoryGeometry();
			_list->update();
		} else {
			_updateHistoryGeometryRequired = false;
		}
	}
}

void HistoryWidget::resizeEvent(QResizeEvent *e) {
	updateTabbedSelectorSectionShown();
	recountChatWidth();
	updateControlsGeometry();
}

void HistoryWidget::updateControlsGeometry() {
	if (_tabbedSection) {
		_tabbedSection->setGeometryToRight(0, 0, st::emojiPanWidth, height());
	}
	_topBar->setGeometryToLeft(0, 0, _chatWidth, st::topBarHeight);

	moveFieldControls();

	auto scrollAreaTop = _topBar->bottomNoMargins();
	if (_pinnedBar) {
		_pinnedBar->cancel->moveToLeft(_chatWidth - _pinnedBar->cancel->width(), scrollAreaTop);
		scrollAreaTop += st::historyReplyHeight;
		_pinnedBar->shadow->setGeometryToLeft(0, scrollAreaTop, _chatWidth, st::lineWidth);
	}
	if (_scroll->y() != scrollAreaTop) {
		_scroll->moveToLeft(0, scrollAreaTop);
		_fieldAutocomplete->setBoundings(_scroll->geometry());
	}
	if (_reportSpamPanel) {
		_reportSpamPanel->setGeometryToLeft(0, _scroll->y(), _chatWidth, _reportSpamPanel->height());
	}

	updateHistoryGeometry(false, false, { ScrollChangeAdd, App::main() ? App::main()->contentScrollAddToY() : 0 });

	updateFieldSize();

	updateHistoryDownPosition();

	if (_membersDropdown) {
		_membersDropdown->setMaxHeight(countMembersDropdownHeightMax());
	}

	switch (_attachDrag) {
	case DragStateFiles:
		_attachDragDocument->resize(width() - st::dragMargin.left() - st::dragMargin.right(), height() - st::dragMargin.top() - st::dragMargin.bottom());
		_attachDragDocument->move(st::dragMargin.left(), st::dragMargin.top());
	break;
	case DragStatePhotoFiles:
		_attachDragDocument->resize(width() - st::dragMargin.left() - st::dragMargin.right(), (height() - st::dragMargin.top() - st::dragMargin.bottom()) / 2);
		_attachDragDocument->move(st::dragMargin.left(), st::dragMargin.top());
		_attachDragPhoto->resize(_attachDragDocument->width(), _attachDragDocument->height());
		_attachDragPhoto->move(st::dragMargin.left(), height() - _attachDragPhoto->height() - st::dragMargin.bottom());
	break;
	case DragStateImage:
		_attachDragPhoto->resize(width() - st::dragMargin.left() - st::dragMargin.right(), height() - st::dragMargin.top() - st::dragMargin.bottom());
		_attachDragPhoto->move(st::dragMargin.left(), st::dragMargin.top());
	break;
	}

	if (_rightShadow) {
		_rightShadow->setGeometryToLeft(_chatWidth - st::lineWidth, 0, st::lineWidth, height());
	}
	auto topShadowLeft = (Adaptive::OneColumn() || _inGrab) ? 0 : st::lineWidth;
	auto topShadowRight = _rightShadow ? st::lineWidth : 0;
	_topShadow->setGeometryToLeft(topShadowLeft, _topBar->bottomNoMargins(), _chatWidth - topShadowLeft - topShadowRight, st::lineWidth);
}

void HistoryWidget::itemRemoved(HistoryItem *item) {
	if (item == _replyEditMsg) {
		if (_editMsgId) {
			cancelEdit();
		} else {
			cancelReply();
		}
	}
	if (item == _replyReturn) {
		calcNextReplyReturn();
	}
	if (_pinnedBar && item->id == _pinnedBar->msgId) {
		pinnedMsgVisibilityUpdated();
	}
	if (_kbReplyTo && item == _kbReplyTo) {
		onKbToggle();
		_kbReplyTo = 0;
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
			result = itemTopForHighlight(item);
			highlightMessage(item);
		}
	} else if (_history->unreadBar || (_migrated && _migrated->unreadBar)) {
		result = unreadBarTop();
	} else {
		return countAutomaticScrollTop();
	}
	return qMin(result, _scroll->scrollTopMax());
}

int HistoryWidget::countAutomaticScrollTop() {
	auto result = ScrollMax;
	if (_migrated && _migrated->showFrom) {
		result = _list->itemTop(_migrated->showFrom);
		if (result < _scroll->scrollTopMax() + HistoryMessageUnreadBar::height() - HistoryMessageUnreadBar::marginTop()) {
			_migrated->addUnreadBar();
			if (hasPendingResizedItems()) {
				updateListSize();
			}
			if (_migrated->unreadBar) {
				setMsgId(ShowAtUnreadMsgId);
				result = countInitialScrollTop();
				App::wnd()->checkHistoryActivation();
				return result;
			}
		}
	} else if (_history->showFrom) {
		result = _list->itemTop(_history->showFrom);
		if (result < _scroll->scrollTopMax() + HistoryMessageUnreadBar::height() - HistoryMessageUnreadBar::marginTop()) {
			_history->addUnreadBar();
			if (hasPendingResizedItems()) {
				updateListSize();
			}
			if (_history->unreadBar) {
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
	if (!_history || (initial && _historyInited) || (!initial && !_historyInited)) return;
	if (_firstLoadRequest || _a_show.animating()) {
		return; // scrollTopMax etc are not working after recountHeight()
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
	auto needResize = (_scroll->width() != _chatWidth) || (_scroll->height() != newScrollHeight);
	if (needResize) {
		_scroll->resize(_chatWidth, newScrollHeight);
		// on initial updateListSize we didn't put the _scroll->scrollTop correctly yet
		// so visibleAreaUpdated() call will erase it with the new (undefined) value
		if (!initial) {
			visibleAreaUpdated();
		}

		_fieldAutocomplete->setBoundings(_scroll->geometry());
		if (!_historyDownShown.animating()) {
			// _historyDown is a child widget of _scroll, not me.
			_historyDown->moveToRight(st::historyToDownPosition.x(), _scroll->height() - _historyDown->height() - st::historyToDownPosition.y());
		}

		controller()->floatPlayerAreaUpdated().notify(true);
	}

	updateListSize();
	_updateHistoryGeometryRequired = false;

	if ((!initial && !wasAtBottom) || (loadedDown && (!_history->showFrom || _history->unreadBar || _history->loadedAtBottom()) && (!_migrated || !_migrated->showFrom || _migrated->unreadBar || _history->loadedAtBottom()))) {
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
	auto newScrollTop = initial ? countInitialScrollTop() : countAutomaticScrollTop();
	if (_scroll->scrollTop() == newScrollTop) {
		visibleAreaUpdated();
	} else {
		synteticScrollToY(newScrollTop);
	}
}

void HistoryWidget::updateListSize() {
	_list->recountHeight();
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

int HistoryWidget::unreadBarTop() const {
	auto getUnreadBar = [this]() -> HistoryItem* {
		if (_migrated && _migrated->unreadBar) {
			return _migrated->unreadBar;
		}
		if (_history->unreadBar) {
			return _history->unreadBar;
		}
		return nullptr;
	};
	if (HistoryItem *bar = getUnreadBar()) {
		int result = _list->itemTop(bar) + HistoryMessageUnreadBar::marginTop();
		if (bar->Has<HistoryMessageDate>()) {
			result += bar->Get<HistoryMessageDate>()->height();
		}
		return result;
	}
	return -1;
}

void HistoryWidget::addMessagesToFront(PeerData *peer, const QVector<MTPMessage> &messages) {
	_list->messagesReceived(peer, messages);
	if (!_firstLoadRequest) {
		updateHistoryGeometry();
		if (_animActiveTimer.isActive() && _activeAnimMsgId > 0 && _migrated && !_migrated->isEmpty() && _migrated->loadedAtBottom() && _migrated->blocks.back()->items.back()->isGroupMigrate() && _list->historyTop() != _list->historyDrawTop() && _history) {
			auto animActiveItem = App::histItemById(_history->channelId(), _activeAnimMsgId);
			if (animActiveItem && animActiveItem->isGroupMigrate()) {
				_activeAnimMsgId = -_migrated->blocks.back()->items.back()->id;
			}
		}
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
	if (_migrated && _showAtMsgId == ShowAtUnreadMsgId && _migrated->unreadCount()) {
		_migrated->updateShowFrom();
	}
	if ((_migrated && _migrated->showFrom) || _showAtMsgId != ShowAtUnreadMsgId || !_history->unreadCount()) {
		_history->showFrom = nullptr;
		return;
	}
	_history->updateShowFrom();
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
				_replyEditMsgText.setText(st::messageTextStyle, TextUtilities::Clean(_kbReplyTo->inReplyText()), _textDlgOptions);
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
}

void HistoryWidget::updateHistoryDownVisibility() {
	if (_a_show.animating()) return;

	auto haveUnreadBelowBottom = [this](History *history) {
		if (!_list || !history || history->unreadCount() <= 0) {
			return false;
		}
		if (!history->showFrom || history->showFrom->detached()) {
			return false;
		}
		return (_list->itemTop(history->showFrom) >= _scroll->scrollTop() + _scroll->height());
	};
	auto historyDownIsVisible = [this, &haveUnreadBelowBottom]() {
		if (!_history || _firstLoadRequest) {
			return false;
		}
		if (!_history->loadedAtBottom() || _replyReturn) {
			return true;
		}
		if (_scroll->scrollTop() + st::historyToDownShownAfter < _scroll->scrollTopMax()) {
			return true;
		}
		if (haveUnreadBelowBottom(_history) || haveUnreadBelowBottom(_migrated)) {
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

void HistoryWidget::mousePressEvent(QMouseEvent *e) {
	_replyForwardPressed = QRect(0, _field->y() - st::historySendPadding - st::historyReplyHeight, st::historyReplySkip, st::historyReplyHeight).contains(e->pos());
	if (_replyForwardPressed && !_fieldBarCancel->isHidden()) {
		updateField();
	} else if (_inReplyEditForward) {
		if (readyToForward()) {
			auto items = _toForward;
			App::main()->cancelForwarding(_history);
			App::main()->showForwardLayer(items);
		} else {
			Ui::showPeerHistory(_peer, _editMsgId ? _editMsgId : replyToId());
		}
	} else if (_inPinnedMsg) {
		t_assert(_pinnedBar != nullptr);
		Ui::showPeerHistory(_peer, _pinnedBar->msgId);
	}
}

void HistoryWidget::keyPressEvent(QKeyEvent *e) {
	if (!_history) return;

	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Back) {
		App::main()->showBackFromStack();
		emit cancelled();
	} else if (e->key() == Qt::Key_PageDown) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_PageUp) {
		_scroll->keyPressEvent(e);
	} else if (e->key() == Qt::Key_Down) {
		if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			_scroll->keyPressEvent(e);
		}
	} else if (e->key() == Qt::Key_Up) {
		if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			if (_history && _history->lastSentMsg && _history->lastSentMsg->canEdit(::date(unixtime()))) {
				if (_field->isEmpty() && !_editMsgId && !_replyToId) {
					App::contextItem(_history->lastSentMsg);
					onEditMessage();
					return;
				}
			}
			_scroll->keyPressEvent(e);
		}
	} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		onListEnterPressed();
	} else {
		e->ignore();
	}
}

void HistoryWidget::onFieldTabbed() {
	if (!_fieldAutocomplete->isHidden()) {
		_fieldAutocomplete->chooseSelected(FieldAutocomplete::ChooseMethod::ByTab);
	}
}

bool HistoryWidget::onStickerSend(DocumentData *sticker) {
	if (auto megagroup = _peer ? _peer->asMegagroup() : nullptr) {
		if (megagroup->restrictedRights().is_send_stickers()) {
			Ui::show(Box<InformBox>(lang(lng_restricted_send_stickers)), KeepOtherLayers);
			return false;
		}
	}
	return sendExistingDocument(sticker, QString());
}

void HistoryWidget::onPhotoSend(PhotoData *photo) {
	if (auto megagroup = _peer ? _peer->asMegagroup() : nullptr) {
		if (megagroup->restrictedRights().is_send_media()) {
			Ui::show(Box<InformBox>(lang(lng_restricted_send_media)), KeepOtherLayers);
			return;
		}
	}
	sendExistingPhoto(photo, QString());
}

void HistoryWidget::onInlineResultSend(InlineBots::Result *result, UserData *bot) {
	if (!_history || !result || !canSendMessages(_peer)) return;

	auto errorText = result->getErrorOnSend(_history);
	if (!errorText.isEmpty()) {
		Ui::show(Box<InformBox>(errorText));
		return;
	}

	App::main()->readServerHistory(_history);
	fastShowAtEnd(_history);

	uint64 randomId = rand_value<uint64>();
	FullMsgId newId(_channel, clientMsgId());

	bool lastKeyboardUsed = lastForceReplyReplied();

	bool out = !_peer->isSelf(), unread = !_peer->isSelf();
	auto flags = NewMessageFlags(_peer) | MTPDmessage::Flag::f_media; // unread, out
	auto sendFlags = qFlags(MTPmessages_SendInlineBotResult::Flag::f_clear_draft);
	if (replyToId()) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendInlineBotResult::Flag::f_reply_to_msg_id;
	}
	bool channelPost = _peer->isChannel() && !_peer->isMegagroup();
	bool silentPost = channelPost && _silent->checked();
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

	auto messageFromId = channelPost ? 0 : AuthSession::CurrentUserId();
	auto messagePostAuthor = channelPost ? (AuthSession::CurrentUser()->firstName + ' ' + AuthSession::CurrentUser()->lastName) : QString();
	MTPint messageDate = MTP_int(unixtime());
	UserId messageViaBotId = bot ? peerToUser(bot->id) : 0;
	MsgId messageId = newId.msg;

	result->addToHistory(_history, flags, messageId, messageFromId, messageDate, messageViaBotId, replyToId(), messagePostAuthor);

	_history->sendRequestId = MTP::send(MTPmessages_SendInlineBotResult(MTP_flags(sendFlags), _peer->input, MTP_int(replyToId()), MTP_long(randomId), MTP_long(result->getQueryId()), MTP_string(result->getId())), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, _history->sendRequestId);
	App::main()->finishForwarding(_history, _silent->checked());
	cancelReply(lastKeyboardUsed);

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
, shadow(parent, st::shadowFg) {
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

	t_assert(_history != nullptr);
	if (!_pinnedBar->msg) {
		_pinnedBar->msg = App::histItemById(_history->channelId(), _pinnedBar->msgId);
	}
	if (_pinnedBar->msg) {
		_pinnedBar->text.setText(st::messageTextStyle, TextUtilities::Clean(_pinnedBar->msg->notificationText()), _textDlgOptions);
		update();
	} else if (force) {
		if (_peer && _peer->isMegagroup()) {
			_peer->asChannel()->mgInfo->pinnedMsgId = 0;
		}
		destroyPinnedBar();
		updateControlsGeometry();
	}
}

bool HistoryWidget::pinnedMsgVisibilityUpdated() {
	auto result = false;
	auto pinnedMsgId = (_peer && _peer->isMegagroup()) ? _peer->asChannel()->mgInfo->pinnedMsgId : 0;
	if (pinnedMsgId && !_peer->asChannel()->canPinMessages()) {
		Global::HiddenPinnedMessagesMap::const_iterator it = Global::HiddenPinnedMessages().constFind(_peer->id);
		if (it != Global::HiddenPinnedMessages().cend()) {
			if (it.value() == pinnedMsgId) {
				pinnedMsgId = 0;
			} else {
				Global::RefHiddenPinnedMessages().remove(_peer->id);
				Local::writeUserSettings();
			}
		}
	}
	if (pinnedMsgId) {
		if (!_pinnedBar) {
			_pinnedBar = std::make_unique<PinnedBar>(pinnedMsgId, this);
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

			if (_scroll->scrollTop() != unreadBarTop()) {
				synteticScrollToY(_scroll->scrollTop() + st::historyReplyHeight);
			}
		} else if (_pinnedBar->msgId != pinnedMsgId) {
			_pinnedBar->msgId = pinnedMsgId;
			_pinnedBar->msg = 0;
			_pinnedBar->text.clear();
			updatePinnedBar();
		}
		if (!_pinnedBar->msg && App::api()) {
			App::api()->requestMessageData(_peer->asChannel(), _pinnedBar->msgId, replyEditMessageDataCallback());
		}
	} else if (_pinnedBar) {
		destroyPinnedBar();
		result = true;
		if (_scroll->scrollTop() != unreadBarTop()) {
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

bool HistoryWidget::sendExistingDocument(DocumentData *doc, const QString &caption) {
	if (!_history || !doc || !canSendMessages(_peer)) {
		return false;
	}

	MTPInputDocument mtpInput = doc->mtpInput();
	if (mtpInput.type() == mtpc_inputDocumentEmpty) {
		return false;
	}

	App::main()->readServerHistory(_history);
	fastShowAtEnd(_history);

	uint64 randomId = rand_value<uint64>();
	FullMsgId newId(_channel, clientMsgId());

	bool lastKeyboardUsed = lastForceReplyReplied();

	bool out = !_peer->isSelf(), unread = !_peer->isSelf();
	auto flags = NewMessageFlags(_peer) | MTPDmessage::Flag::f_media; // unread, out
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (replyToId()) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	bool channelPost = _peer->isChannel() && !_peer->isMegagroup();
	bool silentPost = channelPost && _silent->checked();
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
	auto messageFromId = channelPost ? 0 : AuthSession::CurrentUserId();
	auto messagePostAuthor = channelPost ? (AuthSession::CurrentUser()->firstName + ' ' + AuthSession::CurrentUser()->lastName) : QString();
	_history->addNewDocument(newId.msg, flags, 0, replyToId(), date(MTP_int(unixtime())), messageFromId, messagePostAuthor, doc, caption, MTPnullMarkup);

	_history->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), _peer->input, MTP_int(replyToId()), MTP_inputMediaDocument(MTP_flags(0), mtpInput, MTP_string(caption), MTPint()), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, _history->sendRequestId);
	App::main()->finishForwarding(_history, _silent->checked());
	cancelReplyAfterMediaSend(lastKeyboardUsed);

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

void HistoryWidget::sendExistingPhoto(PhotoData *photo, const QString &caption) {
	if (!_history || !photo || !canSendMessages(_peer)) return;

	App::main()->readServerHistory(_history);
	fastShowAtEnd(_history);

	uint64 randomId = rand_value<uint64>();
	FullMsgId newId(_channel, clientMsgId());

	bool lastKeyboardUsed = lastForceReplyReplied();

	bool out = !_peer->isSelf(), unread = !_peer->isSelf();
	auto flags = NewMessageFlags(_peer) | MTPDmessage::Flag::f_media; // unread, out
	auto sendFlags = MTPmessages_SendMedia::Flags(0);
	if (replyToId()) {
		flags |= MTPDmessage::Flag::f_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::Flag::f_reply_to_msg_id;
	}
	bool channelPost = _peer->isChannel() && !_peer->isMegagroup();
	bool silentPost = channelPost && _silent->checked();
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
	auto messageFromId = channelPost ? 0 : AuthSession::CurrentUserId();
	auto messagePostAuthor = channelPost ? (AuthSession::CurrentUser()->firstName + ' ' + AuthSession::CurrentUser()->lastName) : QString();
	_history->addNewPhoto(newId.msg, flags, 0, replyToId(), date(MTP_int(unixtime())), messageFromId, messagePostAuthor, photo, caption, MTPnullMarkup);

	_history->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_flags(sendFlags), _peer->input, MTP_int(replyToId()), MTP_inputMediaPhoto(MTP_flags(0), MTP_inputPhoto(MTP_long(photo->id), MTP_long(photo->access)), MTP_string(caption), MTPint()), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, _history->sendRequestId);
	App::main()->finishForwarding(_history, _silent->checked());
	cancelReplyAfterMediaSend(lastKeyboardUsed);

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

void HistoryWidget::onReplyToMessage() {
	auto to = App::contextItem();
	if (!to || to->id <= 0 || !_canSendMessages) return;

	if (to->history() == _migrated) {
		if (to->isGroupMigrate() && !_history->isEmpty() && _history->blocks.front()->items.front()->isGroupMigrate() && _history != _migrated) {
			App::contextItem(_history->blocks.front()->items.front());
			onReplyToMessage();
			App::contextItem(to);
		} else {
			if (to->id < 0 || to->serviceMsg()) {
				Ui::show(Box<InformBox>(lang(lng_reply_cant)));
			} else {
				Ui::show(Box<ConfirmBox>(lang(lng_reply_cant_forward), lang(lng_selected_forward), base::lambda_guarded(this, [this] {
					auto item = App::contextItem();
					if (!item || item->id < 0 || item->serviceMsg()) return;

					auto items = SelectedItemSet();
					items.insert(item->id, item);
					App::main()->setForwardDraft(_peer->id, items);
				})));
			}
		}
		return;
	}

	App::main()->cancelForwarding(_history);

	if (_editMsgId) {
		if (auto localDraft = _history->localDraft()) {
			localDraft->msgId = to->id;
		} else {
			_history->setLocalDraft(std::make_unique<Data::Draft>(TextWithTags(), to->id, MessageCursor(), false));
		}
	} else {
		_replyEditMsg = to;
		_replyToId = to->id;
		_replyEditMsgText.setText(st::messageTextStyle, TextUtilities::Clean(_replyEditMsg->inReplyText()), _textDlgOptions);

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

void HistoryWidget::onEditMessage() {
	auto to = App::contextItem();
	if (!to) return;

	if (auto media = to->getMedia()) {
		if (media->canEditCaption()) {
			Ui::show(Box<EditCaptionBox>(media, to->fullId()));
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

	auto original = to->originalText();
	auto editData = TextWithTags { TextUtilities::ApplyEntities(original), ConvertEntitiesToTextTags(original.entities) };
	auto cursor = MessageCursor { editData.text.size(), editData.text.size(), QFIXED_MAX };
	_history->setEditDraft(std::make_unique<Data::Draft>(editData, to->id, cursor, false));
	applyDraft(false);

	_previewData = nullptr;
	if (auto media = to->getMedia()) {
		if (media->type() == MediaTypeWebPage) {
			_previewData = static_cast<HistoryWebPage*>(media)->webpage();
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

void HistoryWidget::onPinMessage() {
	HistoryItem *to = App::contextItem();
	if (!to || !to->canPin() || !_peer || !_peer->isMegagroup()) return;

	Ui::show(Box<PinMessageBox>(_peer->asChannel(), to->id));
}

void HistoryWidget::onUnpinMessage() {
	if (!_peer || !_peer->isMegagroup()) return;

	Ui::show(Box<ConfirmBox>(lang(lng_pinned_unpin_sure), lang(lng_pinned_unpin), base::lambda_guarded(this, [this] {
		if (!_peer || !_peer->isMegagroup()) return;

		_peer->asChannel()->mgInfo->pinnedMsgId = 0;
		if (pinnedMsgVisibilityUpdated()) {
			updateControlsGeometry();
			update();
		}

		Ui::hideLayer();
		MTP::send(MTPchannels_UpdatePinnedMessage(MTP_flags(0), _peer->asChannel()->inputChannel, MTP_int(0)), rpcDone(&HistoryWidget::unpinDone));
	})));
}

void HistoryWidget::unpinDone(const MTPUpdates &updates) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
}

void HistoryWidget::onPinnedHide() {
	if (!_peer || !_peer->isMegagroup()) return;
	if (!_peer->asChannel()->mgInfo->pinnedMsgId) {
		if (pinnedMsgVisibilityUpdated()) {
			updateControlsGeometry();
			update();
		}
		return;
	}

	if (_peer->asChannel()->canPinMessages()) {
		onUnpinMessage();
	} else {
		Global::RefHiddenPinnedMessages().insert(_peer->id, _peer->asChannel()->mgInfo->pinnedMsgId);
		Local::writeUserSettings();
		if (pinnedMsgVisibilityUpdated()) {
			updateControlsGeometry();
			update();
		}
	}
}

void HistoryWidget::onCopyPostLink() {
	auto item = App::contextItem();
	if (!item || !item->hasDirectLink()) return;

	QApplication::clipboard()->setText(item->directLink());
}

bool HistoryWidget::lastForceReplyReplied(const FullMsgId &replyTo) const {
	if (replyTo.msg > 0 && replyTo.channel != _channel) return false;
	return _keyboard->forceReply() && _keyboard->forMsgId() == FullMsgId(_channel, _history->lastKeyboardId) && _keyboard->forMsgId().msg == (replyTo.msg < 0 ? replyToId() : replyTo.msg);
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
	if (!_editMsgId && _keyboard->singleUse() && _keyboard->forceReply() && lastKeyboardUsed) {
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
			if (megagroup->restrictedRights().is_embed_links()) {
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
				_previewRequest = MTP::send(MTPmessages_GetWebPagePreview(MTP_string(_previewLinks)), rpcDone(&HistoryWidget::gotPreview, _previewLinks));
			} else if (i.value()) {
				_previewData = App::webPage(i.value());
				updatePreview();
			} else {
				if (_previewData && _previewData->pendingTill >= 0) previewCancel();
			}
		}
	}
}

void HistoryWidget::onPreviewTimeout() {
	if (_previewData && _previewData->pendingTill > 0 && !_previewLinks.isEmpty()) {
		_previewRequest = MTP::send(MTPmessages_GetWebPagePreview(MTP_string(_previewLinks)), rpcDone(&HistoryWidget::gotPreview, _previewLinks));
	}
}

void HistoryWidget::gotPreview(QString links, const MTPMessageMedia &result, mtpRequestId req) {
	if (req == _previewRequest) {
		_previewRequest = 0;
	}
	if (result.type() == mtpc_messageMediaWebPage) {
		auto data = App::feedWebPage(result.c_messageMediaWebPage().vwebpage);
		_previewCache.insert(links, data->id);
		if (data->pendingTill > 0 && data->pendingTill <= unixtime()) {
			data->pendingTill = -1;
		}
		if (links == _previewLinks && !_previewCancelled) {
			_previewData = (data->id && data->pendingTill >= 0) ? data : 0;
			updatePreview();
		}
		if (App::main()) App::main()->webPagesOrGamesUpdate();
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
			_previewTitle.setText(st::msgNameStyle, lang(lng_preview_loading), _textNameOptions);
#ifndef OS_MAC_OLD
			auto linkText = _previewLinks.splitRef(' ').at(0).toString();
#else // OS_MAC_OLD
			auto linkText = _previewLinks.split(' ').at(0);
#endif // OS_MAC_OLD
			_previewDescription.setText(st::messageTextStyle, TextUtilities::Clean(linkText), _textDlgOptions);

			int32 t = (_previewData->pendingTill - unixtime()) * 1000;
			if (t <= 0) t = 1;
			_previewTimer.start(t);
		} else {
			QString title, desc;
			if (_previewData->siteName.isEmpty()) {
				if (_previewData->title.isEmpty()) {
					if (_previewData->description.text.isEmpty()) {
						title = _previewData->author;
						desc = ((_previewData->document && !_previewData->document->name.isEmpty()) ? _previewData->document->name : _previewData->url);
					} else {
						title = _previewData->description.text;
						desc = _previewData->author.isEmpty() ? ((_previewData->document && !_previewData->document->name.isEmpty()) ? _previewData->document->name : _previewData->url) : _previewData->author;
					}
				} else {
					title = _previewData->title;
					desc = _previewData->description.text.isEmpty() ? (_previewData->author.isEmpty() ? ((_previewData->document && !_previewData->document->name.isEmpty()) ? _previewData->document->name : _previewData->url) : _previewData->author) : _previewData->description.text;
				}
			} else {
				title = _previewData->siteName;
				desc = _previewData->title.isEmpty() ? (_previewData->description.text.isEmpty() ? (_previewData->author.isEmpty() ? ((_previewData->document && !_previewData->document->name.isEmpty()) ? _previewData->document->name : _previewData->url) : _previewData->author) : _previewData->description.text) : _previewData->title;
			}
			if (title.isEmpty()) {
				if (_previewData->document) {
					title = lang(lng_attach_file);
				} else if (_previewData->photo) {
					title = lang(lng_attach_photo);
				}
			}
			_previewTitle.setText(st::msgNameStyle, title, _textNameOptions);
			_previewDescription.setText(st::messageTextStyle, TextUtilities::Clean(desc), _textDlgOptions);
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
	} else  {
		App::main()->showBackFromStack();
		emit cancelled();
	}
}

void HistoryWidget::fullPeerUpdated(PeerData *peer) {
	if (_list && peer == _peer) {
		bool newCanSendMessages = canSendMessages(_peer);
		if (newCanSendMessages != _canSendMessages) {
			_canSendMessages = newCanSendMessages;
			if (!_canSendMessages) {
				cancelReply();
			}
			updateControlsVisibility();
		}
		onCheckFieldAutocomplete();
		updateReportSpamStatus();
		_list->updateBotInfo();
	}
	if (updateCmdStartShown()) {
		updateControlsVisibility();
		updateControlsGeometry();
	} else if (!_scroll->isHidden() && _unblock->isHidden() == isBlocked()) {
		updateControlsVisibility();
		updateControlsGeometry();
	}
}

void HistoryWidget::peerUpdated(PeerData *data) {
	if (data && data == _peer) {
		if (auto channel = data->migrateTo()) {
			Ui::showPeerHistory(channel, ShowAtUnreadMsgId);
			App::api()->requestParticipantsCountDelayed(channel);
			return;
		}
		QString restriction = _peer->restrictionReason();
		if (!restriction.isEmpty()) {
			App::main()->showBackFromStack();
			Ui::show(Box<InformBox>(restriction));
			return;
		}
		bool resize = false;
		if (pinnedMsgVisibilityUpdated()) {
			resize = true;
		}
		updateHistoryGeometry();
		if (_peer->isChannel()) updateReportSpamStatus();
		if (App::api()) {
			if (data->isChat() && data->asChat()->noParticipantInfo()) {
				App::api()->requestFullPeer(data);
			} else if (data->isUser() && (data->asUser()->blockStatus() == UserData::BlockStatus::Unknown || data->asUser()->callsStatus() == UserData::CallsStatus::Unknown)) {
				App::api()->requestFullPeer(data);
			} else if (data->isMegagroup() && !data->asChannel()->mgInfo->botStatus) {
				App::api()->requestBots(data->asChannel());
			}
		}
		if (!_a_show.animating()) {
			if (_unblock->isHidden() == isBlocked() || (!isBlocked() && _joinChannel->isHidden() == isJoinChannel())) {
				resize = true;
			}
			bool newCanSendMessages = canSendMessages(_peer);
			if (newCanSendMessages != _canSendMessages) {
				_canSendMessages = newCanSendMessages;
				if (!_canSendMessages) {
					cancelReply();
				}
				resize = true;
			}
			updateControlsVisibility();
			if (resize) {
				updateControlsGeometry();
			}
		}
		App::main()->updateOnlineDisplay();
	}
}

void HistoryWidget::onForwardSelected() {
	if (!_list) return;
	App::main()->showForwardLayer(getSelectedItems());
}

void HistoryWidget::confirmDeleteContextItem() {
	auto item = App::contextItem();
	if (!item) return;

	if (auto message = item->toHistoryMessage()) {
		if (message->uploading()) {
			App::main()->cancelUploadLayer();
			return;
		}
	}
	App::main()->deleteLayer();
}

void HistoryWidget::confirmDeleteSelectedItems() {
	if (!_list) return;

	auto selected = _list->getSelectedItems();
	if (selected.isEmpty()) return;

	App::main()->deleteLayer(selected.size());
}

void HistoryWidget::deleteContextItem(bool forEveryone) {
	Ui::hideLayer();

	auto item = App::contextItem();
	if (!item) {
		return;
	}

	auto toDelete = QVector<MTPint>(1, MTP_int(item->id));
	auto history = item->history();
	auto wasOnServer = (item->id > 0);
	auto wasLast = (history->lastMsg == item);
	item->destroy();

	if (!wasOnServer && wasLast && !history->lastMsg) {
		App::main()->checkPeerHistory(history->peer);
	}

	if (wasOnServer) {
		App::main()->deleteMessages(history->peer, toDelete, forEveryone);
	}
}

void HistoryWidget::deleteSelectedItems(bool forEveryone) {
	Ui::hideLayer();
	if (!_list) return;

	auto selected = _list->getSelectedItems();
	if (selected.isEmpty()) return;

	QMap<PeerData*, QVector<MTPint>> idsByPeer;
	for_const (auto item, selected) {
		if (item->id > 0) {
			idsByPeer[item->history()->peer].push_back(MTP_int(item->id));
		}
	}

	onClearSelected();
	for_const (auto item, selected) {
		item->destroy();
	}

	for (auto i = idsByPeer.cbegin(), e = idsByPeer.cend(); i != e; ++i) {
		App::main()->deleteMessages(i.key(), i.value(), forEveryone);
	}
}

void HistoryWidget::onListEscapePressed() {
	if (_nonEmptySelection && _list) {
		onClearSelected();
	} else {
		onCancel();
	}
}

void HistoryWidget::onListEnterPressed() {
	if (!_botStart->isHidden()) {
		onBotStart();
	}
}

void HistoryWidget::onClearSelected() {
	if (_list) _list->clearSelectedItems();
}

HistoryItem *HistoryWidget::getItemFromHistoryOrMigrated(MsgId genericMsgId) const {
	if (genericMsgId < 0 && -genericMsgId < ServerMaxMsgId && _migrated) {
		return App::histItemById(_migrated->channelId(), -genericMsgId);
	}
	return App::histItemById(_channel, genericMsgId);
}

void HistoryWidget::onAnimActiveStep() {
	if (!_history || !_activeAnimMsgId || (_activeAnimMsgId < 0 && (!_migrated || -_activeAnimMsgId >= ServerMaxMsgId))) {
		return _animActiveTimer.stop();
	}

	auto item = getItemFromHistoryOrMigrated(_activeAnimMsgId);
	if (!item || item->detached()) {
		return _animActiveTimer.stop();
	}

	if (getms() - _animActiveStart > st::activeFadeInDuration + st::activeFadeOutDuration) {
		stopAnimActive();
	} else {
		Ui::repaintHistoryItem(item);
	}
}

uint64 HistoryWidget::animActiveTimeStart(const HistoryItem *msg) const {
	if (!msg) return 0;
	if ((msg->history() == _history && msg->id == _activeAnimMsgId) || (_migrated && msg->history() == _migrated && msg->id == -_activeAnimMsgId)) {
		return _animActiveTimer.isActive() ? _animActiveStart : 0;
	}
	return 0;
}

void HistoryWidget::stopAnimActive() {
	_animActiveTimer.stop();
	_activeAnimMsgId = 0;
}

SelectedItemSet HistoryWidget::getSelectedItems() const {
	return _list ? _list->getSelectedItems() : SelectedItemSet();
}

void HistoryWidget::updateTopBarSelection() {
	if (!_list) {
		_topBar->showSelected(Window::TopBarWidget::SelectedState {});
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
		_replyEditMsgText.setText(st::messageTextStyle, TextUtilities::Clean(_replyEditMsg->inReplyText()), _textDlgOptions);

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
	updateForwardingItemRemovedSubscription();
	updateControlsVisibility();
	updateControlsGeometry();
}

void HistoryWidget::updateForwardingTexts() {
	int32 version = 0;
	QString from, text;
	if (!_toForward.isEmpty()) {
		QMap<PeerData*, bool> fromUsersMap;
		QVector<PeerData*> fromUsers;
		fromUsers.reserve(_toForward.size());
		for (auto i = _toForward.cbegin(), e = _toForward.cend(); i != e; ++i) {
			auto from = i.value()->peerOriginal();
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

		if (_toForward.size() < 2) {
			text = _toForward.cbegin().value()->inReplyText();
		} else {
			text = lng_forward_messages(lt_count, _toForward.size());
		}
	}
	_toForwardFrom.setText(st::msgNameStyle, from, _textNameOptions);
	_toForwardText.setText(st::messageTextStyle, TextUtilities::Clean(text), _textDlgOptions);
	_toForwardNameVersion = version;
}

void HistoryWidget::checkForwardingInfo() {
	if (!_toForward.isEmpty()) {
		auto version = 0;
		for_const (auto item, _toForward) {
			version += item->peerOriginal()->nameVersion;
		}
		if (version != _toForwardNameVersion) {
			updateForwardingTexts();
		}
	}
}

void HistoryWidget::updateForwardingItemRemovedSubscription() {
	if (_toForward.isEmpty()) {
		unsubscribe(_forwardingItemRemovedSubscription);
		_forwardingItemRemovedSubscription = 0;
	} else if (!_forwardingItemRemovedSubscription) {
		_forwardingItemRemovedSubscription = subscribe(Global::RefItemRemoved(), [this](HistoryItem *item) {
			for (auto i = _toForward.begin(); i != _toForward.end(); ++i) {
				if (i->get() == item) {
					i = _toForward.erase(i);
					updateForwardingItemRemovedSubscription();
					updateForwardingTexts();
					break;
				}
			}
		});
	}
}

void HistoryWidget::updateReplyToName() {
	if (_editMsgId) return;
	if (!_replyEditMsg && (_replyToId || !_kbReplyTo)) return;
	_replyToName.setText(st::msgNameStyle, App::peerName((_replyEditMsg ? _replyEditMsg : _kbReplyTo)->author()), _textNameOptions);
	_replyToNameVersion = (_replyEditMsg ? _replyEditMsg : _kbReplyTo)->author()->nameVersion;
}

void HistoryWidget::updateField() {
	auto fieldAreaTop = _scroll->y() + _scroll->height();
	rtlupdate(0, fieldAreaTop, _chatWidth, height() - fieldAreaTop);
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
	p.fillRect(myrtlrect(0, backy, _chatWidth, backh), st::historyReplyBg);
	if (_editMsgId || _replyToId || (!hasForward && _kbReplyTo)) {
		auto replyLeft = st::historyReplySkip;
		(_editMsgId ? st::historyEditIcon : st::historyReplyIcon).paint(p, st::historyReplyIconPosition + QPoint(0, backy), width());
		if (!drawWebPagePreview) {
			if (drawMsgText) {
				if (drawMsgText->getMedia() && drawMsgText->getMedia()->hasReplyPreview()) {
					auto replyPreview = drawMsgText->getMedia()->replyPreview();
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
					_replyToName.drawElided(p, replyLeft, backy + st::msgReplyPadding.top(), _chatWidth - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
				}
				p.setPen(((drawMsgText->toHistoryMessage() && drawMsgText->toHistoryMessage()->emptyText()) || drawMsgText->serviceMsg()) ? st::historyComposeAreaFgService : st::historyComposeAreaFg);
				_replyEditMsgText.drawElided(p, replyLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, _chatWidth - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
			} else {
				p.setFont(st::msgDateFont);
				p.setPen(st::historyComposeAreaFgService);
				p.drawText(replyLeft, backy + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->elided(lang(lng_profile_loading), _chatWidth - replyLeft - _fieldBarCancel->width() - st::msgReplyPadding.right()));
			}
		}
	} else if (hasForward) {
		auto forwardLeft = st::historyReplySkip;
		st::historyForwardIcon.paint(p, st::historyReplyIconPosition + QPoint(0, backy), width());
		if (!drawWebPagePreview) {
			auto firstItem = _toForward.cbegin().value();
			auto firstMedia = firstItem->getMedia();
			auto serviceColor = (_toForward.size() > 1) || (firstMedia != nullptr) || firstItem->serviceMsg();
			auto preview = (_toForward.size() < 2 && firstMedia && firstMedia->hasReplyPreview()) ? firstMedia->replyPreview() : ImagePtr();
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
			_toForwardText.drawElided(p, forwardLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, _chatWidth - forwardLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
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
		_previewTitle.drawElided(p, previewLeft, backy + st::msgReplyPadding.top(), _chatWidth - previewLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
		p.setPen(st::historyComposeAreaFg);
		_previewDescription.drawElided(p, previewLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, _chatWidth - previewLeft - _fieldBarCancel->width() - st::msgReplyPadding.right());
	}
}

void HistoryWidget::drawRestrictedWrite(Painter &p) {
	auto rect = myrtlrect(0, height() - _unblock->height(), _chatWidth, _unblock->height());
	p.fillRect(rect, st::historyReplyBg);

	p.setFont(st::normalFont);
	p.setPen(st::windowSubTextFg);
	p.drawText(rect.marginsRemoved(QMargins(st::historySendPadding, 0, st::historySendPadding, 0)), lang(lng_restricted_send_message), style::al_center);
}

void HistoryWidget::paintEditHeader(Painter &p, const QRect &rect, int left, int top) const {
	if (!rect.intersects(myrtlrect(left, top, _chatWidth - left, st::normalFont->height))) {
		return;
	}

	p.setFont(st::msgServiceNameFont);
	p.drawTextLeft(left, top + st::msgReplyPadding.top(), width(), lang(lng_edit_message));

	if (!_replyEditMsg || _replyEditMsg->history()->peer->isSelf()) return;

	QString editTimeLeftText;
	int updateIn = -1;
	auto tmp = ::date(unixtime());
	auto timeSinceMessage = _replyEditMsg->date.msecsTo(QDateTime::currentDateTime());
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
	if (rect.contains(myrtlrect(left, top, _chatWidth - left, st::normalFont->height)) && updateIn > 0) {
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
	int32 right = _chatWidth - _send->width();

	p.setPen(anim::pen(st::historyRecordCancel, st::historyRecordCancelActive, 1. - recordActive));
	p.drawText(left + (right - left - _recordCancelWidth) / 2, _attachToggle->y() + st::historyRecordTextTop + st::historyRecordFont->ascent, lang(lng_record_cancel));
}

void HistoryWidget::drawPinnedBar(Painter &p) {
	Expects(_pinnedBar != nullptr);

	auto top = _topBar->bottomNoMargins();
	Text *from = 0, *text = 0;
	bool serviceColor = false, hasForward = readyToForward();
	ImagePtr preview;
	p.fillRect(myrtlrect(0, top, _chatWidth, st::historyReplyHeight), st::historyPinnedBg);

	top += st::msgReplyPadding.top();
	QRect rbar(myrtlrect(st::msgReplyBarSkip + st::msgReplyBarPos.x(), top + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height()));
	p.fillRect(rbar, st::msgInReplyBarColor);

	int32 left = st::msgReplyBarSkip + st::msgReplyBarSkip;
	if (_pinnedBar->msg) {
		if (_pinnedBar->msg->getMedia() && _pinnedBar->msg->getMedia()->hasReplyPreview()) {
			ImagePtr replyPreview = _pinnedBar->msg->getMedia()->replyPreview();
			if (!replyPreview->isNull()) {
				QRect to(left, top, st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				p.drawPixmap(to.x(), to.y(), replyPreview->pixSingle(replyPreview->width() / cIntRetinaFactor(), replyPreview->height() / cIntRetinaFactor(), to.width(), to.height(), ImageRoundRadius::Small));
			}
			left += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
		}
		p.setPen(st::historyReplyNameFg);
		p.setFont(st::msgServiceNameFont);
		p.drawText(left, top + st::msgServiceNameFont->ascent, lang(lng_pinned_message));

		p.setPen(((_pinnedBar->msg->toHistoryMessage() && _pinnedBar->msg->toHistoryMessage()->emptyText()) || _pinnedBar->msg->serviceMsg()) ? st::historyComposeAreaFgService : st::historyComposeAreaFg);
		_pinnedBar->text.drawElided(p, left, top + st::msgServiceNameFont->height, _chatWidth - left - _pinnedBar->cancel->width() - st::msgReplyPadding.right());
	} else {
		p.setFont(st::msgDateFont);
		p.setPen(st::historyComposeAreaFgService);
		p.drawText(left, top + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->elided(lang(lng_profile_loading), _chatWidth - left - _pinnedBar->cancel->width() - st::msgReplyPadding.right()));
	}
}

void HistoryWidget::paintEvent(QPaintEvent *e) {
	if (!App::main() || (App::wnd() && App::wnd()->contentOverlapped(this, e))) {
		return;
	}
	if (hasPendingResizedItems()) {
		updateListSize();
	}

	Painter p(this);
	QRect r(e->rect());
	if (r != rect()) {
		p.setClipRect(r);
	}

	auto ms = getms();
	_historyDownShown.step(ms);
	auto progress = _a_show.current(ms, 1.);
	if (_a_show.animating()) {
		auto animationWidth = (!_tabbedSection || _tabbedSection->isHidden()) ? width() : _chatWidth;
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
		return;
	}

	QRect fill(0, 0, _history ? _chatWidth : width(), App::main()->height());
	auto fromy = App::main()->backgroundFromY();
	auto x = 0, y = 0;
	QPixmap cached = App::main()->cachedBackground(fill, x, y);
	if (cached.isNull()) {
		if (Window::Theme::Background()->tile()) {
			auto &pix = Window::Theme::Background()->pixmapForTiled();
			auto left = r.left();
			auto top = r.top();
			auto right = r.left() + r.width();
			auto bottom = r.top() + r.height();
			auto w = pix.width() / cRetinaFactor();
			auto h = pix.height() / cRetinaFactor();
			auto sx = qFloor(left / w);
			auto sy = qFloor((top - fromy) / h);
			auto cx = qCeil(right / w);
			auto cy = qCeil((bottom - fromy) / h);
			for (auto i = sx; i < cx; ++i) {
				for (auto j = sy; j < cy; ++j) {
					p.drawPixmap(QPointF(i * w, fromy + j * h), pix);
				}
			}
		} else {
			PainterHighQualityEnabler hq(p);

			auto &pix = Window::Theme::Background()->pixmap();
			QRect to, from;
			Window::Theme::ComputeBackgroundRects(fill, pix.size(), to, from);
			to.moveTop(to.top() + fromy);
			p.drawPixmap(to, pix, from);
		}
	} else {
		p.drawPixmap(x, fromy + y, cached);
	}

	if (_list) {
		if (!_field->isHidden() || _recording) {
			drawField(p, r);
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
			HistoryLayout::paintEmpty(p, width(), height() - _field->height() - 2 * st::historySendPadding);
		}
	} else {
		style::font font(st::msgServiceFont);
		int32 w = font->width(lang(lng_willbe_history)) + st::msgPadding.left() + st::msgPadding.right(), h = font->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + 2;
		QRect tr((width() - w) / 2, (height() - _field->height() - 2 * st::historySendPadding - h) / 2, w, h);
		HistoryLayout::ServiceMessagePainter::paintBubble(p, tr.x(), tr.y(), tr.width(), tr.height());

		p.setPen(st::msgServiceFg);
		p.setFont(font->f);
		p.drawText(tr.left() + st::msgPadding.left(), tr.top() + st::msgServicePadding.top() + 1 + font->ascent, lang(lng_willbe_history));
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
	_scroll->scrollToY(y);
	_synteticScrollEvent = false;
}

HistoryWidget::~HistoryWidget() = default;
