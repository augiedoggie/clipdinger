#include "App.h"
#include <PropertyInfo.h>


static property_info prop_list[] = {
	{ "Clip",
		{ B_GET_PROPERTY, 0 },
		{ B_INDEX_SPECIFIER, B_NAME_SPECIFIER, 0 },
		"Get the text of a clip"
	},
	{ "Favorite",
		{ B_GET_PROPERTY, 0 },
		{ B_INDEX_SPECIFIER, B_NAME_SPECIFIER, 0 },
		"Get the text of a favorite"
	},
	{ "Clips",
		{ B_COUNT_PROPERTIES, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Count # of clips stored in history"
	},
	{ "Favorites",
		{ B_COUNT_PROPERTIES, 0 },
		{ B_DIRECT_SPECIFIER, 0 },
		"Count # of favorite clips stored in history"
	},
	0 //terminate list
};


status_t
App::GetSupportedSuites(BMessage* msg) {
	BPropertyInfo info(prop_list);
	msg->AddFlat("messages", &info);
	msg->AddString("suites", "suite/x-vnd.humdinger-clipdinger");

	return BApplication::GetSupportedSuites(msg);
}


BHandler*
App::ResolveSpecifier(BMessage* msg, int32 index, BMessage* specifier, int32 what, const char* property) {
	BPropertyInfo prop_info(prop_list);
	if (prop_info.FindMatch(msg, index, specifier, what, property) >= 0)
		return this;

	return BApplication::ResolveSpecifier(msg, index, specifier, what, property);
}


void
App::_HandleScriptCommand(BMessage* msg)
{
	switch(msg->what) {
		case B_COUNT_PROPERTIES:
			_CountClips(msg);
			break;
		case B_GET_PROPERTY:
			_GetClipInformation(msg);
			break;
	}
}


void
App::_GetClipInformation(BMessage* msg) {
	BMessage reply(B_REPLY), specMsg;

	BString propName;
	if (msg->GetCurrentSpecifier(NULL, &specMsg) != B_OK || specMsg.FindString("property", &propName) != B_OK) {
		reply.what = B_MESSAGE_NOT_UNDERSTOOD;
		reply.AddInt32("error", B_BAD_SCRIPT_SYNTAX);
		msg->SendReply(&reply);
		return;
	}

	bool favorite = false;
	if (propName == "Favorite")
		favorite = true;

	if (specMsg.HasInt32("index")) {
		int32 clipIndex = 0;
		specMsg.FindInt32("index", &clipIndex);

		fMainWindow->Lock();
		BListItem* clipItem = favorite
								? fMainWindow->fFavorites->ItemAt(clipIndex)
								: fMainWindow->fHistory->ItemAt(clipIndex);

		if (clipItem == NULL) {
			reply.what = B_MESSAGE_NOT_UNDERSTOOD;
			reply.AddString("message", "Index out of range");
			reply.AddInt32("error", B_BAD_INDEX);
			msg->SendReply(&reply);
			fMainWindow->Unlock();
			return;
		}

		BString clipString = favorite
								? dynamic_cast<FavItem*>(clipItem)->GetClip()
								: dynamic_cast<ClipItem*>(clipItem)->GetClip();

		fMainWindow->Unlock();

		reply.AddString("result", clipString);

	} else if (specMsg.HasString("name")) {
		BString clipName;
		specMsg.FindString("name", &clipName);


		//TODO search for clip, return an error for now
//		reply.AddString("result", clipString);
		reply.what = B_MESSAGE_NOT_UNDERSTOOD;
		reply.AddString("message", "Name not found");
		reply.AddInt32("error", B_NAME_NOT_FOUND);
		msg->SendReply(&reply);
		return;
	} else {
		reply.what = B_MESSAGE_NOT_UNDERSTOOD;
		reply.AddString("message", "No index or name found");
		reply.AddInt32("error", B_BAD_SCRIPT_SYNTAX);
		msg->SendReply(&reply);
		return;
	}

	reply.AddInt32("error", B_OK);
	msg->SendReply(&reply);
}


void
App::_CountClips(BMessage* msg) {
	BMessage reply(B_REPLY);
	BMessage specMsg;
	BString propName;
	if (msg->GetCurrentSpecifier(NULL, &specMsg) == B_OK && specMsg.FindString("property", &propName) == B_OK) {
		int32 result = -1;
		fMainWindow->Lock();

		if (propName == "Favorites")
			result = fMainWindow->fFavorites->CountItems();
		else if (propName == "Clips")
			result = fMainWindow->fHistory->CountItems();

		fMainWindow->Unlock();

		reply.AddInt32("error", result == -1 ? B_ERROR : B_OK);
		reply.AddInt32("result", result);
	} else {
		reply.what = B_MESSAGE_NOT_UNDERSTOOD;
		reply.AddInt32("error", B_BAD_SCRIPT_SYNTAX);
	}
	msg->SendReply(&reply);
}
