//------------------------------------------------------------------------
//  BUILDING NODES / PLAY THE MAP
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2012-2016 Andrew Apted
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//------------------------------------------------------------------------

#include "main.h"
#include "levels.h"
#include "e_loadsave.h"
#include "w_wad.h"

#include "ui_window.h"

#include "bsp.h"


// config items
bool bsp_fast    = false;
bool bsp_verbose = false;
bool bsp_warn    = false;


#define NODE_PROGRESS_COLOR  fl_color_cube(2,6,2)


class UI_NodeDialog : public Fl_Double_Window
{
public:
	Fl_Browser *browser;

	Fl_Progress *progress;

	Fl_Button * button;

	int cur_prog;
	char prog_label[64];

	bool finished;

	bool want_cancel;
	bool want_close;

public:
	UI_NodeDialog();
	virtual ~UI_NodeDialog();

	/* FLTK method */
	int handle(int event);

public:
	void SetProg(int perc);

	void Print(const char *str);

	void Finish_OK();
	void Finish_Cancel();
	void Finish_Error();

	bool WantCancel() const { return want_cancel; }
	bool WantClose()  const { return want_close;  }

private:
	static void  close_callback(Fl_Widget *, void *);
	static void button_callback(Fl_Widget *, void *);
};


//
//  Callbacks
//

void UI_NodeDialog::close_callback(Fl_Widget *w, void *data)
{
	UI_NodeDialog * that = (UI_NodeDialog *)data;

	that->want_close = true;

	if (! that->finished)
		that->want_cancel = true;
}


void UI_NodeDialog::button_callback(Fl_Widget *w, void *data)
{
	UI_NodeDialog * that = (UI_NodeDialog *)data;

	if (that->finished)
		that->want_close = true;
	else
		that->want_cancel = true;
}


//
//  Constructor
//
UI_NodeDialog::UI_NodeDialog() :
	    Fl_Double_Window(400, 400, "Building Nodes"),
		cur_prog(-1),
		finished(false),
		want_cancel(false),
		want_close(false)
{
	size_range(w(), h());

	callback((Fl_Callback *) close_callback, this);

	color(WINDOW_BG, WINDOW_BG);


	browser = new Fl_Browser(0, 0, w(), h() - 100);


	Fl_Box * ptext = new Fl_Box(FL_NO_BOX, 10, h() - 80, 80, 20, "Progress:");
	(void) ptext;


	progress = new Fl_Progress(100, h() - 80, w() - 120, 20);
	progress->align(FL_ALIGN_INSIDE);
	progress->box(FL_FLAT_BOX);
	progress->color(FL_LIGHT2, NODE_PROGRESS_COLOR);

	progress->minimum(0.0);
	progress->maximum(100.0);
	progress->value(0.0);


	button = new Fl_Button(w() - 100, h() - 46, 80, 30, "Cancel");
	button->callback(button_callback, this);


	end();

	resizable(browser);
}


//
//  Destructor
//
UI_NodeDialog::~UI_NodeDialog()
{ }


int UI_NodeDialog::handle(int event)
{
	if (event == FL_KEYDOWN && Fl::event_key() == FL_Escape)
	{
		if (finished)
			want_close = true;
		else
			want_cancel = true;
		return 1;
	}

	return Fl_Double_Window::handle(event);
}


void UI_NodeDialog::SetProg(int perc)
{
	if (perc == cur_prog)
		return;

	cur_prog = perc;

	sprintf(prog_label, "%d%%", perc);

	progress->value(perc);
	progress->label(prog_label);

	Fl::check();
}


void UI_NodeDialog::Print(const char *str)
{
	// split lines

	static char buffer[256];

	snprintf(buffer, sizeof(buffer), "%s", str);
	buffer[sizeof(buffer) - 1] = 0;

	char * pos = buffer;
	char * next;

	while (pos && *pos)
	{
		next = strchr(pos, '\n');

		if (next) *next++ = 0;

		browser->add(pos);

		pos = next;
	}

	browser->bottomline(browser->size());

	Fl::check();
}


void UI_NodeDialog::Finish_OK()
{
	finished = true;

	button->label("Close");

	progress->value(100);
	progress->label("Success");
	progress->color(FL_BLUE, FL_BLUE);
}

void UI_NodeDialog::Finish_Cancel()
{
	finished = true;

	button->label("Close");

	progress->value(0);
	progress->label("Cancelled");
	progress->color(FL_RED, FL_RED);
}

void UI_NodeDialog::Finish_Error()
{
	finished = true;

	button->label("Close");

	progress->value(100);
	progress->label("ERROR");
	progress->color(FL_RED, FL_RED);
}


//------------------------------------------------------------------------


static nodebuildinfo_t * nb_info;

static char message_buf[MSG_BUF_LEN];

static UI_NodeDialog * dialog;


static const char *build_ErrorString(build_result_e ret)
{
	switch (ret)
	{
		case BUILD_OK: return "OK";

		// building was cancelled
		case BUILD_Cancelled: return "Cancelled by User";

		// the WAD file was corrupt / empty / bad filename
		case BUILD_BadFile: return "Bad File";

		// file errors
		case BUILD_ReadError:  return "Read Error";
		case BUILD_WriteError: return "Write Error";

		default: return "Unknown Error";
	}
}


void GB_PrintMsg(const char *str, ...)
{
	va_list args;

	va_start(args, str);
	vsnprintf(message_buf, MSG_BUF_LEN, str, args);
	va_end(args);

	message_buf[MSG_BUF_LEN-1] = 0;

	dialog->Print(message_buf);

	LogPrintf("BSP: %s", message_buf);
}


// set message for certain errors  [ FIXME : REMOVE THIS ]
static void SetErrorMsg(const char *str, ...)
{
	va_list args;

	va_start(args, str);
	vsnprintf(message_buf, sizeof(message_buf), str, args);
	va_end(args);

SYS_ASSERT(nb_info);

	StringFree(nb_info->message);

	nb_info->message = StringDup(message_buf);
}


static void PrepareInfo(nodebuildinfo_t *info)
{
	info->fast          = bsp_fast ? true : false;
	info->quiet         = bsp_verbose ? false : true;
	info->mini_warnings = bsp_warn ? true : false;

	info->total_big_warn   = 0;
	info->total_small_warn = 0;

	// clear cancelled flag
	info->cancelled = false;
}


static build_result_e BuildAllNodes(nodebuildinfo_t *info,
	const char *input_file, const char *output_file)

{
	// sanity check
	SYS_ASSERT(input_file  && input_file[0]);
	SYS_ASSERT(output_file && output_file[0]);

	SYS_ASSERT(1 <= info->factor && info->factor <= 32);

	if (MatchExtension(input_file, "gwa"))
	{
		SetErrorMsg("Input file cannot be GWA (contains nothing to build)");
		return BUILD_BadFile;
	}

	if (MatchExtension(output_file, "gwa"))
	{
		SetErrorMsg("Output file cannot be GWA");
		return BUILD_BadFile;
	}

	if (y_stricmp(input_file, output_file) == 0)
	{
		SetErrorMsg("Input and Outfile file are the same!");
		return BUILD_BadFile;
	}

	int num_levels = edit_wad->NumLevels();

	if (num_levels <= 0)
	{
		SetErrorMsg("No levels found in wad !");
		return BUILD_BadFile;
	}

	GB_PrintMsg("\n");

	dialog->SetProg(0);

	build_result_e ret;

	// loop over each level in the wad
	for (int n = 0 ; n < num_levels ; n++)
	{
		ret = AJBSP_BuildLevel(info, n);

		if (ret != BUILD_OK)
			break;

		dialog->SetProg(100 * (n + 1) / num_levels);

		Fl::check();

		if (dialog->WantCancel())
		{
			nb_info->cancelled = true;
		}
	}

	// writes all the lumps to the output wad
	if (ret == BUILD_OK)
	{
		GB_PrintMsg("\n");
		GB_PrintMsg("Total serious warnings: %d\n", info->total_big_warn);
		GB_PrintMsg("Total minor warnings: %d\n", info->total_small_warn);

//!!!		ReportFailedLevels();
	}

	return ret;
}


static bool DM_BuildNodes(const char *input_file, const char *output_file)
{
	LogPrintf("\n");

	nb_info = new nodebuildinfo_t;

	PrepareInfo(nb_info);

	build_result_e ret = BuildAllNodes(nb_info, input_file, output_file);

	if (ret == BUILD_Cancelled)
	{
		GB_PrintMsg("\n");
		GB_PrintMsg("Building CANCELLED.\n\n");

		delete nb_info;

		return false;
	}

	if (ret != BUILD_OK)
	{
		// build nodes failed
		GB_PrintMsg("\n");
		GB_PrintMsg("Building FAILED: %s\n", build_ErrorString(ret));
		GB_PrintMsg("Reason: %s\n\n", nb_info->message);

		delete nb_info;

		return false;
	}

	delete nb_info;

	return true;
}


bool CMD_BuildAllNodes()
{
	if (MadeChanges)
	{
		if (DLG_Confirm("Cancel|&Save",
		                "You have unsaved changes, do you want to save them now "
						"and then build the nodes?") <= 0)
		{
			return false;
		}

		if (! CMD_SaveMap())
			return false;
	}

	if (! edit_wad)
	{
		DLG_Notify("Cannot build nodes unless you are editing a PWAD.");
		return false;
	}

	if (edit_wad->IsReadOnly())
	{
		DLG_Notify("Cannot build nodes on a read-only file.");
		return false;
	}

	SYS_ASSERT(edit_wad);


	const char *old_name = StringDup(edit_wad->PathName());
	const char *new_name = ReplaceExtension(old_name, "new");

	if (MatchExtension(old_name, "new"))
	{
		DLG_Notify("Cannot build nodes on a pwad with .NEW extension.");
		return false;
	}


	dialog = new UI_NodeDialog();

	dialog->set_modal();
	dialog->show();

	Fl::check();


	bool was_ok = DM_BuildNodes(old_name, new_name);

	if (was_ok)
	{
		MasterDir_Remove(edit_wad);

		delete edit_wad;
		edit_wad = NULL;
		Pwad_name = NULL;

		// delete the old file, rename the new file
		if (! FileDelete(old_name))
		{
#if 0
fprintf(stderr, "DELETE ERROR: %s\n", strerror(errno));
fprintf(stderr, "old_name : %s\n", old_name);
#endif
			FatalError("Unable to replace the pwad with the new version\n"
			           "containing the freshly built nodes, as the original\n"
					   "could not be deleted.\n");
		}

		if (! FileRename(new_name, old_name))
		{
#if 0
fprintf(stderr, "RENAME ERROR: %s\n", strerror(errno));
fprintf(stderr, "old_name : %s\n", old_name);
fprintf(stderr, "new_name : %s\n", new_name);
#endif
			FatalError("Unable to replace the pwad with the new version\n"
			           "containing the freshly built nodes, as a problem\n"
					   "occurred trying to rename the new file.\n"
					   "\n"
					   "Your wad has been left with the .NEW extension.\n");
		}

		GB_PrintMsg("\n");
		GB_PrintMsg("Replaced the old file with the new file.\n");
	}
	else
	{
		FileDelete(new_name);
	}


	if (was_ok)
	{
		dialog->Finish_OK();
	}
	else if (nb_info->cancelled)
	{
		dialog->Finish_Cancel();

		Status_Set("Cancelled");
	}
	else
	{
		dialog->Finish_Error();

		Status_Set("Error building nodes");
	}

	while (! dialog->WantClose())
	{
		Fl::wait(0.2);
	}

	delete dialog;
	dialog = NULL;

	if (was_ok)
	{
		// re-open the PWAD

		LogPrintf("Re-opening the PWAD...\n");

		edit_wad = Wad_file::Open(old_name, 'a');
		Pwad_name = old_name;

		if (! edit_wad)
			FatalError("Unable to re-open the PWAD.\n");

		MasterDir_Add(edit_wad);

		LogPrintf("Re-opening the map (%s)\n", Level_name);

		LoadLevel(edit_wad, Level_name);

		Status_Set("Built nodes OK");
	}

	return was_ok;
}


//------------------------------------------------------------------------


void CMD_TestMap()
{
	if (! edit_wad)
	{
		DLG_Notify("Cannot test the map unless you are editing a PWAD.");
		return;
	}

	if (MadeChanges)
	{
		if (DLG_Confirm("Cancel|&Save",
		                "You have unsaved changes, do you want to save them now "
						"and build the nodes?") <= 0)
		{
			return;
		}

		if (! CMD_SaveMap())
			return;
	}

	
	// FIXME:
	// if (missing nodes)
	//    DLG_Confirm(  "build the nodes now?")


	// FIXME: figure out the proper directory to cd into
	//        (and ensure that it exists)

	// TODO : remember current dir, reset afterwards

	// FIXME : check if this worked
	FileChangeDir("/home/aapted/oblige");


	char cmd_buffer[FL_PATH_MAX * 2];

	// FIXME: use fl_filename_absolute() to get absolute paths


	// FIXME : handle DOOM1/ULTDOOM style warp option

	snprintf(cmd_buffer, sizeof(cmd_buffer),
	         "./boomPR -iwad %s -file %s -warp %s",
			 game_wad->PathName(),
			 edit_wad->PathName(),
			 Level_name);

	LogPrintf("Playing map using the following command:\n");
	LogPrintf("  %s\n", cmd_buffer);

	Status_Set("TESTING MAP...");

	main_win->redraw();
	Fl::wait(0.1);
	Fl::wait(0.1);

	int status = system(cmd_buffer);

	if (status == 0)
		Status_Set("Result: OK");
	else
		Status_Set("Result code: %d\n", status);
}


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
