/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/weak_ptr.h"
#include "base/object_ptr.h"

class PeerData;

namespace Ui {
class BoxContent;
class VerticalLayout;
} // namespace Ui

namespace style {
struct FlatLabel;
struct RoundButton;
struct IconButton;
} // namespace style

namespace Calls::Group {

struct JoinInfo;

class StartRtmpProcess final {
public:
	StartRtmpProcess() = default;
	~StartRtmpProcess();

	struct Data {
		QString url;
		QString key;
	};

	void start(
		not_null<PeerData*> peer,
		Fn<void(object_ptr<Ui::BoxContent>)> showBox,
		Fn<void(QString)> showToast,
		Fn<void(JoinInfo)> done);

	static void FillRtmpRows(
		not_null<Ui::VerticalLayout*> container,
		bool divider,
		bool disabledMenuForLabels,
		Fn<void(object_ptr<Ui::BoxContent>)> showBox,
		Fn<void(QString)> showToast,
		rpl::producer<StartRtmpProcess::Data> &&data,
		const style::FlatLabel *labelStyle,
		const style::IconButton *showButtonStyle,
		const style::FlatLabel *subsectionTitleStyle,
		const style::RoundButton *attentionButtonStyle);

private:
	void requestUrl(bool revoke);
	void processUrl(Data data);
	void createBox();
	void finish(JoinInfo info);

	struct RtmpRequest {
		not_null<PeerData*> peer;
		rpl::variable<Data> data;
		Fn<void(object_ptr<Ui::BoxContent>)> showBox;
		Fn<void(QString)> showToast;
		Fn<void(JoinInfo)> done;
		base::has_weak_ptr guard;
		QPointer<Ui::BoxContent> box;
		rpl::lifetime lifetime;
		mtpRequestId id = 0;
	};
	std::unique_ptr<RtmpRequest> _request;

};

} // namespace Calls::Group
