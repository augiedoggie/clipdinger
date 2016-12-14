/*
 * Copyright 2015-2016. All rights reserved.
 * Distributed under the terms of the MIT license.
 *
 * Author:
 *	Humdinger, humdingerb@gmail.com
 */

#include <Catalog.h>
#include <ControlLook.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <Roster.h>
#include <Screen.h>

#include <algorithm>

#include "App.h"
#include "ClipItem.h"
#include "Constants.h"
#include "FavItem.h"
#include "KeyCatcher.h"
#include "MainWindow.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MainWindow"


MainWindow::MainWindow(BRect frame)
	:
	BWindow(frame, B_TRANSLATE_SYSTEM_NAME("Clipdinger"), B_TITLED_WINDOW,
		B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS,
		B_ALL_WORKSPACES)
{
	KeyCatcher* catcher = new KeyCatcher("catcher");
	AddChild(catcher);
	catcher->Hide();

	_BuildLayout();
	_SetSplitview();

	if (frame == BRect(-1, -1, -1, -1)) {
		CenterOnScreen();
		ResizeTo(500, 400);
	} else {
		// make sure window is on screen
		BScreen screen(this);
		if (!screen.Frame().InsetByCopy(10, 10).Intersects(Frame()))
			CenterOnScreen();
	}
	ClipdingerSettings* settings = my_app->Settings();
	int32 fade;
	if (settings->Lock()) {
		fAutoPaste = settings->GetAutoPaste();
		fLimit = settings->GetLimit();
		fade = settings->GetFade();
		settings->Unlock();
	}

	if (!fade) {
		fPauseCheckBox->Hide();
		InvalidateLayout();
	}
	fLaunchTime = real_time_clock();

	_LoadHistory();
	_LoadFavorites();

	if (fFavorites->CountItems() > 0)
		_UpdateButtons();

	if (!fHistory->IsEmpty())
		fHistory->Select(0);
	if (!fFavorites->IsEmpty())
		fFavorites->Select(0);

	if (_GetClipboard() == "") {
		if (!fHistory->IsEmpty()) {
			ClipItem* item = dynamic_cast<ClipItem *> (fHistory->ItemAt(0));
			BString text(item->GetClip());
			_PutClipboard(text);
		}
	}
	be_clipboard->StartWatching(this);
	PostMessage(B_CLIPBOARD_CHANGED);
}


MainWindow::~MainWindow()
{
}


bool
MainWindow::QuitRequested()
{
	_SaveHistory();
	_SaveFavorites();

	ClipdingerSettings* settings = my_app->Settings();
	if (settings->Lock()) {
		float leftWeight = fMainSplitView->ItemWeight((int32)0);
		float rightWeight = fMainSplitView->ItemWeight(1);
		bool leftCollapse = fMainSplitView->IsItemCollapsed((int)0);
		bool rightCollapse = fMainSplitView->IsItemCollapsed(1);
		settings->SetSplitWeight(leftWeight, rightWeight);
		settings->SetSplitCollapse(leftCollapse, rightCollapse);
		settings->SetWindowPosition(ConvertToScreen(Bounds()));
		settings->Unlock();
	}

	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
MainWindow::MessageReceived(BMessage* message)
{
	switch (message->what)
	{
		case B_CLIPBOARD_CHANGED:
		{
			BString clip(_GetClipboard());
			if (clip.Length() == 0)
				break;
			fHistory->DeselectAll();

			app_info info;
			BPath path;
			be_roster->GetActiveAppInfo(&info);
			BEntry entry(&info.ref);
			entry.GetPath(&path);

			_MakeItemUnique(clip);
			bigtime_t time(real_time_clock());
			_AddClip(clip, path.Path(), time, time);

			fHistory->Select(0);
			break;
		}
		case ESCAPE:
		{
			Minimize(true);
			break;
		}
		case MENU_ADD:
		{
			if (!fHistory->IsEmpty()) {
				ClipItem* currentClip = dynamic_cast<ClipItem *>
					(fHistory->ItemAt(fHistory->CurrentSelection()));
				if (currentClip == NULL)
					break;

				BMessage* msg = NULL;
				msg = new BMessage(FAV_ADD);
				msg->AddPointer("clip", currentClip);
				PostMessage(msg);
			}
			break;
		}
		case MENU_DEL:
		{
			if (fHistory->IsFocus() && !fHistory->IsEmpty()) {
				ClipItem* currentClip = dynamic_cast<ClipItem *>
					(fHistory->ItemAt(fHistory->CurrentSelection()));
				if (currentClip == NULL)
					break;

				BMessage* msg = NULL;
				msg = new BMessage(DELETE);
				msg->AddPointer("clip", currentClip);
				PostMessage(msg);

			} else if (fFavorites->IsFocus() && !fFavorites->IsEmpty()) {
				FavItem* currentFav = dynamic_cast<FavItem *>
					(fFavorites->ItemAt(fFavorites->CurrentSelection()));
				if (currentFav == NULL)
					break;

				BMessage* msg = NULL;
				msg = new BMessage(FAV_DELETE);
				msg->AddPointer("fav", currentFav);
				PostMessage(msg);
			}
			break;
		}
		case MENU_EDIT:
		{
			if (!fFavorites->IsEmpty()) {
				FavItem* currentFav = dynamic_cast<FavItem *>
					(fFavorites->ItemAt(fFavorites->CurrentSelection()));
				if (currentFav == NULL)
					break;

				BMessage* msg = NULL;
				msg = new BMessage(FAV_EDIT);
				msg->AddPointer("fav", currentFav);
				PostMessage(msg);
			}
			break;
		}
		case DELETE:
		{
			int32 index = fHistory->CurrentSelection();
			ClipItem* clip = NULL;
			if (message->FindPointer("clip",
				reinterpret_cast<void**>(&clip)) != B_OK)
				break;

			fHistory->RemoveItem(clip);
			if (index < 0)
				break;

			int32 count = fHistory->CountItems();
			fHistory->Select((index > count - 1) ? count - 1 : index);
			if (index == 0) {
				ClipItem* item = dynamic_cast<ClipItem *> (fHistory->ItemAt(1));
				BString text(item->GetClip());
				_PutClipboard(text);
			}
			break;
		}
		case PAUSE:
		{
			int32 pause = fPauseCheckBox->Value();
			ClipdingerSettings* settings = my_app->Settings();
			if (settings->Lock()) {
				settings->SetFadePause(pause);
				settings->Unlock();
			}
			break;
		}
		case FAV_ADD:
		{
			if (message->WasDropped()) {
				BRect favScreen = fFavorites->ConvertToScreen(fFavorites->Bounds());
				BPoint dropPoint = message->DropPoint();
				if (!(favScreen.Contains(dropPoint)))
					break;
			}
			ClipItem* clip = NULL;
			if (message->FindPointer("clip",
				reinterpret_cast<void**>(&clip)) != B_OK)
				break;

			int32 lastitem = fFavorites->CountItems();
			fFavorites->AddItem(new FavItem(clip->GetClip(), NULL, lastitem), lastitem);

			if (message->WasDropped()) {	// move new Fav to where it was dropped
				BMessenger messenger(fFavorites);
				BMessage msg(FAV_DRAGGED);
				msg.AddPoint("_drop_point_", message->DropPoint());
				messenger.SendMessage(&msg);
			}
			break;
		}
		case FAV_DELETE:
		{
			int32 index = fFavorites->CurrentSelection();
			FavItem* fav = NULL;
			if (message->FindPointer("fav",
				reinterpret_cast<void**>(&fav)) != B_OK)
				break;

			fFavorites->RemoveItem(fav);
			if (index < 0)
				break;

			_RenumberFavorites(index);
			int32 count = fFavorites->CountItems();
			fFavorites->Select((index > count - 1) ? count - 1 : index);
			break;
		}
		case FAV_EDIT:
		{
			FavItem* fav = NULL;
			if (message->FindPointer("fav",
				reinterpret_cast<void**>(&fav)) != B_OK)
				break;

			fEditWindow = new EditWindow(Frame(), fav);
			fEditWindow->Show();
			break;
		}
		case FAV_DOWN:
		{
			int32 index = fFavorites->CurrentSelection();
			int32 last = fFavorites->CountItems();
			if ((index == last - 1) || (index < 0))
				break;

			fFavorites->SwapItems(index, index + 1);
			_RenumberFavorites(index);
			_UpdateButtons();
			break;
		}
		case FAV_UP:
		{
			int32 index = fFavorites->CurrentSelection();
			if (index < 1)
				break;

			fFavorites->SwapItems(index, index - 1);
			_RenumberFavorites(index - 1);
			_UpdateButtons();
			break;
		}
		case FAV_SELECTION:
		{
			_UpdateButtons();
			break;
		}
		case UPDATE_FAV_DISPLAY:
		{
			fFavorites->Invalidate();
			break;
		}
		case SWITCHLIST:
		{
			int32 listview;
			if (message->FindInt32("listview", &listview) == B_OK) {
				if (listview == 0)
					fFavorites->MakeFocus(true);
				if (listview == 1)
					fHistory->MakeFocus(true);

				fFavorites->Invalidate();
				fHistory->Invalidate();
			}
			break;
		}
		case HELP:
		{
			app_info info;
			BPath path;
			be_roster->GetActiveAppInfo(&info);
			BEntry entry(&info.ref);

			entry.GetPath(&path);
			path.GetParent(&path);
			path.Append("ReadMe.html");

			entry = path.Path();
			entry_ref ref;
			entry.GetRef(&ref);
			be_roster->Launch(&ref);
			break;
		}
		case PASTE_SPRUNGE:
		{
			if (fHistory->IsFocus() && !fHistory->IsEmpty()) {
				BString text;
				ClipItem* clip = NULL;
				if (message->FindPointer("clip",
					reinterpret_cast<void**>(&clip)) == B_OK)
					text = clip->GetClip();
				else {
					int32 index = fHistory->CurrentSelection();
					if (index < 0)
						break;
					ClipItem* item = dynamic_cast<ClipItem *> (fHistory->ItemAt(index));
					text = item->GetClip();
				}
				_PutClipboard(text);

			} else if (fFavorites->IsFocus() && !fFavorites->IsEmpty()) {
				BString text;
				FavItem* fav = NULL;
				if (message->FindPointer("fav",
					reinterpret_cast<void**>(&fav)) == B_OK)
					text = fav->GetClip();
				else {
					int32 index = fFavorites->CurrentSelection();
					if (index < 0)
						break;
					FavItem* item = dynamic_cast<FavItem *> (fFavorites->ItemAt(index));
					text = item->GetClip();
				}
				_PutClipboard(text);

			} else
				break;

			Minimize(true);
			PostMessage(B_CLIPBOARD_CHANGED);

			BString command(
				"stat=$(curl -m 2 -s -I http://google.com | grep HTTP/1 | awk {'print $2'}) ; "
				"if [ -z  \"$stat\" ] ; then "	// network up in general?
					"URL='%ERROR%' ; "
				"else "
					"stat=$(echo -e \"GET http://sprunge.us HTTP/1.0\n\n\" | nc sprunge.us 80 | grep HTTP/1 | awk {'print $2'}) ; "
					"if [ -z \"$stat\" ] || [ $stat -ne 200 ] ; then "	// sprunge.us only accepts GET and PUT
						"URL='%ERROR% '$stat ; "
					"else "
						"URL=$(clipboard -p | curl -F 'sprunge=<-' http://sprunge.us) ; "
					"fi ; "
				"fi ; "
				"echo $URL ; "
				"clipboard -c \"$URL\" ; "
				"exit");
			command.ReplaceAll("%ERROR%",
				B_TRANSLATE("Sprunge.us service not available."));
			system(command.String());
			break;
		}
		case CLEAR_HISTORY:
		{
			fHistory->MakeEmpty();
			PostMessage(B_CLIPBOARD_CHANGED);
			break;
		}
		case INSERT_HISTORY:
		{
			int32 itemindex;
			message->FindInt32("index", &itemindex);
			if ((fHistory->IsEmpty()) || (itemindex < 0))
				break;

			Minimize(true);
			be_clipboard->StopWatching(this);

			ClipItem* item = dynamic_cast<ClipItem *> (fHistory->ItemAt(itemindex));
			BString text(item->GetClip());
			_PutClipboard(text);
			if (fAutoPaste)
				_AutoPaste();
			_MoveClipToTop();
			_UpdateColors();

			be_clipboard->StartWatching(this);
			break;
		}
		case INSERT_FAVORITE:
		{
			int32 itemindex;
			message->FindInt32("index", &itemindex);
			if ((fFavorites->IsEmpty()) || (itemindex < 0) ||
				(fFavorites->CountItems() <= itemindex))
				break;

			Minimize(true);

			FavItem* item = dynamic_cast<FavItem *> (fFavorites->ItemAt(itemindex));
			BString text(item->GetClip());
			_PutClipboard(text);
			if (fAutoPaste)
				_AutoPaste();
			break;
		}
		case UPDATE_SETTINGS:
		{
			int32 newValue;
			if (message->FindInt32("limit", &newValue) == B_OK) {
				if (fLimit >= newValue)
					_CropHistory(newValue);

				if (fLimit != newValue)
					fLimit = newValue;

				BMessenger messenger(fHistory);
				BMessage message(ADJUSTCOLORS);
				messenger.SendMessage(&message);
			}

			if (message->FindInt32("autopaste", &newValue) == B_OK)
				fAutoPaste = newValue;

			if (message->FindInt32("fade", &newValue) == B_OK) {
				bool invisible = fPauseCheckBox->IsHidden();
				if ((invisible == true) && (newValue == 1))
					fPauseCheckBox->Show();
				else if ((invisible == false) && (newValue == 0)) 
					fPauseCheckBox->Hide();
				else
					break;

				InvalidateLayout();
			}
			break;
		}
		default:
		{
			BWindow::MessageReceived(message);
			break;
		}
	}
}


// #pragma mark - Layout


void
MainWindow::_BuildLayout()
{
	// The menu
	BMenuBar* menuBar = new BMenuBar("menubar");
	BMenu* menu;
	BMenu* submenu;
	BMenuItem* item;

	menu = new BMenu(B_TRANSLATE("App"));
	item = new BMenuItem(B_TRANSLATE("Clipboard monitor" B_UTF8_ELLIPSIS),
		new BMessage(CLIPMONITOR));
	item->SetTarget(be_app);
	menu->AddItem(item);
	item = new BMenuItem(B_TRANSLATE("Settings" B_UTF8_ELLIPSIS),
		new BMessage(SETTINGS));
	item->SetTarget(be_app);
	menu->AddItem(item);
	item = new BMenuItem(B_TRANSLATE("Help"),
		new BMessage(HELP));
	menu->AddItem(item);
	item = new BMenuItem(B_TRANSLATE("About Clipdinger"),
		new BMessage(B_ABOUT_REQUESTED));
	menu->AddItem(item);
	item->SetTarget(be_app);
	item = new BMenuItem(B_TRANSLATE("Minimize"),
		new BMessage(ESCAPE), 'W');
	menu->AddItem(item);
	item = new BMenuItem(B_TRANSLATE("Quit"),
		new BMessage(B_QUIT_REQUESTED), 'Q');
	menu->AddItem(item);
	menuBar->AddItem(menu);

	menu = new BMenu(B_TRANSLATE("Clip"));
	item = new BMenuItem(B_TRANSLATE("Paste to Sprunge.us"),
		new BMessage(PASTE_SPRUNGE), 'P');
	menu->AddItem(item);
	item = new BMenuItem(B_TRANSLATE("Add to favorites"),
		new BMessage(MENU_ADD), 'A');
	menu->AddItem(item);
	item = new BMenuItem(B_TRANSLATE("Edit favorite title"),
		new BMessage(MENU_EDIT), 'E');
	menu->AddItem(item);
	item = new BMenuItem(B_TRANSLATE("Remove"),
		new BMessage(MENU_DEL));
	menu->AddItem(item);
	menuBar->AddItem(menu);

	menu = new BMenu(B_TRANSLATE("History"));
	item = new BMenuItem(B_TRANSLATE("Clear history"),
		new BMessage(CLEAR_HISTORY));
	menu->AddItem(item);
	menuBar->AddItem(menu);

	// The lists
	fHistory = new ClipView("history");
	fHistory->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fFavorites = new FavView("favorites");
	fFavorites->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	fHistoryScrollView = new BScrollView("historyscroll", fHistory,
		B_WILL_DRAW, false, true);
	fFavoriteScrollView = new BScrollView("favoritescroll", fFavorites,
		B_WILL_DRAW, false, true);

	BStringView* favoriteHeader = new BStringView("title",
		B_TRANSLATE("Favorites"));
	favoriteHeader->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
	BFont font(be_bold_font);
	favoriteHeader->SetFont(&font);
	favoriteHeader->SetAlignment(B_ALIGN_CENTER);

	// The pause checkbox
	fPauseCheckBox = new BCheckBox("pause", B_TRANSLATE("Pause fading"),
		new BMessage(PAUSE));
	fPauseCheckBox->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	// The buttons
	fButtonUp = new BButton("up", B_TRANSLATE("Move up"),
		new BMessage(FAV_UP));
	fButtonUp->SetEnabled(false);

	fButtonDown = new BButton("down", B_TRANSLATE("Move down"),
		new BMessage(FAV_DOWN));
	fButtonDown->SetEnabled(false);

	// do the layouting
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(menuBar)
		.AddSplit(B_HORIZONTAL, B_USE_SMALL_SPACING)
		.GetSplitView(&fMainSplitView)
			.AddGroup(B_VERTICAL)
				.Add(fHistoryScrollView)
				.Add(fPauseCheckBox)
			.End()
			.AddGroup(B_VERTICAL, B_USE_SMALL_SPACING)
				.Add(favoriteHeader)
				.Add(fFavoriteScrollView)
				.AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING)
					.AddGlue()
					.Add(fButtonUp)
					.Add(fButtonDown)
					.AddGlue()
				.End()
			.End()
		.SetInsets(B_USE_SMALL_INSETS)
		.End();

	fHistory->MakeFocus(true);
	fHistory->SetInvocationMessage(new BMessage(INSERT_HISTORY));
	fHistory->SetViewColor(B_TRANSPARENT_COLOR);
	fFavorites->SetInvocationMessage(new BMessage(INSERT_FAVORITE));
	fFavorites->SetSelectionMessage(new BMessage(FAV_SELECTION));
	fFavorites->SetViewColor(B_TRANSPARENT_COLOR);
}


void
MainWindow::_SetSplitview()
{
	float leftWeight;
	float rightWeight;
	bool leftCollapse;
	bool rightCollapse;
	ClipdingerSettings* settings = my_app->Settings();
	if (settings->Lock()) {
		settings->GetSplitWeight(leftWeight, rightWeight);
		settings->GetSplitCollapse(leftCollapse, rightCollapse);
		settings->Unlock();
	}
	fMainSplitView->SetItemWeight(0, leftWeight, false);
	fMainSplitView->SetItemCollapsed(0, leftCollapse);

	fMainSplitView->SetItemWeight(1, rightWeight, true);
	fMainSplitView->SetItemCollapsed(1, rightCollapse);
}


// #pragma mark - Load & Save Lists


void
MainWindow::_SaveHistory()
{
	BPath path;
	BMessage msg;

	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) < B_OK)
		return;
	status_t ret = path.Append(kSettingsFolder);

	if (ret == B_OK)
		ret = create_directory(path.Path(), 0777);

	if (ret == B_OK)
		path.Append(kHistoryFile);

	if (ret == B_OK) {
		BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		ret = file.InitCheck();

		if (ret == B_OK) {
			for (int i = fHistory->CountItems() - 1; i >= 0 ; i--)
			{
				ClipItem* sItem = dynamic_cast<ClipItem *>
					(fHistory->ItemAt(i));

				BString clip(sItem->GetClip());
				BString path(sItem->GetOrigin());
				bigtime_t added(sItem->GetTimeAdded());
				msg.AddString("clip", clip.String());
				msg.AddString("origin", path.String());
				msg.AddInt64("time", added);
			}
			msg.AddInt64("quittime", real_time_clock());
			msg.Flatten(&file);
		}
	}
}


void
MainWindow::_LoadHistory()
{
	BPath path;
	BMessage msg;

	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		status_t ret = path.Append(kSettingsFolder);
		if (ret == B_OK) {
			path.Append(kHistoryFile);
			BFile file(path.Path(), B_READ_ONLY);

			if (file.InitCheck() != B_OK || (msg.Unflatten(&file) != B_OK))
				return;
			else {
				BString clip;
				BString path;
				int32 old_added = 0;	// used int32 pre v.0.5.5,
				int32 old_quittime = 0;	// read old history files too.
				bigtime_t added = 0;
				bigtime_t quittime = 0;
				bigtime_t since = 0;

				if (msg.FindInt32("quittime", &old_quittime) == B_OK)
					quittime = (int64)old_quittime;
				else if (msg.FindInt64("quittime", &quittime) != B_OK)
					quittime = real_time_clock();

				int32 i = 0;
				while ((msg.FindString("clip", i, &clip) == B_OK) &&
						(msg.FindString("origin", i, &path) == B_OK) &&
						((msg.FindInt32("time", i, &old_added) == B_OK) ||
						(msg.FindInt64("time", i, &added) == B_OK))) {

					if (added == 0)
						added = (int64)old_added;

					since = added + (fLaunchTime - quittime);
					_AddClip(clip, path, added, since);
					i++;
				}
				fHistory->AdjustColors();
			}
		}
	}
}


void
MainWindow::_SaveFavorites()
{
	BPath path;
	BMessage msg;

	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) < B_OK)
		return;
	status_t ret = path.Append(kSettingsFolder);

	if (ret == B_OK)
		ret = create_directory(path.Path(), 0777);

	if (ret == B_OK)
		path.Append(kFavoriteFile);

	if (ret == B_OK) {
		BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
		ret = file.InitCheck();

		if (ret == B_OK) {
			for (int i = 0; i < fFavorites->CountItems(); i++)
			{
				FavItem* sItem = dynamic_cast<FavItem *>
					(fFavorites->ItemAt(i));

				BString clip(sItem->GetClip());
				BString title(sItem->GetTitle());
				msg.AddString("clip", clip.String());
				msg.AddString("title", title.String());
			}
			msg.Flatten(&file);
		}
	}
}


void
MainWindow::_LoadFavorites()
{
	BPath path;
	BMessage msg;

	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		status_t ret = path.Append(kSettingsFolder);
		if (ret == B_OK) {
			path.Append(kFavoriteFile);
			BFile file(path.Path(), B_READ_ONLY);

			if (file.InitCheck() != B_OK || (msg.Unflatten(&file) != B_OK))
				return;
			else {
				BString clip;
				BString title;
				int32 i = 0;
				while (msg.FindString("clip", i, &clip) == B_OK &&
						msg.FindString("title", i, &title) == B_OK) {
					fFavorites->AddItem(new FavItem(clip, title, i), i);
					i++;
				}
			}
		}
	}
}


// #pragma mark - Clips etc.

void
MainWindow::_AddClip(BString clip, BString path, bigtime_t added,
				bigtime_t since)
{
	if (fHistory->CountItems() > fLimit - 1)
		fHistory->RemoveItem(fHistory->LastItem());

	fHistory->AddItem(new ClipItem(clip, path, added, since), 0);
}


void
MainWindow::_MakeItemUnique(BString clip)
{
	if (fHistory->IsEmpty())
		return;

	for (int i = 0; i < fHistory->CountItems(); i++) {
		ClipItem* sItem =
			dynamic_cast<ClipItem *> (fHistory->ItemAt(i));
		BString* listItem = new BString(sItem->GetClip());

		if (clip.Compare(*listItem) == 0)
			fHistory->RemoveItem(i);
	}
}



void
MainWindow::_MoveClipToTop()
{
	fHistory->MoveItem(fHistory->CurrentSelection(), 0);
	fHistory->Select(0);

	bigtime_t time(real_time_clock());
	ClipItem* item = dynamic_cast<ClipItem *> (fHistory->ItemAt(0));
	item->SetTimeAdded(time);
}


void
MainWindow::_CropHistory(int32 limit)
{
	if (limit < fLimit) {
		if (fHistory->CountItems() > limit) {
			int count = fHistory->CountItems() - limit - 1;
			if (limit == 0)
				limit = 1;
			fHistory->RemoveItems(limit, count);
		}
	}
}


void
MainWindow::_RenumberFavorites(int32 start)
{
	for (start; start < fFavorites->CountItems(); start++) {
		FavItem* item = dynamic_cast<FavItem *> (fFavorites->ItemAt(start));
		item->SetFavNumber(start);
	}
}


// #pragma mark - Clipboard


BString
MainWindow::_GetClipboard()
{
	const char* text = NULL;
	ssize_t textLen = 0;
	BMessage* clipboard = (BMessage *)NULL;

	if (be_clipboard->Lock()) {
		if ((clipboard = be_clipboard->Data()))
			clipboard->FindData("text/plain", B_MIME_TYPE,
				(const void **)&text, &textLen);
		be_clipboard->Unlock();
	}
	BString clip(text, textLen);
	return clip;
}


void
MainWindow::_PutClipboard(BString text)
{
	ssize_t textLen = text.Length();
	BMessage* clip = (BMessage *)NULL;

	if (be_clipboard->Lock()) {
		be_clipboard->Clear();
		if (clip = be_clipboard->Data()) {
			clip->AddData("text/plain", B_MIME_TYPE, text.String(), textLen);
			be_clipboard->Commit();
		}
		be_clipboard->Unlock();
	}
}


// #pragma mark - Settings & Display


void
MainWindow::_AutoPaste()
{
	port_id port = find_port(OUTPUT_PORT_NAME);
	if (port != B_NAME_NOT_FOUND)
		write_port(port, 'CtSV', NULL, 0);
}


void
MainWindow::_UpdateButtons()
{
	int32 selection = fFavorites->CurrentSelection();
	int32 count = fFavorites->CountItems();

	if (selection < 0)
		count = -1;

	fButtonUp->SetEnabled((count > 1 && selection > 0) ? true : false);
	fButtonDown->SetEnabled((count > 1 && selection < count - 1) ? true : false);
}


void
MainWindow::_UpdateColors()
{
	BMessenger messenger(fHistory);
	BMessage message(ADJUSTCOLORS);
	messenger.SendMessage(&message);
}
