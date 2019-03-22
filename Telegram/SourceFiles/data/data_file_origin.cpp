/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_file_origin.h"

namespace Data {
namespace {

struct FileReferenceAccumulator {
	template <typename Type>
	void push(const MTPVector<Type> &data) {
		for (const auto &item : data.v) {
			push(item);
		}
	}
	void push(const MTPPhoto &data) {
		data.match([&](const MTPDphoto &data) {
			result.data.emplace(
				PhotoFileLocationId{ data.vid.v },
				data.vfile_reference.v);
		}, [](const MTPDphotoEmpty &data) {
		});
	}
	void push(const MTPDocument &data) {
		data.match([&](const MTPDdocument &data) {
			result.data.emplace(
				DocumentFileLocationId{ data.vid.v },
				data.vfile_reference.v);
		}, [](const MTPDdocumentEmpty &data) {
		});
	}
	void push(const MTPWallPaper &data) {
		data.match([&](const MTPDwallPaper &data) {
			push(data.vdocument);
		});
	}
	void push(const MTPWebPage &data) {
		data.match([&](const MTPDwebPage &data) {
			if (data.has_document()) {
				push(data.vdocument);
			}
			if (data.has_photo()) {
				push(data.vphoto);
			}
		}, [](const auto &data) {
		});
	}
	void push(const MTPGame &data) {
		data.match([&](const MTPDgame &data) {
			if (data.has_document()) {
				push(data.vdocument);
			}
		}, [](const auto &data) {
		});
	}
	void push(const MTPMessageMedia &data) {
		data.match([&](const MTPDmessageMediaPhoto &data) {
			if (data.has_photo()) {
				push(data.vphoto);
			}
		}, [&](const MTPDmessageMediaDocument &data) {
			if (data.has_document()) {
				push(data.vdocument);
			}
		}, [&](const MTPDmessageMediaWebPage &data) {
			push(data.vwebpage);
		}, [&](const MTPDmessageMediaGame &data) {
			push(data.vgame);
		}, [](const auto &data) {
		});
	}
	void push(const MTPMessage &data) {
		data.match([&](const MTPDmessage &data) {
			if (data.has_media()) {
				push(data.vmedia);
			}
		}, [&](const MTPDmessageService &data) {
			data.vaction.match(
			[&](const MTPDmessageActionChatEditPhoto &data) {
				push(data.vphoto);
			}, [](const auto &data) {
			});
		}, [](const MTPDmessageEmpty &data) {
		});
	}
	void push(const MTPmessages_Messages &data) {
		data.match([](const MTPDmessages_messagesNotModified &) {
		}, [&](const auto &data) {
			push(data.vmessages);
		});
	}
	void push(const MTPphotos_Photos &data) {
		data.match([&](const auto &data) {
			push(data.vphotos);
		});
	}
	void push(const MTPmessages_RecentStickers &data) {
		data.match([&](const MTPDmessages_recentStickers &data) {
			push(data.vstickers);
		}, [](const MTPDmessages_recentStickersNotModified &data) {
		});
	}
	void push(const MTPmessages_FavedStickers &data) {
		data.match([&](const MTPDmessages_favedStickers &data) {
			push(data.vstickers);
		}, [](const MTPDmessages_favedStickersNotModified &data) {
		});
	}
	void push(const MTPmessages_StickerSet &data) {
		data.match([&](const MTPDmessages_stickerSet &data) {
			push(data.vdocuments);
		});
	}
	void push(const MTPmessages_SavedGifs &data) {
		data.match([&](const MTPDmessages_savedGifs &data) {
			push(data.vgifs);
		}, [](const MTPDmessages_savedGifsNotModified &data) {
		});
	}

	UpdatedFileReferences result;
};

template <typename Type>
UpdatedFileReferences GetFileReferencesHelper(const Type &data) {
	FileReferenceAccumulator result;
	result.push(data);
	return result.result;
}

} // namespace

UpdatedFileReferences GetFileReferences(const MTPmessages_Messages &data) {
	return GetFileReferencesHelper(data);
}

UpdatedFileReferences GetFileReferences(const MTPphotos_Photos &data) {
	return GetFileReferencesHelper(data);
}

UpdatedFileReferences GetFileReferences(
		const MTPmessages_RecentStickers &data) {
	return GetFileReferencesHelper(data);
}

UpdatedFileReferences GetFileReferences(
		const MTPmessages_FavedStickers &data) {
	return GetFileReferencesHelper(data);
}

UpdatedFileReferences GetFileReferences(
		const MTPmessages_StickerSet &data) {
	return GetFileReferencesHelper(data);
}

UpdatedFileReferences GetFileReferences(const MTPmessages_SavedGifs &data) {
	return GetFileReferencesHelper(data);
}

UpdatedFileReferences GetFileReferences(const MTPWallPaper &data) {
	return GetFileReferencesHelper(data);
}

} // namespace Data
