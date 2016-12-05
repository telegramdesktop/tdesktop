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
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "stickers_box.h"

#include "lang.h"
#include "mainwidget.h"
#include "stickers/stickers.h"
#include "boxes/confirmbox.h"
#include "boxes/stickersetbox.h"
#include "apiwrap.h"
#include "localstorage.h"
#include "dialogs/dialogs_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_stickers.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/scroll_area.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/slide_animation.h"
#include "ui/widgets/discrete_sliders.h"

namespace {

constexpr int kArchivedLimitFirstRequest = 10;
constexpr int kArchivedLimitPerPage = 30;

} // namespace

int stickerPacksCount(bool includeArchivedOfficial) {
	auto result = 0;
	auto &order = Global::StickerSetsOrder();
	auto &sets = Global::StickerSets();
	for (auto i = 0, l = order.size(); i < l; ++i) {
		auto it = sets.constFind(order.at(i));
		if (it != sets.cend()) {
			if (!(it->flags & MTPDstickerSet::Flag::f_archived) || ((it->flags & MTPDstickerSet::Flag::f_official) && includeArchivedOfficial)) {
				++result;
			}
		}
	}
	return result;
}

class StickersBox::CounterWidget : public TWidget, private base::Subscriber {
public:
	CounterWidget(QWidget *parent);

	void setCounter(int counter);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void updateCounter();

	QString _text;
	Dialogs::Layout::UnreadBadgeStyle _st;

};

StickersBox::CounterWidget::CounterWidget(QWidget *parent) : TWidget(parent) {
	setAttribute(Qt::WA_TransparentForMouseEvents);

	_st.sizeId = Dialogs::Layout::UnreadBadgeInStickersBox;
	_st.textTop = st::stickersFeaturedBadgeTextTop;
	_st.size = st::stickersFeaturedBadgeSize;
	_st.padding = st::stickersFeaturedBadgePadding;
	_st.font = st::stickersFeaturedBadgeFont;

	subscribe(Global::RefFeaturedStickerSetsUnreadCountChanged(), [this] { updateCounter(); });
	updateCounter();
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

void StickersBox::CounterWidget::updateCounter() {
	setCounter(Global::FeaturedStickerSetsUnreadCount());
	update();
}

StickersBox::StickersBox(Section section) : ItemListBox(st::boxScroll)
, _topShadow(this, st::shadowColor)
, _tabs(this, st::defaultTabsSlider)
, _unreadBadge(this)
, _section(section)
, _installed(0, this, Section::Installed)
, _featured(1, this, Section::Featured)
, _archived(2, this, Section::Archived) {
	setup();
}

StickersBox::StickersBox(const Stickers::Order &archivedIds) : ItemListBox(st::boxScroll)
, _topShadow(this, st::shadowColor)
, _section(Section::ArchivedPart)
, _archived(0, this, archivedIds)
, _aboutWidth(st::boxWideWidth - 2 * st::stickersReorderPadding.top())
, _about(st::boxTextFont, lang(lng_stickers_packs_archived), _defaultOptions, _aboutWidth) {
	setup();
}

void StickersBox::getArchivedDone(uint64 offsetId, const MTPmessages_ArchivedStickers &result) {
	_archivedRequestId = 0;
	_archivedLoaded = true;
	if (result.type() != mtpc_messages_archivedStickers) {
		return;
	}

	auto &stickers = result.c_messages_archivedStickers();
	auto &archived = Global::RefArchivedStickerSetsOrder();
	if (offsetId) {
		auto index = archived.indexOf(offsetId);
		if (index >= 0) {
			archived = archived.mid(0, index + 1);
		}
	} else {
		archived.clear();
	}

	bool addedSet = false;
	auto &v = stickers.vsets.c_vector().v;
	for_const (auto &stickerSet, v) {
		const MTPDstickerSet *setData = nullptr;
		switch (stickerSet.type()) {
		case mtpc_stickerSetCovered: {
			auto &d = stickerSet.c_stickerSetCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				setData = &d.vset.c_stickerSet();
			}
		} break;
		case mtpc_stickerSetMultiCovered: {
			auto &d = stickerSet.c_stickerSetMultiCovered();
			if (d.vset.type() == mtpc_stickerSet) {
				setData = &d.vset.c_stickerSet();
			}
		} break;
		}
		if (!setData) continue;

		if (auto set = Stickers::feedSet(*setData)) {
			auto index = archived.indexOf(set->id);
			if (archived.isEmpty() || index != archived.size() - 1) {
				if (index < archived.size() - 1) {
					archived.removeAt(index);
				}
				archived.push_back(set->id);
			}
			if (_archived.widget->appendSet(*set)) {
				addedSet = true;
				if (set->stickers.isEmpty() || (set->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
					App::api()->scheduleStickerSetRequest(set->id, set->access);
				}
			}
		}
	}
	if (addedSet) {
		_archived.widget->updateSize();
		_archived.widget->setVisibleScrollbar((scrollArea()->scrollTopMax() > 0) ? (st::boxScroll.width - st::boxScroll.deltax) : 0);
	} else {
		_allArchivedLoaded = v.isEmpty() || (offsetId != 0);
	}

	refreshTabs();
	_someArchivedLoaded = true;
	if (_section == Section::Archived && addedSet) {
		App::api()->requestStickerSets();
	}
	checkLoadMoreArchived();
}

void StickersBox::setup() {
	if (_section == Section::Installed) {
		Local::readArchivedStickers();
	} else if (_section == Section::Archived) {
		requestArchivedSets();
	} else if (_section == Section::ArchivedPart) {
		setTitleText(lang(lng_stickers_archived));
	}
	if (Global::ArchivedStickerSetsOrder().isEmpty()) {
		preloadArchivedSets();
	}
	if (_tabs) {
		_tabs->setSectionActivatedCallback([this] {
			switchTab();
		});
		refreshTabs();
	}
	if (_installed.widget && _section != Section::Installed) _installed.widget->hide();
	if (_featured.widget && _section != Section::Featured) _featured.widget->hide();
	if (_section != Section::Archived && _section != Section::ArchivedPart) _archived.widget->hide();

	if (_featured.widget) {
		_featured.widget->setInstallSetCallback([this](uint64 setId) { installSet(setId); });
	}
	_archived.widget->setInstallSetCallback([this](uint64 setId) { installSet(setId); });

	_done.create(this, lang(lng_about_done), st::defaultBoxButton);
	connect(_done, SIGNAL(clicked()), this, SLOT(onClose()));

	auto bottomSkip = st::boxButtonPadding.top() + _done->height() + st::boxButtonPadding.bottom();
	if (_section == Section::Installed) {
		_bottomShadow.create(this);
		_tab = &_installed;
	} else if (_section == Section::ArchivedPart) {
		_aboutHeight = st::stickersReorderPadding.top() + _about.countHeight(_aboutWidth) + st::stickersReorderPadding.bottom();
		_tab = &_archived;
	} else if (_section == Section::Archived) {
		_bottomShadow.create(this);
		_tab = &_archived;
	} else { // _section == Section::Featured
		_bottomShadow.create(this);
		_tab = &_featured;
	}
	ItemListBox::init(_tab->widget, bottomSkip, titleHeight() + _aboutHeight);
	setMaxHeight((_section == Section::ArchivedPart) ? st::sessionsHeight : st::boxMaxListHeight);

	connect(App::main(), SIGNAL(stickersUpdated()), this, SLOT(onStickersUpdated()));
	App::main()->updateStickers();

	if (_installed.widget) {
		connect(_installed.widget, SIGNAL(checkDraggingScroll(int)), this, SLOT(onCheckDraggingScroll(int)));
		connect(_installed.widget, SIGNAL(noDraggingScroll()), this, SLOT(onNoDraggingScroll()));
	}
	connect(&_scrollTimer, SIGNAL(timeout()), this, SLOT(onScrollTimer()));
	connect(scrollArea(), SIGNAL(scrolled()), this, SLOT(onScroll()));
	_scrollTimer.setSingleShot(false);

	rebuildList();

	raiseShadow();
}

void StickersBox::refreshTabs() {
	if (!_tabs) return;

	_tabIndices.clear();
	auto sections = QStringList();
	sections.push_back(lang(lng_stickers_installed_tab).toUpper());
	_tabIndices.push_back(Section::Installed);
	if (!Global::FeaturedStickerSetsOrder().isEmpty()) {
		sections.push_back(lang(lng_stickers_featured_tab).toUpper());
		_tabIndices.push_back(Section::Featured);
	}
	if (!Global::ArchivedStickerSetsOrder().isEmpty()) {
		sections.push_back(lang(lng_stickers_archived_tab).toUpper());
		_tabIndices.push_back(Section::Archived);
	}
	_tabs->setSections(sections);
	if ((_tab == &_archived && !_tabIndices.contains(Section::Archived))
		|| (_tab == &_featured && !_tabIndices.contains(Section::Featured))) {
		switchTab();
	}
	updateTabsGeometry();
	setBlockTitle(true, (sections.size() < 3), false);
}

void StickersBox::onScroll() {
	updateVisibleTopBottom();
	checkLoadMoreArchived();
}

void StickersBox::updateVisibleTopBottom() {
	auto visibleTop = scrollArea()->scrollTop();
	auto visibleBottom = visibleTop + scrollArea()->height();
	_tab->widget->setVisibleTopBottom(visibleTop, visibleBottom);
}

void StickersBox::checkLoadMoreArchived() {
	if (_section != Section::Archived) return;

	int scrollTop = scrollArea()->scrollTop(), scrollTopMax = scrollArea()->scrollTopMax();
	if (scrollTop + PreloadHeightsCount * scrollArea()->height() >= scrollTopMax) {
		if (!_archivedRequestId && !_allArchivedLoaded) {
			uint64 lastId = 0;
			for (auto setId = Global::ArchivedStickerSetsOrder().cend(), e = Global::ArchivedStickerSetsOrder().cbegin(); setId != e;) {
				--setId;
				auto it = Global::StickerSets().constFind(*setId);
				if (it != Global::StickerSets().cend()) {
					if (it->flags & MTPDstickerSet::Flag::f_archived) {
						lastId = it->id;
						break;
					}
				}
			}
			MTPmessages_GetArchivedStickers::Flags flags = 0;
			_archivedRequestId = MTP::send(MTPmessages_GetArchivedStickers(MTP_flags(flags), MTP_long(lastId), MTP_int(kArchivedLimitPerPage)), rpcDone(&StickersBox::getArchivedDone, lastId));
		}
	}
}

void StickersBox::paintEvent(QPaintEvent *e) {
	AbstractBox::paintEvent(e);

	Painter p(this);

	p.translate(0, titleHeight());

	if (_aboutHeight > 0) {
		p.fillRect(0, 0, width(), _aboutHeight, st::contactsAboutBg);
		p.setPen(st::stickersReorderFg);
		_about.draw(p, st::stickersReorderPadding.top(), st::stickersReorderPadding.top(), _aboutWidth, style::al_center);
	}

	if (_slideAnimation) {
		_slideAnimation->paintFrame(p, scrollArea()->x(), scrollArea()->y() - titleHeight(), width(), getms());
		if (!_slideAnimation->animating()) {
			_slideAnimation.reset();
			scrollArea()->show();
			update();
		}
	}
}

void StickersBox::updateTabsGeometry() {
	if (!_tabs) return;

	_tabs->resizeToWidth(_tabIndices.size() * width() / 3);
	_unreadBadge->setVisible(_tabIndices.contains(Section::Featured));

	auto featuredLeft = width() / 3;
	auto featuredRight = 2 * width() / 3;
	auto featuredTextWidth = st::defaultTabsSlider.labelFont->width(lang(lng_stickers_featured_tab).toUpper());
	auto featuredTextRight = featuredLeft + (featuredRight - featuredLeft - featuredTextWidth) / 2 + featuredTextWidth;
	auto unreadBadgeLeft = featuredTextRight - st::stickersFeaturedBadgeSkip;
	auto unreadBadgeTop = st::stickersFeaturedBadgeTop;
	if (unreadBadgeLeft + _unreadBadge->width() > featuredRight) {
		unreadBadgeLeft = featuredRight - _unreadBadge->width();
	}
	_unreadBadge->moveToLeft(unreadBadgeLeft, unreadBadgeTop);

	_tabs->moveToLeft(0, 0);
}

void StickersBox::switchTab() {
	if (!_tabs) return;

	auto tab = _tabs->activeSection();
	t_assert(tab >= 0 && tab < _tabIndices.size());
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
	if (_tab != newTab) {
		if (_tab == &_installed) {
			_localOrder = _tab->widget->getFullOrder();
			_localRemoved = _tab->widget->getRemovedSets();
		}
		auto scroll = scrollArea();
		auto wasCache = grabContentCache();
		auto wasIndex = _tab->index;
		_tab->scrollTop = scroll->scrollTop();
		auto widget = scroll->takeWidget();
		widget->setParent(this);
		widget->hide();
		_tab = newTab;
		_section = newSection;
		scroll->setOwnedWidget(newTab->widget);
		_tab->widget->show();
		rebuildList();
		scroll->scrollToY(_tab->scrollTop);
		auto nowCache = grabContentCache();
		auto nowIndex = _tab->index;

		_slideAnimation = std_::make_unique<Ui::SlideAnimation>();
		_slideAnimation->setSnapshots(std_::move(wasCache), std_::move(nowCache));
		auto slideLeft = wasIndex > nowIndex;
		_slideAnimation->start(slideLeft, [this] { update(); }, st::slideDuration);
		scrollArea()->hide();
		update();
	}
}

QPixmap StickersBox::grabContentCache() {
	_topShadow->hide();
	_bottomShadow->hide();
	_tabs->hide();
	auto result = myGrab(this, scrollArea()->geometry());
	_topShadow->show();
	_bottomShadow->show();
	_tabs->show();
	return std_::move(result);
}

void StickersBox::installSet(uint64 setId) {
	auto &sets = Global::RefStickerSets();
	auto it = sets.find(setId);
	if (it == sets.cend()) {
		rebuildList();
		return;
	}

	if (_localRemoved.contains(setId)) {
		_localRemoved.removeOne(setId);
		if (_installed.widget) _installed.widget->setRemovedSets(_localRemoved);
		if (_featured.widget) _featured.widget->setRemovedSets(_localRemoved);
		_archived.widget->setRemovedSets(_localRemoved);
	}
	if (!(it->flags & MTPDstickerSet::Flag::f_installed) || (it->flags & MTPDstickerSet::Flag::f_archived)) {
		MTP::send(MTPmessages_InstallStickerSet(Stickers::inputSetId(*it), MTP_boolFalse()), rpcDone(&StickersBox::installDone), rpcFail(&StickersBox::installFail, setId));

		Stickers::installLocally(setId);
	}
}

void StickersBox::installDone(const MTPmessages_StickerSetInstallResult &result) {
	if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
		Stickers::applyArchivedResult(result.c_messages_stickerSetInstallResultArchive());
	}
}

bool StickersBox::installFail(uint64 setId, const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	auto &sets = Global::RefStickerSets();
	auto it = sets.find(setId);
	if (it == sets.cend()) {
		rebuildList();
		return true;
	}

	Stickers::undoInstallLocally(setId);
	return true;
}

void StickersBox::preloadArchivedSets() {
	if (!_archivedRequestId) {
		MTPmessages_GetArchivedStickers::Flags flags = 0;
		_archivedRequestId = MTP::send(MTPmessages_GetArchivedStickers(MTP_flags(flags), MTP_long(0), MTP_int(kArchivedLimitFirstRequest)), rpcDone(&StickersBox::getArchivedDone, 0ULL));
	}
}

void StickersBox::requestArchivedSets() {
	// Reload the archived list.
	if (!_archivedLoaded) {
		preloadArchivedSets();
	}

	auto &sets = Global::StickerSets();
	for_const (auto setId, Global::ArchivedStickerSetsOrder()) {
		auto it = sets.constFind(setId);
		if (it != sets.cend()) {
			if (it->stickers.isEmpty() && (it->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
				App::api()->scheduleStickerSetRequest(setId, it->access);
			}
		}
	}
	App::api()->requestStickerSets();
}

void StickersBox::resizeEvent(QResizeEvent *e) {
	ItemListBox::resizeEvent(e);
	if (_tabs) {
		updateTabsGeometry();
		_topShadow->setGeometryToLeft(0, _tabs->height() - st::lineWidth, width(), st::lineWidth);
	} else {
		_topShadow->setGeometryToLeft(0, scrollArea()->y(), width(), st::lineWidth);
	}
	if (_installed.widget) _installed.widget->resize(width(), _installed.widget->height());
	if (_featured.widget) _featured.widget->resize(width(), _featured.widget->height());
	_archived.widget->resize(width(), _archived.widget->height());
	_tab->widget->setVisibleScrollbar((scrollArea()->scrollTopMax() > 0) ? (st::boxScroll.width - st::boxScroll.deltax) : 0);
	updateVisibleTopBottom();
	if (_done) {
		_done->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _done->height());
	}
	if (_bottomShadow) {
		_bottomShadow->setGeometry(0, height() - st::boxButtonPadding.bottom() - _done->height() - st::boxButtonPadding.top() - st::lineWidth, width(), st::lineWidth);
	}
}

void StickersBox::onStickersUpdated() {
	if (_section == Section::Installed || _section == Section::Featured) {
		rebuildList();
	} else {
		_tab->widget->updateRows();
	}
	if (Global::ArchivedStickerSetsOrder().isEmpty()) {
		preloadArchivedSets();
	} else {
		refreshTabs();
	}
}

void StickersBox::rebuildList() {
	if (_tab == &_installed) {
		_localOrder = _tab->widget->getFullOrder();
		_localRemoved = _tab->widget->getRemovedSets();
	}
	_tab->widget->rebuild();
	if (_tab == &_installed) {
		_tab->widget->setFullOrder(_localOrder);
	}
	_tab->widget->setRemovedSets(_localRemoved);
	_tab->widget->setVisibleScrollbar((scrollArea()->scrollTopMax() > 0) ? (st::boxScroll.width - st::boxScroll.deltax) : 0);
}

void StickersBox::onCheckDraggingScroll(int localY) {
	if (localY < scrollArea()->scrollTop()) {
		_scrollDelta = localY - scrollArea()->scrollTop();
	} else if (localY >= scrollArea()->scrollTop() + scrollArea()->height()) {
		_scrollDelta = localY - scrollArea()->scrollTop() - scrollArea()->height() + 1;
	} else {
		_scrollDelta = 0;
	}
	if (_scrollDelta) {
		_scrollTimer.start(15);
	} else {
		_scrollTimer.stop();
	}
}

void StickersBox::onNoDraggingScroll() {
	_scrollTimer.stop();
}

void StickersBox::onScrollTimer() {
	int32 d = (_scrollDelta > 0) ? qMin(_scrollDelta * 3 / 20 + 1, int32(MaxScrollSpeed)) : qMax(_scrollDelta * 3 / 20 - 1, -int32(MaxScrollSpeed));
	scrollArea()->scrollToY(scrollArea()->scrollTop() + d);
}

void StickersBox::closePressed() {
	if (!_installed.widget) {
		return;
	}
	if (_someArchivedLoaded) {
		Local::writeArchivedStickers();
	}
	if (auto api = App::api()) {
		api->saveStickerSets(_installed.widget->getOrder(), _installed.widget->getRemovedSets());
	}
}

StickersBox::~StickersBox() = default;

StickersBox::Inner::Inner(QWidget *parent, StickersBox::Section section) : TWidget(parent)
, _section(section)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _a_shifting(animation(this, &Inner::step_shifting))
, _itemsTop(st::membersPadding.top())
, _addText(lang(lng_stickers_featured_add).toUpper())
, _addWidth(st::stickersTrendingAdd.font->width(_addText))
, _undoText(lang(lng_stickers_return).toUpper())
, _undoWidth(st::stickersUndoRemove.font->width(_undoText))
, _aboveShadow(st::boxShadow) {
	setup();
}

StickersBox::Inner::Inner(QWidget *parent, const Stickers::Order &archivedIds) : TWidget(parent)
, _section(StickersBox::Section::ArchivedPart)
, _archivedIds(archivedIds)
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _a_shifting(animation(this, &Inner::step_shifting))
, _itemsTop(st::membersPadding.top())
, _addText(lang(lng_stickers_featured_add).toUpper())
, _addWidth(st::stickersTrendingAdd.font->width(_addText))
, _aboveShadow(st::boxShadow) {
	setup();
}

void StickersBox::Inner::setup() {
	subscribe(FileDownload::ImageLoaded(), [this] {
		update();
		readVisibleSets();
	});
	setMouseTracking(true);
}

void StickersBox::Inner::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	Painter p(this);

	_a_shifting.step();

	auto ms = getms();
	p.fillRect(r, st::boxBg);
	p.setClipRect(r);

	int y = st::membersPadding.top();
	if (_rows.isEmpty()) {
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(QRect(0, y, width(), st::noContactsHeight), lang(lng_contacts_loading), style::al_center);
	} else {
		p.translate(0, _itemsTop);

		int32 yFrom = r.y() - _itemsTop, yTo = r.y() + r.height() - _itemsTop;
		int32 from = floorclamp(yFrom - _rowHeight, _rowHeight, 0, _rows.size());
		int32 to = ceilclamp(yTo + _rowHeight, _rowHeight, 0, _rows.size());
		p.translate(0, from * _rowHeight);
		for (int32 i = from; i < to; ++i) {
			if (i != _above) {
				paintRow(p, i, ms);
			}
			p.translate(0, _rowHeight);
		}
		if (from <= _above && _above < to) {
			p.translate(0, (_above - to) * _rowHeight);
			paintRow(p, _above, ms);
		}
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

void StickersBox::Inner::paintRow(Painter &p, int index, TimeMs ms) {
	auto s = _rows.at(index);

	auto xadd = 0, yadd = qRound(s->yadd.current());
	if (xadd || yadd) p.translate(xadd, yadd);

	if (_section == Section::Installed) {
		paintFakeButton(p, index, ms);
		if (index == _above) {
			float64 current = _aboveShadowFadeOpacity.current();
			if (_started >= 0) {
				float64 o = aboveShadowOpacity();
				if (o > current) {
					_aboveShadowFadeOpacity = anim::value(o, o);
					current = o;
				}
			}
			p.setOpacity(current);
			QRect row(myrtlrect(_aboveShadow.getDimensions(st::boxShadowShift).left(), st::contactsPadding.top() / 2, width() - (st::contactsPadding.left() / 2) - _scrollbar - _aboveShadow.getDimensions(st::boxShadowShift).right(), _rowHeight - ((st::contactsPadding.top() + st::contactsPadding.bottom()) / 2)));
			_aboveShadow.paint(p, row, st::boxShadowShift);
			p.fillRect(row, st::boxBg);
			p.setOpacity(1);
		}
	} else {
		paintFakeButton(p, index, ms);
	}

	if (s->removed && _section == Section::Installed) {
		p.setOpacity(st::stickersRowDisabledOpacity);
	}

	auto stickerx = st::contactsPadding.left();

	if (_section == Section::Installed) {
		stickerx += st::stickersReorderIcon.width() + st::stickersReorderSkip;
		if (!s->isRecentSet()) {
			st::stickersReorderIcon.paint(p, st::contactsPadding.left(), (_rowHeight - st::stickersReorderIcon.height()) / 2, width());
		}
	}

	if (s->sticker) {
		s->sticker->thumb->load();
		QPixmap pix(s->sticker->thumb->pix(s->pixw, s->pixh));
		p.drawPixmapLeft(stickerx + (st::contactsPhotoSize - s->pixw) / 2, st::contactsPadding.top() + (st::contactsPhotoSize - s->pixh) / 2, width(), pix);
	}

	int namex = stickerx + st::contactsPhotoSize + st::contactsPadding.left();
	int namey = st::contactsPadding.top() + st::contactsNameTop;

	int statusx = namex;
	int statusy = st::contactsPadding.top() + st::contactsStatusTop;

	p.setFont(st::contactsNameFont);
	p.setPen(st::contactsNameFg);
	p.drawTextLeft(namex, namey, width(), s->title, s->titleWidth);

	if (s->unread) {
		p.setPen(Qt::NoPen);
		p.setBrush(st::stickersFeaturedUnreadBg);

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(rtlrect(namex + s->titleWidth + st::stickersFeaturedUnreadSkip, namey + st::stickersFeaturedUnreadTop, st::stickersFeaturedUnreadSize, st::stickersFeaturedUnreadSize, width()));
		}
	}

	p.setFont(st::contactsStatusFont);
	p.setPen(st::contactsStatusFg);
	p.drawTextLeft(statusx, statusy, width(), lng_stickers_count(lt_count, s->count));

	p.setOpacity(1);
	if (xadd || yadd) p.translate(-xadd, -yadd);
}

void StickersBox::Inner::paintFakeButton(Painter &p, int index, TimeMs ms) {
	auto set = _rows[index];
	auto removeButton = (_section == Section::Installed && !set->removed);
	auto rect = relativeButtonRect(removeButton);
	if (_section != Section::Installed && set->installed && !set->archived && !set->removed) {
		// Checkbox after installed from Trending or Archived.
		int checkx = width() - (st::contactsPadding.right() + st::contactsCheckPosition.x() + (rect.width() + st::stickersFeaturedInstalled.width()) / 2);
		int checky = st::contactsPadding.top() + (st::contactsPhotoSize - st::stickersFeaturedInstalled.height()) / 2;
		st::stickersFeaturedInstalled.paint(p, QPoint(checkx, checky), width());
	} else {
		auto selected = (index == _actionSel && _actionDown < 0) || (index == _actionDown);
		if (removeButton) {
			// Trash icon button when not disabled in Installed.
			if (set->ripple) {
				set->ripple->paint(p, rect.x(), rect.y(), width(), ms);
				if (set->ripple->empty()) {
					set->ripple.reset();
				}
			}
			auto &icon = selected ? st::stickersRemove.iconOver : st::stickersRemove.icon;
			auto position = st::stickersRemove.iconPosition;
			if (position.x() < 0) position.setX((rect.width() - icon.width()) / 2);
			if (position.y() < 0) position.setY((rect.height() - icon.height()) / 2);
			icon.paint(p, rect.topLeft() + position, ms);
		} else {
			// Round button ADD when not installed from Trending or Archived.
			// Or round button UNDO after disabled from Installed.
			auto &st = (_section == Section::Installed) ? st::stickersUndoRemove : st::stickersTrendingAdd;
			auto textWidth = (_section == Section::Installed) ? _undoWidth : _addWidth;
			auto &text = (_section == Section::Installed) ? _undoText : _addText;
			auto &textBg = selected ? st.textBgOver : st.textBg;
			App::roundRect(p, myrtlrect(rect), textBg, ImageRoundRadius::Small);
			if (set->ripple) {
				set->ripple->paint(p, rect.x(), rect.y(), width(), ms);
				if (set->ripple->empty()) {
					set->ripple.reset();
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
	onUpdateSelected();

	_pressed = _selected;
	if (_actionSel >= 0) {
		setActionDown(_actionSel);
		update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
	} else if (_selected >= 0 && _section == Section::Installed && !_rows.at(_selected)->isRecentSet() && _inDragArea) {
		_above = _dragging = _started = _selected;
		_dragStart = mapFromGlobal(_mouse);
	}
}

void StickersBox::Inner::setActionDown(int newActionDown) {
	if (_actionDown == newActionDown) {
		return;
	}
	if (_actionDown >= 0 && _actionDown < _rows.size()) {
		update(0, _itemsTop + _actionDown * _rowHeight, width(), _rowHeight);
		auto set = _rows[_actionDown];
		if (set->ripple) {
			set->ripple->lastStop();
		}
	}
	_actionDown = newActionDown;
	if (_actionDown >= 0 && _actionDown < _rows.size()) {
		update(0, _itemsTop + _actionDown * _rowHeight, width(), _rowHeight);
		auto set = _rows[_actionDown];
		auto removeButton = (_section == Section::Installed && !set->removed);
		if (!set->ripple) {
			if (_section == Section::Installed) {
				if (set->removed) {
					auto rippleSize = QSize(_undoWidth - st::stickersUndoRemove.width, st::stickersUndoRemove.height);
					auto rippleMask = Ui::RippleAnimation::roundRectMask(rippleSize, st::buttonRadius);
					ensureRipple(st::stickersUndoRemove.ripple, std_::move(rippleMask), removeButton);
				} else {
					auto rippleSize = st::stickersRemove.rippleAreaSize;
					auto rippleMask = Ui::RippleAnimation::ellipseMask(QSize(rippleSize, rippleSize));
					ensureRipple(st::stickersRemove.ripple, std_::move(rippleMask), removeButton);
				}
			} else if (!set->installed || set->archived || set->removed) {
				auto rippleSize = QSize(_addWidth - st::stickersTrendingAdd.width, st::stickersTrendingAdd.height);
				auto rippleMask = Ui::RippleAnimation::roundRectMask(rippleSize, st::buttonRadius);
				ensureRipple(st::stickersTrendingAdd.ripple, std_::move(rippleMask), removeButton);
			}
		}
		if (set->ripple) {
			auto rect = relativeButtonRect(removeButton);
			set->ripple->add(mapFromGlobal(QCursor::pos()) - QPoint(myrtlrect(rect).x(), _itemsTop + _actionDown * _rowHeight + rect.y()));
		}
	}
}

void StickersBox::Inner::ensureRipple(const style::RippleAnimation &st, QImage mask, bool removeButton) {
	_rows[_actionDown]->ripple = MakeShared<Ui::RippleAnimation>(st, std_::move(mask), [this, index = _actionDown, removeButton] {
		update(myrtlrect(relativeButtonRect(removeButton).translated(0, _itemsTop + index * _rowHeight)));
	});
}

void StickersBox::Inner::mouseMoveEvent(QMouseEvent *e) {
	_mouse = e->globalPos();
	onUpdateSelected();
}

void StickersBox::Inner::onUpdateSelected() {
	auto local = mapFromGlobal(_mouse);
	if (_dragging >= 0) {
		auto shift = 0;
		auto ms = getms();
		int firstSetIndex = 0;
		if (_rows.at(firstSetIndex)->isRecentSet()) {
			++firstSetIndex;
		}
		if (_dragStart.y() > local.y() && _dragging > 0) {
			shift = -floorclamp(_dragStart.y() - local.y() + (_rowHeight / 2), _rowHeight, 0, _dragging - firstSetIndex);
			for (int32 from = _dragging, to = _dragging + shift; from > to; --from) {
				qSwap(_rows[from], _rows[from - 1]);
				_rows[from]->yadd = anim::value(_rows[from]->yadd.current() - _rowHeight, 0);
				_animStartTimes[from] = ms;
			}
		} else if (_dragStart.y() < local.y() && _dragging + 1 < _rows.size()) {
			shift = floorclamp(local.y() - _dragStart.y() + (_rowHeight / 2), _rowHeight, 0, _rows.size() - _dragging - 1);
			for (int32 from = _dragging, to = _dragging + shift; from < to; ++from) {
				qSwap(_rows[from], _rows[from + 1]);
				_rows[from]->yadd = anim::value(_rows[from]->yadd.current() + _rowHeight, 0);
				_animStartTimes[from] = ms;
			}
		}
		if (shift) {
			_dragging += shift;
			_above = _dragging;
			_dragStart.setY(_dragStart.y() + shift * _rowHeight);
			if (!_a_shifting.animating()) {
				_a_shifting.start();
			}
		}
		_rows[_dragging]->yadd = anim::value(local.y() - _dragStart.y(), local.y() - _dragStart.y());
		_animStartTimes[_dragging] = 0;
		_a_shifting.step(getms(), true);

		emit checkDraggingScroll(local.y());
	} else {
		bool in = rect().marginsRemoved(QMargins(0, _itemsTop, 0, st::membersPadding.bottom())).contains(local);
		auto selected = -1;
		auto actionSel = -1;
		auto inDragArea = false;
		if (in && !_rows.isEmpty()) {
			selected = floorclamp(local.y() - _itemsTop, _rowHeight, 0, _rows.size() - 1);
			local.setY(local.y() - _itemsTop - selected * _rowHeight);
			auto set = _rows[selected];
			if (_section == Section::Installed || !set->installed || set->archived || set->removed) {
				auto removeButton = (_section == Section::Installed && !set->removed);
				auto rect = myrtlrect(relativeButtonRect(removeButton));
				actionSel = rect.contains(local) ? selected : -1;
			} else {
				actionSel = -1;
			}
			if (_section == Section::Installed && !set->isRecentSet()) {
				auto dragAreaWidth = st::contactsPadding.left() + st::stickersReorderIcon.width() + st::stickersReorderSkip;
				auto dragArea = myrtlrect(0, 0, dragAreaWidth, _rowHeight);
				inDragArea = dragArea.contains(local);
			}
		} else {
			selected = -1;
		}
		if (_selected != selected) {
			if (_section != Section::Installed && ((_selected >= 0 || _pressed >= 0) != (selected >= 0 || _pressed >= 0))) {
				if (!inDragArea) {
					setCursor((selected >= 0 || _pressed >= 0) ? style::cur_pointer : style::cur_default);
				}
			}
			_selected = selected;
		}
		if (_inDragArea != inDragArea) {
			_inDragArea = inDragArea;
			setCursor(_inDragArea ? style::cur_sizeall : (_selected >= 0 || _pressed >= 0) ? style::cur_pointer : style::cur_default);
		}
		setActionSel(actionSel);
		emit noDraggingScroll();
	}
}

float64 StickersBox::Inner::aboveShadowOpacity() const {
	if (_above < 0) return 0;

	auto dx = 0;
	auto dy = qAbs(_above * _rowHeight + qRound(_rows[_above]->yadd.current()) - _started * _rowHeight);
	return qMin((dx + dy)  * 2. / _rowHeight, 1.);
}

void StickersBox::Inner::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = base::take(_pressed, -1);

	if (_section != Section::Installed && _selected < 0 && pressed >= 0) {
		setCursor(style::cur_default);
	}

	_mouse = e->globalPos();
	onUpdateSelected();
	if (_actionDown == _actionSel && _actionSel >= 0) {
		if (_section == Section::Installed) {
			setRowRemoved(_actionDown, !_rows[_actionDown]->removed);
		} else if (_installSetCallback) {
			_installSetCallback(_rows[_actionDown]->id);
		}
	} else if (_dragging >= 0) {
		QPoint local(mapFromGlobal(_mouse));
		_rows[_dragging]->yadd.start(0.);
		_aboveShadowFadeStart = _animStartTimes[_dragging] = getms();
		_aboveShadowFadeOpacity = anim::value(aboveShadowOpacity(), 0);
		if (!_a_shifting.animating()) {
			_a_shifting.start();
		}

		_dragging = _started = -1;
	} else if (pressed == _selected && _actionSel < 0 && _actionDown < 0) {
		if (_selected >= 0 && !_inDragArea) {
			auto &sets = Global::RefStickerSets();
			auto it = sets.find(_rows.at(pressed)->id);
			if (it != sets.cend()) {
				_selected = -1;
				Ui::showLayer(new StickerSetBox(Stickers::inputSetId(*it)), KeepOtherLayers);
			}
		}
	}
	setActionDown(-1);
}

void StickersBox::Inner::setRowRemoved(int index, bool removed) {
	auto row = _rows[index];
	if (row->removed != removed) {
		row->removed = removed;
		row->ripple.reset();
		update(0, _itemsTop + index * _rowHeight, width(), _rowHeight);
		onUpdateSelected();
	}
}

void StickersBox::Inner::leaveEvent(QEvent *e) {
	_mouse = QPoint(-1, -1);
	onUpdateSelected();
}

void StickersBox::Inner::step_shifting(TimeMs ms, bool timer) {
	auto animating = false;
	auto updateMin = -1;
	auto updateMax = 0;
	for (auto i = 0, l = _animStartTimes.size(); i < l; ++i) {
		auto start = _animStartTimes.at(i);
		if (start) {
			if (updateMin < 0) updateMin = i;
			updateMax = i;
			if (start + st::stickersRowDuration > ms && ms >= start) {
				_rows[i]->yadd.update(float64(ms - start) / st::stickersRowDuration, anim::sineInOut);
				animating = true;
			} else {
				_rows[i]->yadd.finish();
				_animStartTimes[i] = 0;
			}
		}
	}
	if (_aboveShadowFadeStart) {
		if (updateMin < 0 || updateMin > _above) updateMin = _above;
		if (updateMax < _above) updateMin = _above;
		if (_aboveShadowFadeStart + st::stickersRowDuration > ms && ms > _aboveShadowFadeStart) {
			_aboveShadowFadeOpacity.update(float64(ms - _aboveShadowFadeStart) / st::stickersRowDuration, anim::sineInOut);
			animating = true;
		} else {
			_aboveShadowFadeOpacity.finish();
			_aboveShadowFadeStart = 0;
		}
	}
	if (timer) {
		if (_dragging >= 0) {
			if (updateMin < 0 || updateMin > _dragging) updateMin = _dragging;
			if (updateMax < _dragging) updateMax = _dragging;
		}
		if (updateMin >= 0) {
			update(0, _itemsTop + _rowHeight * (updateMin - 1), width(), _rowHeight * (updateMax - updateMin + 3));
		}
	}
	if (!animating) {
		_above = _dragging;
		_a_shifting.stop();
	}
}

void StickersBox::Inner::clear() {
	for (int32 i = 0, l = _rows.size(); i < l; ++i) {
		delete _rows.at(i);
	}
	_rows.clear();
	_animStartTimes.clear();
	_aboveShadowFadeStart = 0;
	_aboveShadowFadeOpacity = anim::value();
	_a_shifting.stop();
	_above = _dragging = _started = -1;
	_selected = -1;
	_pressed = -1;
	_actionDown = -1;
	setActionSel(-1);
	update();
}

void StickersBox::Inner::setActionSel(int32 actionSel) {
	if (actionSel != _actionSel) {
		if (_actionSel >= 0) update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
		_actionSel = actionSel;
		if (_actionSel >= 0) update(0, _itemsTop + _actionSel * _rowHeight, width(), _rowHeight);
		if (_section == Section::Installed) {
			setCursor((_actionSel >= 0 && (_actionDown < 0 || _actionDown == _actionSel)) ? style::cur_pointer : style::cur_default);
		}
	}
}

void StickersBox::Inner::rebuild() {
	_itemsTop = st::membersPadding.top();

	int maxNameWidth = countMaxNameWidth();

	clear();
	auto &order = ([this]() -> const Stickers::Order & {
		if (_section == Section::Installed) {
			return Global::StickerSetsOrder();
		} else if (_section == Section::Featured) {
			return Global::FeaturedStickerSetsOrder();
		} else if (_section == Section::Archived) {
			return Global::ArchivedStickerSetsOrder();
		}
		return _archivedIds;
	})();
	_rows.reserve(order.size() + 1);
	_animStartTimes.reserve(order.size() + 1);

	auto &sets = Global::StickerSets();
	if (_section == Section::Installed) {
		auto cloudIt = sets.constFind(Stickers::CloudRecentSetId);
		if (cloudIt != sets.cend() && !cloudIt->stickers.isEmpty()) {
			rebuildAppendSet(cloudIt.value(), maxNameWidth);
		}
	}
	for_const (auto setId, order) {
		auto it = sets.constFind(setId);
		if (it == sets.cend()) {
			continue;
		}

		rebuildAppendSet(it.value(), maxNameWidth);

		if (it->stickers.isEmpty() || (it->flags & MTPDstickerSet_ClientFlag::f_not_loaded)) {
			App::api()->scheduleStickerSetRequest(it->id, it->access);
		}
	}
	App::api()->requestStickerSets();
	updateSize();
}

void StickersBox::Inner::updateSize() {
	resize(width(), _itemsTop + _rows.size() * _rowHeight + st::membersPadding.bottom());
}

void StickersBox::Inner::updateRows() {
	int maxNameWidth = countMaxNameWidth();
	auto &sets = Global::StickerSets();
	for_const (auto row, _rows) {
		auto it = sets.constFind(row->id);
		if (it != sets.cend()) {
			auto &set = it.value();
			if (!row->sticker) {
				DocumentData *sticker = nullptr;
				int pixw = 0, pixh = 0;
				fillSetCover(set, &sticker, &pixw, &pixh);
				if (sticker) {
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
	}
	update();
}

bool StickersBox::Inner::appendSet(const Stickers::Set &set) {
	for_const (auto row, _rows) {
		if (row->id == set.id) {
			return false;
		}
	}
	rebuildAppendSet(set, countMaxNameWidth());
	return true;
}

int StickersBox::Inner::countMaxNameWidth() const {
	int namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	int namew = st::boxWideWidth - namex - st::contactsPadding.right() - st::contactsCheckPosition.x();
	if (_section == Section::Installed) {
		namew -= _undoWidth - st::stickersUndoRemove.width;
	} else {
		namew -= _addWidth - st::stickersTrendingAdd.width;
		if (_section == Section::Featured) {
			namew -= st::stickersFeaturedUnreadSize + st::stickersFeaturedUnreadSkip;
		}
	}
	return namew;
}

void StickersBox::Inner::rebuildAppendSet(const Stickers::Set &set, int maxNameWidth) {
	bool installed = true, official = true, unread = false, archived = false, removed = false;
	if (set.id != Stickers::CloudRecentSetId) {
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

	_rows.push_back(new Row(set.id, sticker, count, title, titleWidth, installed, official, unread, archived, removed, pixw, pixh));
	_animStartTimes.push_back(0);
}

void StickersBox::Inner::fillSetCover(const Stickers::Set &set, DocumentData **outSticker, int *outWidth, int *outHeight) const {
	if (set.stickers.isEmpty()) {
		*outSticker = nullptr;
		*outWidth = *outHeight = 0;
		return;
	}
	auto sticker = *outSticker = set.stickers.front();

	auto pixw = sticker->thumb->width();
	auto pixh = sticker->thumb->height();
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

int StickersBox::Inner::fillSetCount(const Stickers::Set &set) const {
	int result = set.stickers.isEmpty() ? set.count : set.stickers.size(), added = 0;
	if (set.id == Stickers::CloudRecentSetId) {
		auto customIt = Global::StickerSets().constFind(Stickers::CustomSetId);
		if (customIt != Global::StickerSets().cend()) {
			added = customIt->stickers.size();
			for_const (auto &sticker, cGetRecentStickers()) {
				if (customIt->stickers.indexOf(sticker.first) < 0) {
					++added;
				}
			}
		} else {
			added = cGetRecentStickers().size();
		}
	}
	return result + added;
}

QString StickersBox::Inner::fillSetTitle(const Stickers::Set &set, int maxNameWidth, int *outTitleWidth) const {
	auto result = set.title;
	int titleWidth = st::contactsNameFont->width(result);
	if (titleWidth > maxNameWidth) {
		result = st::contactsNameFont->elided(result, maxNameWidth);
		titleWidth = st::contactsNameFont->width(result);
	}
	if (outTitleWidth) {
		*outTitleWidth = titleWidth;
	}
	return result;
}

void StickersBox::Inner::fillSetFlags(const Stickers::Set &set, bool *outInstalled, bool *outOfficial, bool *outUnread, bool *outArchived) {
	*outInstalled = (set.flags & MTPDstickerSet::Flag::f_installed);
	*outOfficial = (set.flags & MTPDstickerSet::Flag::f_official);
	*outArchived = (set.flags & MTPDstickerSet::Flag::f_archived);
	if (_section == Section::Featured) {
		*outUnread = (set.flags & MTPDstickerSet_ClientFlag::f_unread);
	} else {
		*outUnread = false;
	}
}

template <typename Check>
Stickers::Order StickersBox::Inner::collectSets(Check check) const {
	Stickers::Order result;
	result.reserve(_rows.size());
	for_const (auto row, _rows) {
		if (check(row)) {
			result.push_back(row->id);
		}
	}
	return result;
}

Stickers::Order StickersBox::Inner::getOrder() const {
	return collectSets([](Row *row) {
		return !row->archived && !row->removed && !row->isRecentSet();
	});
}

Stickers::Order StickersBox::Inner::getFullOrder() const {
	return collectSets([](Row *row) {
		return !row->isRecentSet();
	});
}

Stickers::Order StickersBox::Inner::getRemovedSets() const {
	return collectSets([](Row *row) {
		return row->removed;
	});
}

int StickersBox::Inner::getRowIndex(uint64 setId) const {
	for (auto i = 0, count = _rows.size(); i != count; ++i) {
		auto row = _rows[i];
		if (row->id == setId) {
			return i;
		}
	}
	return -1;
}

void StickersBox::Inner::setFullOrder(const Stickers::Order &order) {
	for_const (auto setId, order) {
		auto index = getRowIndex(setId);
		if (index >= 0) {
			auto row = _rows[index];
			auto count = _rows.size();
			for (auto i = index + 1; i != count; ++i) {
				_rows[i - 1] = _rows[i];
			}
			_rows[count - 1] = row;
		}
	}
}

void StickersBox::Inner::setRemovedSets(const Stickers::Order &removed) {
	for (auto i = 0, count = _rows.size(); i != count; ++i) {
		setRowRemoved(i, removed.contains(_rows[i]->id));
	}
}

void StickersBox::Inner::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	if (_section == Section::Featured) {
		_visibleTop = visibleTop;
		_visibleBottom = visibleBottom;
		readVisibleSets();
	}
}

void StickersBox::Inner::readVisibleSets() {
	auto itemsVisibleTop = _visibleTop - _itemsTop;
	auto itemsVisibleBottom = _visibleBottom - _itemsTop;
	int rowFrom = floorclamp(itemsVisibleTop, _rowHeight, 0, _rows.size());
	int rowTo = ceilclamp(itemsVisibleBottom, _rowHeight, 0, _rows.size());
	for (int i = rowFrom; i < rowTo; ++i) {
		if (!_rows[i]->unread) {
			continue;
		}
		if (i * _rowHeight < itemsVisibleTop || (i + 1) * _rowHeight > itemsVisibleBottom) {
			continue;
		}
		if (!_rows[i]->sticker || _rows[i]->sticker->thumb->loaded() || _rows[i]->sticker->loaded()) {
			Stickers::markFeaturedAsRead(_rows[i]->id);
		}
	}
}

void StickersBox::Inner::setVisibleScrollbar(int32 width) {
	_scrollbar = width;
}

StickersBox::Inner::~Inner() {
	clear();
}
