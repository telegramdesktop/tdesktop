/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "base/weak_ptr.h"
#include "data/data_file_origin.h"
#include "data/data_msg_id.h"

#include <QtCore/QString>

#include <memory>

struct WebPageData;

namespace HistoryView {
class Element;
} // namespace HistoryView

namespace Main {
class Session;
} // namespace Main

namespace Iv::Markdown {
class MediaRuntime;
} // namespace Iv::Markdown

namespace Window {
class SessionController;
} // namespace Window

namespace Iv {

[[nodiscard]] auto CreateCachedPageMediaRuntime(
	not_null<Main::Session*> session,
	not_null<WebPageData*> page,
	Fn<void(QString)> openChannel,
	Fn<void(QString)> joinChannel)
-> std::shared_ptr<Markdown::MediaRuntime>;

[[nodiscard]] auto CreateMessageMediaRuntime(
	not_null<Main::Session*> session,
	FullMsgId itemId,
	Fn<void(QString)> openChannel,
	Fn<void(QString)> joinChannel,
	::Data::FileOrigin draftOrigin = {},
	base::weak_ptr<Window::SessionController> controller = {})
-> std::shared_ptr<Markdown::MediaRuntime>;

[[nodiscard]] auto CreateMessageMediaRuntime(
	not_null<Main::Session*> session,
	not_null<HistoryView::Element*> view,
	Fn<void(QString)> openChannel,
	Fn<void(QString)> joinChannel)
-> std::shared_ptr<Markdown::MediaRuntime>;

} // namespace Iv
