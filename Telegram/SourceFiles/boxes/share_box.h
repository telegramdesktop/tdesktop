/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "boxes/abstract_box.h"
#include "base/observer.h"
#include "base/timer.h"
#include "ui/effects/round_checkbox.h"

namespace Dialogs {
class Row;
class IndexedList;
} // namespace Dialogs

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Ui {
class MultiSelect;
class InputField;
struct ScrollToRequest;
template <typename Widget>
class SlideWrap;
} // namespace Ui

QString AppendShareGameScoreUrl(const QString &url, const FullMsgId &fullId);
void ShareGameScoreByHash(const QString &hash);

class ShareBox : public BoxContent, public RPCSender {
public:
	using CopyCallback = Fn<void()>;
	using SubmitCallback = Fn<void(QVector<PeerData*>&&, TextWithTags&&)>;
	using FilterCallback = Fn<bool(PeerData*)>;
	ShareBox(
		QWidget*,
		CopyCallback &&copyCallback,
		SubmitCallback &&submitCallback,
		FilterCallback &&filterCallback);

protected:
	void prepare() override;
	void setInnerFocus() override;

	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void prepareCommentField();
	void scrollAnimationCallback();

	void submit();
	void copyLink();
	bool searchByUsername(bool useCache = false);

	void scrollTo(Ui::ScrollToRequest request);
	void needSearchByUsername();
	void onFilterUpdate(const QString &query);
	void selectedChanged();
	void createButtons();
	int getTopScrollSkip() const;
	int getBottomScrollSkip() const;
	int contentHeight() const;
	void updateScrollSkips();

	void addPeerToMultiSelect(PeerData *peer, bool skipAnimation = false);
	void innerSelectedChanged(PeerData *peer, bool checked);

	void peopleReceived(
		const MTPcontacts_Found &result,
		mtpRequestId requestId);
	bool peopleFailed(const RPCError &error, mtpRequestId requestId);

	CopyCallback _copyCallback;
	SubmitCallback _submitCallback;
	FilterCallback _filterCallback;

	object_ptr<Ui::MultiSelect> _select;
	object_ptr<Ui::SlideWrap<Ui::InputField>> _comment;

	class Inner;
	QPointer<Inner> _inner;

	bool _hasSelected = false;

	base::Timer _searchTimer;
	QString _peopleQuery;
	bool _peopleFull = false;
	mtpRequestId _peopleRequest = 0;

	using PeopleCache = QMap<QString, MTPcontacts_Found>;
	PeopleCache _peopleCache;

	using PeopleQueries = QMap<mtpRequestId, QString>;
	PeopleQueries _peopleQueries;

	Animation _scrollAnimation;

};
