/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stickers_box.h"

#include "data/data_document.h"
#include "data/data_session.h"
#include "data/data_channel.h"
#include "data/data_file_origin.h"
#include "data/data_document_media.h"
#include "data/data_premium_limits.h"
#include "data/stickers/data_stickers.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "ui/boxes/boost_box.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/peers/edit_peer_color_box.h"
#include "boxes/sticker_set_box.h"
#include "apiwrap.h"
#include "storage/storage_account.h"
#include "lottie/lottie_single_player.h"
#include "chat_helpers/compose/compose_show.h"
#include "chat_helpers/stickers_lottie.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/slide_animation.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/special_fields.h"
#include "ui/image/image.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/unread_badge_paint.h"
#include "media/clip/media_clip_reader.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

namespace {

using Data::StickersSet;
using Data::StickersSetsOrder;
using Data::StickersSetThumbnailView;
using SetFlag = Data::StickersSetFlag;

constexpr auto kArchivedLimitFirstRequest = 10;
constexpr auto kArchivedLimitPerPage = 30;
constexpr auto kHandleMegagroupSetAddressChangeTimeout = crl::time(1000);

[[nodiscard]] QString FillSetTitle(
		not_null<StickersSet*> set,
		int maxNameWidth,
		int *outTitleWidth) {
	auto result = set->title;
	auto titleWidth = st::contactsNameStyle.font->width(result);
	if (titleWidth > maxNameWidth) {
		result = st::contactsNameStyle.font->elided(result, maxNameWidth);
		titleWidth = st::contactsNameStyle.font->width(result);
	}
	if (outTitleWidth) {
		*outTitleWidth = titleWidth;
	}
	return result;
}

} // namespace

class StickersBox::CounterWidget : public Ui::RpWidget {
public:
	CounterWidget(QWidget *parent, rpl::producer<int> count);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void setCounter(int counter);

	QString _text;
	Ui::UnreadBadgeStyle _st;

};

// This class is hold in header because it requires Qt preprocessing.
class StickersBox::Inner : public Ui::RpWidget {
public:
	using Section = StickersBox::Section;

	Inner(
		QWidget *parent,
		std::shared_ptr<ChatHelpers::Show> show,
		Section section);
	Inner(
		QWidget *parent,
		std::shared_ptr<ChatHelpers::Show> show,
		not_null<ChannelData*> megagroup,
		bool isEmoji);

	[[nodiscard]] Main::Session &session() const;

	rpl::producer<int> scrollsToY() const {
		return _scrollsToY.events();
	}
	void setInnerFocus();

	void saveGroupSet(Fn<void()> done);

	void rebuild(bool masks);
	void updateSize(int newWidth = 0);
	void updateRows(); // refresh only pack cover stickers
	bool appendSet(not_null<StickersSet*> set);

	StickersSetsOrder order() const;
	StickersSetsOrder fullOrder() const;
	StickersSetsOrder removedSets() const;

	void setFullOrder(const StickersSetsOrder &order);
	void setRemovedSets(const StickersSetsOrder &removed);

	void setRowRemovedBySetId(uint64 setId, bool removed);

	void setInstallSetCallback(Fn<void(uint64 setId)> callback) {
		_installSetCallback = std::move(callback);
	}
	void setRemoveSetCallback(Fn<void(uint64 setId)> callback) {
		_removeSetCallback = std::move(callback);
	}
	void setLoadMoreCallback(Fn<void()> callback) {
		_loadMoreCallback = std::move(callback);
	}

	int getVisibleTop() const {
		return _visibleTop;
	}

	[[nodiscard]] rpl::producer<int> draggingScrollDelta() const {
		return _draggingScrollDelta.events();
	}

	~Inner();

protected:
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void leaveToChildEvent(QEvent *e, QWidget *child) override;

private:
	struct Row {
		Row(
			not_null<StickersSet*> set,
			DocumentData *sticker,
			int32 count,
			const QString &title,
			int titleWidth,
			Data::StickersSetFlags flagsOverride,
			bool removed,
			int32 pixw,
			int32 pixh);
		~Row();

		[[nodiscard]] bool isRecentSet() const;
		[[nodiscard]] bool isMasksSet() const;
		[[nodiscard]] bool isEmojiSet() const;
		[[nodiscard]] bool isInstalled() const;
		[[nodiscard]] bool isUnread() const;
		[[nodiscard]] bool isArchived() const;

		const not_null<StickersSet*> set;
		DocumentData *sticker = nullptr;
		std::shared_ptr<Data::DocumentMedia> stickerMedia;
		std::shared_ptr<StickersSetThumbnailView> thumbnailMedia;
		int32 count = 0;
		QString title;
		int titleWidth = 0;
		Data::StickersSetFlags flagsOverride;
		bool removed = false;
		int32 pixw = 0;
		int32 pixh = 0;
		anim::value yadd;
		std::unique_ptr<Ui::RippleAnimation> ripple;
		std::unique_ptr<Lottie::SinglePlayer> lottie;
		Media::Clip::ReaderPointer webm;
	};
	struct MegagroupSet {
		inline bool operator==(const MegagroupSet &other) const {
			return true;
		}
		inline bool operator!=(const MegagroupSet &other) const {
			return false;
		}
	};
	using SelectedRow = std::variant<v::null_t, MegagroupSet, int>;
	class AddressField : public Ui::UsernameInput {
	public:
		using UsernameInput::UsernameInput;

	protected:
		void correctValue(
			const QString &was,
			int wasCursor,
			QString &now,
			int &nowCursor) override;

	};

	template <typename Check>
	StickersSetsOrder collectSets(Check check) const;

	void updateSelected();
	void checkGroupLevel(Fn<void()> done);

	void checkLoadMore();
	void updateScrollbarWidth();
	int getRowIndex(uint64 setId) const;
	void setRowRemoved(int index, bool removed);

	void setSelected(SelectedRow selected);
	void setActionDown(int newActionDown);
	void setPressed(SelectedRow pressed);
	void setup();
	QRect relativeButtonRect(bool removeButton, bool installedSet) const;
	void ensureRipple(
		const style::RippleAnimation &st,
		QImage mask,
		bool removeButton,
		bool installedSet);

	bool shiftingAnimationCallback(crl::time now);
	void paintRow(Painter &p, not_null<Row*> row, int index);
	void paintRowThumbnail(Painter &p, not_null<Row*> row, int left);
	void paintFakeButton(Painter &p, not_null<Row*> row, int index);
	void clear();
	void updateCursor();
	void setActionSel(int32 actionSel);
	float64 aboveShadowOpacity() const;
	void validateLottieAnimation(not_null<Row*> row);
	void validateWebmAnimation(not_null<Row*> row);
	void validateAnimation(not_null<Row*> row);
	void updateRowThumbnail(not_null<Row*> row);

	void clipCallback(
		not_null<Row*> row,
		Media::Clip::Notification notification);

	void readVisibleSets();

	void updateControlsGeometry();
	void rebuildAppendSet(not_null<StickersSet*> set);
	void fillSetCover(not_null<StickersSet*> set, DocumentData **outSticker, int *outWidth, int *outHeight) const;
	int fillSetCount(not_null<StickersSet*> set) const;
	[[nodiscard]] Data::StickersSetFlags fillSetFlags(
		not_null<StickersSet*> set) const;
	void rebuildMegagroupSet();
	void handleMegagroupSetAddressChange();
	void setMegagroupSelectedSet(const StickerSetIdentifier &set);

	int countMaxNameWidth(bool installedSet) const;
	[[nodiscard]] bool skipPremium() const;

	const style::PeerListItem &_st;
	const std::shared_ptr<ChatHelpers::Show> _show;
	const not_null<Main::Session*> _session;
	MTP::Sender _api;

	const Section _section;
	const bool _isInstalledTab;

	Ui::RoundRect _buttonBgOver, _buttonBg, _inactiveButtonBg;

	int32 _rowHeight = 0;

	std::vector<std::unique_ptr<Row>> _rows;
	std::vector<std::unique_ptr<Row>> _oldRows;
	std::vector<crl::time> _shiftingStartTimes;
	crl::time _aboveShadowFadeStart = 0;
	anim::value _aboveShadowFadeOpacity;
	Ui::Animations::Basic _shiftingAnimation;

	Fn<void(uint64 setId)> _installSetCallback;
	Fn<void(uint64 setId)> _removeSetCallback;
	Fn<void()> _loadMoreCallback;

	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _itemsTop = 0;

	int _actionSel = -1;
	int _actionDown = -1;

	QString _addText;
	int _addWidth = 0;
	QString _undoText;
	int _undoWidth = 0;
	QString _installedText;
	int _installedWidth = 0;

	QPoint _mouse;
	bool _inDragArea = false;
	SelectedRow _selected;
	SelectedRow _pressed;
	QPoint _dragStart;
	int _started = -1;
	int _dragging = -1;
	int _above = -1;
	rpl::event_stream<int> _draggingScrollDelta;

	rpl::event_stream<int> _scrollsToY;

	int _minHeight = 0;

	int _scrollbar = 0;
	ChannelData *_megagroupSet = nullptr;
	bool _megagroupSetEmoji = false;
	bool _checkingGroupLevel = false;
	StickerSetIdentifier _megagroupSetInput;
	std::unique_ptr<Row> _megagroupSelectedSet;
	object_ptr<AddressField> _megagroupSetField = { nullptr };
	object_ptr<Ui::PlainShadow> _megagroupSelectedShadow = { nullptr };
	object_ptr<Ui::CrossButton> _megagroupSelectedRemove = { nullptr };
	object_ptr<Ui::BoxContentDivider> _megagroupDivider = { nullptr };
	object_ptr<Ui::FlatLabel> _megagroupSubTitle = { nullptr };
	base::Timer _megagroupSetAddressChangedTimer;
	mtpRequestId _megagroupSetRequestId = 0;

};

StickersBox::CounterWidget::CounterWidget(
	QWidget *parent,
	rpl::producer<int> count)
: RpWidget(parent) {
	setAttribute(Qt::WA_TransparentForMouseEvents);

	_st.sizeId = Dialogs::Ui::UnreadBadgeSize::StickersBox;
	_st.textTop = st::stickersFeaturedBadgeTextTop;
	_st.size = st::stickersFeaturedBadgeSize;
	_st.padding = st::stickersFeaturedBadgePadding;
	_st.font = st::stickersFeaturedBadgeFont;

	std::move(
		count
	) | rpl::start_with_next([=](int count) {
		setCounter(count);
		update();
	}, lifetime());
}

void StickersBox::CounterWidget::setCounter(int counter) {
	_text = (counter > 0) ? QString::number(counter) : QString();
	auto dummy = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
	auto p = QPainter(&dummy);

	const auto badge = Ui::PaintUnreadBadge(p, _text, 0, 0, _st);

	resize(badge.width(), st::stickersFeaturedBadgeSize);
}

void StickersBox::CounterWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_text.isEmpty()) {
		const auto unreadRight = rtl() ? 0 : width();
		const auto unreadTop = 0;
		Ui::PaintUnreadBadge(p, _text, unreadRight, unreadTop, _st);
	}
}

template <typename ...Args>
StickersBox::Tab::Tab(int index, Args&&... args)
: _index(index)
, _widget(std::forward<Args>(args)...)
, _weak(_widget) {
}

object_ptr<StickersBox::Inner> StickersBox::Tab::takeWidget() {
	return std::move(_widget);
}

void StickersBox::Tab::returnWidget(object_ptr<Inner> widget) {
	_widget = std::move(widget);
	Assert(_widget == _weak);
}

StickersBox::Inner *StickersBox::Tab::widget() const {
	return _weak;
}

int StickersBox::Tab::index() const {
	return _index;
}

void StickersBox::Tab::saveScrollTop() {
	_scrollTop = widget()->getVisibleTop();
}

StickersBox::StickersBox(
	QWidget*,
	std::shared_ptr<ChatHelpers::Show> show,
	Section section,
	bool masks)
: _st(st::stickersRowItem)
, _show(std::move(show))
, _session(&_show->session())
, _api(&_session->mtp())
, _tabs(this, st::stickersTabs)
, _unreadBadge(
	this,
	_session->data().stickers().featuredSetsUnreadCountValue())
, _section(section)
, _isMasks(masks)
, _isEmoji(false)
, _installed(_isMasks ? Tab() : Tab(0, this, _show, Section::Installed))
, _masks(_isMasks ? Tab(0, this, _show, Section::Masks) : Tab())
, _featured(_isMasks ? Tab() : Tab(1, this, _show, Section::Featured))
, _archived((_isMasks ? 1 : 2), this, _show, Section::Archived) {
	_tabs->setRippleTopRoundRadius(st::boxRadius);
}

StickersBox::StickersBox(
	QWidget*,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<ChannelData*> megagroup,
	bool isEmoji)
: _st(st::stickersRowItem)
, _show(std::move(show))
, _session(&_show->session())
, _api(&_session->mtp())
, _section(Section::Installed)
, _isMasks(false)
, _isEmoji(isEmoji)
, _installed(0, this, _show, megagroup, isEmoji)
, _megagroupSet(megagroup) {
	_installed.widget()->scrollsToY(
	) | rpl::start_with_next([=](int y) {
		scrollToY(y);
	}, lifetime());
}

StickersBox::StickersBox(
	QWidget*,
	std::shared_ptr<ChatHelpers::Show> show,
	const QVector<MTPStickerSetCovered> &attachedSets)
: _st(st::stickersRowItem)
, _show(std::move(show))
, _session(&_show->session())
, _api(&_session->mtp())
, _section(Section::Attached)
, _isMasks(false)
, _isEmoji(false)
, _attached(0, this, _show, Section::Attached)
, _attachedType(Data::StickersType::Stickers)
, _attachedSets(attachedSets) {
}

StickersBox::StickersBox(
	QWidget*,
	std::shared_ptr<ChatHelpers::Show> show,
	const std::vector<StickerSetIdentifier> &emojiSets)
: _st(st::stickersRowItem)
, _show(std::move(show))
, _session(&_show->session())
, _api(&_session->mtp())
, _section(Section::Attached)
, _isMasks(false)
, _isEmoji(true)
, _attached(0, this, _show, Section::Attached)
, _attachedType(Data::StickersType::Emoji)
, _emojiSets(emojiSets) {
}

Main::Session &StickersBox::session() const {
	return *_session;
}

void StickersBox::showAttachedStickers() {
	const auto stickers = &session().data().stickers();

	auto addedSet = false;
	const auto add = [&](not_null<StickersSet*> set) {
		if (_attached.widget()->appendSet(set)) {
			addedSet = true;
			if (set->stickers.isEmpty()
				|| (set->flags & SetFlag::NotLoaded)) {
				session().api().scheduleStickerSetRequest(
					set->id,
					set->accessHash);
			}
		}
	};
	for (const auto &set : _attachedSets) {
		add(stickers->feedSet(set));
	}
	for (const auto &setId : _emojiSets) {
		const auto i = stickers->sets().find(setId.id);
		if (i != end(stickers->sets())) {
			add(i->second.get());
		}
	}
	if (addedSet) {
		_attached.widget()->updateSize();
	}

	if (_section == Section::Attached && addedSet) {
		session().api().requestStickerSets();
	}
}

void StickersBox::getArchivedDone(
		const MTPmessages_ArchivedStickers &result,
		uint64 offsetId) {
	_archivedRequestId = 0;
	_archivedLoaded = true;
	if (result.type() != mtpc_messages_archivedStickers) {
		return;
	}

	auto &stickers = result.c_messages_archivedStickers();
	auto &archived = archivedSetsOrderRef();
	if (offsetId) {
		auto index = archived.indexOf(offsetId);
		if (index >= 0) {
			archived = archived.mid(0, index + 1);
		}
	} else {
		archived.clear();
	}

	auto addedSet = false;
	auto changedSets = false;
	for (const auto &data : stickers.vsets().v) {
		const auto set = session().data().stickers().feedSet(data);
		const auto index = archived.indexOf(set->id);
		if (archived.isEmpty() || index != archived.size() - 1) {
			changedSets = true;
			if (index >= 0 && index < archived.size() - 1) {
				archived.removeAt(index);
			}
			archived.push_back(set->id);
		}
		if (_archived.widget()->appendSet(set)) {
			addedSet = true;
			if (set->flags & SetFlag::NotLoaded) {
				session().api().scheduleStickerSetRequest(
					set->id,
					set->accessHash);
			}
		}
	}
	if (addedSet) {
		_archived.widget()->updateSize();
	} else {
		_allArchivedLoaded = stickers.vsets().v.isEmpty()
			|| (!changedSets && offsetId != 0);
		if (changedSets) {
			loadMoreArchived();
		}
	}

	refreshTabs();
	_someArchivedLoaded = true;
	if (_section == Section::Archived && addedSet) {
		session().api().requestStickerSets();
	}
}

void StickersBox::prepare() {
	if (_section == Section::Installed) {
		if (_tabs) {
			if (_isMasks) {
				session().local().readArchivedMasks();
			} else {
				session().local().readArchivedStickers();
			}
		} else {
			setTitle(_isEmoji
				? tr::lng_emoji_group_set()
				: tr::lng_stickers_group_set());
		}
	} else if (_section == Section::Archived) {
		requestArchivedSets();
	} else if (_section == Section::Attached) {
		setTitle(_attachedType == Data::StickersType::Emoji
			? tr::lng_custom_emoji_used_sets()
			: tr::lng_stickers_attached_sets());
	}
	if (_tabs) {
		if (archivedSetsOrder().isEmpty()) {
			preloadArchivedSets();
		}
		setNoContentMargin(true);
		_tabs->sectionActivated(
		) | rpl::filter([=] {
			return !_ignoreTabActivation;
		}) | rpl::start_with_next(
			[this] { switchTab(); },
			lifetime());
		refreshTabs();
	}
	if (_installed.widget() && _section != Section::Installed) {
		_installed.widget()->hide();
	}
	if (_masks.widget() && _section != Section::Masks) {
		_masks.widget()->hide();
	}
	if (_featured.widget() && _section != Section::Featured) {
		_featured.widget()->hide();
	}
	if (_archived.widget() && _section != Section::Archived) {
		_archived.widget()->hide();
	}
	if (_attached.widget() && _section != Section::Attached) {
		_attached.widget()->hide();
	}

	{
		const auto installCallback = [=](uint64 setId) { installSet(setId); };
		const auto markAsInstalledCallback = [=](uint64 setId) {
			if (_installed.widget()) {
				_installed.widget()->setRowRemovedBySetId(setId, false);
			}
			if (_featured.widget()) {
				_featured.widget()->setRowRemovedBySetId(setId, false);
			}
		};
		const auto markAsRemovedCallback = [=](uint64 setId) {
			if (_installed.widget()) {
				_installed.widget()->setRowRemovedBySetId(setId, true);
			}
			if (_featured.widget()) {
				_featured.widget()->setRowRemovedBySetId(setId, true);
			}
		};
		if (const auto installed = _installed.widget()) {
			installed->setInstallSetCallback(markAsInstalledCallback);
			installed->setRemoveSetCallback(markAsRemovedCallback);
		}
		if (const auto featured = _featured.widget()) {
			featured->setInstallSetCallback([=](uint64 setId) {
				installCallback(setId);
				markAsInstalledCallback(setId);
			});
			featured->setRemoveSetCallback(markAsRemovedCallback);
		}
		if (const auto archived = _archived.widget()) {
			archived->setInstallSetCallback(installCallback);
			archived->setLoadMoreCallback([=] { loadMoreArchived(); });
		}
		if (const auto attached = _attached.widget()) {
			attached->setInstallSetCallback(installCallback);
			attached->setLoadMoreCallback([=] { showAttachedStickers(); });
		}
	}

	if (_megagroupSet) {
		addButton(tr::lng_settings_save(), [=] {
			_installed.widget()->saveGroupSet(crl::guard(this, [=] {
				closeBox();
			}));
		});
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	} else {
		const auto close = _section == Section::Attached;
		addButton(
			close ? tr::lng_close() : tr::lng_about_done(),
			[=] { closeBox(); });
	}

	if (_section == Section::Installed) {
		_tab = &_installed;
	} else if (_section == Section::Masks) {
		_tab = &_masks;
	} else if (_section == Section::Archived) {
		_tab = &_archived;
	} else if (_section == Section::Attached) {
		_tab = &_attached;
	} else { // _section == Section::Featured
		_tab = &_featured;
	}
	setInnerWidget(_tab->takeWidget(), topSkip());
	setDimensions(st::boxWideWidth, st::boxMaxListHeight);

	session().data().stickers().updated(_isEmoji
		? Data::StickersType::Emoji
		: _isMasks
		? Data::StickersType::Masks
		: Data::StickersType::Stickers
	) | rpl::start_with_next([=] {
		handleStickersUpdated();
	}, lifetime());

	if (_isEmoji) {
		session().api().updateCustomEmoji();
	} else if (_isMasks) {
		session().api().updateMasks();
	} else {
		session().api().updateStickers();
	}

	for (const auto &widget : { _installed.widget(), _masks.widget() }) {
		if (widget) {
			widget->draggingScrollDelta(
			) | rpl::start_with_next([=](int delta) {
				scrollByDraggingDelta(delta);
			}, widget->lifetime());
		}
	}
	if (!_megagroupSet) {
		boxClosing() | rpl::start_with_next([=] {
			saveChanges();
		}, lifetime());
	}

	if (_tabs) {
		_tabs->raise();
		_unreadBadge->raise();
	}
	rebuildList();
}

void StickersBox::refreshTabs() {
	if (!_tabs) {
		return;
	}

	auto &stickers = session().data().stickers();

	_tabIndices.clear();
	auto sections = std::vector<QString>();
	if (_installed.widget()) {
		sections.push_back(tr::lng_stickers_installed_tab(tr::now));
		_tabIndices.push_back(Section::Installed);
	}
	if (_masks.widget()) {
		sections.push_back(tr::lng_stickers_masks_tab(tr::now));
		_tabIndices.push_back(Section::Masks);
	}
	if (!stickers.featuredSetsOrder().isEmpty() && _featured.widget()) {
		sections.push_back(tr::lng_stickers_featured_tab(tr::now));
		_tabIndices.push_back(Section::Featured);
	}
	if (!archivedSetsOrder().isEmpty() && _archived.widget()) {
		sections.push_back(tr::lng_stickers_archived_tab(tr::now));
		_tabIndices.push_back(Section::Archived);
	}
	_tabs->setSections(sections);
	if ((_tab == &_archived && !_tabIndices.contains(Section::Archived))
		|| (_tab == &_featured && !_tabIndices.contains(Section::Featured))
		|| (_tab == &_masks && !_tabIndices.contains(Section::Masks))) {
		switchTab();
	} else {
		_ignoreTabActivation = true;
		_tabs->setActiveSectionFast(_tabIndices.indexOf((_tab == &_archived)
			? Section::Archived
			: (_tab == &_featured)
			? Section::Featured
			: (_tab == &_masks)
			? Section::Masks
			: Section::Installed));
		_ignoreTabActivation = false;
	}
	updateTabsGeometry();
}

void StickersBox::loadMoreArchived() {
	if (_section != Section::Archived
		|| _allArchivedLoaded
		|| _archivedRequestId) {
		return;
	}

	uint64 lastId = 0;
	const auto &order = archivedSetsOrder();
	const auto &sets = session().data().stickers().sets();
	for (auto setIt = order.cend(), e = order.cbegin(); setIt != e;) {
		--setIt;
		auto it = sets.find(*setIt);
		if (it != sets.cend()) {
			if (it->second->flags & SetFlag::Archived) {
				lastId = it->second->id;
				break;
			}
		}
	}
	const auto flags = _isMasks
		? MTPmessages_GetArchivedStickers::Flag::f_masks
		: MTPmessages_GetArchivedStickers::Flags(0);
	_archivedRequestId = _api.request(MTPmessages_GetArchivedStickers(
		MTP_flags(flags),
		MTP_long(lastId),
		MTP_int(kArchivedLimitPerPage)
	)).done([=](const MTPmessages_ArchivedStickers &result) {
		getArchivedDone(result, lastId);
	}).send();
}

void StickersBox::paintEvent(QPaintEvent *e) {
	BoxContent::paintEvent(e);

	Painter p(this);

	if (_slideAnimation) {
		_slideAnimation->paintFrame(p, 0, topSkip(), width());
		if (!_slideAnimation->animating()) {
			_slideAnimation.reset();
			setInnerVisible(true);
			update();
		}
	}
}

void StickersBox::updateTabsGeometry() {
	if (!_tabs) return;

	const auto maxTabs = _isMasks ? 2 : 3;

	_tabs->resizeToWidth(_tabIndices.size() * width() / maxTabs);
	_unreadBadge->setVisible(_tabIndices.contains(Section::Featured));

	setInnerTopSkip(topSkip());

	auto featuredLeft = width() / maxTabs;
	auto featuredRight = 2 * width() / maxTabs;
	auto featuredTextWidth = st::stickersTabs.labelStyle.font->width(tr::lng_stickers_featured_tab(tr::now));
	auto featuredTextRight = featuredLeft + (featuredRight - featuredLeft - featuredTextWidth) / 2 + featuredTextWidth;
	auto unreadBadgeLeft = featuredTextRight - st::stickersFeaturedBadgeSkip;
	auto unreadBadgeTop = st::stickersFeaturedBadgeTop;
	if (unreadBadgeLeft + _unreadBadge->width() > featuredRight) {
		unreadBadgeLeft = featuredRight - _unreadBadge->width();
	}
	_unreadBadge->moveToLeft(unreadBadgeLeft, unreadBadgeTop);

	_tabs->moveToLeft(0, 0);
}

int StickersBox::topSkip() const {
	return _tabs ? (_tabs->height() - st::lineWidth) : 0;
}

void StickersBox::switchTab() {
	if (!_tabs) return;

	auto tab = _tabs->activeSection();
	Assert(tab >= 0 && tab < _tabIndices.size());
	auto newSection = _tabIndices[tab];

	auto newTab = _tab;
	if (newSection == Section::Installed) {
		newTab = &_installed;
	} else if (newSection == Section::Featured) {
		newTab = &_featured;
	} else if (newSection == Section::Archived) {
		newTab = &_archived;
		requestArchivedSets();
	} else if (newSection == Section::Masks) {
		newTab = &_masks;
		session().api().updateMasks();
	}
	if (_tab == newTab) {
		scrollToY(0);
		return;
	}

	if (_tab == &_installed) {
		_localOrder = _tab->widget()->fullOrder();
		_localRemoved = _tab->widget()->removedSets();
	}
	auto wasCache = grabContentCache();
	auto wasIndex = _tab->index();
	_tab->saveScrollTop();
	auto widget = takeInnerWidget<Inner>();
	widget->setParent(this);
	widget->hide();
	_tab->returnWidget(std::move(widget));
	_tab = newTab;
	_section = newSection;
	setInnerWidget(_tab->takeWidget(), topSkip());
	_tabs->raise();
	_unreadBadge->raise();
	_tab->widget()->show();
	rebuildList();
	scrollToY(_tab->scrollTop());
	setInnerVisible(true);
	auto nowCache = grabContentCache();
	auto nowIndex = _tab->index();

	_slideAnimation = std::make_unique<Ui::SlideAnimation>();
	_slideAnimation->setSnapshots(std::move(wasCache), std::move(nowCache));
	auto slideLeft = wasIndex > nowIndex;
	_slideAnimation->start(slideLeft, [=] { update(); }, st::slideDuration);
	setInnerVisible(false);

	setFocus();
	update();
}

QPixmap StickersBox::grabContentCache() {
	_tabs->hide();
	auto result = grabInnerCache();
	_tabs->show();
	return result;
}

std::array<StickersBox::Inner*, 5> StickersBox::widgets() const {
	return {
		_installed.widget(),
		_featured.widget(),
		_archived.widget(),
		_attached.widget(),
		_masks.widget()
	};
}

void StickersBox::installSet(uint64 setId) {
	const auto &sets = session().data().stickers().sets();
	const auto it = sets.find(setId);
	if (it == sets.cend()) {
		rebuildList();
		return;
	}

	const auto set = it->second.get();
	if (_localRemoved.contains(setId)) {
		_localRemoved.removeOne(setId);
		for (const auto &widget : widgets()) {
			if (widget) {
				widget->setRemovedSets(_localRemoved);
			}
		}
	}
	if (!(set->flags & SetFlag::Installed)
		|| (set->flags & SetFlag::Archived)) {
		_api.request(MTPmessages_InstallStickerSet(
			set->mtpInput(),
			MTP_boolFalse()
		)).done([=](const MTPmessages_StickerSetInstallResult &result) {
			installDone(result);
		}).fail([=](const MTP::Error &error) {
			installFail(error, setId);
		}).send();

		session().data().stickers().installLocally(setId);
	}
}

void StickersBox::installDone(
		const MTPmessages_StickerSetInstallResult &result) const {
	if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
		session().data().stickers().applyArchivedResult(
			result.c_messages_stickerSetInstallResultArchive());
	}
}

void StickersBox::installFail(const MTP::Error &error, uint64 setId) {
	const auto &sets = session().data().stickers().sets();
	const auto it = sets.find(setId);
	if (it == sets.cend()) {
		rebuildList();
	} else {
		session().data().stickers().undoInstallLocally(setId);
	}
}

void StickersBox::preloadArchivedSets() {
	if (!_tabs) {
		return;
	}
	if (!_archivedRequestId) {
		const auto flags = _isMasks
			? MTPmessages_GetArchivedStickers::Flag::f_masks
			: MTPmessages_GetArchivedStickers::Flags(0);
		_archivedRequestId = _api.request(MTPmessages_GetArchivedStickers(
			MTP_flags(flags),
			MTP_long(0),
			MTP_int(kArchivedLimitFirstRequest)
		)).done([=](const MTPmessages_ArchivedStickers &result) {
			getArchivedDone(result, 0);
		}).send();
	}
}

void StickersBox::requestArchivedSets() {
	// Reload the archived list.
	if (!_archivedLoaded) {
		preloadArchivedSets();
	}

	const auto &sets = session().data().stickers().sets();
	const auto &order = archivedSetsOrder();
	for (const auto setId : order) {
		auto it = sets.find(setId);
		if (it != sets.cend()) {
			const auto set = it->second.get();
			if (set->stickers.isEmpty()
				&& (set->flags & SetFlag::NotLoaded)) {
				session().api().scheduleStickerSetRequest(
					setId,
					set->accessHash);
			}
		}
	}
	session().api().requestStickerSets();
}

void StickersBox::resizeEvent(QResizeEvent *e) {
	BoxContent::resizeEvent(e);

	if (_tabs) {
		updateTabsGeometry();
	}
	if (_titleShadow) {
		_titleShadow->setGeometry(0, 0, width(), st::lineWidth);
	}
	for (const auto &widget : widgets()) {
		if (widget) {
			widget->resize(width(), widget->height());
		}
	}
}

void StickersBox::handleStickersUpdated() {
	if (_section == Section::Installed
		|| _section == Section::Featured
		|| _section == Section::Masks) {
		rebuildList();
	} else {
		_tab->widget()->updateRows();
	}
	if (archivedSetsOrder().isEmpty()) {
		preloadArchivedSets();
	} else {
		refreshTabs();
	}
}

void StickersBox::rebuildList(Tab *tab) {
	if (_section == Section::Attached) {
		return;
	}
	if (!tab) {
		tab = _tab;
	}

	if ((tab == &_installed) || (tab == &_masks) || (_tab == &_featured)) {
		_localOrder = tab->widget()->fullOrder();
		_localRemoved = tab->widget()->removedSets();
	}
	tab->widget()->rebuild(_isMasks);
	if ((tab == &_installed) || (tab == &_masks) || (_tab == &_featured)) {
		tab->widget()->setFullOrder(_localOrder);
	}
	tab->widget()->setRemovedSets(_localRemoved);
}

void StickersBox::saveChanges() {
	const auto installed = _installed.widget();
	const auto masks = _masks.widget();

	// Make sure that our changes in other tabs are applied in the Installed tab.
	if (installed) {
		rebuildList(&_installed);
	}
	if (masks) {
		rebuildList(&_masks);
	}

	if (_someArchivedLoaded) {
		if (_isMasks) {
			session().local().writeArchivedMasks();
		} else {
			session().local().writeArchivedStickers();
		}
	}
	if (installed) {
		session().api().saveStickerSets(
			installed->order(),
			installed->removedSets(),
			Data::StickersType::Stickers);
	}
	if (masks) {
		session().api().saveStickerSets(
			masks->order(),
			masks->removedSets(),
			Data::StickersType::Masks);
	}
}

void StickersBox::setInnerFocus() {
	if (_megagroupSet) {
		_installed.widget()->setInnerFocus();
	} else {
		BoxContent::setInnerFocus();
	}
}

const Data::StickersSetsOrder &StickersBox::archivedSetsOrder() const {
	return !_isMasks
		? session().data().stickers().archivedSetsOrder()
		: session().data().stickers().archivedMaskSetsOrder();
}

Data::StickersSetsOrder &StickersBox::archivedSetsOrderRef() const {
	return !_isMasks
		? session().data().stickers().archivedSetsOrderRef()
		: session().data().stickers().archivedMaskSetsOrderRef();
}

StickersBox::~StickersBox() = default;

StickersBox::Inner::Row::Row(
	not_null<StickersSet*> set,
	DocumentData *sticker,
	int32 count,
	const QString &title,
	int titleWidth,
	Data::StickersSetFlags flagsOverride,
	bool removed,
	int32 pixw,
	int32 pixh)
: set(set)
, sticker(sticker)
, count(count)
, title(title)
, titleWidth(titleWidth)
, flagsOverride(flagsOverride)
, removed(removed)
, pixw(pixw)
, pixh(pixh) {
	++set->locked;
}

StickersBox::Inner::Row::~Row() {
	if (!--set->locked) {
		const auto installed = !!(set->flags & SetFlag::Installed);
		const auto featured = !!(set->flags & SetFlag::Featured);
		const auto special = !!(set->flags & SetFlag::Special);
		const auto archived = !!(set->flags & SetFlag::Archived);
		const auto emoji = !!(set->flags & SetFlag::Emoji);
		if (!installed && !featured && !special && !archived && !emoji) {
			auto &sets = set->owner().stickers().setsRef();
			if (const auto i = sets.find(set->id); i != end(sets)) {
				sets.erase(i);
			}
		}
	}
}

bool StickersBox::Inner::Row::isRecentSet() const {
	return (set->id == Data::Stickers::CloudRecentSetId)
		|| (set->id == Data::Stickers::CloudRecentAttachedSetId);
}

bool StickersBox::Inner::Row::isMasksSet() const {
	return (set->type() == Data::StickersType::Masks);
}

bool StickersBox::Inner::Row::isEmojiSet() const {
	return (set->type() == Data::StickersType::Emoji);
}

bool StickersBox::Inner::Row::isInstalled() const {
	return (flagsOverride & SetFlag::Installed);
}

bool StickersBox::Inner::Row::isUnread() const {
	return (flagsOverride & SetFlag::Unread);
}

bool StickersBox::Inner::Row::isArchived() const {
	return (flagsOverride & SetFlag::Archived);
}

StickersBox::Inner::Inner(
	QWidget *parent,
	std::shared_ptr<ChatHelpers::Show> show,
	StickersBox::Section section)
: RpWidget(parent)
, _st(st::stickersRowItem)
, _show(std::move(show))
, _session(&_show->session())
, _api(&_session->mtp())
, _section(section)
, _isInstalledTab(_section == Section::Installed
	|| _section == Section::Masks)
, _buttonBgOver(
	ImageRoundRadius::Large,
	(_isInstalledTab
		? st::stickersUndoRemove
		: st::stickersTrendingAdd).textBgOver)
, _buttonBg(
	ImageRoundRadius::Large,
	(_isInstalledTab
		? st::stickersUndoRemove
		: st::stickersTrendingAdd).textBg)
, _inactiveButtonBg(
	ImageRoundRadius::Large,
	st::stickersTrendingInstalled.textBg)
, _rowHeight(_st.height)
, _shiftingAnimation([=](crl::time now) {
	return shiftingAnimationCallback(now);
})
, _itemsTop(st::lineWidth)
, _addText(tr::lng_stickers_featured_add(tr::now))
, _addWidth(st::stickersTrendingAdd.style.font->width(_addText))
, _undoText(tr::lng_stickers_return(tr::now))
, _undoWidth(st::stickersUndoRemove.style.font->width(_undoText))
, _installedText(tr::lng_stickers_featured_installed(tr::now))
, _installedWidth(st::stickersTrendingInstalled.style.font->width(
		_installedText)) {
	setup();
}

StickersBox::Inner::Inner(
	QWidget *parent,
	std::shared_ptr<ChatHelpers::Show> show,
	not_null<ChannelData*> megagroup,
	bool isEmoji)
: RpWidget(parent)
, _st(st::stickersRowItem)
, _show(std::move(show))
, _session(&_show->session())
, _api(&_session->mtp())
, _section(StickersBox::Section::Installed)
, _isInstalledTab(_section == Section::Installed
	|| _section == Section::Masks)
, _buttonBgOver(
	ImageRoundRadius::Large,
	(_isInstalledTab
		? st::stickersUndoRemove
		: st::stickersTrendingAdd).textBgOver)
, _buttonBg(
	ImageRoundRadius::Large,
	(_isInstalledTab
		? st::stickersUndoRemove
		: st::stickersTrendingAdd).textBg)
, _inactiveButtonBg(
	ImageRoundRadius::Large,
	st::stickersTrendingInstalled.textBg)
, _rowHeight(_st.height)
, _shiftingAnimation([=](crl::time now) {
	return shiftingAnimationCallback(now);
})
, _itemsTop(st::lineWidth)
, _megagroupSet(megagroup)
, _megagroupSetEmoji(isEmoji)
, _megagroupSetInput(isEmoji
	? _megagroupSet->mgInfo->emojiSet
	: _megagroupSet->mgInfo->stickerSet)
, _megagroupSetField(
	this,
	st::groupStickersField,
	rpl::single(isEmoji ? u"emojipack"_q : u"stickerset"_q),
	QString(),
	_session->createInternalLink(QString()))
, _megagroupDivider(this)
, _megagroupSubTitle(
		this,
		(isEmoji
			? tr::lng_emoji_group_from_your
			: tr::lng_stickers_group_from_your)(tr::now),
		st::boxTitle) {
	_megagroupSetField->setLinkPlaceholder(
		_session->createInternalLink(
			isEmoji ? u"addemoji/"_q : u"addstickers/"_q));
	_megagroupSetField->setPlaceholderHidden(false);
	_megagroupSetAddressChangedTimer.setCallback([this] {
		handleMegagroupSetAddressChange();
	});
	connect(
		_megagroupSetField,
		&Ui::MaskedInputField::changed,
		[=] {
			_megagroupSetAddressChangedTimer.callOnce(
				kHandleMegagroupSetAddressChangeTimeout);
		});
	connect(
		_megagroupSetField,
		&Ui::MaskedInputField::submitted,
		[=] {
			_megagroupSetAddressChangedTimer.cancel();
			handleMegagroupSetAddressChange();
		});

	setup();
}

Main::Session &StickersBox::Inner::session() const {
	return *_session;
}

void StickersBox::Inner::setup() {
	session().downloaderTaskFinished(
	) | rpl::start_with_next([=] {
		update();
		readVisibleSets();
	}, lifetime());

	setMouseTracking(true);
}

void StickersBox::Inner::setInnerFocus() {
	if (_megagroupSetField) {
		_megagroupSetField->setFocusFast();
	}
}

void StickersBox::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto clip = e->rect();
	p.fillRect(clip, st::boxBg);
	p.setClipRect(clip);

	if (_megagroupSelectedSet) {
		auto setTop = _megagroupDivider->y() - _rowHeight;
		p.translate(0, setTop);
		paintRow(p, _megagroupSelectedSet.get(), -1);
		p.translate(0, -setTop);
	}

	auto y = _itemsTop;
	if (_rows.empty()) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, y, width(), st::noContactsHeight), tr::lng_contacts_loading(tr::now), style::al_center);
	} else {
		p.translate(0, _itemsTop);

		int32 yFrom = clip.y() - _itemsTop, yTo = clip.y() + clip.height() - _itemsTop;
		int32 from = floorclamp(yFrom - _rowHeight, _rowHeight, 0, _rows.size());
		int32 to = ceilclamp(yTo + _rowHeight, _rowHeight, 0, _rows.size());
		p.translate(0, from * _rowHeight);
		for (int32 i = from; i < to; ++i) {
			if (i != _above) {
				paintRow(p, _rows[i].get(), i);
			}
			p.translate(0, _rowHeight);
		}
		if (from <= _above && _above < to) {
			p.translate(0, (_above - to) * _rowHeight);
			paintRow(p, _rows[_above].get(), _above);
		}
	}
}

void StickersBox::Inner::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void StickersBox::Inner::updateControlsGeometry() {
	if (_megagroupSet) {
		auto top = st::groupStickersFieldPadding.top();
		auto fieldLeft = st::boxTitlePosition.x();
		_megagroupSetField->setGeometryToLeft(fieldLeft, top, width() - fieldLeft - st::groupStickersFieldPadding.right(), _megagroupSetField->height());
		top += _megagroupSetField->height() + st::groupStickersFieldPadding.bottom();
		if (_megagroupSelectedRemove) {
			_megagroupSelectedShadow->setGeometryToLeft(0, top, width(), st::lineWidth);
			top += st::lineWidth;
			_megagroupSelectedRemove->moveToRight(st::groupStickersRemovePosition.x(), top + st::groupStickersRemovePosition.y());
			top += _rowHeight;
		}
		_megagroupDivider->setGeometryToLeft(0, top, width(), _megagroupDivider->height());
		top += _megagroupDivider->height();
		_megagroupSubTitle->resizeToNaturalWidth(width() - 2 * st::boxTitlePosition.x());
		_megagroupSubTitle->moveToLeft(st::boxTitlePosition.x(), top + st::boxTitlePosition.y());
	}
}

QRect StickersBox::Inner::relativeButtonRect(
		bool removeButton,
		bool installedSet) const {
	auto buttonw = st::stickersRemove.width;
	auto buttonh = st::stickersRemove.height;
	auto buttonshift = st::stickersRemoveSkip;
	if (!removeButton) {
		const auto &st = installedSet
			? st::stickersTrendingInstalled
			: _isInstalledTab
			? st::stickersUndoRemove
			: st::stickersTrendingAdd;
		const auto textWidth = installedSet
			? _installedWidth
			: _isInstalledTab
			? _undoWidth
			: _addWidth;
		buttonw = textWidth - st.width;
		buttonh = st.height;
		buttonshift = 0;
	}
	auto buttonx = width() - st::contactsPadding.right() - buttonw + buttonshift;
	auto buttony = (_st.height - buttonh) / 2;
	return QRect(buttonx, buttony, buttonw, buttonh);
}

void StickersBox::Inner::paintRow(Painter &p, not_null<Row*> row, int index) {
	auto xadd = 0, yadd = qRound(row->yadd.current());
	if (xadd || yadd) p.translate(xadd, yadd);

	if (_megagroupSet) {
		auto selectedIndex = [&] {
			if (auto index = std::get_if<int>(&_selected)) {
				return *index;
			}
			return -1;
		}();
		if (index >= 0 && index == selectedIndex) {
			p.fillRect(0, 0, width(), _rowHeight, _st.button.textBgOver);
			if (row->ripple) {
				row->ripple->paint(p, 0, 0, width());
			}
		}
	}

	if (_isInstalledTab) {
		if (index >= 0 && index == _above) {
			auto current = _aboveShadowFadeOpacity.current();
			if (_started >= 0) {
				auto reachedOpacity = aboveShadowOpacity();
				if (reachedOpacity > current) {
					_aboveShadowFadeOpacity = anim::value(reachedOpacity, reachedOpacity);
					current = reachedOpacity;
				}
			}
			auto rect = myrtlrect(_st.photoPosition.x() / 2, _st.photoPosition.y() / 2, width() - _st.photoPosition.x() - _scrollbar, _rowHeight - _st.photoPosition.y());
			p.setOpacity(current);
			Ui::Shadow::paint(p, rect, width(), st::boxRoundShadow);
			p.setOpacity(1);

			Ui::FillRoundRect(p, rect, st::boxBg, Ui::BoxCorners);

			p.setOpacity(1. - current);
			paintFakeButton(p, row, index);
			p.setOpacity(1.);
		} else if (!_megagroupSet) {
			paintFakeButton(p, row, index);
		}
	} else if (!_megagroupSet) {
		paintFakeButton(p, row, index);
	}

	if (row->removed && _isInstalledTab) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	}

	auto stickerskip = 0;

	if (!_megagroupSet && _isInstalledTab) {
		stickerskip += st::stickersReorderIcon.width() + st::stickersReorderSkip;
		if (!row->isRecentSet()) {
			st::stickersReorderIcon.paint(p, _st.photoPosition.x(), (_rowHeight - st::stickersReorderIcon.height()) / 2, width());
		}
	}

	if (row->sticker) {
		paintRowThumbnail(p, row, stickerskip + _st.photoPosition.x());
	}

	int namex = stickerskip + _st.namePosition.x();
	int namey = _st.namePosition.y();

	int statusx = stickerskip + _st.statusPosition.x();
	int statusy = _st.statusPosition.y();

	p.setFont(st::contactsNameStyle.font);
	p.setPen(_st.nameFg);
	p.drawTextLeft(namex, namey, width(), row->title, row->titleWidth);

	if (row->isUnread()) {
		p.setPen(Qt::NoPen);
		p.setBrush(st::stickersFeaturedUnreadBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(style::rtlrect(namex + row->titleWidth + st::stickersFeaturedUnreadSkip, namey + st::stickersFeaturedUnreadTop, st::stickersFeaturedUnreadSize, st::stickersFeaturedUnreadSize, width()));
		}
	}

	const auto statusText = (row->count == 0)
		? tr::lng_contacts_loading(tr::now)
		: row->isEmojiSet()
		? tr::lng_custom_emoji_count(tr::now, lt_count, row->count)
		: row->isMasksSet()
		? tr::lng_masks_count(tr::now, lt_count, row->count)
		: tr::lng_stickers_count(tr::now, lt_count, row->count);

	p.setFont(st::contactsStatusFont);
	p.setPen(_st.statusFg);
	p.drawTextLeft(statusx, statusy, width(), statusText);

	p.setOpacity(1);
	if (xadd || yadd) p.translate(-xadd, -yadd);
}

void StickersBox::Inner::paintRowThumbnail(
		Painter &p,
		not_null<Row*> row,
		int left) {
	const auto origin = Data::FileOriginStickerSet(
		row->set->id,
		row->set->accessHash);
	if (row->set->hasThumbnail()) {
		if (!row->thumbnailMedia) {
			row->thumbnailMedia = row->set->createThumbnailView();
			row->set->loadThumbnail();
		}
	} else if (row->sticker) {
		if (!row->stickerMedia) {
			row->stickerMedia = row->sticker->createMediaView();
			row->stickerMedia->thumbnailWanted(origin);
		}
	}
	validateAnimation(row);
	const auto thumb = row->thumbnailMedia
		? row->thumbnailMedia->image()
		: row->stickerMedia
		? row->stickerMedia->thumbnail()
		: nullptr;
	const auto paused = _show->paused(ChatHelpers::PauseReason::Layer);
	const auto x = left + (_st.photoSize - row->pixw) / 2;
	const auto y = _st.photoPosition.y() + (_st.photoSize - row->pixh) / 2;
	if (row->lottie && row->lottie->ready()) {
		const auto frame = row->lottie->frame();
		const auto size = frame.size() / style::DevicePixelRatio();
		p.drawImage(
			QRect(
				left + (_st.photoSize - size.width()) / 2,
				_st.photoPosition.y() + (_st.photoSize - size.height()) / 2,
				size.width(),
				size.height()),
			frame);
		if (!paused) {
			row->lottie->markFrameShown();
		}
	} else if (row->webm && row->webm->started()) {
		p.drawImage(
			x,
			y,
			row->webm->current(
				{ .frame = { row->pixw, row->pixh }, .keepAlpha = true },
				paused ? 0 : crl::now()));
	} else if (thumb) {
		p.drawPixmapLeft(
			x,
			y,
			width(),
			thumb->pix(row->pixw, row->pixh));
	}
}

void StickersBox::Inner::validateLottieAnimation(not_null<Row*> row) {
	if (row->lottie
		|| !ChatHelpers::HasLottieThumbnail(
			row->set->thumbnailType(),
			row->thumbnailMedia.get(),
			row->stickerMedia.get())) {
		return;
	}
	auto player = ChatHelpers::LottieThumbnail(
		row->thumbnailMedia.get(),
		row->stickerMedia.get(),
		ChatHelpers::StickerLottieSize::SetsListThumbnail,
		QSize(_st.photoSize, _st.photoSize) * style::DevicePixelRatio());
	if (!player) {
		return;
	}
	row->lottie = std::move(player);
	row->lottie->updates(
	) | rpl::start_with_next([=] {
		updateRowThumbnail(row);
	}, lifetime());
}

void StickersBox::Inner::validateWebmAnimation(not_null<Row*> row) {
	if (row->webm
		|| !ChatHelpers::HasWebmThumbnail(
			row->set->thumbnailType(),
			row->thumbnailMedia.get(),
			row->stickerMedia.get())) {
		return;
	}
	auto callback = [=](Media::Clip::Notification notification) {
		clipCallback(row, notification);
	};
	row->webm = ChatHelpers::WebmThumbnail(
		row->thumbnailMedia.get(),
		row->stickerMedia.get(),
		std::move(callback));
}

void StickersBox::Inner::clipCallback(
		not_null<Row*> row,
		Media::Clip::Notification notification) {
	using namespace Media::Clip;
	switch (notification) {
	case Notification::Reinit: {
		if (!row->webm) {
			return;
		} else if (row->webm->state() == State::Error) {
			row->webm.setBad();
		} else if (row->webm->ready() && !row->webm->started()) {
			row->webm->start({
				.frame = { row->pixw, row->pixh },
				.keepAlpha = true,
			});
		}
	} break;

	case Notification::Repaint: break;
	}
	updateRowThumbnail(row);
}

void StickersBox::Inner::validateAnimation(not_null<Row*> row) {
	validateWebmAnimation(row);
	validateLottieAnimation(row);
}

void StickersBox::Inner::updateRowThumbnail(not_null<Row*> row) {
	const auto rowTop = [&] {
		if (row == _megagroupSelectedSet.get()) {
			return _megagroupDivider->y() - _rowHeight;
		}
		auto top = _itemsTop;
		for (const auto &entry : _rows) {
			if (entry.get() == row) {
				return top + qRound(row->yadd.current());
			}
			top += _rowHeight;
		}
		Unexpected("StickersBox::Inner::updateRowThumbnail: row not found");
	}();
	const auto left = _st.photoPosition.x()
		+ ((!_megagroupSet && _isInstalledTab)
			? st::stickersReorderIcon.width() + st::stickersReorderSkip
			: 0);
	const auto top = rowTop + _st.photoPosition.y();
	update(left, top, _st.photoSize, _st.photoSize);
}

void StickersBox::Inner::paintFakeButton(Painter &p, not_null<Row*> row, int index) {
	const auto removeButton = (_isInstalledTab && !row->removed);
	if (!_isInstalledTab && row->isInstalled() && !row->isArchived() && !row->removed) {
		// Round button "Added" after installed from Trending or Archived.
		const auto rect = relativeButtonRect(removeButton, true);
		const auto &st = st::stickersTrendingInstalled;
		const auto textWidth = _installedWidth;
		const auto &text = _installedText;
		_inactiveButtonBg.paint(p, myrtlrect(rect));
		if (row->ripple) {
			row->ripple->paint(p, rect.x(), rect.y(), width());
			if (row->ripple->empty()) {
				row->ripple.reset();
			}
		}
		p.setFont(st.style.font);
		p.setPen(st.textFg);
		p.drawTextLeft(rect.x() - (st.width / 2), rect.y() + st.textTop, width(), text, textWidth);
	} else {
		const auto rect = relativeButtonRect(removeButton, false);
		auto selected = (index == _actionSel && _actionDown < 0) || (index == _actionDown);
		if (removeButton) {
			// Trash icon button when not disabled in Installed.
			if (row->ripple) {
				row->ripple->paint(p, rect.x(), rect.y(), width());
				if (row->ripple->empty()) {
					row->ripple.reset();
				}
			}
			auto &icon = selected ? st::stickersRemove.iconOver : st::stickersRemove.icon;
			auto position = st::stickersRemove.iconPosition;
			if (position.x() < 0) position.setX((rect.width() - icon.width()) / 2);
			if (position.y() < 0) position.setY((rect.height() - icon.height()) / 2);
			icon.paint(p, rect.topLeft() + position, width());
		} else {
			// Round button ADD when not installed from Trending or Archived.
			// Or round button UNDO after disabled from Installed.
			const auto &st = _isInstalledTab
				? st::stickersUndoRemove
				: st::stickersTrendingAdd;
			const auto textWidth = _isInstalledTab ? _undoWidth : _addWidth;
			const auto &text = _isInstalledTab ? _undoText : _addText;
			(selected ? _buttonBgOver : _buttonBg).paint(p, myrtlrect(rect));
			if (row->ripple) {
				row->ripple->paint(p, rect.x(), rect.y(), width());
				if (row->ripple->empty()) {
					row->ripple.reset();
				}
			}
			p.setFont(st.style.font);
			p.setPen(selected ? st.textFgOver : st.textFg);
			p.drawTextLeft(rect.x() - (st.width / 2), rect.y() + st.textTop, width(), text, textWidth);
		}
	}
}

void StickersBox::Inner::mousePressEvent(QMouseEvent *e) {
	if (_dragging >= 0) {
		mouseReleaseEvent(e);
	}
	_mouse = e->globalPos();
	updateSelected();

	setPressed(_selected);
	if (_actionSel >= 0) {
		setActionDown(_actionSel);
		update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
	} else if (auto selectedIndex = std::get_if<int>(&_selected)) {
		if (_isInstalledTab && !_rows[*selectedIndex]->isRecentSet() && _inDragArea) {
			_above = _dragging = _started = *selectedIndex;
			_dragStart = mapFromGlobal(_mouse);
		}
	}
}

void StickersBox::Inner::setActionDown(int newActionDown) {
	if (_actionDown == newActionDown) {
		return;
	}
	if (_actionDown >= 0 && _actionDown < _rows.size()) {
		update(0, _itemsTop + _actionDown * _rowHeight, width(), _rowHeight);
		const auto row = _rows[_actionDown].get();
		if (row->ripple) {
			row->ripple->lastStop();
		}
	}
	_actionDown = newActionDown;
	if (_actionDown >= 0 && _actionDown < _rows.size()) {
		update(0, _itemsTop + _actionDown * _rowHeight, width(), _rowHeight);
		const auto row = _rows[_actionDown].get();
		auto removeButton = (_isInstalledTab && !row->removed);
		if (!row->ripple) {
			if (_isInstalledTab) {
				if (row->removed) {
					auto rippleSize = QSize(_undoWidth - st::stickersUndoRemove.width, st::stickersUndoRemove.height);
					auto rippleMask = Ui::RippleAnimation::RoundRectMask(rippleSize, st::roundRadiusLarge);
					ensureRipple(st::stickersUndoRemove.ripple, std::move(rippleMask), removeButton, false);
				} else {
					auto rippleSize = st::stickersRemove.rippleAreaSize;
					auto rippleMask = Ui::RippleAnimation::EllipseMask(QSize(rippleSize, rippleSize));
					ensureRipple(st::stickersRemove.ripple, std::move(rippleMask), removeButton, false);
				}
			} else {
				const auto installedSet = row->isInstalled()
					&& !row->isArchived()
					&& !row->removed;
				const auto &st = installedSet
					? st::stickersTrendingInstalled
					: st::stickersTrendingAdd;
				const auto buttonTextWidth = installedSet
					? _installedWidth
					: _addWidth;
				auto rippleMask = Ui::RippleAnimation::RoundRectMask(
					QSize(buttonTextWidth - st.width, st.height),
					st::roundRadiusLarge);
				ensureRipple(
					st.ripple,
					std::move(rippleMask),
					removeButton,
					installedSet);
			}
		}
		if (row->ripple) {
			auto rect = relativeButtonRect(removeButton, false);
			row->ripple->add(mapFromGlobal(QCursor::pos()) - QPoint(myrtlrect(rect).x(), _itemsTop + _actionDown * _rowHeight + rect.y()));
		}
	}
}

void StickersBox::Inner::setSelected(SelectedRow selected) {
	if (_selected == selected) {
		return;
	}
	auto countSelectedIndex = [&] {
		if (auto index = std::get_if<int>(&_selected)) {
			return *index;
		}
		return -1;
	};
	auto selectedIndex = countSelectedIndex();
	if (_megagroupSet && selectedIndex >= 0 && selectedIndex < _rows.size()) {
		update(0, _itemsTop + selectedIndex * _rowHeight, width(), _rowHeight);
	}
	_selected = selected;
	updateCursor();
	selectedIndex = countSelectedIndex();
	if (_megagroupSet && selectedIndex >= 0 && selectedIndex < _rows.size()) {
		update(0, _itemsTop + selectedIndex * _rowHeight, width(), _rowHeight);
	}
}

void StickersBox::Inner::setPressed(SelectedRow pressed) {
	if (_pressed == pressed) {
		return;
	}
	auto countPressedIndex = [&] {
		if (auto index = std::get_if<int>(&_pressed)) {
			return *index;
		}
		return -1;
	};
	auto pressedIndex = countPressedIndex();
	if (_megagroupSet && pressedIndex >= 0 && pressedIndex < _rows.size()) {
		update(0, _itemsTop + pressedIndex * _rowHeight, width(), _rowHeight);
		const auto row = _rows[pressedIndex].get();
		if (row->ripple) {
			row->ripple->lastStop();
		}
	}
	_pressed = pressed;
	pressedIndex = countPressedIndex();
	if (_megagroupSet && pressedIndex >= 0 && pressedIndex < _rows.size()) {
		update(0, _itemsTop + pressedIndex * _rowHeight, width(), _rowHeight);
		auto &set = _rows[pressedIndex];
		if (!set->ripple) {
			auto rippleMask = Ui::RippleAnimation::RectMask(QSize(width(), _rowHeight));
			set->ripple = std::make_unique<Ui::RippleAnimation>(st::defaultRippleAnimation, std::move(rippleMask), [this, pressedIndex] {
				update(0, _itemsTop + pressedIndex * _rowHeight, width(), _rowHeight);
			});
		}
		set->ripple->add(mapFromGlobal(QCursor::pos()) - QPoint(0, _itemsTop + pressedIndex * _rowHeight));
	}
}

void StickersBox::Inner::ensureRipple(
		const style::RippleAnimation &st,
		QImage mask,
		bool removeButton,
		bool installedSet) {
	const auto dy = _itemsTop + _actionDown * _rowHeight;
	_rows[_actionDown]->ripple = std::make_unique<Ui::RippleAnimation>(st, std::move(mask), [=] {
		update(myrtlrect(relativeButtonRect(removeButton, installedSet).translated(0, dy)));
	});
}

void StickersBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	_mouse = e->globalPos();
	updateSelected();
}

void StickersBox::Inner::updateSelected() {
	auto local = mapFromGlobal(_mouse);
	if (_dragging >= 0) {
		auto shift = 0;
		auto now = crl::now();
		int firstSetIndex = 0;
		if (_rows.at(firstSetIndex)->isRecentSet()) {
			++firstSetIndex;
		}
		if (_dragStart.y() > local.y() && _dragging > 0) {
			shift = -floorclamp(_dragStart.y() - local.y() + (_rowHeight / 2), _rowHeight, 0, _dragging - firstSetIndex);
			for (int32 from = _dragging, to = _dragging + shift; from > to; --from) {
				qSwap(_rows[from], _rows[from - 1]);
				_rows[from]->yadd = anim::value(_rows[from]->yadd.current() - _rowHeight, 0);
				_shiftingStartTimes[from] = now;
			}
		} else if (_dragStart.y() < local.y() && _dragging + 1 < _rows.size()) {
			shift = floorclamp(local.y() - _dragStart.y() + (_rowHeight / 2), _rowHeight, 0, _rows.size() - _dragging - 1);
			for (int32 from = _dragging, to = _dragging + shift; from < to; ++from) {
				qSwap(_rows[from], _rows[from + 1]);
				_rows[from]->yadd = anim::value(_rows[from]->yadd.current() + _rowHeight, 0);
				_shiftingStartTimes[from] = now;
			}
		}
		if (shift) {
			_dragging += shift;
			_above = _dragging;
			_dragStart.setY(_dragStart.y() + shift * _rowHeight);
			if (!_shiftingAnimation.animating()) {
				_shiftingAnimation.start();
			}
		}
		_rows[_dragging]->yadd = anim::value(local.y() - _dragStart.y(), local.y() - _dragStart.y());
		_shiftingStartTimes[_dragging] = 0;
		shiftingAnimationCallback(now);

		_draggingScrollDelta.fire_copy([&] {
			if (local.y() < _visibleTop) {
				return local.y() - _visibleTop;
			} else if (local.y() >= _visibleBottom) {
				return local.y() + 1 - _visibleBottom;
			}
			return 0;
		}());
	} else {
		bool in = rect().marginsRemoved(QMargins(0, _itemsTop, 0, st::membersMarginBottom)).contains(local);
		auto selected = SelectedRow();
		auto actionSel = -1;
		auto inDragArea = false;
		if (in && !_rows.empty()) {
			auto selectedIndex = floorclamp(local.y() - _itemsTop, _rowHeight, 0, _rows.size() - 1);
			selected = selectedIndex;
			local.setY(local.y() - _itemsTop - selectedIndex * _rowHeight);
			const auto row = _rows[selectedIndex].get();
			if (!_megagroupSet
				&& (_isInstalledTab
					|| (_section == Section::Featured)
					|| !row->isInstalled()
					|| row->isArchived()
					|| row->removed)) {
				auto removeButton = (_isInstalledTab && !row->removed);

				const auto installedSetButton = !_isInstalledTab
					&& row->isInstalled()
					&& !row->isArchived()
					&& !row->removed;
				auto rect = myrtlrect(relativeButtonRect(removeButton, installedSetButton));
				actionSel = rect.contains(local) ? selectedIndex : -1;
			} else {
				actionSel = -1;
			}
			if (!_megagroupSet && _isInstalledTab && !row->isRecentSet()) {
				auto dragAreaWidth = _st.photoPosition.x() + st::stickersReorderIcon.width() + st::stickersReorderSkip;
				auto dragArea = myrtlrect(0, 0, dragAreaWidth, _rowHeight);
				inDragArea = dragArea.contains(local);
			}
		} else if (_megagroupSelectedSet) {
			auto setTop = _megagroupDivider->y() - _rowHeight;
			if (QRect(0, setTop, width(), _rowHeight).contains(local)) {
				selected = MegagroupSet();
			}
		}
		setSelected(selected);
		if (_inDragArea != inDragArea) {
			_inDragArea = inDragArea;
			updateCursor();
		}
		setActionSel(actionSel);
		_draggingScrollDelta.fire(0);
	}
}

void StickersBox::Inner::updateCursor() {
	setCursor(_inDragArea
		? style::cur_sizeall
		: (!_megagroupSet && _isInstalledTab)
		? ((_actionSel >= 0 && (_actionDown < 0 || _actionDown == _actionSel))
			? style::cur_pointer
			: style::cur_default)
		: (!v::is_null(_selected) || !v::is_null(_pressed))
		? style::cur_pointer
		: style::cur_default);
}

float64 StickersBox::Inner::aboveShadowOpacity() const {
	if (_above < 0) return 0;

	auto dx = 0;
	auto dy = qAbs(_above * _rowHeight + qRound(_rows[_above]->yadd.current()) - _started * _rowHeight);
	return qMin((dx + dy) * 2. / _rowHeight, 1.);
}

void StickersBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = std::exchange(_pressed, SelectedRow());

	updateCursor();

	_mouse = e->globalPos();
	updateSelected();
	const auto down = _actionDown;
	setActionDown(-1);
	if (down == _actionSel && _actionSel >= 0) {
		const auto row = _rows[down].get();
		const auto installedSet = row->isInstalled()
			&& !row->isArchived()
			&& !row->removed;
		const auto callback = installedSet
			? _removeSetCallback
			: _installSetCallback;
		if (callback) {
			row->ripple.reset();
			callback(row->set->id);
		}
	} else if (_dragging >= 0) {
		_rows[_dragging]->yadd.start(0.);
		_aboveShadowFadeStart = _shiftingStartTimes[_dragging] = crl::now();
		_aboveShadowFadeOpacity = anim::value(aboveShadowOpacity(), 0);
		if (!_shiftingAnimation.animating()) {
			_shiftingAnimation.start();
		}

		_dragging = _started = -1;
	} else if (pressed == _selected && _actionSel < 0 && down < 0) {
		const auto selectedIndex = [&] {
			if (auto index = std::get_if<int>(&_selected)) {
				return *index;
			}
			return -1;
		}();
		const auto showSetByRow = [&](const Row &row) {
			setSelected(SelectedRow());
			_show->showBox(Box<StickerSetBox>(_show, row.set));
		};
		if (selectedIndex >= 0 && !_inDragArea) {
			const auto row = _rows[selectedIndex].get();
			if (!row->isRecentSet()) {
				if (_megagroupSet) {
					setMegagroupSelectedSet(row->set->identifier());
				} else {
					showSetByRow(*row);
				}
			}
		} else if (_megagroupSelectedSet && v::is<MegagroupSet>(_selected)) {
			showSetByRow(*_megagroupSelectedSet);
		}
	}
}

void StickersBox::Inner::saveGroupSet(Fn<void()> done) {
	Expects(_megagroupSet != nullptr);

	auto oldId = _megagroupSetEmoji
		? _megagroupSet->mgInfo->emojiSet.id
		: _megagroupSet->mgInfo->stickerSet.id;
	auto newId = _megagroupSetInput.id;
	if (newId == oldId) {
		done();
	} else if (_megagroupSetEmoji) {
		checkGroupLevel(done);
	} else {
		session().api().setGroupStickerSet(_megagroupSet, _megagroupSetInput);
		session().data().stickers().notifyStickerSetInstalled(
			Data::Stickers::MegagroupSetId);
	}
}

void StickersBox::Inner::checkGroupLevel(Fn<void()> done) {
	Expects(_megagroupSet != nullptr);
	Expects(_megagroupSetEmoji);

	const auto peer = _megagroupSet;
	const auto save = [=] {
		session().api().setGroupEmojiSet(peer, _megagroupSetInput);
		session().data().stickers().notifyEmojiSetInstalled(
			Data::Stickers::MegagroupSetId);
		done();
	};

	if (!_megagroupSetInput) {
		save();
		return;
	} else if (_checkingGroupLevel) {
		return;
	}
	_checkingGroupLevel = true;

	const auto weak = base::make_weak(this);
	CheckBoostLevel(_show, peer, [=](int level) {
		if (!weak) {
			return std::optional<Ui::AskBoostReason>();
		}
		_checkingGroupLevel = false;
		const auto required = Data::LevelLimits(
			&peer->session()).groupEmojiStickersLevelMin();
		if (level >= required) {
			save();
			return std::optional<Ui::AskBoostReason>();
		}
		return std::make_optional(Ui::AskBoostReason{
			Ui::AskBoostEmojiPack{ required }
		});
	}, [=] { _checkingGroupLevel = false; });
}

void StickersBox::Inner::setRowRemovedBySetId(uint64 setId, bool removed) {
	const auto index = getRowIndex(setId);
	if (index >= 0) {
		setRowRemoved(index, removed);
	}
}

void StickersBox::Inner::setRowRemoved(int index, bool removed) {
	auto &row = _rows[index];
	if (row->removed != removed) {
		row->removed = removed;
		row->ripple.reset();
		update(0, _itemsTop + index * _rowHeight, width(), _rowHeight);
		updateSelected();
	}
}

void StickersBox::Inner::leaveEventHook(QEvent *e) {
	_mouse = QPoint(-1, -1);
	updateSelected();
}

void StickersBox::Inner::leaveToChildEvent(QEvent *e, QWidget *child) {
	_mouse = QPoint(-1, -1);
	updateSelected();
}

bool StickersBox::Inner::shiftingAnimationCallback(crl::time now) {
	if (anim::Disabled()) {
		now += st::stickersRowDuration;
	}
	auto animating = false;
	auto updateMin = -1;
	auto updateMax = 0;
	for (auto i = 0, count = int(_shiftingStartTimes.size()); i != count; ++i) {
		const auto start = _shiftingStartTimes[i];
		if (start) {
			if (updateMin < 0) {
				updateMin = i;
			}
			updateMax = i;
			if (start + st::stickersRowDuration > now && now >= start) {
				_rows[i]->yadd.update(float64(now - start) / st::stickersRowDuration, anim::sineInOut);
				animating = true;
			} else {
				_rows[i]->yadd.finish();
				_shiftingStartTimes[i] = 0;
			}
		}
	}
	if (_aboveShadowFadeStart) {
		if (updateMin < 0 || updateMin > _above) updateMin = _above;
		if (updateMax < _above) updateMin = _above;
		if (_aboveShadowFadeStart + st::stickersRowDuration > now && now > _aboveShadowFadeStart) {
			_aboveShadowFadeOpacity.update(float64(now - _aboveShadowFadeStart) / st::stickersRowDuration, anim::sineInOut);
			animating = true;
		} else {
			_aboveShadowFadeOpacity.finish();
			_aboveShadowFadeStart = 0;
		}
	}
	if (_dragging >= 0) {
		if (updateMin < 0 || updateMin > _dragging) {
			updateMin = _dragging;
		}
		if (updateMax < _dragging) updateMax = _dragging;
	}
	if (updateMin == 1 && _rows[0]->isRecentSet()) {
		updateMin = 0; // Repaint from the very top of the content.
	}
	if (updateMin >= 0) {
		update(0, _itemsTop + _rowHeight * (updateMin - 1), width(), _rowHeight * (updateMax - updateMin + 3));
	}
	if (!animating) {
		_above = _dragging;
	}
	return animating;
}

void StickersBox::Inner::clear() {
	_rows.clear();
	_shiftingStartTimes.clear();
	_aboveShadowFadeStart = 0;
	_aboveShadowFadeOpacity = anim::value();
	_shiftingAnimation.stop();
	_above = _dragging = _started = -1;
	setSelected(SelectedRow());
	setPressed(SelectedRow());
	setActionSel(-1);
	setActionDown(-1);
	update();
}

void StickersBox::Inner::setActionSel(int32 actionSel) {
	if (actionSel != _actionSel) {
		if (_actionSel >= 0) update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
		_actionSel = actionSel;
		if (_actionSel >= 0) update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
		updateCursor();
	}
}

void StickersBox::Inner::AddressField::correctValue(
		const QString &was,
		int wasCursor,
		QString &now,
		int &nowCursor) {
	auto newText = now;
	auto newCursor = nowCursor;
	auto removeFromBeginning = {
		u"http://"_q,
		u"https://"_q,
		u"www.t.me/"_q,
		u"www.telegram.me/"_q,
		u"www.telegram.dog/"_q,
		u"t.me/"_q,
		u"telegram.me/"_q,
		u"telegram.dog/"_q,
		u"addstickers/"_q,
	};
	for (auto &removePhrase : removeFromBeginning) {
		if (newText.startsWith(removePhrase)) {
			newText = newText.mid(removePhrase.size());
			newCursor = newText.size();
		}
	}
	setCorrectedText(now, nowCursor, newText, newCursor);
}

void StickersBox::Inner::handleMegagroupSetAddressChange() {
	auto text = _megagroupSetField->getLastText().trimmed();
	if (text.isEmpty()) {
		if (_megagroupSelectedSet) {
			const auto &sets = session().data().stickers().sets();
			const auto it = sets.find(_megagroupSelectedSet->set->id);
			if (it != sets.cend() && !it->second->shortName.isEmpty()) {
				setMegagroupSelectedSet({});
			}
		}
	} else if (!_megagroupSetRequestId) {
		_megagroupSetRequestId = _api.request(MTPmessages_GetStickerSet(
			MTP_inputStickerSetShortName(MTP_string(text)),
			MTP_int(0) // hash
		)).done([=](const MTPmessages_StickerSet &result) {
			_megagroupSetRequestId = 0;
			result.match([&](const MTPDmessages_stickerSet &data) {
				const auto set = session().data().stickers().feedSetFull(data);
				setMegagroupSelectedSet(set->identifier());
			}, [](const MTPDmessages_stickerSetNotModified &) {
				LOG(("API Error: Unexpected messages.stickerSetNotModified."));
			});
		}).fail([=] {
			_megagroupSetRequestId = 0;
			setMegagroupSelectedSet({});
		}).send();
	} else {
		_megagroupSetAddressChangedTimer.callOnce(kHandleMegagroupSetAddressChangeTimeout);
	}
}

void StickersBox::Inner::rebuildMegagroupSet() {
	Expects(_megagroupSet != nullptr);

	const auto clearCurrent = [&] {
		if (_megagroupSelectedSet) {
			_megagroupSetField->setText(QString());
			_megagroupSetField->finishAnimating();
		}
		_megagroupSelectedSet = nullptr;
		_megagroupSelectedRemove.destroy();
		_megagroupSelectedShadow.destroy();
	};
	if (!_megagroupSetInput.id) {
		clearCurrent();
		return;
	}
	auto setId = _megagroupSetInput.id;
	const auto &sets = session().data().stickers().sets();
	auto it = sets.find(setId);
	if (it == sets.cend()
		|| (it->second->flags & SetFlag::NotLoaded)) {
		// It may have been in sets and stored in _megagroupSelectedSet
		// already, but then removed from sets. We need to clear the stored
		// pointer, otherwise we may crash in paint event while loading.
		clearCurrent();
		session().api().scheduleStickerSetRequest(
			_megagroupSetInput.id,
			_megagroupSetInput.accessHash);
		return;
	}

	const auto set = it->second.get();
	auto count = fillSetCount(set);
	auto sticker = (DocumentData*)nullptr;
	auto pixw = 0, pixh = 0;
	fillSetCover(set, &sticker, &pixw, &pixh);
	auto flagsOverride = SetFlag::Installed;
	auto removed = false;
	auto maxNameWidth = countMaxNameWidth(!_isInstalledTab);
	auto titleWidth = 0;
	auto title = FillSetTitle(set, maxNameWidth, &titleWidth);
	if (!_megagroupSelectedSet
		|| _megagroupSelectedSet->set->id != set->id) {
		_megagroupSetField->setText(set->shortName);
		_megagroupSetField->finishAnimating();
	}
	_megagroupSelectedSet = std::make_unique<Row>(
		set,
		sticker,
		count,
		title,
		titleWidth,
		flagsOverride,
		removed,
		pixw,
		pixh);
	_itemsTop += st::lineWidth + _rowHeight;

	if (!_megagroupSelectedRemove) {
		_megagroupSelectedRemove.create(this, st::groupStickersRemove);
		_megagroupSelectedRemove->show(anim::type::instant);
		_megagroupSelectedRemove->setClickedCallback([this] {
			setMegagroupSelectedSet({});
		});
		_megagroupSelectedShadow.create(this);
		updateControlsGeometry();
	}
}

void StickersBox::Inner::rebuild(bool masks) {
	_itemsTop = st::lineWidth;

	if (_megagroupSet) {
		_itemsTop += st::groupStickersFieldPadding.top() + _megagroupSetField->height() + st::groupStickersFieldPadding.bottom();
		_itemsTop += _megagroupDivider->height() + st::groupStickersSubTitleHeight;
		rebuildMegagroupSet();
	}

	_oldRows = std::move(_rows);
	clear();
	const auto &order = ([&]() -> const StickersSetsOrder & {
		if (_section == Section::Installed) {
			auto &result = _megagroupSetEmoji
				? session().data().stickers().emojiSetsOrder()
				: session().data().stickers().setsOrder();
			if (_megagroupSet && result.empty()) {
				return _megagroupSetEmoji
					? session().data().stickers().featuredEmojiSetsOrder()
					: session().data().stickers().featuredSetsOrder();
			}
			return result;
		} else if (_section == Section::Masks) {
			return session().data().stickers().maskSetsOrder();
		} else if (_section == Section::Featured) {
			return session().data().stickers().featuredSetsOrder();
		}
		return masks
			? session().data().stickers().archivedMaskSetsOrder()
			: session().data().stickers().archivedSetsOrder();
	})();
	_rows.reserve(order.size() + 1);
	_shiftingStartTimes.reserve(order.size() + 1);

	const auto &sets = session().data().stickers().sets();
	if (_megagroupSet) {
		auto usingFeatured = _megagroupSetEmoji
			? session().data().stickers().emojiSetsOrder().empty()
			: session().data().stickers().setsOrder().empty();
		_megagroupSubTitle->setText(usingFeatured
			? (_megagroupSetEmoji
				? tr::lng_stickers_group_from_featured(tr::now)
				: tr::lng_emoji_group_from_featured(tr::now))
			: _megagroupSetEmoji
			? tr::lng_emoji_group_from_your(tr::now)
			: tr::lng_stickers_group_from_your(tr::now));
		updateControlsGeometry();
	} else if (_isInstalledTab) {
		const auto cloudIt = sets.find((_section == Section::Masks)
			? Data::Stickers::CloudRecentAttachedSetId
			: Data::Stickers::CloudRecentSetId); // Section::Installed.
		if (cloudIt != sets.cend() && !cloudIt->second->stickers.isEmpty()) {
			rebuildAppendSet(cloudIt->second.get());
		}
	}
	for (const auto setId : order) {
		auto it = sets.find(setId);
		if (it == sets.cend()) {
			continue;
		}

		const auto set = it->second.get();
		rebuildAppendSet(set);

		if (set->stickers.isEmpty()
			|| (set->flags & SetFlag::NotLoaded)) {
			session().api().scheduleStickerSetRequest(
				set->id,
				set->accessHash);
		}
	}
	_oldRows.clear();
	session().api().requestStickerSets();
	updateSize();
}

void StickersBox::Inner::setMegagroupSelectedSet(
		const StickerSetIdentifier &set) {
	_megagroupSetInput = set;
	rebuild(false);
	_scrollsToY.fire(0);
	updateSelected();
}

void StickersBox::Inner::updateSize(int newWidth) {
	auto naturalHeight = _itemsTop + int(_rows.size()) * _rowHeight + st::membersMarginBottom;
	resize(newWidth ? newWidth : width(), qMax(_minHeight, naturalHeight));
	updateControlsGeometry();
	checkLoadMore();
}

void StickersBox::Inner::updateRows() {
	const auto maxNameWidth = countMaxNameWidth(false);
	const auto maxNameWidthInstalled = countMaxNameWidth(true);
	const auto &sets = session().data().stickers().sets();
	for (const auto &row : _rows) {
		const auto it = sets.find(row->set->id);
		if (it == sets.cend()) {
			continue;
		}
		const auto set = it->second.get();
		if (!row->sticker) {
			auto sticker = (DocumentData*)nullptr;
			auto pixw = 0, pixh = 0;
			fillSetCover(set, &sticker, &pixw, &pixh);
			if (sticker) {
				if (row->sticker != sticker && !row->thumbnailMedia) {
					row->lottie = nullptr;
					row->stickerMedia = nullptr;
				}
				row->sticker = sticker;
				row->pixw = pixw;
				row->pixh = pixh;
			}
		}
		if (!row->isRecentSet()) {
			auto wasInstalled = row->isInstalled();
			auto wasArchived = row->isArchived();
			row->flagsOverride = fillSetFlags(set);
			if (_isInstalledTab) {
				row->flagsOverride &= ~SetFlag::Archived;
			}
			if (row->isInstalled() != wasInstalled
				|| row->isArchived() != wasArchived) {
				row->ripple.reset();
			}
		}
		const auto installedSet = (!_isInstalledTab
			&& row->isInstalled()
			&& !row->isArchived()
			&& !row->removed);
		row->title = FillSetTitle(
			set,
			installedSet ? maxNameWidthInstalled : maxNameWidth,
			&row->titleWidth);
		row->count = fillSetCount(set);
	}
	update();
}

bool StickersBox::Inner::appendSet(not_null<StickersSet*> set) {
	for (const auto &row : _rows) {
		if (row->set == set) {
			return false;
		}
	}
	rebuildAppendSet(set);
	return true;
}

bool StickersBox::Inner::skipPremium() const {
	return !_session->premiumPossible();
}

int StickersBox::Inner::countMaxNameWidth(bool installedSet) const {
	int namex = _st.namePosition.x();
	if (!_megagroupSet && _isInstalledTab) {
		namex += st::stickersReorderIcon.width() + st::stickersReorderSkip;
	}
	int namew = st::boxWideWidth - namex - st::contactsPadding.right();
	if (_isInstalledTab) {
		if (!_megagroupSet) {
			namew -= _undoWidth - st::stickersUndoRemove.width;
		}
	} else {
		namew -= installedSet
			? (_installedWidth - st::stickersTrendingInstalled.width)
			: (_addWidth - st::stickersTrendingAdd.width);
		if (_section == Section::Featured) {
			namew -= st::stickersFeaturedUnreadSize + st::stickersFeaturedUnreadSkip;
		}
	}
	return namew;
}

void StickersBox::Inner::rebuildAppendSet(not_null<StickersSet*> set) {
	auto flagsOverride = (set->id != Data::Stickers::CloudRecentSetId)
		? fillSetFlags(set)
		: SetFlag::Installed;
	auto removed = false;
	if (_isInstalledTab && (flagsOverride & SetFlag::Archived)) {
		return;
	}

	DocumentData *sticker = nullptr;
	int pixw = 0, pixh = 0;
	fillSetCover(set, &sticker, &pixw, &pixh);

	const auto maxNameWidth = countMaxNameWidth(!_isInstalledTab
		&& (flagsOverride & SetFlag::Installed)
		&& !(flagsOverride & SetFlag::Archived)
		&& !removed);
	int titleWidth = 0;
	QString title = FillSetTitle(set, maxNameWidth, &titleWidth);
	int count = fillSetCount(set);

	const auto existing = [&]{
		const auto now = int(_rows.size());
		const auto setProj = [](const std::unique_ptr<Row> &row) {
			return row ? row->set.get() : nullptr;
		};
		if (_oldRows.size() > now
			&& setProj(_oldRows[now]) == set.get()) {
			return _oldRows.begin() + now;
		}
		return ranges::find(_oldRows, set.get(), setProj);
	}();
	if (existing != end(_oldRows)) {
		const auto raw = existing->get();
		raw->sticker = sticker;
		raw->count = count;
		raw->title = title;
		raw->titleWidth = titleWidth;
		raw->flagsOverride = flagsOverride;
		raw->removed = removed;
		raw->pixw = pixw;
		raw->pixh = pixh;
		raw->yadd = {};
		auto oldStickerMedia = std::move(raw->stickerMedia);
		auto oldThumbnailMedia = std::move(raw->thumbnailMedia);
		raw->stickerMedia = sticker ? sticker->activeMediaView() : nullptr;
		raw->thumbnailMedia = set->activeThumbnailView();
		if (raw->thumbnailMedia != oldThumbnailMedia
			|| (!raw->thumbnailMedia && raw->stickerMedia != oldStickerMedia)) {
			raw->lottie = nullptr;
		}
		_rows.push_back(std::move(*existing));
	} else {
		_rows.push_back(std::make_unique<Row>(
			set,
			sticker,
			count,
			title,
			titleWidth,
			flagsOverride,
			removed,
			pixw,
			pixh));
	}
	_shiftingStartTimes.push_back(0);
}

void StickersBox::Inner::fillSetCover(
		not_null<StickersSet*> set,
		DocumentData **outSticker,
		int *outWidth,
		int *outHeight) const {
	if (set->stickers.isEmpty()) {
		*outSticker = nullptr;
		*outWidth = *outHeight = 0;
		return;
	}
	auto sticker = *outSticker = set->stickers.front();

	const auto size = set->hasThumbnail()
		? QSize(
			set->thumbnailLocation().width(),
			set->thumbnailLocation().height())
		: sticker->hasThumbnail()
		? QSize(
			sticker->thumbnailLocation().width(),
			sticker->thumbnailLocation().height())
		: QSize(1, 1);
	auto pixw = size.width();
	auto pixh = size.height();
	if (pixw > _st.photoSize) {
		if (pixw > pixh) {
			pixh = (pixh * _st.photoSize) / pixw;
			pixw = _st.photoSize;
		} else {
			pixw = (pixw * _st.photoSize) / pixh;
			pixh = _st.photoSize;
		}
	} else if (pixh > _st.photoSize) {
		pixw = (pixw * _st.photoSize) / pixh;
		pixh = _st.photoSize;
	}
	*outWidth = pixw;
	*outHeight = pixh;
}

int StickersBox::Inner::fillSetCount(not_null<StickersSet*> set) const {
	const auto skipPremium = this->skipPremium();
	int result = set->stickers.isEmpty()
		? set->count
		: set->stickers.size();
	if (skipPremium && !set->stickers.isEmpty()) {
		result -= ranges::count(
			set->stickers,
			true,
			&DocumentData::isPremiumSticker);
	}
	auto added = 0;
	if (set->id == Data::Stickers::CloudRecentSetId) {
		const auto &sets = session().data().stickers().sets();
		const auto &recent = session().data().stickers().getRecentPack();
		auto customIt = sets.find(Data::Stickers::CustomSetId);
		if (customIt != sets.cend()) {
			auto &custom = customIt->second->stickers;
			added = custom.size();
			if (skipPremium) {
				added -= ranges::count(
					custom,
					true,
					&DocumentData::isPremiumSticker);
			}
			for (const auto &sticker : recent) {
				if (skipPremium && sticker.first->isPremiumSticker()) {
					continue;
				} else if (customIt->second->stickers.indexOf(sticker.first) < 0) {
					++added;
				}
			}
		} else {
			added = recent.size();
		}
	}
	return result + added;
}

Data::StickersSetFlags StickersBox::Inner::fillSetFlags(
		not_null<StickersSet*> set) const {
	const auto result = set->flags;
	return (_section == Section::Featured)
		? result
		: (result & ~SetFlag::Unread);
}

template <typename Check>
StickersSetsOrder StickersBox::Inner::collectSets(Check check) const {
	StickersSetsOrder result;
	result.reserve(_rows.size());
	for (const auto &row : _rows) {
		if (check(row.get())) {
			result.push_back(row->set->id);
		}
	}
	return result;
}

StickersSetsOrder StickersBox::Inner::order() const {
	return collectSets([](Row *row) {
		return !row->isArchived() && !row->removed && !row->isRecentSet();
	});
}

StickersSetsOrder StickersBox::Inner::fullOrder() const {
	return collectSets([](Row *row) {
		return !row->isRecentSet();
	});
}

StickersSetsOrder StickersBox::Inner::removedSets() const {
	return collectSets([](Row *row) {
		return row->removed;
	});
}

int StickersBox::Inner::getRowIndex(uint64 setId) const {
	for (auto i = 0, count = int(_rows.size()); i != count; ++i) {
		auto &row = _rows[i];
		if (row->set->id == setId) {
			return i;
		}
	}
	return -1;
}

void StickersBox::Inner::setFullOrder(const StickersSetsOrder &order) {
	for (const auto setId : order) {
		auto index = getRowIndex(setId);
		if (index >= 0) {
			auto row = std::move(_rows[index]);
			auto count = _rows.size();
			for (auto i = index + 1; i != count; ++i) {
				_rows[i - 1] = std::move(_rows[i]);
			}
			_rows[count - 1] = std::move(row);
		}
	}
}

void StickersBox::Inner::setRemovedSets(const StickersSetsOrder &removed) {
	for (auto i = 0, count = int(_rows.size()); i != count; ++i) {
		setRowRemoved(i, removed.contains(_rows[i]->set->id));
	}
}

void StickersBox::Inner::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;
	updateScrollbarWidth();
	if (_section == Section::Featured) {
		readVisibleSets();
	}
	checkLoadMore();
}

void StickersBox::Inner::checkLoadMore() {
	if (_loadMoreCallback) {
		auto scrollHeight = (_visibleBottom - _visibleTop);
		int scrollTop = _visibleTop, scrollTopMax = height() - scrollHeight;
		if (scrollTop + PreloadHeightsCount * scrollHeight >= scrollTopMax) {
			_loadMoreCallback();
		}
	}
}

void StickersBox::Inner::readVisibleSets() {
	auto itemsVisibleTop = _visibleTop - _itemsTop;
	auto itemsVisibleBottom = _visibleBottom - _itemsTop;
	int rowFrom = floorclamp(itemsVisibleTop, _rowHeight, 0, _rows.size());
	int rowTo = ceilclamp(itemsVisibleBottom, _rowHeight, 0, _rows.size());
	for (int i = rowFrom; i < rowTo; ++i) {
		const auto row = _rows[i].get();
		if (!row->isUnread()) {
			continue;
		}
		if ((i * _rowHeight < itemsVisibleTop)
			|| ((i + 1) * _rowHeight > itemsVisibleBottom)) {
			continue;
		}
		const auto thumbnailLoading = row->set->hasThumbnail()
			? row->set->thumbnailLoading()
			: row->sticker
			? row->sticker->thumbnailLoading()
			: false;
		const auto thumbnailLoaded = row->set->hasThumbnail()
			? (row->thumbnailMedia
				&& (row->thumbnailMedia->image()
					|| !row->thumbnailMedia->content().isEmpty()))
			: row->sticker
			? (row->stickerMedia && row->stickerMedia->loaded())
			: true;
		if (!thumbnailLoading || thumbnailLoaded) {
			session().api().readFeaturedSetDelayed(row->set->id);
		}
	}
}

void StickersBox::Inner::updateScrollbarWidth() {
	auto width = (_visibleBottom - _visibleTop < height()) ? (st::boxScroll.width - st::boxScroll.deltax) : 0;
	if (_scrollbar != width) {
		_scrollbar = width;
		update();
	}
}

StickersBox::Inner::~Inner() {
	clear();
}
