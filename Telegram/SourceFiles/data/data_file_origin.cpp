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
	template <typename Type>
	void push(const tl::conditional<Type> &data) {
		if (data) {
			push(*data);
		}
	}
	void push(const MTPPhoto &data) {
		data.match([&](const MTPDphoto &data) {
			result.data.emplace(
				PhotoFileLocationId{ data.vid().v },
				data.vfile_reference().v);
		}, [](const MTPDphotoEmpty &data) {
		});
	}
	void push(const MTPDocument &data) {
		data.match([&](const MTPDdocument &data) {
			result.data.emplace(
				DocumentFileLocationId{ data.vid().v },
				data.vfile_reference().v);
		}, [](const MTPDdocumentEmpty &data) {
		});
	}
	void push(const MTPPage &data) {
		push(data.data().vphotos());
		push(data.data().vdocuments());
	}
	void push(const MTPWallPaper &data) {
		data.match([&](const MTPDwallPaper &data) {
			push(data.vdocument());
		}, [&](const MTPDwallPaperNoFile &data) {
		});
	}
	void push(const MTPTheme &data) {
		push(data.data().vdocument());
	}
	void push(const MTPWebPageAttribute &data) {
		data.match([&](const MTPDwebPageAttributeStory &data) {
			push(data.vstory());
		}, [&](const MTPDwebPageAttributeTheme &data) {
			push(data.vdocuments());
		}, [&](const MTPDwebPageAttributeStickerSet &data) {
			push(data.vstickers());
		});
	}
	void push(const MTPWebPage &data) {
		data.match([&](const MTPDwebPage &data) {
			push(data.vdocument());
			push(data.vattributes());
			push(data.vphoto());
			push(data.vcached_page());
		}, [](const auto &data) {
		});
	}
	void push(const MTPGame &data) {
		data.match([&](const MTPDgame &data) {
			push(data.vdocument());
		}, [](const auto &data) {
		});
	}
	void push(const MTPMessageExtendedMedia &data) {
		data.match([&](const MTPDmessageExtendedMediaPreview &data) {
		}, [&](const MTPDmessageExtendedMedia &data) {
			push(data.vmedia());
		});
	}
	void push(const MTPMessageMedia &data) {
		data.match([&](const MTPDmessageMediaPhoto &data) {
			push(data.vphoto());
		}, [&](const MTPDmessageMediaDocument &data) {
			push(data.vdocument());
		}, [&](const MTPDmessageMediaWebPage &data) {
			push(data.vwebpage());
		}, [&](const MTPDmessageMediaGame &data) {
			push(data.vgame());
		}, [&](const MTPDmessageMediaInvoice &data) {
			push(data.vextended_media());
		}, [&](const MTPDmessageMediaPaidMedia &data) {
			push(data.vextended_media());
		}, [](const auto &data) {
		});
	}
	void push(const MTPMessageReplyHeader &data) {
		data.match([&](const MTPDmessageReplyHeader &data) {
			push(data.vreply_media());
		}, [](const MTPDmessageReplyStoryHeader &data) {
		});
	}
	void push(const MTPMessage &data) {
		data.match([&](const MTPDmessage &data) {
			push(data.vmedia());
			push(data.vreply_to());
		}, [&](const MTPDmessageService &data) {
			data.vaction().match(
			[&](const MTPDmessageActionChatEditPhoto &data) {
				push(data.vphoto());
			}, [&](const MTPDmessageActionSuggestProfilePhoto &data) {
				push(data.vphoto());
			}, [&](const MTPDmessageActionSetChatWallPaper &data) {
				push(data.vwallpaper());
			}, [](const auto &data) {
			});
			push(data.vreply_to());
		}, [](const MTPDmessageEmpty &data) {
		});
	}
	void push(const MTPStoryItem &data) {
		data.match([&](const MTPDstoryItem &data) {
			push(data.vmedia());
		}, [](const MTPDstoryItemDeleted &) {
		}, [](const MTPDstoryItemSkipped &) {
		});
	}
	void push(const MTPmessages_Messages &data) {
		data.match([](const MTPDmessages_messagesNotModified &) {
		}, [&](const auto &data) {
			push(data.vmessages());
		});
	}
	void push(const MTPphotos_Photos &data) {
		data.match([&](const auto &data) {
			push(data.vphotos());
		});
	}
	void push(const MTPusers_UserFull &data) {
		push(data.data().vfull_user().data().vpersonal_photo());
	}
	void push(const MTPmessages_RecentStickers &data) {
		data.match([&](const MTPDmessages_recentStickers &data) {
			push(data.vstickers());
		}, [](const MTPDmessages_recentStickersNotModified &data) {
		});
	}
	void push(const MTPmessages_FavedStickers &data) {
		data.match([&](const MTPDmessages_favedStickers &data) {
			push(data.vstickers());
		}, [](const MTPDmessages_favedStickersNotModified &data) {
		});
	}
	void push(const MTPmessages_StickerSet &data) {
		data.match([&](const MTPDmessages_stickerSet &data) {
			push(data.vdocuments());
		}, [](const MTPDmessages_stickerSetNotModified &data) {
		});
	}
	void push(const MTPmessages_SavedGifs &data) {
		data.match([&](const MTPDmessages_savedGifs &data) {
			push(data.vgifs());
		}, [](const MTPDmessages_savedGifsNotModified &data) {
		});
	}
	void push(const MTPaccount_SavedRingtones &data) {
		data.match([&](const MTPDaccount_savedRingtones &data) {
			push(data.vringtones());
		}, [](const MTPDaccount_savedRingtonesNotModified &data) {
		});
	}
	void push(const MTPhelp_PremiumPromo &data) {
		push(data.data().vvideos());
	}
	void push(const MTPmessages_WebPage &data) {
		push(data.data().vwebpage());
	}
	void push(const MTPstories_Stories &data) {
		push(data.data().vstories());
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

UpdatedFileReferences GetFileReferences(const MTPusers_UserFull &data) {
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

UpdatedFileReferences GetFileReferences(const MTPTheme &data) {
	return GetFileReferencesHelper(data);
}

UpdatedFileReferences GetFileReferences(
		const MTPaccount_SavedRingtones &data) {
	return GetFileReferencesHelper(data);
}

UpdatedFileReferences GetFileReferences(const MTPhelp_PremiumPromo &data) {
	return GetFileReferencesHelper(data);
}

UpdatedFileReferences GetFileReferences(const MTPmessages_WebPage &data) {
	return GetFileReferencesHelper(data);
}

UpdatedFileReferences GetFileReferences(const MTPstories_Stories &data) {
	return GetFileReferencesHelper(data);
}

UpdatedFileReferences GetFileReferences(const MTPMessageMedia &data) {
	return GetFileReferencesHelper(data);
}

} // namespace Data
