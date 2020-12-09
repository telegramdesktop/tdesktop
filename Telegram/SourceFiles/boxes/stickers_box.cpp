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
#include "data/stickers/data_stickers.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "boxes/confirm_box.h"
#include "boxes/sticker_set_box.h"
#include "apiwrap.h"
#include "storage/storage_account.h"
#include "dialogs/dialogs_layout.h"
#include "lottie/lottie_single_player.h"
#include "chat_helpers/stickers_lottie.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/slide_animation.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/input_fields.h"
#include "ui/image/image.h"
#include "ui/cached_round_corners.h"
#include "window/window_session_controller.h"
#include "main/main_session.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_chat_helpers.h"

namespace {

using Data::StickersSet;
using Data::StickersSetsOrder;
using Data::StickersSetThumbnailView;

constexpr auto kArchivedLimitFirstRequest = 10;
constexpr auto kArchivedLimitPerPage = 30;
constexpr auto kHandleMegagroupSetAddressChangeTimeout = crl::time(1000);

} // namespace

class StickersBox::CounterWidget : public Ui::RpWidget {
public:
	CounterWidget(QWidget *parent, rpl::producer<int> count);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void setCounter(int counter);

	QString _text;
	Dialogs::Layout::UnreadBadgeStyle _st;

};

// This class is hold in header because it requires Qt preprocessing.
class StickersBox::Inner
	: public Ui::RpWidget
	, private base::Subscriber {
public:
	using Section = StickersBox::Section;

	Inner(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Section section);
	Inner(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		not_null<ChannelData*> megagroup);

	[[nodiscard]] Main::Session &session() const;

	base::Observable<int> scrollToY;
	void setInnerFocus();

	void saveGroupSet();

	void rebuild();
	void updateSize(int newWidth = 0);
	void updateRows(); // refresh only pack cover stickers
	bool appendSet(not_null<StickersSet*> set);

	StickersSetsOrder getOrder() const;
	StickersSetsOrder getFullOrder() const;
	StickersSetsOrder getRemovedSets() const;

	void setFullOrder(const StickersSetsOrder &order);
	void setRemovedSets(const StickersSetsOrder &removed);

	void setInstallSetCallback(Fn<void(uint64 setId)> callback) {
		_installSetCallback = std::move(callback);
	}
	void setLoadMoreCallback(Fn<void()> callback) {
		_loadMoreCallback = std::move(callback);
	}

	void setMinHeight(int newWidth, int minHeight);

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
			bool installed,
			bool official,
			bool unread,
			bool archived,
			bool removed,
			int32 pixw,
			int32 pixh);
		~Row();

		bool isRecentSet() const;

		const not_null<StickersSet*> set;
		DocumentData *sticker = nullptr;
		std::shared_ptr<Data::DocumentMedia> stickerMedia;
		std::shared_ptr<StickersSetThumbnailView> thumbnailMedia;
		int32 count = 0;
		QString title;
		int titleWidth = 0;
		bool installed = false;
		bool official = false;
		bool unread = false;
		bool archived = false;
		bool removed = false;
		int32 pixw = 0;
		int32 pixh = 0;
		anim::value yadd;
		std::unique_ptr<Ui::RippleAnimation> ripple;
		std::unique_ptr<Lottie::SinglePlayer> lottie;
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

	void checkLoadMore();
	void updateScrollbarWidth();
	int getRowIndex(uint64 setId) const;
	void setRowRemoved(int index, bool removed);

	void setSelected(SelectedRow selected);
	void setActionDown(int newActionDown);
	void setPressed(SelectedRow pressed);
	void setup();
	QRect relativeButtonRect(bool removeButton) const;
	void ensureRipple(const style::RippleAnimation &st, QImage mask, bool removeButton);

	bool shiftingAnimationCallback(crl::time now);
	void paintRow(Painter &p, not_null<Row*> row, int index);
	void paintRowThumbnail(Painter &p, not_null<Row*> row, int left);
	void paintFakeButton(Painter &p, not_null<Row*> row, int index);
	void clear();
	void updateCursor();
	void setActionSel(int32 actionSel);
	float64 aboveShadowOpacity() const;
	void validateLottieAnimation(not_null<Row*> row);
	void updateRowThumbnail(not_null<Row*> row);

	void readVisibleSets();

	void updateControlsGeometry();
	void rebuildAppendSet(not_null<StickersSet*> set, int maxNameWidth);
	void fillSetCover(not_null<StickersSet*> set, DocumentData **outSticker, int *outWidth, int *outHeight) const;
	int fillSetCount(not_null<StickersSet*> set) const;
	QString fillSetTitle(not_null<StickersSet*> set, int maxNameWidth, int *outTitleWidth) const;
	void fillSetFlags(not_null<StickersSet*> set, bool *outInstalled, bool *outOfficial, bool *outUnread, bool *outArchived);
	void rebuildMegagroupSet();
	void fixupMegagroupSetAddress();
	void handleMegagroupSetAddressChange();
	void setMegagroupSelectedSet(const MTPInputStickerSet &set);

	int countMaxNameWidth() const;

	const not_null<Window::SessionController*> _controller;
	MTP::Sender _api;

	Section _section;

	int32 _rowHeight;

	std::vector<std::unique_ptr<Row>> _rows;
	std::vector<crl::time> _shiftingStartTimes;
	crl::time _aboveShadowFadeStart = 0;
	anim::value _aboveShadowFadeOpacity;
	Ui::Animations::Basic _shiftingAnimation;

	Fn<void(uint64 setId)> _installSetCallback;
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

	int _buttonHeight = 0;

	QPoint _mouse;
	bool _inDragArea = false;
	SelectedRow _selected;
	SelectedRow _pressed;
	QPoint _dragStart;
	int _started = -1;
	int _dragging = -1;
	int _above = -1;
	rpl::event_stream<int> _draggingScrollDelta;

	int _minHeight = 0;

	int _scrollbar = 0;
	ChannelData *_megagroupSet = nullptr;
	MTPInputStickerSet _megagroupSetInput = MTP_inputStickerSetEmpty();
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

	_st.sizeId = Dialogs::Layout::UnreadBadgeInStickersBox;
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
	Painter p(&dummy);

	auto newWidth = 0;
	Dialogs::Layout::paintUnreadCount(p, _text, 0, 0, _st, &newWidth);

	resize(newWidth, st::stickersFeaturedBadgeSize);
}

void StickersBox::CounterWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	if (!_text.isEmpty()) {
		auto unreadRight = rtl() ? 0 : width();
		auto unreadTop = 0;
		Dialogs::Layout::paintUnreadCount(p, _text, unreadRight, unreadTop, _st);
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

StickersBox::Inner *StickersBox::Tab::widget() {
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
	not_null<Window::SessionController*> controller,
	Section section)
: _controller(controller)
, _api(&controller->session().mtp())
, _tabs(this, st::stickersTabs)
, _unreadBadge(
	this,
	controller->session().data().stickers().featuredSetsUnreadCountValue())
, _section(section)
, _installed(0, this, controller, Section::Installed)
, _featured(1, this, controller, Section::Featured)
, _archived(2, this, controller, Section::Archived) {
	_tabs->setRippleTopRoundRadius(st::boxRadius);
}

StickersBox::StickersBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> megagroup)
: _controller(controller)
, _api(&controller->session().mtp())
, _section(Section::Installed)
, _installed(0, this, controller, megagroup)
, _megagroupSet(megagroup) {
	subscribe(_installed.widget()->scrollToY, [=](int y) {
		onScrollToY(y);
	});
}

StickersBox::StickersBox(
	QWidget*,
	not_null<Window::SessionController*> controller,
	const MTPVector<MTPStickerSetCovered> &attachedSets)
: _controller(controller)
, _api(&controller->session().mtp())
, _section(Section::Attached)
, _attached(0, this, controller, Section::Attached)
, _attachedSets(attachedSets) {
}

Main::Session &StickersBox::session() const {
	return _controller->session();
}

void StickersBox::showAttachedStickers() {
	auto addedSet = false;
	for (const auto &stickerSet : _attachedSets.v) {
		const auto setData = stickerSet.match([&](const auto &data) {
			return data.vset().match([&](const MTPDstickerSet &data) {
				return &data;
			});
		});

		if (const auto set = session().data().stickers().feedSet(*setData)) {
			if (_attached.widget()->appendSet(set)) {
				addedSet = true;
				if (set->stickers.isEmpty() || (set->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
					session().api().scheduleStickerSetRequest(set->id, set->access);
				}
			}
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
	auto &archived = session().data().stickers().archivedSetsOrderRef();
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
	for_const (const auto &stickerSet, stickers.vsets().v) {
		const MTPDstickerSet *setData = nullptr;
		switch (stickerSet.type()) {
		case mtpc_stickerSetCovered: {
			auto &d = stickerSet.c_stickerSetCovered();
			if (d.vset().type() == mtpc_stickerSet) {
				setData = &d.vset().c_stickerSet();
			}
		} break;
		case mtpc_stickerSetMultiCovered: {
			auto &d = stickerSet.c_stickerSetMultiCovered();
			if (d.vset().type() == mtpc_stickerSet) {
				setData = &d.vset().c_stickerSet();
			}
		} break;
		}
		if (!setData) continue;

		if (const auto set = session().data().stickers().feedSet(*setData)) {
			const auto index = archived.indexOf(set->id);
			if (archived.isEmpty() || index != archived.size() - 1) {
				changedSets = true;
				if (index < archived.size() - 1) {
					archived.removeAt(index);
				}
				archived.push_back(set->id);
			}
			if (_archived.widget()->appendSet(set)) {
				addedSet = true;
				if (set->stickers.isEmpty() || (set->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
					session().api().scheduleStickerSetRequest(set->id, set->access);
				}
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
			session().local().readArchivedStickers();
		} else {
			setTitle(tr::lng_stickers_group_set());
		}
	} else if (_section == Section::Archived) {
		requestArchivedSets();
	} else if (_section == Section::Attached) {
		setTitle(tr::lng_stickers_attached_sets());
	}
	if (_tabs) {
		if (session().data().stickers().archivedSetsOrder().isEmpty()) {
			preloadArchivedSets();
		}
		setNoContentMargin(true);
		_tabs->sectionActivated(
		) | rpl::start_with_next(
			[this] { switchTab(); },
			lifetime());
		refreshTabs();
	}
	if (_installed.widget() && _section != Section::Installed) _installed.widget()->hide();
	if (_featured.widget() && _section != Section::Featured) _featured.widget()->hide();
	if (_archived.widget() && _section != Section::Archived) _archived.widget()->hide();
	if (_attached.widget() && _section != Section::Attached) _attached.widget()->hide();

	if (_featured.widget()) {
		_featured.widget()->setInstallSetCallback([this](uint64 setId) { installSet(setId); });
	}
	if (_archived.widget()) {
		_archived.widget()->setInstallSetCallback([this](uint64 setId) { installSet(setId); });
		_archived.widget()->setLoadMoreCallback([this] { loadMoreArchived(); });
	}
	if (_attached.widget()) {
		_attached.widget()->setInstallSetCallback([this](uint64 setId) { installSet(setId); });
		_attached.widget()->setLoadMoreCallback([this] { showAttachedStickers(); });
	}

	if (_megagroupSet) {
		addButton(
			tr::lng_settings_save(),
			[=] { _installed.widget()->saveGroupSet(); closeBox(); });
		addButton(tr::lng_cancel(), [=] { closeBox(); });
	} else {
		const auto close = _section == Section::Attached;
		addButton(
			close ? tr::lng_close() : tr::lng_about_done(),
			[=] { closeBox(); });
	}

	if (_section == Section::Installed) {
		_tab = &_installed;
	} else if (_section == Section::Archived) {
		_tab = &_archived;
	} else if (_section == Section::Attached) {
		_tab = &_attached;
	} else { // _section == Section::Featured
		_tab = &_featured;
	}
	setInnerWidget(_tab->takeWidget(), getTopSkip());
	setDimensions(st::boxWideWidth, st::boxMaxListHeight);

	session().data().stickers().updated(
	) | rpl::start_with_next(
		[this] { handleStickersUpdated(); },
		lifetime());
	session().api().updateStickers();

	if (_installed.widget()) {
		_installed.widget()->draggingScrollDelta(
		) | rpl::start_with_next([=](int delta) {
			scrollByDraggingDelta(delta);
		}, _installed.widget()->lifetime());
		if (!_megagroupSet) {
			boxClosing() | rpl::start_with_next([=] {
				saveChanges();
			}, lifetime());
		}
	}

	if (_tabs) {
		_tabs->raise();
		_unreadBadge->raise();
	}
	rebuildList();
}

void StickersBox::refreshTabs() {
	if (!_tabs) return;

	_tabIndices.clear();
	auto sections = QStringList();
	sections.push_back(tr::lng_stickers_installed_tab(tr::now).toUpper());
	_tabIndices.push_back(Section::Installed);
	if (!session().data().stickers().featuredSetsOrder().isEmpty()) {
		sections.push_back(tr::lng_stickers_featured_tab(tr::now).toUpper());
		_tabIndices.push_back(Section::Featured);
	}
	if (!session().data().stickers().archivedSetsOrder().isEmpty()) {
		sections.push_back(tr::lng_stickers_archived_tab(tr::now).toUpper());
		_tabIndices.push_back(Section::Archived);
	}
	_tabs->setSections(sections);
	if ((_tab == &_archived && !_tabIndices.contains(Section::Archived))
		|| (_tab == &_featured && !_tabIndices.contains(Section::Featured))) {
		switchTab();
	} else if (_tab == &_archived) {
		_tabs->setActiveSectionFast(_tabIndices.indexOf(Section::Archived));
	} else if (_tab == &_featured) {
		_tabs->setActiveSectionFast(_tabIndices.indexOf(Section::Featured));
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
	const auto &order = session().data().stickers().archivedSetsOrder();
	const auto &sets = session().data().stickers().sets();
	for (auto setIt = order.cend(), e = order.cbegin(); setIt != e;) {
		--setIt;
		auto it = sets.find(*setIt);
		if (it != sets.cend()) {
			if (it->second->flags & MTPDstickerSet::Flag::f_archived) {
				lastId = it->second->id;
				break;
			}
		}
	}
	_archivedRequestId = _api.request(MTPmessages_GetArchivedStickers(
		MTP_flags(0),
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
		_slideAnimation->paintFrame(p, 0, getTopSkip(), width());
		if (!_slideAnimation->animating()) {
			_slideAnimation.reset();
			setInnerVisible(true);
			update();
		}
	}
}

void StickersBox::updateTabsGeometry() {
	if (!_tabs) return;

	_tabs->resizeToWidth(_tabIndices.size() * width() / 3);
	_unreadBadge->setVisible(_tabIndices.contains(Section::Featured));

	setInnerTopSkip(getTopSkip());

	auto featuredLeft = width() / 3;
	auto featuredRight = 2 * width() / 3;
	auto featuredTextWidth = st::stickersTabs.labelStyle.font->width(tr::lng_stickers_featured_tab(tr::now).toUpper());
	auto featuredTextRight = featuredLeft + (featuredRight - featuredLeft - featuredTextWidth) / 2 + featuredTextWidth;
	auto unreadBadgeLeft = featuredTextRight - st::stickersFeaturedBadgeSkip;
	auto unreadBadgeTop = st::stickersFeaturedBadgeTop;
	if (unreadBadgeLeft + _unreadBadge->width() > featuredRight) {
		unreadBadgeLeft = featuredRight - _unreadBadge->width();
	}
	_unreadBadge->moveToLeft(unreadBadgeLeft, unreadBadgeTop);

	_tabs->moveToLeft(0, 0);
}

int StickersBox::getTopSkip() const {
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
	}
	if (_tab == newTab) {
		onScrollToY(0);
		return;
	}

	if (_tab == &_installed) {
		_localOrder = _tab->widget()->getFullOrder();
		_localRemoved = _tab->widget()->getRemovedSets();
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
	setInnerWidget(_tab->takeWidget(), getTopSkip());
	_tabs->raise();
	_unreadBadge->raise();
	_tab->widget()->show();
	rebuildList();
	onScrollToY(_tab->getScrollTop());
	setInnerVisible(true);
	auto nowCache = grabContentCache();
	auto nowIndex = _tab->index();

	_slideAnimation = std::make_unique<Ui::SlideAnimation>();
	_slideAnimation->setSnapshots(std::move(wasCache), std::move(nowCache));
	auto slideLeft = wasIndex > nowIndex;
	_slideAnimation->start(slideLeft, [this] { update(); }, st::slideDuration);
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
		if (_installed.widget()) _installed.widget()->setRemovedSets(_localRemoved);
		if (_featured.widget()) _featured.widget()->setRemovedSets(_localRemoved);
		if (_archived.widget()) _archived.widget()->setRemovedSets(_localRemoved);
		if (_attached.widget()) _attached.widget()->setRemovedSets(_localRemoved);
	}
	if (!(set->flags & MTPDstickerSet::Flag::f_installed_date)
		|| (set->flags & MTPDstickerSet::Flag::f_archived)) {
		_api.request(MTPmessages_InstallStickerSet(
			set->mtpInput(),
			MTP_boolFalse()
		)).done([=](const MTPmessages_StickerSetInstallResult &result) {
			installDone(result);
		}).fail([=](const RPCError &error) {
			installFail(error, setId);
		}).send();

		session().data().stickers().installLocally(setId);
	}
}

void StickersBox::installDone(const MTPmessages_StickerSetInstallResult &result) {
	if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
		session().data().stickers().applyArchivedResult(
			result.c_messages_stickerSetInstallResultArchive());
	}
}

void StickersBox::installFail(const RPCError &error, uint64 setId) {
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
		_archivedRequestId = _api.request(MTPmessages_GetArchivedStickers(
			MTP_flags(0),
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
	const auto &order = session().data().stickers().archivedSetsOrder();
	for (const auto setId : order) {
		auto it = sets.find(setId);
		if (it != sets.cend()) {
			const auto set = it->second.get();
			if (set->stickers.isEmpty() && (set->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
				session().api().scheduleStickerSetRequest(setId, set->access);
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
	if (_installed.widget()) _installed.widget()->resize(width(), _installed.widget()->height());
	if (_featured.widget()) _featured.widget()->resize(width(), _featured.widget()->height());
	if (_archived.widget()) _archived.widget()->resize(width(), _archived.widget()->height());
	if (_attached.widget()) _attached.widget()->resize(width(), _attached.widget()->height());
}

void StickersBox::handleStickersUpdated() {
	if (_section == Section::Installed || _section == Section::Featured) {
		rebuildList();
	} else {
		_tab->widget()->updateRows();
	}
	if (session().data().stickers().archivedSetsOrder().isEmpty()) {
		preloadArchivedSets();
	} else {
		refreshTabs();
	}
}

void StickersBox::rebuildList(Tab *tab) {
	if (_section == Section::Attached) return;
	if (!tab) tab = _tab;

	if (tab == &_installed) {
		_localOrder = tab->widget()->getFullOrder();
		_localRemoved = tab->widget()->getRemovedSets();
	}
	tab->widget()->rebuild();
	if (tab == &_installed) {
		tab->widget()->setFullOrder(_localOrder);
	}
	tab->widget()->setRemovedSets(_localRemoved);
}

void StickersBox::saveChanges() {
	// Make sure that our changes in other tabs are applied in the Installed tab.
	rebuildList(&_installed);

	if (_someArchivedLoaded) {
		session().local().writeArchivedStickers();
	}
	session().api().saveStickerSets(_installed.widget()->getOrder(), _installed.widget()->getRemovedSets());
}

void StickersBox::setInnerFocus() {
	if (_megagroupSet) {
		_installed.widget()->setInnerFocus();
	}
}

StickersBox::~StickersBox() = default;

StickersBox::Inner::Row::Row(
	not_null<StickersSet*> set,
	DocumentData *sticker,
	int32 count,
	const QString &title,
	int titleWidth,
	bool installed,
	bool official,
	bool unread,
	bool archived,
	bool removed,
	int32 pixw,
	int32 pixh)
: set(set)
, sticker(sticker)
, count(count)
, title(title)
, titleWidth(titleWidth)
, installed(installed)
, official(official)
, unread(unread)
, archived(archived)
, removed(removed)
, pixw(pixw)
, pixh(pixh) {
}

StickersBox::Inner::Row::~Row() = default;

bool StickersBox::Inner::Row::isRecentSet() const {
	return (set->id == Data::Stickers::CloudRecentSetId);
}

StickersBox::Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	StickersBox::Section section)
: RpWidget(parent)
, _controller(controller)
, _api(&_controller->session().mtp())
, _section(section)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _shiftingAnimation([=](crl::time now) {
	return shiftingAnimationCallback(now);
})
, _itemsTop(st::membersMarginTop)
, _addText(tr::lng_stickers_featured_add(tr::now).toUpper())
, _addWidth(st::stickersTrendingAdd.font->width(_addText))
, _undoText(tr::lng_stickers_return(tr::now).toUpper())
, _undoWidth(st::stickersUndoRemove.font->width(_undoText)) {
	setup();
}

StickersBox::Inner::Inner(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	not_null<ChannelData*> megagroup)
: RpWidget(parent)
, _controller(controller)
, _api(&_controller->session().mtp())
, _section(StickersBox::Section::Installed)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _shiftingAnimation([=](crl::time now) {
	return shiftingAnimationCallback(now);
})
, _itemsTop(st::membersMarginTop)
, _megagroupSet(megagroup)
, _megagroupSetInput(_megagroupSet->mgInfo->stickerSet)
, _megagroupSetField(
	this,
	st::groupStickersField,
	rpl::single(qsl("stickerset")),
	QString(),
	_controller->session().createInternalLink(QString()))
, _megagroupDivider(this)
, _megagroupSubTitle(this, tr::lng_stickers_group_from_your(tr::now), st::boxTitle) {
	_megagroupSetField->setLinkPlaceholder(
		_controller->session().createInternalLink(qsl("addstickers/")));
	_megagroupSetField->setPlaceholderHidden(false);
	_megagroupSetAddressChangedTimer.setCallback([this] { handleMegagroupSetAddressChange(); });
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
	return _controller->session();
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

QRect StickersBox::Inner::relativeButtonRect(bool removeButton) const {
	auto buttonw = st::stickersRemove.width;
	auto buttonh = st::stickersRemove.height;
	auto buttonshift = st::stickersRemoveSkip;
	if (!removeButton) {
		auto &st = (_section == Section::Installed) ? st::stickersUndoRemove : st::stickersTrendingAdd;
		auto textWidth = (_section == Section::Installed) ? _undoWidth : _addWidth;
		buttonw = textWidth - st.width;
		buttonh = st.height;
		buttonshift = 0;
	}
	auto buttonx = width() - st::contactsPadding.right() - st::contactsCheckPosition.x() - buttonw + buttonshift;
	auto buttony = st::contactsPadding.top() + (st::contactsPhotoSize - buttonh) / 2;
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
			p.fillRect(0, 0, width(), _rowHeight, st::contactsBgOver);
			if (row->ripple) {
				row->ripple->paint(p, 0, 0, width());
			}
		}
	}

	if (_section == Section::Installed) {
		if (index >= 0 && index == _above) {
			auto current = _aboveShadowFadeOpacity.current();
			if (_started >= 0) {
				auto reachedOpacity = aboveShadowOpacity();
				if (reachedOpacity > current) {
					_aboveShadowFadeOpacity = anim::value(reachedOpacity, reachedOpacity);
					current = reachedOpacity;
				}
			}
			auto rect = myrtlrect(st::contactsPadding.left() / 2, st::contactsPadding.top() / 2, width() - (st::contactsPadding.left() / 2) - _scrollbar - st::contactsPadding.left() / 2, _rowHeight - ((st::contactsPadding.top() + st::contactsPadding.bottom()) / 2));
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

	if (row->removed && _section == Section::Installed) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	}

	auto stickerx = st::contactsPadding.left();

	if (!_megagroupSet && _section == Section::Installed) {
		stickerx += st::stickersReorderIcon.width() + st::stickersReorderSkip;
		if (!row->isRecentSet()) {
			st::stickersReorderIcon.paint(p, st::contactsPadding.left(), (_rowHeight - st::stickersReorderIcon.height()) / 2, width());
		}
	}

	if (row->sticker) {
		paintRowThumbnail(p, row, stickerx);
	}

	int namex = stickerx + st::contactsPhotoSize + st::contactsPadding.left();
	int namey = st::contactsPadding.top() + st::contactsNameTop;

	int statusx = namex;
	int statusy = st::contactsPadding.top() + st::contactsStatusTop;

	p.setFont(st::contactsNameStyle.font);
	p.setPen(st::contactsNameFg);
	p.drawTextLeft(namex, namey, width(), row->title, row->titleWidth);

	if (row->unread) {
		p.setPen(Qt::NoPen);
		p.setBrush(st::stickersFeaturedUnreadBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(style::rtlrect(namex + row->titleWidth + st::stickersFeaturedUnreadSkip, namey + st::stickersFeaturedUnreadTop, st::stickersFeaturedUnreadSize, st::stickersFeaturedUnreadSize, width()));
		}
	}

	auto statusText = (row->count > 0) ? tr::lng_stickers_count(tr::now, lt_count, row->count) : tr::lng_contacts_loading(tr::now);

	p.setFont(st::contactsStatusFont);
	p.setPen(st::contactsStatusFg);
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
		row->set->access);
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
	validateLottieAnimation(row);
	if (!row->lottie) {
		const auto thumb = row->thumbnailMedia
			? row->thumbnailMedia->image()
			: row->stickerMedia
			? row->stickerMedia->thumbnail()
			: nullptr;
		if (!thumb) {
			return;
		}
		p.drawPixmapLeft(
			left + (st::contactsPhotoSize - row->pixw) / 2,
			st::contactsPadding.top() + (st::contactsPhotoSize - row->pixh) / 2,
			width(),
			thumb->pix(row->pixw, row->pixh));
	} else if (row->lottie->ready()) {
		const auto frame = row->lottie->frame();
		const auto size = frame.size() / cIntRetinaFactor();
		p.drawImage(
			QRect(
				left + (st::contactsPhotoSize - size.width()) / 2,
				st::contactsPadding.top() + (st::contactsPhotoSize - size.height()) / 2,
				size.width(),
				size.height()),
			frame);
		const auto paused = _controller->isGifPausedAtLeastFor(
			Window::GifPauseReason::Layer);
		if (!paused) {
			row->lottie->markFrameShown();
		}
	}
}

void StickersBox::Inner::validateLottieAnimation(not_null<Row*> row) {
	if (row->lottie
		|| !ChatHelpers::HasLottieThumbnail(
			row->thumbnailMedia.get(),
			row->stickerMedia.get())) {
		return;
	}
	auto player = ChatHelpers::LottieThumbnail(
		row->thumbnailMedia.get(),
		row->stickerMedia.get(),
		ChatHelpers::StickerLottieSize::SetsListThumbnail,
		QSize(
			st::contactsPhotoSize,
			st::contactsPhotoSize) * cIntRetinaFactor());
	if (!player) {
		return;
	}
	row->lottie = std::move(player);
	row->lottie->updates(
	) | rpl::start_with_next([=] {
		updateRowThumbnail(row);
	}, lifetime());
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
	const auto left = st::contactsPadding.left()
		+ ((!_megagroupSet && _section == Section::Installed)
			? st::stickersReorderIcon.width() + st::stickersReorderSkip
			: 0);
	update(
		left,
		rowTop + st::contactsPadding.top(),
		st::contactsPhotoSize,
		st::contactsPhotoSize);
}

void StickersBox::Inner::paintFakeButton(Painter &p, not_null<Row*> row, int index) {
	auto removeButton = (_section == Section::Installed && !row->removed);
	auto rect = relativeButtonRect(removeButton);
	if (_section != Section::Installed && row->installed && !row->archived && !row->removed) {
		// Checkbox after installed from Trending or Archived.
		int checkx = width() - (st::contactsPadding.right() + st::contactsCheckPosition.x() + (rect.width() + st::stickersFeaturedInstalled.width()) / 2);
		int checky = st::contactsPadding.top() + (st::contactsPhotoSize - st::stickersFeaturedInstalled.height()) / 2;
		st::stickersFeaturedInstalled.paint(p, QPoint(checkx, checky), width());
	} else {
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
			auto &st = (_section == Section::Installed) ? st::stickersUndoRemove : st::stickersTrendingAdd;
			auto textWidth = (_section == Section::Installed) ? _undoWidth : _addWidth;
			auto &text = (_section == Section::Installed) ? _undoText : _addText;
			auto &textBg = selected ? st.textBgOver : st.textBg;
			Ui::FillRoundRect(p, myrtlrect(rect), textBg, ImageRoundRadius::Small);
			if (row->ripple) {
				row->ripple->paint(p, rect.x(), rect.y(), width());
				if (row->ripple->empty()) {
					row->ripple.reset();
				}
			}
			p.setFont(st.font);
			p.setPen(selected ? st.textFgOver : st.textFg);
			p.drawTextLeft(rect.x() - (st.width / 2), rect.y() + st.textTop, width(), text, textWidth);
		}
	}
}

void StickersBox::Inner::mousePressEvent(QMouseEvent *e) {
	if (_dragging >= 0) mouseReleaseEvent(e);
	_mouse = e->globalPos();
	updateSelected();

	setPressed(_selected);
	if (_actionSel >= 0) {
		setActionDown(_actionSel);
		update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
	} else if (auto selectedIndex = std::get_if<int>(&_selected)) {
		if (_section == Section::Installed && !_rows[*selectedIndex]->isRecentSet() && _inDragArea) {
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
		auto removeButton = (_section == Section::Installed && !row->removed);
		if (!row->ripple) {
			if (_section == Section::Installed) {
				if (row->removed) {
					auto rippleSize = QSize(_undoWidth - st::stickersUndoRemove.width, st::stickersUndoRemove.height);
					auto rippleMask = Ui::RippleAnimation::roundRectMask(rippleSize, st::roundRadiusSmall);
					ensureRipple(st::stickersUndoRemove.ripple, std::move(rippleMask), removeButton);
				} else {
					auto rippleSize = st::stickersRemove.rippleAreaSize;
					auto rippleMask = Ui::RippleAnimation::ellipseMask(QSize(rippleSize, rippleSize));
					ensureRipple(st::stickersRemove.ripple, std::move(rippleMask), removeButton);
				}
			} else if (!row->installed || row->archived || row->removed) {
				auto rippleSize = QSize(_addWidth - st::stickersTrendingAdd.width, st::stickersTrendingAdd.height);
				auto rippleMask = Ui::RippleAnimation::roundRectMask(rippleSize, st::roundRadiusSmall);
				ensureRipple(st::stickersTrendingAdd.ripple, std::move(rippleMask), removeButton);
			}
		}
		if (row->ripple) {
			auto rect = relativeButtonRect(removeButton);
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
		auto rippleMask = Ui::RippleAnimation::rectMask(QSize(width(), _rowHeight));
		if (!set->ripple) {
			set->ripple = std::make_unique<Ui::RippleAnimation>(st::defaultRippleAnimation, std::move(rippleMask), [this, pressedIndex] {
				update(0, _itemsTop + pressedIndex * _rowHeight, width(), _rowHeight);
			});
		}
		set->ripple->add(mapFromGlobal(QCursor::pos()) - QPoint(0, _itemsTop + pressedIndex * _rowHeight));
	}
}

void StickersBox::Inner::ensureRipple(const style::RippleAnimation &st, QImage mask, bool removeButton) {
	_rows[_actionDown]->ripple = std::make_unique<Ui::RippleAnimation>(st, std::move(mask), [this, index = _actionDown, removeButton] {
		update(myrtlrect(relativeButtonRect(removeButton).translated(0, _itemsTop + index * _rowHeight)));
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
			if (!_megagroupSet && (_section == Section::Installed || !row->installed || row->archived || row->removed)) {
				auto removeButton = (_section == Section::Installed && !row->removed);
				auto rect = myrtlrect(relativeButtonRect(removeButton));
				actionSel = rect.contains(local) ? selectedIndex : -1;
			} else {
				actionSel = -1;
			}
			if (!_megagroupSet && _section == Section::Installed && !row->isRecentSet()) {
				auto dragAreaWidth = st::contactsPadding.left() + st::stickersReorderIcon.width() + st::stickersReorderSkip;
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
		: (!_megagroupSet && _section == Section::Installed)
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
	return qMin((dx + dy)  * 2. / _rowHeight, 1.);
}

void StickersBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = std::exchange(_pressed, SelectedRow());

	updateCursor();

	_mouse = e->globalPos();
	updateSelected();
	if (_actionDown == _actionSel && _actionSel >= 0) {
		if (_section == Section::Installed) {
			setRowRemoved(_actionDown, !_rows[_actionDown]->removed);
		} else if (_installSetCallback) {
			_installSetCallback(_rows[_actionDown]->set->id);
		}
	} else if (_dragging >= 0) {
		QPoint local(mapFromGlobal(_mouse));
		_rows[_dragging]->yadd.start(0.);
		_aboveShadowFadeStart = _shiftingStartTimes[_dragging] = crl::now();
		_aboveShadowFadeOpacity = anim::value(aboveShadowOpacity(), 0);
		if (!_shiftingAnimation.animating()) {
			_shiftingAnimation.start();
		}

		_dragging = _started = -1;
	} else if (pressed == _selected && _actionSel < 0 && _actionDown < 0) {
		const auto selectedIndex = [&] {
			if (auto index = std::get_if<int>(&_selected)) {
				return *index;
			}
			return -1;
		}();
		const auto showSetByRow = [&](const Row &row) {
			setSelected(SelectedRow());
			Ui::show(
				Box<StickerSetBox>(_controller, row.set->mtpInput()),
				Ui::LayerOption::KeepOther);
		};
		if (selectedIndex >= 0 && !_inDragArea) {
			const auto row = _rows[selectedIndex].get();
			if (!row->isRecentSet()) {
				if (_megagroupSet) {
					setMegagroupSelectedSet(row->set->mtpInput());
				} else {
					showSetByRow(*row);
				}
			}
		} else if (_megagroupSelectedSet && v::is<MegagroupSet>(_selected)) {
			showSetByRow(*_megagroupSelectedSet);
		}
	}
	setActionDown(-1);
}

void StickersBox::Inner::saveGroupSet() {
	Expects(_megagroupSet != nullptr);

	auto oldId = (_megagroupSet->mgInfo->stickerSet.type() == mtpc_inputStickerSetID)
		? _megagroupSet->mgInfo->stickerSet.c_inputStickerSetID().vid().v
		: 0;
	auto newId = (_megagroupSetInput.type() == mtpc_inputStickerSetID)
		? _megagroupSetInput.c_inputStickerSetID().vid().v
		: 0;
	if (newId != oldId) {
		session().api().setGroupStickerSet(_megagroupSet, _megagroupSetInput);
		session().api().stickerSetInstalled(Data::Stickers::MegagroupSetId);
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
		qstr("http://"),
		qstr("https://"),
		qstr("www.t.me/"),
		qstr("www.telegram.me/"),
		qstr("www.telegram.dog/"),
		qstr("t.me/"),
		qstr("telegram.me/"),
		qstr("telegram.dog/"),
		qstr("addstickers/"),
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
				setMegagroupSelectedSet(MTP_inputStickerSetEmpty());
			}
		}
	} else if (!_megagroupSetRequestId) {
		_megagroupSetRequestId = _api.request(MTPmessages_GetStickerSet(
			MTP_inputStickerSetShortName(MTP_string(text))
		)).done([=](const MTPmessages_StickerSet &result) {
			_megagroupSetRequestId = 0;
			auto set = session().data().stickers().feedSetFull(result);
			setMegagroupSelectedSet(MTP_inputStickerSetID(
				MTP_long(set->id),
				MTP_long(set->access)));
		}).fail([=](const RPCError &error) {
			_megagroupSetRequestId = 0;
			setMegagroupSelectedSet(MTP_inputStickerSetEmpty());
		}).send();
	} else {
		_megagroupSetAddressChangedTimer.callOnce(kHandleMegagroupSetAddressChangeTimeout);
	}
}

void StickersBox::Inner::rebuildMegagroupSet() {
	Expects(_megagroupSet != nullptr);
	if (_megagroupSetInput.type() != mtpc_inputStickerSetID) {
		if (_megagroupSelectedSet) {
			_megagroupSetField->setText(QString());
			_megagroupSetField->finishAnimating();
		}
		_megagroupSelectedSet.reset();
		_megagroupSelectedRemove.destroy();
		_megagroupSelectedShadow.destroy();
		return;
	}
	auto &inputId = _megagroupSetInput.c_inputStickerSetID();
	auto setId = inputId.vid().v;
	const auto &sets = session().data().stickers().sets();
	auto it = sets.find(setId);
	if (it == sets.cend()
		|| (it->second->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
		session().api().scheduleStickerSetRequest(
			inputId.vid().v,
			inputId.vaccess_hash().v);
		return;
	}

	const auto set = it->second.get();
	auto maxNameWidth = countMaxNameWidth();
	auto titleWidth = 0;
	auto title = fillSetTitle(set, maxNameWidth, &titleWidth);
	auto count = fillSetCount(set);
	auto sticker = (DocumentData*)nullptr;
	auto pixw = 0, pixh = 0;
	fillSetCover(set, &sticker, &pixw, &pixh);
	auto installed = true, official = false, unread = false, archived = false, removed = false;
	if (!_megagroupSelectedSet || _megagroupSelectedSet->set->id != set->id) {
		_megagroupSetField->setText(set->shortName);
		_megagroupSetField->finishAnimating();
	}
	_megagroupSelectedSet = std::make_unique<Row>(
		set,
		sticker,
		count,
		title,
		titleWidth,
		installed,
		official,
		unread,
		archived,
		removed,
		pixw,
		pixh);
	_itemsTop += st::lineWidth + _rowHeight;

	if (!_megagroupSelectedRemove) {
		_megagroupSelectedRemove.create(this, st::groupStickersRemove);
		_megagroupSelectedRemove->show(anim::type::instant);
		_megagroupSelectedRemove->setClickedCallback([this] {
			setMegagroupSelectedSet(MTP_inputStickerSetEmpty());
		});
		_megagroupSelectedShadow.create(this);
		updateControlsGeometry();
	}
}

void StickersBox::Inner::rebuild() {
	_itemsTop = st::membersMarginTop;

	if (_megagroupSet) {
		_itemsTop += st::groupStickersFieldPadding.top() + _megagroupSetField->height() + st::groupStickersFieldPadding.bottom();
		_itemsTop += _megagroupDivider->height() + st::groupStickersSubTitleHeight;
		rebuildMegagroupSet();
	}

	auto maxNameWidth = countMaxNameWidth();

	clear();
	const auto &order = ([&]() -> const StickersSetsOrder & {
		if (_section == Section::Installed) {
			auto &result = session().data().stickers().setsOrder();
			if (_megagroupSet && result.empty()) {
				return session().data().stickers().featuredSetsOrder();
			}
			return result;
		} else if (_section == Section::Featured) {
			return session().data().stickers().featuredSetsOrder();
		}
		return session().data().stickers().archivedSetsOrder();
	})();
	_rows.reserve(order.size() + 1);
	_shiftingStartTimes.reserve(order.size() + 1);

	const auto &sets = session().data().stickers().sets();
	if (_megagroupSet) {
		auto usingFeatured = session().data().stickers().setsOrder().empty();
		_megagroupSubTitle->setText(usingFeatured
			? tr::lng_stickers_group_from_featured(tr::now)
			: tr::lng_stickers_group_from_your(tr::now));
		updateControlsGeometry();
	} else if (_section == Section::Installed) {
		auto cloudIt = sets.find(Data::Stickers::CloudRecentSetId);
		if (cloudIt != sets.cend() && !cloudIt->second->stickers.isEmpty()) {
			rebuildAppendSet(cloudIt->second.get(), maxNameWidth);
		}
	}
	for (const auto setId : order) {
		auto it = sets.find(setId);
		if (it == sets.cend()) {
			continue;
		}

		const auto set = it->second.get();
		rebuildAppendSet(set, maxNameWidth);

		if (set->stickers.isEmpty()
			|| (set->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
			session().api().scheduleStickerSetRequest(set->id, set->access);
		}
	}
	session().api().requestStickerSets();
	updateSize();
}

void StickersBox::Inner::setMegagroupSelectedSet(const MTPInputStickerSet &set) {
	_megagroupSetInput = set;
	rebuild();
	scrollToY.notify(0, true);
	updateSelected();
}

void StickersBox::Inner::setMinHeight(int newWidth, int minHeight) {
	_minHeight = minHeight;
	updateSize(newWidth);
}

void StickersBox::Inner::updateSize(int newWidth) {
	auto naturalHeight = _itemsTop + int(_rows.size()) * _rowHeight + st::membersMarginBottom;
	resize(newWidth ? newWidth : width(), qMax(_minHeight, naturalHeight));
	updateControlsGeometry();
	checkLoadMore();
}

void StickersBox::Inner::updateRows() {
	int maxNameWidth = countMaxNameWidth();
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
			auto wasInstalled = row->installed;
			auto wasArchived = row->archived;
			fillSetFlags(set, &row->installed, &row->official, &row->unread, &row->archived);
			if (_section == Section::Installed) {
				row->archived = false;
			}
			if (row->installed != wasInstalled || row->archived != wasArchived) {
				row->ripple.reset();
			}
		}
		row->title = fillSetTitle(set, maxNameWidth, &row->titleWidth);
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
	rebuildAppendSet(set, countMaxNameWidth());
	return true;
}

int StickersBox::Inner::countMaxNameWidth() const {
	int namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	if (!_megagroupSet && _section == Section::Installed) {
		namex += st::stickersReorderIcon.width() + st::stickersReorderSkip;
	}
	int namew = st::boxWideWidth - namex - st::contactsPadding.right() - st::contactsCheckPosition.x();
	if (_section == Section::Installed) {
		if (!_megagroupSet) {
			namew -= _undoWidth - st::stickersUndoRemove.width;
		}
	} else {
		namew -= _addWidth - st::stickersTrendingAdd.width;
		if (_section == Section::Featured) {
			namew -= st::stickersFeaturedUnreadSize + st::stickersFeaturedUnreadSkip;
		}
	}
	return namew;
}

void StickersBox::Inner::rebuildAppendSet(
		not_null<StickersSet*> set,
		int maxNameWidth) {
	bool installed = true, official = true, unread = false, archived = false, removed = false;
	if (set->id != Data::Stickers::CloudRecentSetId) {
		fillSetFlags(set, &installed, &official, &unread, &archived);
	}
	if (_section == Section::Installed && archived) {
		return;
	}

	DocumentData *sticker = nullptr;
	int pixw = 0, pixh = 0;
	fillSetCover(set, &sticker, &pixw, &pixh);

	int titleWidth = 0;
	QString title = fillSetTitle(set, maxNameWidth, &titleWidth);
	int count = fillSetCount(set);

	_rows.push_back(std::make_unique<Row>(
		set,
		sticker,
		count,
		title,
		titleWidth,
		installed,
		official,
		unread,
		archived,
		removed,
		pixw,
		pixh));
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
	if (pixw > st::contactsPhotoSize) {
		if (pixw > pixh) {
			pixh = (pixh * st::contactsPhotoSize) / pixw;
			pixw = st::contactsPhotoSize;
		} else {
			pixw = (pixw * st::contactsPhotoSize) / pixh;
			pixh = st::contactsPhotoSize;
		}
	} else if (pixh > st::contactsPhotoSize) {
		pixw = (pixw * st::contactsPhotoSize) / pixh;
		pixh = st::contactsPhotoSize;
	}
	*outWidth = pixw;
	*outHeight = pixh;
}

int StickersBox::Inner::fillSetCount(not_null<StickersSet*> set) const {
	int result = set->stickers.isEmpty()
		? set->count
		: set->stickers.size();
	auto added = 0;
	if (set->id == Data::Stickers::CloudRecentSetId) {
		const auto &sets = session().data().stickers().sets();
		auto customIt = sets.find(Data::Stickers::CustomSetId);
		if (customIt != sets.cend()) {
			added = customIt->second->stickers.size();
			const auto &recent = session().data().stickers().getRecentPack();
			for (const auto &sticker : recent) {
				if (customIt->second->stickers.indexOf(sticker.first) < 0) {
					++added;
				}
			}
		} else {
			added = session().data().stickers().getRecentPack().size();
		}
	}
	return result + added;
}

QString StickersBox::Inner::fillSetTitle(
		not_null<StickersSet*> set,
		int maxNameWidth,
		int *outTitleWidth) const {
	auto result = set->title;
	int titleWidth = st::contactsNameStyle.font->width(result);
	if (titleWidth > maxNameWidth) {
		result = st::contactsNameStyle.font->elided(result, maxNameWidth);
		titleWidth = st::contactsNameStyle.font->width(result);
	}
	if (outTitleWidth) {
		*outTitleWidth = titleWidth;
	}
	return result;
}

void StickersBox::Inner::fillSetFlags(
		not_null<StickersSet*> set,
		bool *outInstalled,
		bool *outOfficial,
		bool *outUnread,
		bool *outArchived) {
	*outInstalled = (set->flags & MTPDstickerSet::Flag::f_installed_date);
	*outOfficial = (set->flags & MTPDstickerSet::Flag::f_official);
	*outArchived = (set->flags & MTPDstickerSet::Flag::f_archived);
	if (_section == Section::Featured) {
		*outUnread = (set->flags & MTPDstickerSet_ClientFlag::f_unread);
	} else {
		*outUnread = false;
	}
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

StickersSetsOrder StickersBox::Inner::getOrder() const {
	return collectSets([](Row *row) {
		return !row->archived && !row->removed && !row->isRecentSet();
	});
}

StickersSetsOrder StickersBox::Inner::getFullOrder() const {
	return collectSets([](Row *row) {
		return !row->isRecentSet();
	});
}

StickersSetsOrder StickersBox::Inner::getRemovedSets() const {
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
	for_const (auto setId, order) {
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
		if (!row->unread) {
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
