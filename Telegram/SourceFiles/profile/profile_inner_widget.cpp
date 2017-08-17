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
#include "profile/profile_inner_widget.h"

#include "styles/style_profile.h"
#include "styles/style_window.h"
#include "profile/profile_cover.h"
#include "profile/profile_block_info.h"
#include "profile/profile_block_settings.h"
#include "profile/profile_block_invite_link.h"
#include "profile/profile_block_shared_media.h"
#include "profile/profile_block_actions.h"
#include "profile/profile_block_channel_members.h"
#include "profile/profile_block_group_members.h"
#include "apiwrap.h"

namespace Profile {

InnerWidget::InnerWidget(QWidget *parent, PeerData *peer) : TWidget(parent)
, _peer(peer)
, _cover(this, peer) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	createBlocks();
}

void InnerWidget::createBlocks() {
	auto user = _peer->asUser();
	auto chat = _peer->asChat();
	auto channel = _peer->asChannel();
	auto megagroup = _peer->isMegagroup() ? channel : nullptr;
	if (user || channel || megagroup) {
		_blocks.push_back({ new InfoWidget(this, _peer), RectPart::Right });
	}
	_blocks.push_back({ new SettingsWidget(this, _peer), RectPart::Right });
	if (chat || channel || megagroup) {
		_blocks.push_back({ new InviteLinkWidget(this, _peer), RectPart::Right });
	}
	_blocks.push_back({ new SharedMediaWidget(this, _peer), RectPart::Right });
	if (channel && !megagroup) {
		_blocks.push_back({ new ChannelMembersWidget(this, _peer), RectPart::Right });
	}
	_blocks.push_back({ new ActionsWidget(this, _peer), RectPart::Right });
	if (chat || megagroup) {
		auto membersWidget = new GroupMembersWidget(this, _peer);
		connect(membersWidget, SIGNAL(onlineCountUpdated(int)), _cover, SLOT(onOnlineCountUpdated(int)));
		_cover->onOnlineCountUpdated(membersWidget->onlineCount());
		_blocks.push_back({ membersWidget, RectPart::Left });
	}
	for_const (auto &blockData, _blocks) {
		connect(blockData.block, SIGNAL(heightUpdated()), this, SLOT(onBlockHeightUpdated()));
	}
}

void InnerWidget::setVisibleTopBottom(int visibleTop, int visibleBottom) {
	_visibleTop = visibleTop;
	_visibleBottom = visibleBottom;

	int notDisplayedAtBottom = height() - _visibleBottom;
	if (notDisplayedAtBottom > 0) {
		decreaseAdditionalHeight(notDisplayedAtBottom);
	}

	for_const (auto &blockData, _blocks) {
		int blockY = blockData.block->y();
		blockData.block->setVisibleTopBottom(visibleTop - blockY, visibleBottom - blockY);
	}
}

bool InnerWidget::shareContactButtonShown() const {
	return _cover->shareContactButtonShown();
}

void InnerWidget::saveState(not_null<SectionMemento*> memento) {
	for_const (auto &blockData, _blocks) {
		blockData.block->saveState(memento);
	}
}

void InnerWidget::restoreState(not_null<SectionMemento*> memento) {
	for_const (auto &blockData, _blocks) {
		blockData.block->restoreState(memento);
	}
}

void InnerWidget::showFinished() {
	_cover->showFinished();
	for_const (auto &blockData, _blocks) {
		blockData.block->showFinished();
	}
}

void InnerWidget::decreaseAdditionalHeight(int removeHeight) {
	resizeToWidth(width(), height() - removeHeight);
}

void InnerWidget::paintEvent(QPaintEvent *e) {
	Painter p(this);

	p.fillRect(e->rect(), st::profileBg);

	if (_mode == Mode::TwoColumn) {
		int leftHeight = countBlocksHeight(RectPart::Left);
		int rightHeight = countBlocksHeight(RectPart::Right);
		int shadowHeight = rightHeight;// qMin(leftHeight, rightHeight);

		int shadowLeft = _blocksLeft + _leftColumnWidth + _columnDivider;
		int shadowTop = _blocksTop + st::profileBlockMarginTop;
		p.fillRect(rtlrect(shadowLeft, shadowTop, st::lineWidth, shadowHeight - st::profileBlockMarginTop, width()), st::shadowFg);
	}
}

void InnerWidget::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		emit cancelled();
	}
}

int InnerWidget::countBlocksHeight(RectPart countSide) const {
	int result = 0;
	for_const (auto &blockData, _blocks) {
		if (blockData.side != countSide || blockData.block->isHidden()) {
			continue;
		}

		result += blockData.block->height();
	}
	return result;
}

int InnerWidget::countBlocksLeft(int newWidth) const {
	int result = st::profileBlockLeftMin;
	result += (newWidth - st::windowMinWidth) / 2;
	return qMin(result, st::profileBlockLeftMax);
}

InnerWidget::Mode InnerWidget::countBlocksMode(int newWidth) const {
	bool hasLeftWidget = false, hasRightWidget = false;
	for_const (auto &blockData, _blocks) {
		if (!blockData.block->isHidden()) {
			if (blockData.side == RectPart::Left) {
				hasLeftWidget = true;
			} else {
				hasRightWidget = true;
			}
		}
	}
	if (!hasLeftWidget || !hasRightWidget) {
		return Mode::OneColumn;
	}

	int availWidth = newWidth - _blocksLeft;
	if (availWidth >= st::profileBlockWideWidthMin + _columnDivider + st::profileBlockNarrowWidthMin) {
		return Mode::TwoColumn;
	}
	return Mode::OneColumn;
}

int InnerWidget::countLeftColumnWidth(int newWidth) const {
	int result = st::profileBlockWideWidthMin;

	int availWidth = newWidth - _blocksLeft;
	int additionalWidth = (availWidth - st::profileBlockWideWidthMin - _columnDivider - st::profileBlockNarrowWidthMin);
	if (additionalWidth > 0) {
		result += (additionalWidth / 2);
		accumulate_min(result, st::profileBlockWideWidthMax);
	}
	return result;
}

void InnerWidget::refreshBlocksPositions() {
	auto layoutBlocks = [this](RectPart layoutSide, int left) {
		int top = _blocksTop;
		for_const (auto &blockData, _blocks) {
			if (_mode == Mode::TwoColumn && blockData.side != layoutSide) {
				continue;
			}
			if (blockData.block->isHidden()) {
				continue;
			}
			blockData.block->moveToLeft(left, top);
			blockData.block->setVisibleTopBottom(_visibleTop - top, _visibleBottom - top);

			top += blockData.block->height();
		}
	};
	layoutBlocks(RectPart::Left, _blocksLeft);
	if (_mode == Mode::TwoColumn) {
		layoutBlocks(RectPart::Right, _blocksLeft + _leftColumnWidth + _columnDivider);
	}
}

void InnerWidget::resizeBlocks(int newWidth) {
	for_const (auto &blockData, _blocks) {
		int blockWidth = newWidth - _blocksLeft;
		if (_mode == Mode::OneColumn) {
			blockWidth -= _blocksLeft;
		} else {
			if (blockData.side == RectPart::Left) {
				blockWidth = _leftColumnWidth;
			} else {
				blockWidth -= _leftColumnWidth + _columnDivider;
			}
		}
		blockData.block->resizeToWidth(blockWidth);
	}
}

int InnerWidget::resizeGetHeight(int newWidth) {
	_cover->resizeToWidth(newWidth);

	_blocksTop = _cover->y() + _cover->height() + st::profileBlocksTop;
	_blocksLeft = countBlocksLeft(newWidth);
	_columnDivider = st::profileMemberPaddingLeft;
	_mode = countBlocksMode(newWidth);
	_leftColumnWidth = countLeftColumnWidth(newWidth);
	resizeBlocks(newWidth);

	refreshBlocksPositions();

	update();
	auto naturalHeight = countHeight();

	_addedHeight = qMax(_minHeight - naturalHeight, 0);
	return naturalHeight + _addedHeight;
}

int InnerWidget::countHeight() const {
	auto newHeight = _cover->height();
	auto leftHeight = countBlocksHeight(RectPart::Left);
	auto rightHeight = countBlocksHeight(RectPart::Right);

	auto blocksHeight = (_mode == Mode::OneColumn) ? (leftHeight + rightHeight) : qMax(leftHeight, rightHeight);
	newHeight += st::profileBlocksTop + blocksHeight + st::profileBlocksBottom;

	return newHeight;
}

void InnerWidget::onBlockHeightUpdated() {
	refreshBlocksPositions();

	int naturalHeight = countHeight();
	int notDisplayedAtBottom = naturalHeight - _visibleBottom;
	if (notDisplayedAtBottom < 0) {
		_addedHeight = -notDisplayedAtBottom;
	} else {
		_addedHeight = 0;
	}
	if (naturalHeight + _addedHeight != height()) {
		resize(width(), naturalHeight + _addedHeight);
	}
}

} // namespace Profile
