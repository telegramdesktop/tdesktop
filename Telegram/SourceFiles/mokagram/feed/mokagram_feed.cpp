#include "mokagram/feed/mokagram_feed.h"

#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "base/algorithm.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "data/data_changes.h"
#include "data/data_message_reactions.h"
#include "core/ui_integration.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_folder.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_document_resolver.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/reactions/history_view_reactions_selector.h"
#include "payments/payments_reaction_process.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "mokagram/lang/mokagram_lang.h"
#include "mokagram/settings/mokagram_settings.h"
#include "mtproto/sender.h"
#include "ui/effects/radial_animation.h"
#include "ui/layers/generic_box.h"
#include "ui/image/image.h"
#include "ui/text/text.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/painter.h"
#include "ui/userpic_view.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include "window/window_session_controller.h"

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QUuid>
#include <QtGui/QKeyEvent>
#include <QtGui/QMouseEvent>

#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_mokagram_feed.h"

namespace Mokagram::Feed {
namespace {

constexpr auto kMaxCategories = 20;
constexpr auto kMaxSources = 32;
constexpr auto kPageSize = 20;
constexpr auto kMaxPosts = 2000;

QString Text(const char *value) {
	return QString::fromUtf8(Lang::Tr(value));
}

void PaintFocus(Painter &p, not_null<QWidget*> parent, QWidget *control) {
	if (!control || !control->hasFocus() || control->isHidden()) {
		return;
	}
	const auto inset = st::feedFocusInset;
	const auto area = QRect(control->mapTo(parent, QPoint()), control->size())
		.adjusted(-inset, -inset, inset, inset);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(QPen(st::windowActiveTextFg->c, st::feedFocusWidth));
	p.setBrush(Qt::NoBrush);
	p.drawRoundedRect(area, st::feedFocusRadius, st::feedFocusRadius);
}

struct Category {
	QString id;
	QString name;
	std::vector<PeerId> sources;
};

struct Post {
	PeerId peer;
	int id = 0;
	int date = 0;
	QString text;
};

struct Source {
	PeerId peer;
	int offset = 0;
	bool exhausted = false;
};

struct FeedSnapshot {
	std::vector<Post> posts;
	std::vector<Source> sources;
	std::vector<PeerId> pendingSources;
	int failures = 0;
	int unavailableSources = 0;
};

class FeedMemento final : public Window::SectionMemento {
public:
	FeedMemento(QString categoryId = {}, int scrollTop = 0,
		std::shared_ptr<const FeedSnapshot> snapshot = nullptr);
	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	QString categoryId;
	int scrollTop = 0;
	std::shared_ptr<const FeedSnapshot> snapshot;

};

struct ReactionControl {
	Data::ReactionId id;
	Ui::RoundButton *button = nullptr;
};

class PostRow final : public Ui::RpWidget {
public:
	PostRow(QWidget *parent, not_null<Window::SessionController*> controller,
		const Post &post, Fn<void()> open, Fn<void()> comments,
		Fn<void()> openMedia);
	~PostRow() override;
	QString accessibilityName() override;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *event) override;
	bool eventFilter(QObject *object, QEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void leaveEventHook(QEvent *event) override;

private:
	[[nodiscard]] bool isVoiceState(const Media::Player::TrackState &state) const;
	[[nodiscard]] float64 voiceProgress() const;
	void updateVoiceButtonIcon();
	void updateVoiceSeeking(QPoint position);
	void syncReactions();
	void scheduleReactionsUpdate();
	void toggleReaction(const Data::ReactionId &id);
	void showReactionMenu();

	not_null<Window::SessionController*> _controller;
	not_null<Main::Session*> _session;
	FullMsgId _itemId;
	std::vector<ReactionControl> _reactionControls;
	Ui::RoundButton *_reactionMore = nullptr;
	Ui::RoundButton *_reactionExpand = nullptr;
	bool _reactionsExpanded = false;
	Ui::FlatLabel *_views = nullptr;
	base::unique_qptr<Ui::PopupMenu> _reactionMenu;
	PeerData *_peer = nullptr;
	Ui::PeerUserpicView _userpic;
	Ui::Text::String _title;
	style::FlatLabel _bodyStyle;
	Ui::FlatLabel *_body = nullptr;
	Ui::FlatLabel *_sender = nullptr;
	QString _date;
	QString _mediaLabel;
	std::shared_ptr<Data::PhotoMedia> _photo;
	std::shared_ptr<Data::DocumentMedia> _document;
	DocumentData *_voiceDocument = nullptr;
	Ui::LinkButton *_expand = nullptr;
	Ui::LinkButton *_open = nullptr;
	Ui::LinkButton *_comments = nullptr;
	Ui::AbstractButton *_mediaButton = nullptr;
	Ui::IconButton *_voiceButton = nullptr;
	QSize _mediaSize;
	QRect _voiceSeekRect;
	int _padding = 0;
	int _bodyHeight = 0;
	int _mediaTop = 0;
	int _mediaHeight = 0;
	int _mediaWidth = 0;
	bool _expanded = false;
	bool _reactionUpdatePending = false;
	bool _voiceSeeking = false;
	float64 _voiceSeekProgress = 0.;
	float64 _voiceHoverProgress = -1.;

};

class SourcesController final : public ChatsListBoxController {
public:
	SourcesController(not_null<Main::Session*> session, Category category);
	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void loadMoreRows() override;
	rpl::producer<QString> searchPlaceholder() const override;
	QString savedMessagesChatStatus() const override;
	QString categoryName() const;
	void setNameField(not_null<Ui::InputField*> field);
	void showNameError();

private:
	void prepareViewHook() override;
	std::unique_ptr<Row> createRow(not_null<History*> history) override;

	not_null<Main::Session*> _session;
	Category _category;
	QPointer<Ui::InputField> _name;

};

class FeedSection final : public Window::SectionWidget {
public:
	FeedSection(QWidget *parent,
		not_null<Window::SessionController*> controller,
		const FeedMemento &memento);
	bool showInternal(not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	std::shared_ptr<Window::SectionMemento> createIdentityMemento() override;
	QPixmap grabForShowAnimation(const Window::SectionSlideParams &params) override;
	bool floatPlayerHandleWheelEvent(QEvent *event) override;
	QRect floatPlayerAvailableRect() override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void showFinishedHook() override;
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	void syncVisibility();
	void updateLayout();
	void updateCategory();
	void showCategories();
	void showMenu();
	void showEmpty(const QString &title, const QString &hint, bool canEdit);
	void renderPosts();
	void loadCategories();
	void saveCategories();
	void populateCategories(int selected = 0);
	void editCategory(bool create);
	void refresh();
	void resolvePendingSources();
	void loadMore();
	void loadNext();

	not_null<Main::Session*> _session;
	Ui::InfiniteRadialAnimation _loading;
	QString _key;
	std::unique_ptr<MTP::Sender> _api;
	Ui::IconButton *_back = nullptr;
	Ui::IconButton *_menuButton = nullptr;
	Ui::RoundButton *_category = nullptr;
	Ui::ScrollArea *_scroll = nullptr;
	QPointer<Ui::VerticalLayout> _content;
	Ui::RpWidget *_empty = nullptr;
	Ui::FlatLabel *_emptyTitle = nullptr;
	Ui::FlatLabel *_emptyHint = nullptr;
	Ui::RoundButton *_emptyAction = nullptr;
	QPointer<Ui::RoundButton> _older;
	QPointer<PeerListBox> _editor;
	base::unique_qptr<Ui::PopupMenu> _menu;
	std::vector<Category> _groups;
	std::vector<Source> _sources;
	std::vector<PeerId> _pendingSources;
	std::vector<Post> _posts;
	int _selected = -1;
	int _restoreTop = 0;
	size_t _source = 0;
	int _failures = 0;
	int _unavailableSources = 0;
	bool _busy = false;
	bool _showingEmpty = true;

};

FeedMemento::FeedMemento(
		QString categoryId,
		int scrollTop,
		std::shared_ptr<const FeedSnapshot> snapshot)
: categoryId(std::move(categoryId))
, scrollTop(scrollTop)
, snapshot(std::move(snapshot)) {
}

object_ptr<Window::SectionWidget> FeedMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	if (column == Window::Column::Third) {
		return nullptr;
	}
	auto result = object_ptr<FeedSection>(parent, controller, *this);
	result->setGeometry(geometry);
	return result;
}

PostRow::PostRow(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const Post &post,
		Fn<void()> open,
		Fn<void()> comments,
		Fn<void()> openMedia)
: Ui::RpWidget(parent)
, _controller(controller)
, _session(&controller->session())
, _itemId(post.peer, MsgId(post.id))
, _peer(_session->data().peerLoaded(post.peer))
, _title(st::semiboldTextStyle, _peer
	? (_peer->isSelf() ? tr::lng_saved_messages(tr::now) : _peer->name())
	: QString())
, _bodyStyle(st::feedPostLabel)
, _date(QDateTime::fromSecsSinceEpoch(post.date).toLocalTime()
	.toString(u"dd.MM · HH:mm"_q)) {
	setMouseTracking(true);
	setToolTip(_title.toString() + u" · "_q + _date);
	_body = Ui::CreateChild<Ui::FlatLabel>(this, post.text, _bodyStyle);
	_body->setSelectable(true);
	_open = Ui::CreateChild<Ui::LinkButton>(this, Text("Показать в чате"));
	_open->setClickedCallback(std::move(open));
	_open->setFocusPolicy(Qt::StrongFocus);
	_open->installEventFilter(this);
	_expand = Ui::CreateChild<Ui::LinkButton>(this, Text("Показать ещё"));
	_expand->setFocusPolicy(Qt::StrongFocus);
	_expand->installEventFilter(this);
	_expand->setClickedCallback([=] {
		_expanded = !_expanded;
		_expand->setText(_expanded ? Text("Свернуть") : Text("Показать ещё"));
		resizeToWidth(width());
		update();
	});
	_reactionMore = Ui::CreateChild<Ui::RoundButton>(this,
		rpl::single(u"+"_q), st::feedReactionButton);
	_reactionMore->setAccessibleName(tr::lng_notification_reactions(tr::now));
	_reactionMore->setClickedCallback([=] { showReactionMenu(); });
	_reactionMore->setFocusPolicy(Qt::StrongFocus);
	_reactionMore->installEventFilter(this);
	_reactionExpand = Ui::CreateChild<Ui::RoundButton>(this,
		rpl::single(QString()), st::feedReactionButton);
	_reactionExpand->setClickedCallback([=] {
		_reactionsExpanded = !_reactionsExpanded;
		syncReactions();
	});
	_reactionExpand->setFocusPolicy(Qt::StrongFocus);
	_reactionExpand->installEventFilter(this);
	_views = Ui::CreateChild<Ui::FlatLabel>(this, QString(), st::feedViews);
	syncReactions();
	_session->data().itemDataChanges() | rpl::on_next([=](const auto item) {
		if (item->fullId() == _itemId) {
			scheduleReactionsUpdate();
		}
	}, lifetime());
	_session->data().itemRepaintRequest() | rpl::on_next([=](const auto item) {
		if (item->fullId() == _itemId) {
			update();
		}
	}, lifetime());
	if (_peer) {
		_session->changes().peerUpdates(_peer,
			Data::PeerUpdate::Flag::Reactions
		) | rpl::on_next([=] { scheduleReactionsUpdate(); }, lifetime());
	}
	_session->data().reactions().defaultUpdates(
	) | rpl::on_next([=] { scheduleReactionsUpdate(); }, lifetime());
	const auto item = _session->data().message(post.peer, MsgId(post.id));
	if (!item) {
		return;
	}
	if (item->author() != _peer && !item->author()->name().isEmpty()) {
		_sender = Ui::CreateChild<Ui::FlatLabel>(
			this, item->author()->name(), st::feedViews);
		_sender->setSelectable(true);
	}
	_body->setMarkedText(item->originalText(), Core::TextContext({
		.session = _session,
		.repaint = [=] { update(); },
		.customEmojiLoopLimit = -1,
	}));
	if (item->repliesAreComments()) {
		const auto count = item->repliesCount();
		_comments = Ui::CreateChild<Ui::LinkButton>(this, count
			? tr::lng_comments_open_count(tr::now, lt_count, count)
			: tr::lng_comments_open_none(tr::now));
		_comments->setClickedCallback(std::move(comments));
		_comments->setFocusPolicy(Qt::StrongFocus);
		_comments->installEventFilter(this);
	}
	const auto media = item->media();
	if (!media) {
		return;
	}
	if (media->webpage()) {
		return;
	}
	_mediaLabel = media->photo() ? Text("Фото") : media->notificationText().text;
	if (media->hasSpoilerForPreview() || media->ttlSeconds()) {
		return;
	}
	if (const auto photo = media->photo()) {
		_photo = photo->createMediaView();
		_mediaSize = photo->size(Data::PhotoSize::Large).value_or(QSize());
		_photo->wanted(Data::PhotoSize::Thumbnail, item->fullId());
	} else if (const auto document = media->document()) {
		if (document->isVoiceMessage()) {
			_voiceDocument = document;
			_mediaLabel = media->notificationText().text;
			if (_mediaLabel.isEmpty()) {
				_mediaLabel = Text("Голосовые");
			}
			_voiceButton = Ui::CreateChild<Ui::IconButton>(
				this,
				st::feedVoiceButton);
			_voiceButton->setIconOverride(
				&st::feedVoicePlayIcon,
				&st::feedVoicePlayIcon);
			_voiceButton->setClickedCallback([=] {
				if (const auto voiceItem = _session->data().message(_itemId)) {
					Data::ResolveDocument(
						_controller.get(),
						_voiceDocument,
						voiceItem,
						voiceItem->topicRootId(),
						PeerId(),
						false);
				}
			});
			_voiceButton->setAccessibleName(_mediaLabel);
			_voiceButton->setFocusPolicy(Qt::StrongFocus);
			_voiceButton->installEventFilter(this);
			updateVoiceButtonIcon();
			Media::Player::instance()->updatedNotifier(
			) | rpl::filter([](const Media::Player::TrackState &state) {
				return state.id.type() == AudioMsgId::Type::Voice;
			}) | rpl::on_next([=] {
				updateVoiceButtonIcon();
				update();
			}, lifetime());
		} else {
			_mediaLabel = document->isVideoFile()
				? Text("Видео") : document->filename();
			if (_mediaLabel.isEmpty()) {
				_mediaLabel = media->notificationText().text;
			}
			_mediaSize = document->dimensions;
			_document = document->createMediaView();
			_document->thumbnailWanted(item->fullId());
		}
	}
	if (_photo || _document) {
		_mediaButton = Ui::CreateChild<Ui::AbstractButton>(this);
		_mediaButton->setClickedCallback(std::move(openMedia));
		_mediaButton->setAccessibleName(_mediaLabel);
		_mediaButton->setPointerCursor(true);
		_mediaButton->setFocusPolicy(Qt::StrongFocus);
		_mediaButton->installEventFilter(this);
	}
}

PostRow::~PostRow() {
	if (_voiceSeeking) {
		Media::Player::instance()->cancelSeeking(AudioMsgId::Type::Voice);
	}
}

QString PostRow::accessibilityName() {
	return _title.toString() + u" · "_q + _date
		+ '\n' + (_sender ? _sender->accessibilityName() + '\n' : QString())
		+ _body->accessibilityName() + '\n' + _mediaLabel;
}

bool PostRow::isVoiceState(const Media::Player::TrackState &state) const {
	return _voiceDocument
		&& (state.id.audio() == _voiceDocument)
		&& (state.id.contextId() == _itemId);
}

float64 PostRow::voiceProgress() const {
	const auto state = Media::Player::instance()->getState(
		AudioMsgId::Type::Voice);
	if (!isVoiceState(state)
		|| Media::Player::IsStoppedOrStopping(state.state)
		|| !state.length) {
		return 0.;
	}
	return std::clamp(
		float64(state.position) / state.length,
		0.,
		1.);
}

void PostRow::updateVoiceButtonIcon() {
	if (!_voiceButton) {
		return;
	}
	const auto state = Media::Player::instance()->getState(
		AudioMsgId::Type::Voice);
	const auto icon = isVoiceState(state)
		&& Media::Player::ShowPauseIcon(state.state)
		? &st::feedVoicePauseIcon
		: &st::feedVoicePlayIcon;
	_voiceButton->setIconOverride(icon, icon);
}

void PostRow::updateVoiceSeeking(QPoint position) {
	if (!_voiceSeeking || _voiceSeekRect.width() <= 0) {
		return;
	}
	_voiceSeekProgress = std::clamp(
		(rtl()
			? _voiceSeekRect.x() + _voiceSeekRect.width() - position.x()
			: position.x() - _voiceSeekRect.x())
			/ float64(_voiceSeekRect.width()),
		0.,
		1.);
	Media::Player::instance()->updateSeeking(
		AudioMsgId::Type::Voice,
		_voiceSeekProgress);
	update();
}

int PostRow::resizeGetHeight(int newWidth) {
	_padding = (newWidth < st::feedCompactWidth)
		? st::feedPostCompactPadding
		: st::feedPostPadding;
	const auto textWidth = std::max(1, newWidth - 2 * _padding);
	_bodyStyle.maxHeight = 0;
	_body->resizeToWidth(textWidth);
	const auto fullHeight = _body->height();
	const auto limit = st::feedPostText.lineHeight * st::feedPostPreviewLines;
	_bodyStyle.maxHeight = _expanded ? 0 : limit;
	_body->resizeToWidth(textWidth);
	_bodyHeight = _body->height();
	_expand->setVisible(fullHeight > limit);
	auto top = _padding + st::feedAvatarSize;
	if (_sender) {
		top += st::feedPostGap;
		_sender->resizeToWidth(textWidth);
		_sender->moveToLeft(_padding, top, newWidth);
		top += _sender->height();
	}
	if (_bodyHeight) {
		top += st::feedPostGap;
		_body->moveToLeft(_padding, top, newWidth);
		top += _bodyHeight;
	}
	if (!_expand->isHidden()) {
		top += st::feedMetaGap;
		_expand->resizeToNaturalWidth(textWidth);
		_expand->moveToLeft(_padding, top, newWidth);
		top += _expand->height();
	}
	_mediaWidth = textWidth;
	_mediaHeight = _voiceDocument
		? st::feedVoiceHeight
		: _mediaLabel.isEmpty() ? 0 : st::feedMediaLabelHeight;
	if (!_voiceDocument && (_photo || _document) && !_mediaSize.isEmpty()) {
		const auto fitted = _mediaSize.scaled(
			QSize(textWidth, st::feedMediaMaxHeight), Qt::KeepAspectRatio);
		_mediaWidth = fitted.width();
		_mediaHeight = fitted.height();
	}
	_mediaTop = top + st::feedPostGap;
	if (_mediaHeight) {
		top = _mediaTop + _mediaHeight;
	}
	if (_mediaButton) {
		_mediaButton->resize(_mediaWidth, _mediaHeight);
		_mediaButton->moveToLeft(_padding, _mediaTop, newWidth);
	}
	if (_voiceButton) {
		_voiceButton->resize(st::feedVoiceButtonSize, st::feedVoiceButtonSize);
		_voiceButton->moveToLeft(_padding, _mediaTop
			+ (_mediaHeight - st::feedVoiceButtonSize) / 2, newWidth);
		const auto seekLeft = _padding
			+ st::feedVoiceButtonSize
			+ st::feedVoiceButtonGap;
		const auto seekTop = _mediaTop
			+ (_mediaHeight - st::feedVoiceWaveHeight) / 2;
		_voiceSeekRect = style::rtlrect(
			seekLeft,
			seekTop - st::feedVoiceWaveLabelGap,
			std::max(0, textWidth - st::feedVoiceButtonSize
				- st::feedVoiceButtonGap),
			st::feedVoiceWaveHeight + st::feedVoiceWaveLabelGap
				+ st::feedMetaFont->height,
			newWidth);
	}
	if (!_reactionControls.empty() || !_reactionMore->isHidden()) {
		top += st::feedPostGap;
		auto x = _padding;
		const auto place = [&](not_null<Ui::RoundButton*> button) {
			button->setFullWidth(0);
			if (button->width() > textWidth) {
				button->setFullWidth(textWidth);
			}
			if (x > _padding && x + button->width()
					> newWidth - _padding) {
				x = _padding;
				top += button->height() + st::feedReactionGap;
			}
			button->moveToLeft(x, top, newWidth);
			x += button->width() + st::feedReactionGap;
		};
		for (const auto &control : _reactionControls) {
			place(control.button);
		}
		if (!_reactionExpand->isHidden()) {
			place(_reactionExpand);
		}
		if (!_reactionMore->isHidden()) {
			place(_reactionMore);
		}
		top += st::feedReactionButton.height;
	}
	top += st::feedPostGap;
	const auto viewsWidth = _views->isHidden() ? 0 : [&] {
		_views->resizeToNaturalWidth(textWidth);
		_views->moveToRight(_padding, top, newWidth);
		return _views->width() + st::feedMetaGap + st::feedViewsIcon.width();
	}();
	const auto actionWidth = std::max(0, textWidth
		- (viewsWidth ? viewsWidth + st::feedPostGap : 0));
	_open->resizeToNaturalWidth(actionWidth);
	_open->moveToLeft(_padding, top, newWidth);
	const auto firstRowHeight = std::max(_open->height(),
		_views->isHidden() ? 0 : _views->height());
	if (_comments) {
		_comments->resizeToNaturalWidth(textWidth);
		const auto available = std::max(0,
			actionWidth - _open->width() - st::feedPostGap);
		if (_comments->width() > available) {
			top += firstRowHeight + st::feedPostGap;
			_comments->moveToLeft(_padding, top, newWidth);
			top += _comments->height();
		} else {
			_comments->moveToLeft(_padding + _open->width()
				+ st::feedPostGap, top, newWidth);
			top += std::max(firstRowHeight, _comments->height());
		}
	} else {
		top += firstRowHeight;
	}
	return top + _padding;
}

void PostRow::paintEvent(QPaintEvent *event) {
	auto p = Painter(this);
	p.fillRect(event->rect(), st::mainMenuBg);
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(rect(), st::feedPostRadius, st::feedPostRadius);
	}
	if (const auto item = _session->data().message(_itemId)) {
		_session->data().reactions().poll(item, crl::now());
	}
	if (!_views->isHidden()) {
		const auto &icon = st::feedViewsIcon;
		icon.paint(p, width() - _padding - _views->width()
			- st::feedMetaGap - icon.width(),
			_views->y() + (_views->height() - icon.height()) / 2, width());
	}
	const auto left = _padding;
	const auto top = _padding;
	if (_peer) {
		_peer->paintUserpicLeft(p, _userpic, left, top,
			width(), st::feedAvatarSize);
	}
	const auto textLeft = left + st::feedAvatarSize + st::feedPostGap;
	const auto titleY = top
		+ (st::feedAvatarSize - st::semiboldFont->height) / 2;
	const auto dateY = top
		+ (st::feedAvatarSize - st::feedMetaFont->height) / 2;
	const auto titleWidth = std::max(0, width() - left - textLeft
		- st::feedMetaGap - st::feedMetaFont->width(_date));
	p.setPen(st::windowFg);
	_title.draw(p, {
		.position = { textLeft, titleY },
		.outerWidth = width(),
		.availableWidth = titleWidth,
		.elisionLines = 1,
	});
	p.setFont(st::feedMetaFont);
	p.setPen(st::windowSubTextFg);
	p.drawTextRight(left, dateY, width(), st::feedMetaFont->elided(
		_date, std::max(0, width() - 2 * left)));
	p.setPen(st::windowFg);
	if (_mediaHeight) {
		const auto area = myrtlrect(left, _mediaTop, _mediaWidth, _mediaHeight);
		p.save();
		auto clip = QPainterPath();
		clip.addRoundedRect(area, st::feedMediaRadius, st::feedMediaRadius);
		p.setClipPath(clip, Qt::IntersectClip);
		p.fillRect(area, st::windowBgOver);
		if (_voiceDocument) {
			if (_voiceButton) {
				const auto button = QRect(
					_voiceButton->x(),
					_voiceButton->y(),
					_voiceButton->width(),
					_voiceButton->height());
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::windowBgActive->c);
				p.drawEllipse(button);
			}
			const auto voiceData = _voiceDocument->voice();
			const auto waveform = voiceData ? &voiceData->waveform : nullptr;
			const auto validWaveform = waveform
				&& !waveform->isEmpty()
				&& (waveform->at(0) >= 0);
			const auto waveTop = _mediaTop
				+ (_mediaHeight - st::feedVoiceWaveHeight) / 2;
			const auto waveWidth = _voiceSeekRect.width();
			const auto barWidth = st::feedVoiceBarWidth;
			const auto barSkip = st::feedVoiceBarSkip;
			const auto barStep = barWidth + barSkip;
			const auto sampleCount = validWaveform
				? int(waveform->size())
				: std::max(1, waveWidth / std::max(1, barStep));
			const auto barCount = std::min(
				std::max(0, waveWidth / std::max(1, barStep)),
				sampleCount);
			const auto state = Media::Player::instance()->getState(
				AudioMsgId::Type::Voice);
			const auto active = isVoiceState(state)
				&& !Media::Player::IsStoppedOrStopping(state.state);
			auto progress = voiceProgress();
			auto duration = std::max<int64>(
				0,
				_voiceDocument->duration() / 1000);
			auto played = int64();
			if (isVoiceState(state) && state.length > 0 && state.frequency > 0) {
				duration = state.length / state.frequency;
				if (active) {
					played = state.position / state.frequency;
				}
			}
			if (_voiceSeeking) {
				progress = _voiceSeekProgress;
				played = base::SafeRound(progress * duration);
			} else if (_voiceHoverProgress >= 0. && active) {
				played = base::SafeRound(_voiceHoverProgress * duration);
			}
			const auto activeWidth = base::SafeRound(waveWidth * progress);
			const auto maxValue = validWaveform
				? std::max(1, int(voiceData->wavemax) + 1)
				: 1;
			const auto maxDelta = st::feedVoiceBarMax - st::feedVoiceBarMin;
			p.setPen(Qt::NoPen);
			auto hq = PainterHighQualityEnabler(p);
			for (auto i = 0; i != barCount; ++i) {
				const auto sample = validWaveform
					? std::clamp(
						int(waveform->at(i * waveform->size() / barCount)),
						0,
						maxValue - 1)
					: 0;
				const auto height = st::feedVoiceBarMin
					+ (sample * maxDelta + maxValue / 2) / maxValue;
				const auto x = rtl()
					? _voiceSeekRect.x() + waveWidth - barWidth - i * barStep
					: _voiceSeekRect.x() + i * barStep;
				const auto y = waveTop + (st::feedVoiceWaveHeight - height) / 2;
				p.setBrush(i * barStep < activeWidth
					? st::windowActiveTextFg->c
					: st::windowSubTextFg->c);
				p.drawRoundedRect(
					QRectF(x, y, barWidth, height),
					barWidth / 2.,
					barWidth / 2.);
			}
			p.setFont(st::feedMetaFont);
			p.setPen(st::windowSubTextFg);
			p.drawText(
				QRect(_voiceSeekRect.x(), waveTop + st::feedVoiceWaveHeight
					+ st::feedVoiceWaveLabelGap, waveWidth, st::feedMetaFont->height),
				Qt::AlignLeading | Qt::AlignVCenter,
				(active || _voiceSeeking || _voiceHoverProgress >= 0.)
					? Ui::FormatPlayedText(played, duration)
					: Ui::FormatDurationText(duration));
		} else {
			auto image = _photo
				? _photo->image(Data::PhotoSize::Large)
				: (_document ? _document->thumbnail() : nullptr);
			if (!image && _photo) {
				image = _photo->image(Data::PhotoSize::Thumbnail);
			}
			if (image && !image->isNull()) {
				const auto fitted = image->size().scaled(area.size(), Qt::KeepAspectRatio);
				p.drawPixmap(area.x() + (area.width() - fitted.width()) / 2,
					area.y() + (area.height() - fitted.height()) / 2,
					image->pix(fitted));
				if (_document && _document->owner()->isVideoFile()) {
					const auto size = st::feedMediaPlaySize;
					const auto center = area.center();
					p.setPen(Qt::NoPen);
					p.setBrush(st::mainMenuBg);
					p.drawEllipse(center, size / 2, size / 2);
					st::feedMediaPlayIcon.paintInCenter(p, area);
				}
			} else {
				p.setFont(st::feedMetaFont);
				p.setPen(st::windowSubTextFg);
				p.drawText(area.adjusted(st::feedPostGap, 0, -st::feedPostGap, 0),
					Qt::AlignCenter, st::feedMetaFont->elided(_mediaLabel,
						std::max(1, area.width() - 2 * st::feedPostGap)));
			}
		}
		p.restore();
	}

	for (const auto child : std::array<QWidget*, 7>{
			_open, _expand, _comments, _mediaButton, _voiceButton,
			_reactionMore, _reactionExpand }) {
		PaintFocus(p, this, child);
	}
	for (const auto &control : _reactionControls) {
		PaintFocus(p, this, control.button);
	}
}

bool PostRow::eventFilter(QObject *object, QEvent *event) {
	if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
		update();
	}
	return Ui::RpWidget::eventFilter(object, event);
}

void PostRow::mousePressEvent(QMouseEvent *event) {
	if (_voiceDocument
		&& event->button() == Qt::LeftButton
		&& _voiceSeekRect.contains(event->pos())) {
		const auto state = Media::Player::instance()->getState(
			AudioMsgId::Type::Voice);
		if (isVoiceState(state)
			&& !Media::Player::IsStoppedOrStopping(state.state)
			&& state.length > 0) {
			_voiceSeeking = true;
			_voiceHoverProgress = -1.;
			Media::Player::instance()->startSeeking(AudioMsgId::Type::Voice);
			updateVoiceSeeking(event->pos());
			event->accept();
			return;
		}
	}
	Ui::RpWidget::mousePressEvent(event);
}

void PostRow::mouseMoveEvent(QMouseEvent *event) {
	if (_voiceSeeking) {
		const auto state = Media::Player::instance()->getState(
			AudioMsgId::Type::Voice);
		if (isVoiceState(state)) {
			updateVoiceSeeking(event->pos());
		} else {
			Media::Player::instance()->cancelSeeking(AudioMsgId::Type::Voice);
			_voiceSeeking = false;
			unsetCursor();
			updateVoiceButtonIcon();
			update();
		}
		event->accept();
		return;
	}
	const auto state = Media::Player::instance()->getState(
		AudioMsgId::Type::Voice);
	const auto hover = _voiceDocument
		&& isVoiceState(state)
		&& !Media::Player::IsStoppedOrStopping(state.state)
		&& state.length > 0
		&& _voiceSeekRect.contains(event->pos());
	if (hover) {
		_voiceHoverProgress = std::clamp(
			(rtl()
				? _voiceSeekRect.x() + _voiceSeekRect.width() - event->pos().x()
				: event->pos().x() - _voiceSeekRect.x())
				/ float64(std::max(1, _voiceSeekRect.width())),
			0.,
			1.);
		setCursor(style::cur_pointer);
	} else {
		_voiceHoverProgress = -1.;
		unsetCursor();
	}
	update();
	Ui::RpWidget::mouseMoveEvent(event);
}

void PostRow::mouseReleaseEvent(QMouseEvent *event) {
	if (_voiceSeeking && event->button() == Qt::LeftButton) {
		updateVoiceSeeking(event->pos());
		const auto state = Media::Player::instance()->getState(
			AudioMsgId::Type::Voice);
		if (isVoiceState(state)) {
			Media::Player::instance()->finishSeeking(
				AudioMsgId::Type::Voice,
				_voiceSeekProgress);
		} else {
			Media::Player::instance()->cancelSeeking(AudioMsgId::Type::Voice);
		}
		_voiceSeeking = false;
		_voiceHoverProgress = -1.;
		unsetCursor();
		updateVoiceButtonIcon();
		update();
		event->accept();
		return;
	}
	Ui::RpWidget::mouseReleaseEvent(event);
}

void PostRow::leaveEventHook(QEvent *event) {
	if (!_voiceSeeking && _voiceHoverProgress >= 0.) {
		_voiceHoverProgress = -1.;
		unsetCursor();
		update();
	}
	Ui::RpWidget::leaveEventHook(event);
}

void PostRow::scheduleReactionsUpdate() {
	if (_reactionUpdatePending) {
		return;
	}
	_reactionUpdatePending = true;
	base::call_delayed(0, this, [=] {
		_reactionUpdatePending = false;
		syncReactions();
	});
}

void PostRow::syncReactions() {
	auto focused = std::optional<Data::ReactionId>();
	for (const auto &control : _reactionControls) {
		if (control.button->hasFocus()) {
			focused = control.id;
		}
		control.button->hide();
		control.button->deleteLater();
	}
	_reactionControls.clear();
	const auto item = _session->data().message(_itemId);
	auto shown = item ? item->reactionsWithLocal() : std::vector<Data::MessageReaction>();
	const auto possible = item ? Data::LookupPossibleReactions(item)
		: Data::PossibleItemReactionsRef();
	auto allowed = std::vector<Data::ReactionId>();
	for (const auto reaction : possible.recent) {
		if (!reaction->id.paid()) {
			allowed.push_back(reaction->id);
		}
	}
	for (const auto &id : allowed | ranges::views::take(3)) {
		if (!ranges::contains(shown, id, &Data::MessageReaction::id)) {
			shown.push_back({ .id = id });
		}
	}
	_reactionMore->setVisible(!allowed.empty());
	const auto reactionCount = ranges::count_if(shown, [](const auto &reaction) {
		return !reaction.id.paid();
	});
	_reactionExpand->setVisible(reactionCount > 3);
	_reactionExpand->setText(rpl::single(_reactionsExpanded
		? u"−"_q
		: u"+"_q + QString::number(reactionCount - 3)));
	_reactionExpand->setAccessibleName(_reactionsExpanded
		? Text("Свернуть") : Text("Показать ещё"));
	auto visibleCount = 0;
	for (const auto &reaction : shown) {
		if (reaction.id.paid()) {
			continue;
		}
		if (!_reactionsExpanded && visibleCount >= 3) {
			continue;
		}
		++visibleCount;
		const auto button = Ui::CreateChild<Ui::RoundButton>(this,
			rpl::single(QString()), reaction.my
				? st::feedChosenReactionButton : st::feedReactionButton);
		button->setContext(Core::TextContext({
			.session = _session,
			.repaint = [=] { button->update(); },
			.customEmojiLoopLimit = -1,
		}));
		auto label = TextWithEntities{ reaction.id.emoji() };
		if (reaction.id.custom()) {
			label.text = u"￼"_q;
			label.entities.push_back({ EntityType::CustomEmoji, 0, 1,
				Data::ReactionEntityData(reaction.id) });
		}
		if (reaction.count > 0) {
			label.text += u" "_q + QString::number(reaction.count);
		}
		button->setText(rpl::single(std::move(label)));
		button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		button->setDisabled(!reaction.my && !ranges::contains(allowed, reaction.id));
		button->setFocusPolicy(Qt::StrongFocus);
		button->installEventFilter(this);
		const auto id = reaction.id;
		button->setClickedCallback([=] { toggleReaction(id); });
		button->show();
		if (focused == id) {
			button->setFocus();
		}
		_reactionControls.push_back({ id, button });
	}
	if (item && _comments) {
		const auto count = item->repliesCount();
		_comments->setText(count
			? tr::lng_comments_open_count(tr::now, lt_count, count)
			: tr::lng_comments_open_none(tr::now));
	}
	_views->setVisible(item && item->hasViews());
	if (item && item->hasViews()) {
		_views->setText(QString::number(item->viewsCount()));
		_views->setToolTip(tr::lng_views_tooltip(tr::now, lt_count, item->viewsCount()));
	}
	if (width() > 2 * st::feedPostPadding) {
		resizeToWidth(width());
	}
	update();
}

void PostRow::toggleReaction(const Data::ReactionId &id) {
	const auto item = _session->data().message(_itemId);
	if (!item || id.paid()) {
		return;
	}
	const auto chosen = item->chosenReactions();
	const auto possible = Data::LookupPossibleReactions(item);
	if (ranges::contains(chosen, id)
		|| ranges::any_of(possible.recent, [&](const auto reaction) {
			return reaction->id == id;
		})) {
		item->toggleReaction(id, HistoryReactionSource::Existing);
	}
}

void PostRow::showReactionMenu() {
	const auto item = _session->data().message(_itemId);
	if (!item) {
		return;
	}
	_reactionMenu = base::make_unique_q<Ui::PopupMenu>(this);
	_reactionMenu->addAction(Text("Показать в чате"), [=] {
		if (const auto peer = _session->data().peerLoaded(_itemId.peer)) {
			_controller->showPeerHistory(peer, {}, _itemId.msg);
		}
	});
	const auto position = _reactionMore->mapToGlobal(QPoint(0, _reactionMore->height()));
	using namespace HistoryView::Reactions;
	const auto attached = AttachSelectorToMenu(
		_reactionMenu.get(), _controller, position, item,
		[=](ChosenReaction reaction) {
			const auto current = _session->data().message(reaction.context);
			if (!current) {
				return;
			} else if (reaction.id.paid()) {
				Payments::ShowPaidReactionDetails(
					_controller, current, nullptr, HistoryReactionSource::Selector);
			} else if (!Window::ShowReactPremiumError(_controller, current, reaction.id)) {
				current->toggleReaction(reaction.id, HistoryReactionSource::Selector);
			}
		}, ItemReactionsAbout(item));
	if (attached == AttachSelectorResult::Attached) {
		_reactionMenu->popupPrepared();
	} else if (attached == AttachSelectorResult::Skipped) {
		_reactionMenu->popup(position);
	} else {
		_reactionMenu = nullptr;
	}
}

SourcesController::SourcesController(
		not_null<Main::Session*> session,
		Category category)
: ChatsListBoxController(std::unique_ptr<PeerListSearchController>())
, _session(session)
, _category(std::move(category)) {
	setStyleOverrides(&st::feedPeerList, &st::feedSearch);
}

Main::Session &SourcesController::session() const {
	return *_session;
}

void SourcesController::prepareViewHook() {
	delegate()->peerListSetTitle(rpl::single(Text("Выбрать источники")));
	auto selected = std::vector<not_null<PeerData*>>();
	for (const auto id : _category.sources) {
		const auto peer = _session->data().peer(id);
		delegate()->peerListAppendRow(
			std::make_unique<Row>(_session->data().history(peer)));
		selected.push_back(peer);
	}
	delegate()->peerListAddSelectedPeers(selected);
	_session->api().requestDialogs();
	_session->api().requestDialogs(
		_session->data().folder(Data::Folder::kId));
}

std::unique_ptr<ChatsListBoxController::Row> SourcesController::createRow(
		not_null<History*> history) {
	if (!history->peer->isSelf() && !history->inChatList()) {
		return nullptr;
	}
	if (const auto channel = history->peer->asChannel()) {
		if (!channel->amIn()) {
			return nullptr;
		}
	}
	if (const auto chat = history->peer->asChat()) {
		if (!chat->amIn()) {
			return nullptr;
		}
	}
	return std::make_unique<Row>(history);
}

QString SourcesController::savedMessagesChatStatus() const {
	return tr::lng_saved_messages(tr::now);
}

void SourcesController::loadMoreRows() {
	if (!_session->data().chatsListLoaded()) {
		_session->api().requestDialogs();
	}
	const auto archive = _session->data().folder(Data::Folder::kId);
	if (!_session->data().chatsListLoaded(archive)) {
		_session->api().requestDialogs(archive);
	}
}

void SourcesController::rowClicked(not_null<PeerListRow*> row) {
	const auto checked = delegate()->peerListIsRowChecked(row);
	if (!checked && delegate()->peerListSelectedRowsCount() >= kMaxSources) {
		delegate()->peerListUiShow()->showToast(Text("Выберите не больше 32 источников"));
		return;
	}
	delegate()->peerListSetRowChecked(row, !checked);
}

rpl::producer<QString> SourcesController::searchPlaceholder() const {
	return rpl::single(Text("Найти источник"));
}

QString SourcesController::categoryName() const {
	return _name ? _name->getLastText().trimmed() : QString();
}

void SourcesController::setNameField(not_null<Ui::InputField*> field) {
	_name = field;
}

void SourcesController::showNameError() {
	if (_name) {
		_name->showError();
		_name->setFocusFast();
	}
}

FeedSection::FeedSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		const FeedMemento &memento)
: Window::SectionWidget(parent, controller)
, _session(&controller->session())
, _loading([=] { update(); }, st::feedLoading)
, _key(u"feed.%1.categories"_q.arg(_session->uniqueId()))
, _api(std::make_unique<MTP::Sender>(&_session->mtp()))
, _restoreTop(memento.scrollTop) {
	Settings::Register({ .key = _key, .kind = Settings::Kind::String,
		.defaultValue = QString(), .hidden = true });
	loadCategories();
	_session->data().chatsListChanges() | rpl::on_next([=] {
		base::call_delayed(0, this, [=] { resolvePendingSources(); });
	}, lifetime());
	_back = Ui::CreateChild<Ui::IconButton>(this, st::backButton);
	_back->setAccessibleName(tr::lng_go_back(tr::now));
	_back->setClickedCallback([=] { this->controller()->showBackFromStack(); });
	_menuButton = Ui::CreateChild<Ui::IconButton>(this, st::menuToggle);
	_menuButton->setAccessibleName(Text("Управление лентой"));
	_menuButton->setClickedCallback([=] { showMenu(); });
	_category = Ui::CreateChild<Ui::RoundButton>(this,
		rpl::single(QString()), st::feedCategoryButton);
	_category->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	_category->setClickedCallback([=] { showCategories(); });
	_scroll = Ui::CreateChild<Ui::ScrollArea>(this, st::feedScroll);
	_content = _scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(_scroll));
	_empty = Ui::CreateChild<Ui::RpWidget>(this);
	_emptyTitle = Ui::CreateChild<Ui::FlatLabel>(_empty,
		rpl::single(QString()), st::feedEmptyTitle);
	_emptyHint = Ui::CreateChild<Ui::FlatLabel>(_empty,
		rpl::single(QString()), st::feedEmptyHint);
	_emptyAction = Ui::CreateChild<Ui::RoundButton>(_empty,
		rpl::single(Text("Добавить категорию")), st::feedAction);
	_emptyAction->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	_emptyAction->setClickedCallback([=] { editCategory(_groups.empty()); });
	for (const auto control : std::array<Ui::AbstractButton*, 4>{
			_back, _menuButton, _category, _emptyAction }) {
		control->setFocusPolicy(Qt::StrongFocus);
		control->installEventFilter(this);
	}
	_empty->paintRequest() | rpl::on_next([=] {
		auto p = Painter(_empty);
		PaintFocus(p, _empty, _emptyAction);
	}, _empty->lifetime());
	_content->paintRequest() | rpl::on_next([=] {
		auto p = Painter(_content.data());
		PaintFocus(p, _content.data(), _older.data());
	}, _content->lifetime());
	style::PaletteChanged() | rpl::on_next([=] {
		update();
		_content->update();
		for (const auto child : _content->children()) {
			if (const auto widget = qobject_cast<QWidget*>(child)) {
				widget->update();
			}
		}
		_scroll->updateBars();
	}, lifetime());
	_session->downloaderTaskFinished() | rpl::on_next([=] {
		if (!_content->isVisible()) {
			return;
		}
		for (const auto child : _content->children()) {
			if (const auto widget = qobject_cast<QWidget*>(child);
					widget && widget->isVisible()) {
				widget->update();
			}
		}
	}, lifetime());
	const auto position = std::find_if(_groups.begin(), _groups.end(),
		[&](const Category &group) { return group.id == memento.categoryId; });
	if (memento.snapshot && position != _groups.end()) {
		_selected = int(position - _groups.begin());
		updateCategory();
		_posts = memento.snapshot->posts;
		_sources = memento.snapshot->sources;
		_pendingSources = memento.snapshot->pendingSources;
		_failures = memento.snapshot->failures;
		_unavailableSources = memento.snapshot->unavailableSources;
		std::sort(_posts.begin(), _posts.end(), [](const Post &a, const Post &b) {
			return std::tie(a.date, a.peer, a.id) > std::tie(b.date, b.peer, b.id);
		});
		renderPosts();
		resolvePendingSources();
	} else {
		populateCategories(position == _groups.end()
			? 0 : int(position - _groups.begin()));
	}
}

bool FeedSection::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	const auto feed = dynamic_cast<FeedMemento*>(memento.get());
	if (!feed) {
		return false;
	}
	if (feed->categoryId.isEmpty()) {
		return true;
	}
	const auto position = std::find_if(_groups.begin(), _groups.end(),
		[&](const Category &group) { return group.id == feed->categoryId; });
	const auto selected = position == _groups.end()
		? 0 : int(position - _groups.begin());
	if (selected != _selected) {
		_restoreTop = feed->scrollTop;
		populateCategories(selected);
	} else if (!feed->categoryId.isEmpty()) {
		_scroll->scrollToY(feed->scrollTop);
	}
	return true;
}

std::shared_ptr<Window::SectionMemento> FeedSection::createMemento() {
	return createIdentityMemento();
}

std::shared_ptr<Window::SectionMemento> FeedSection::createIdentityMemento() {
	const auto snapshot = _posts.empty() ? nullptr
		: std::make_shared<FeedSnapshot>(FeedSnapshot{
			.posts = _posts,
			.sources = _sources,
			.pendingSources = _pendingSources,
			.failures = _failures,
			.unavailableSources = _unavailableSources,
		});
	return std::make_shared<FeedMemento>(
		_selected >= 0 ? _groups[_selected].id : QString(),
		_scroll->scrollTop(), snapshot);
}

QPixmap FeedSection::grabForShowAnimation(
		const Window::SectionSlideParams &params) {
	syncVisibility();
	return Window::SectionWidget::grabForShowAnimation(params);
}

void FeedSection::showFinishedHook() {
	syncVisibility();
	updateLayout();
}

void FeedSection::syncVisibility() {
	_empty->setVisible(_showingEmpty);
	_scroll->setVisible(!_showingEmpty);
}

bool FeedSection::floatPlayerHandleWheelEvent(QEvent *event) {
	return _scroll->viewportEvent(event);
}

QRect FeedSection::floatPlayerAvailableRect() {
	return QRect(_scroll->mapToGlobal(QPoint()), _scroll->size());
}

void FeedSection::paintEvent(QPaintEvent *event) {
	if (animatingShow()) {
		Window::SectionWidget::paintEvent(event);
		return;
	}
	auto p = Painter(this);
	p.fillRect(event->rect(), st::mainMenuBg);
	p.setFont(st::boxTitleFont);
	p.setPen(st::windowFg);
	p.drawText(QRect(st::feedTitleLeft, st::feedTitleTop,
		std::max(0, width() - st::feedTitleLeft - st::feedTitleRight),
		st::boxTitleFont->height), Qt::AlignCenter,
		st::boxTitleFont->elided(Text("Лента"),
			std::max(0, width() - st::feedTitleLeft - st::feedTitleRight)));
	if (_busy) {
		_loading.draw(p, QPoint(width() - st::feedTitleRight,
			st::feedTitleTop), width());
	}
	auto line = st::windowFg->c;
	line.setAlphaF(0.08);
	p.fillRect(0, st::feedHeaderHeight - st::lineWidth,
		width(), st::lineWidth, line);
	for (const auto control : std::array<QWidget*, 3>{
			_back, _menuButton, _category }) {
		PaintFocus(p, this, control);
	}
}

bool FeedSection::eventFilter(QObject *object, QEvent *event) {
	if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
		if (const auto control = qobject_cast<QWidget*>(object)) {
			control->parentWidget()->update();
		}
	}
	return Window::SectionWidget::eventFilter(object, event);
}

void FeedSection::resizeEvent(QResizeEvent *event) {
	Window::SectionWidget::resizeEvent(event);
	updateLayout();
}

void FeedSection::keyPressEvent(QKeyEvent *event) {
	if (event->key() == Qt::Key_Escape) {
		controller()->showBackFromStack();
		return;
	}
	Window::SectionWidget::keyPressEvent(event);
}

void FeedSection::updateLayout() {
	const auto padding = (width() < st::feedCompactWidth)
		? st::feedPostCompactPadding
		: st::feedPostPadding;
	_back->moveToLeft(st::feedControlInset, st::feedControlInset);
	_menuButton->moveToRight(st::feedControlInset, st::feedControlInset);
	_category->setFullWidth(0);
	const auto available = std::max(1, width() - 2 * padding);
	_category->setFullWidth(std::min(_category->width(), available));
	_category->moveToLeft(padding,
		st::feedFilterTop + (st::feedFilterHeight - _category->height()) / 2);
	_scroll->setGeometry(0, st::feedHeaderHeight, width(),
		std::max(0, height() - st::feedHeaderHeight));
	_content->resizeToWidth(_scroll->width());
	_empty->setGeometry(_scroll->geometry());
	const auto textWidth = std::max(1,
		width() - 2 * st::feedEmptyPadding);
	_emptyTitle->resizeToWidth(textWidth);
	_emptyHint->resizeToWidth(textWidth);
	const auto total = _emptyTitle->height() + st::feedEmptyGap
		+ _emptyHint->height() + (_emptyAction->isHidden()
			? 0 : st::feedEmptyActionGap + _emptyAction->height());
	const auto top = std::max(st::feedPostPadding, (_empty->height() - total) / 2);
	_emptyTitle->moveToLeft(st::feedEmptyPadding, top);
	_emptyHint->moveToLeft(st::feedEmptyPadding,
		top + _emptyTitle->height() + st::feedEmptyGap);
	_emptyAction->setFullWidth(0);
	if (_emptyAction->width() > textWidth) {
		_emptyAction->setFullWidth(textWidth);
	}
	_emptyAction->move((width() - _emptyAction->width()) / 2,
		top + _emptyTitle->height() + st::feedEmptyGap
			+ _emptyHint->height() + st::feedEmptyActionGap);
}

void FeedSection::updateCategory() {
	_category->setText(rpl::single(_selected >= 0
		? _groups[_selected].name : Text("Категории")));
	updateLayout();
	update();
}

void FeedSection::showCategories() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	for (auto i = 0; i != int(_groups.size()); ++i) {
		_menu->addAction(_groups[i].name, [=] {
			_restoreTop = 0;
			populateCategories(i);
		}, i == _selected ? &st::menuIconSelect : nullptr);
	}
	if (!_groups.empty()) {
		_menu->addSeparator();
	}
	_menu->addAction(Text("Добавить категорию"), [=] {
		editCategory(true);
	}, &st::menuIconAdd)->setEnabled(_groups.size() < kMaxCategories);
	_menu->popup(_category->mapToGlobal(QPoint(0, _category->height())));
}

void FeedSection::showMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(this, st::popupMenuWithIcons);
	_menu->addAction(Text("Обновить"), [=] { refresh(); }, &st::feedReloadIcon)
		->setEnabled(_selected >= 0 && !_busy);
	_menu->addAction(Text("Выбрать источники"), [=] { editCategory(false); },
		&st::menuIconChannel)->setEnabled(_selected >= 0);
	_menu->addAction(Text("Добавить категорию"), [=] { editCategory(true); },
		&st::menuIconAdd)->setEnabled(_groups.size() < kMaxCategories);
	if (_selected >= 0) {
		_menu->addSeparator();
		const auto id = _groups[_selected].id;
		_menu->addAction(Text("Удалить категорию"), [=] {
			const auto position = std::find_if(_groups.begin(), _groups.end(),
				[&](const Category &group) { return group.id == id; });
			if (position == _groups.end()) {
				return;
			}
			_groups.erase(position);
			saveCategories();
			populateCategories(_selected);
		}, &st::menuIconDeleteAttention);
	}
	_menu->popup(_menuButton->mapToGlobal(QPoint(0, _menuButton->height())));
}

void FeedSection::showEmpty(
		const QString &title,
		const QString &hint,
		bool canEdit) {
	_showingEmpty = true;
	_scroll->hide();
	_emptyTitle->setText(title);
	_emptyHint->setText(hint);
	_emptyAction->setVisible(canEdit);
	_emptyAction->setText(rpl::single(_groups.empty()
		? Text("Добавить категорию") : Text("Выбрать источники")));
	_empty->show();
	updateLayout();
	update();
}

void FeedSection::renderPosts() {
	const auto scrollTop = _restoreTop
		? base::take(_restoreTop) : _scroll->scrollTop();
	_older = nullptr;
	_content->clear();
	for (const auto &post : _posts) {
		const auto row = _content->add(object_ptr<PostRow>(
			_content, controller(), post, [=] {
				if (const auto peer = _session->data().peerLoaded(post.peer)) {
					controller()->showPeerHistory(peer, {}, MsgId(post.id));
				}
			}, [=] {
				if (const auto item = _session->data().message(post.peer, MsgId(post.id))) {
					controller()->showRepliesForMessage(item->history(), item->id);
				}
			}, [=] {
				const auto item = _session->data().message(post.peer, MsgId(post.id));
				const auto media = item ? item->media() : nullptr;
				if (!media) {
					return;
				}
				if (const auto photo = media->photo()) {
					controller()->openPhoto(photo, { .id = item->fullId() });
				} else if (const auto document = media->document()) {
					controller()->openDocument(document, true, { .id = item->fullId() });
				}
			}), st::feedPostMargin, Qt::AlignHCenter);
		row->setNaturalWidth(st::feedPostMaxWidth);
		row->show();
	}
	const auto canLoad = _posts.size() < kMaxPosts && std::any_of(
		_sources.begin(), _sources.end(), [](const Source &source) {
			return !source.exhausted;
		});
	if (canLoad) {
		_older = _content->add(object_ptr<Ui::RoundButton>(_content,
			rpl::single(Text("Ещё сообщения")), st::feedMoreButton),
			st::feedMorePadding, Qt::AlignHCenter);
		_older->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		_older->setClickedCallback([=] { loadMore(); });
		_older->setFocusPolicy(Qt::StrongFocus);
		_older->installEventFilter(this);
	}
	_content->resizeToWidth(_scroll->width());
	if (_posts.empty()) {
		showEmpty(Text("Пока нет публикаций"), _failures
			? Text("Не удалось загрузить часть источников. Обновите ленту через меню")
			: Text("Новые сообщения выбранных источников появятся здесь"), false);
	} else {
		_showingEmpty = false;
		_empty->hide();
		_scroll->show();
		_scroll->scrollToY(scrollTop);
	}
	if (_failures && !_posts.empty()) {
		controller()->showToast(Text("Не удалось загрузить часть источников. Обновите ленту через меню"));
	}
	update();
}

void FeedSection::loadCategories() {
	const auto bytes = Settings::String(_key).toUtf8();
	if (bytes.size() > 65536) return;
	const auto groups = QJsonDocument::fromJson(bytes).array();
	for (const auto &value : groups) {
		if (_groups.size() == kMaxCategories) break;
		const auto object = value.toObject();
		auto category = Category{
			.id = object.value(u"id"_q).toString().left(64),
			.name = object.value(u"name"_q).toString().trimmed().left(80),
		};
		if (category.id.isEmpty() || category.name.isEmpty()) continue;
		for (const auto &id : object.value(u"channels"_q).toArray()) {
			auto ok = false;
			const auto raw = id.toString().toULongLong(&ok);
			const auto peer = PeerId(raw);
			if (ok && raw && std::find(category.sources.begin(),
				category.sources.end(), peer) == category.sources.end()) {
				category.sources.push_back(peer);
				if (category.sources.size() == kMaxSources) break;
			}
		}
		_groups.push_back(std::move(category));
	}
}

void FeedSection::saveCategories() {
	auto groups = QJsonArray();
	for (const auto &category : _groups) {
		auto channels = QJsonArray();
		for (const auto id : category.sources) channels.push_back(QString::number(id.value));
		groups.push_back(QJsonObject{
			{ u"id"_q, category.id }, { u"name"_q, category.name },
			{ u"channels"_q, channels },
		});
	}
	Settings::SetString(_key,
		QString::fromUtf8(QJsonDocument(groups).toJson(QJsonDocument::Compact)));
}

void FeedSection::populateCategories(int selected) {
	_selected = _groups.empty() ? -1
		: std::clamp(selected, 0, int(_groups.size()) - 1);
	updateCategory();
	refresh();
}

void FeedSection::editCategory(bool create) {
	if (_editor || (create && _groups.size() >= kMaxCategories)
		|| (!create && _selected < 0)) {
		return;
	}
	const auto category = create ? Category{
		.id = QUuid::createUuid().toString(QUuid::WithoutBraces),
	} : _groups[_selected];
	auto list = std::make_unique<SourcesController>(_session, category);
	const auto listController = list.get();
	const auto weak = QPointer(this);
	controller()->show(Box<PeerListBox>(std::move(list),
		[=](not_null<PeerListBox*> box) {
			if (!weak) {
				box->closeBox();
				return;
			}
			weak->_editor = box;
			box->setStyle(st::feedEditorBox);
			const auto header = Ui::CreateChild<Ui::VerticalLayout>(box.get());
			header->paintOn([=](QPainter &p) {
				p.fillRect(header->rect(), st::mainMenuBg);
			});
			const auto name = header->add(object_ptr<Ui::InputField>(header,
				st::feedNameField, rpl::single(Text("Название категории")),
				category.name), st::feedEditorPadding);
			name->setMaxLength(80);
			listController->setNameField(name);
			header->add(object_ptr<Ui::FlatLabel>(header,
				rpl::single(Text("Выберите до 32 источников")), st::feedHint),
				st::feedEditorHintPadding);
			box->widthValue() | rpl::on_next([=](int width) {
				header->resizeToWidth(width);
			}, header->lifetime());
			header->heightValue() | rpl::on_next([=](int height) {
				box->setAddedTopScrollSkip(height, true);
			}, header->lifetime());
			header->show();
			header->raise();
			if (create) {
				listController->setShowFinishedCallback([=] { name->setFocusFast(); });
			}
			box->addButton(tr::lng_settings_save(), [=] {
				if (!weak) {
					box->closeBox();
					return;
				}
				auto updated = category;
				updated.name = listController->categoryName();
				if (updated.name.isEmpty()) {
					listController->showNameError();
					return;
				}
				updated.sources.clear();
				for (const auto &peer : box->collectSelectedRows()) {
					updated.sources.push_back(peer->id);
				}
				if (updated.sources.size() > kMaxSources) {
					return;
				}
				const auto position = std::find_if(weak->_groups.begin(),
					weak->_groups.end(), [&](const Category &group) {
						return group.id == updated.id;
					});
				auto selected = 0;
				if (position == weak->_groups.end()) {
					if (weak->_groups.size() >= kMaxCategories) {
						return;
					}
					weak->_groups.push_back(std::move(updated));
					selected = int(weak->_groups.size()) - 1;
				} else {
					selected = int(position - weak->_groups.begin());
					*position = std::move(updated);
				}
				weak->saveCategories();
				box->closeBox();
				weak->populateCategories(selected);
			});
			box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		}));
}

void FeedSection::refresh() {
	_api = std::make_unique<MTP::Sender>(&_session->mtp());
	_busy = false;
	_loading.stop(anim::type::instant);
	_failures = 0;
	_unavailableSources = 0;
	_sources.clear();
	_pendingSources.clear();
	_posts.clear();
	_older = nullptr;
	_content->clear();
	if (_selected >= 0) {
		for (const auto id : _groups[_selected].sources) {
			const auto peer = _session->data().peerLoaded(id);
			if (!peer) {
				_pendingSources.push_back(id);
				++_unavailableSources;
				continue;
			}
			if (const auto channel = peer->asChannel()) {
				if (!channel->amIn()) {
					++_unavailableSources;
					continue;
				}
			}
			if (const auto chat = peer->asChat()) {
				if (!chat->amIn()) {
					++_unavailableSources;
					continue;
				}
			}
			_sources.push_back({ .peer = id });
		}
	}
	_failures = _unavailableSources;
	if (_sources.empty()) {
		showEmpty(Text("Выберите источники для ленты"),
			_unavailableSources
				? Text("Не удалось загрузить часть источников. Обновите ленту через меню")
				: Text("Объедините каналы, группы и личные чаты в категорию"), true);
	} else {
		loadMore();
	}
	resolvePendingSources();
}

void FeedSection::resolvePendingSources() {
	if (_pendingSources.empty()) {
		return;
	}
	if (ranges::any_of(_pendingSources, [=](PeerId id) {
		return _session->data().peerLoaded(id) != nullptr;
	})) {
		_restoreTop = _scroll->scrollTop();
		refresh();
		return;
	}
	if (!_session->data().chatsListLoaded()) {
		_session->api().requestDialogs();
	}
	const auto archive = _session->data().folder(Data::Folder::kId);
	if (!_session->data().chatsListLoaded(archive)) {
		_session->api().requestDialogs(archive);
	}
}

void FeedSection::loadMore() {
	if (_busy || _posts.size() >= kMaxPosts || _sources.empty()) {
		return;
	}
	_busy = true;
	_loading.start();
	if (_older) {
		_older->setDisabled(true);
		_older->setText(rpl::single(Text("Загрузка…")));
	}
	if (_posts.empty()) {
		showEmpty(Text("Загрузка публикаций"), QString(), false);
	}
	_source = 0;
	_failures = _unavailableSources;
	loadNext();
}

void FeedSection::loadNext() {
	while (_source < _sources.size() && _sources[_source].exhausted) {
		++_source;
	}
	if (_source == _sources.size() || _posts.size() >= kMaxPosts) {
		_busy = false;
		_loading.stop(anim::type::instant);
		std::sort(_posts.begin(), _posts.end(), [](const Post &a, const Post &b) {
			return std::tie(a.date, a.peer, a.id) > std::tie(b.date, b.peer, b.id);
		});
		renderPosts();
		return;
	}
	const auto peer = _session->data().peerLoaded(_sources[_source].peer);
	if (!peer) {
		++_failures;
		++_source;
		loadNext();
		return;
	}
	update();
	if (!_sources[_source].offset) {
		_session->api().requestFullPeer(peer);
	}
	_api->request(MTPmessages_GetHistory(peer->input(), MTP_int(_sources[_source].offset),
		MTP_int(0), MTP_int(0), MTP_int(kPageSize), MTP_int(0), MTP_int(0), MTP_long(0)
	)).done([=](const MTPmessages_Messages &result) {
		_session->data().processExistingMessages(peer->asChannel(), result);
		auto messages = QVector<MTPMessage>();
		result.match([&](const MTPDmessages_messages &data) { messages = data.vmessages().v; },
			[&](const MTPDmessages_messagesSlice &data) { messages = data.vmessages().v; },
			[&](const MTPDmessages_channelMessages &data) { messages = data.vmessages().v; },
			[&](const MTPDmessages_messagesNotModified &) {});
		auto &source = _sources[_source];
		auto lowest = source.offset;
		for (const auto &message : messages) {
			message.match([&](const MTPDmessage &data) {
				const auto id = data.vid().v;
				if (id <= 0) return;
				lowest = lowest ? std::min(lowest, id) : id;
				if (std::any_of(_posts.begin(), _posts.end(), [&](const Post &p) {
					return p.peer == source.peer && p.id == id;
				})) return;
				if (_posts.size() < kMaxPosts) _posts.push_back({ source.peer, id,
					data.vdate().v, qs(data.vmessage()) });
			}, [&](const MTPDmessageService &data) {
				lowest = lowest ? std::min(lowest, data.vid().v) : data.vid().v;
			}, [&](const MTPDmessageEmpty &) {});
		}
		source.exhausted = messages.size() < kPageSize || !lowest || lowest == source.offset;
		source.offset = lowest;
		++_source;
		loadNext();
	}).fail([=](const MTP::Error &error) {
		++_failures;
		if (error.type().startsWith(u"FLOOD_WAIT"_q)) _source = _sources.size();
		else ++_source;
		loadNext();
	}).handleFloodErrors().send();
}

} // namespace

void Show(not_null<Window::SessionController*> controller) {
	controller->showSection(std::make_shared<FeedMemento>());
}

} // namespace Mokagram::Feed
