/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"
#include "rpl/producer.h"

#include <algorithm>
#include <vector>

class PeerData;

namespace Data {
enum class StorySourcesList : uchar;
class Story;
} // namespace Data

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class DynamicImage;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Dialogs::Stories {

struct Element {
	uint64 id = 0;
	QString name;
	std::shared_ptr<Ui::DynamicImage> thumbnail;
	uint32 count : 15 = 0;
	uint32 unreadCount : 15 = 0;
	uint32 hasVideoStream : 1 = 0;
	uint32 skipSmall : 1 = 0;
	uint32 mine : 1 = 0;
	uint32 mineUnread : 1 = 0;
	uint32 uploading : 1 = 0;
	uint32 uploadFailed : 1 = 0;
	uint32 uploadProgress : 7 = 0;
	uint64 uploadId = 0;

	[[nodiscard]] bool canAddFirstStory() const {
		return mine && !count && !uploading;
	}

	friend inline bool operator==(
		const Element &a,
		const Element &b) = default;
};

struct Content {
	std::vector<Element> elements;
	int total = 0;

	void placeMyStoryFirst(const Fn<Element()> &makePlaceholder) {
		if (elements.empty()) {
			return;
		}
		const auto i = std::ranges::find_if(
			elements,
			[](const Element &item) { return item.mine; });
		if (i != end(elements)) {
			std::rotate(begin(elements), i, i + 1);
		} else if (makePlaceholder) {
			elements.insert(begin(elements), makePlaceholder());
			++total;
		}
	}

	friend inline bool operator==(
		const Content &a,
		const Content &b) = default;
};

struct ShowMenuRequest;

[[nodiscard]] rpl::producer<Content> ContentForSession(
	not_null<Main::Session*> session,
	Data::StorySourcesList list);

[[nodiscard]] rpl::producer<Content> LastForPeer(not_null<PeerData*> peer);

void FillSourceMenu(
	not_null<Window::SessionController*> controller,
	const ShowMenuRequest &request);

void ConfirmCancelUpload(
	not_null<Window::SessionController*> controller,
	uint64 uploadId);

[[nodiscard]] bool AddStoryButtonHidden();
void SetAddStoryButtonHidden(bool hidden);

} // namespace Dialogs::Stories
