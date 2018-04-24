//------------------------------------------------------------------------
//  COMMAND FUNCTIONS
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2001-2016 Andrew Apted
//  Copyright (C) 1997-2003 Andr� Majorel et al
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
//
//  Based on Yadex which incorporated code from DEU 5.21 that was put
//  in the public domain in 1994 by Rapha�l Quinet and Brendon Wyber.
//
//------------------------------------------------------------------------

#include "main.h"

#include "e_checks.h"
#include "e_cutpaste.h"
#include "e_hover.h"
#include "r_grid.h"
#include "e_linedef.h"
#include "e_main.h"
#include "e_objects.h"
#include "e_sector.h"
#include "e_path.h"
#include "e_vertex.h"
#include "m_config.h"
#include "m_events.h"
#include "m_loadsave.h"
#include "m_nodes.h"
#include "r_render.h"
#include "ui_window.h"
#include "ui_about.h"
#include "ui_misc.h"
#include "ui_prefs.h"


// config items
int minimum_drag_pixels = 5;


void CMD_Nothing()
{
	/* hey jude, don't make it bad */
}


void CMD_MetaKey()
{
	if (edit.sticky_mod)
	{
		ClearStickyMod();
		return;
	}

	Status_Set("META...");

	edit.sticky_mod = MOD_META;
}


void CMD_EditMode()
{
	char mode = tolower(EXEC_Param[0][0]);

	if (! mode || ! strchr("lstvr", mode))
	{
		Beep("Bad parameter for EditMode: '%s'", EXEC_Param[0]);
		return;
	}

	Editor_ChangeMode(mode);
}


void CMD_Select()
{
	if (edit.render3d)
		return;

	// FIXME : action in effect?

	// FIXME : split_line in effect?

	if (edit.highlight.is_nil())
	{
		Beep("Nothing under cursor");
		return;
	}

	int obj_num = edit.highlight.num;

	edit.Selected->toggle(obj_num);

	RedrawMap();
}


void CMD_SelectAll()
{
	Editor_ClearErrorMode();

	int total = NumObjects(edit.mode);

	Selection_Push();

	edit.Selected->change_type(edit.mode);
	edit.Selected->frob_range(0, total-1, BOP_ADD);

	RedrawMap();
}


void CMD_UnselectAll()
{
	Editor_ClearErrorMode();

	if (edit.action == ACT_DRAW_LINE ||
		edit.action == ACT_TRANSFORM)
	{
		Editor_ClearAction();
	}

	Selection_Clear();

	RedrawMap();
}


void CMD_InvertSelection()
{
	// do not clear selection when in error mode
	edit.error_mode = false;

	int total = NumObjects(edit.mode);

	if (edit.Selected->what_type() != edit.mode)
	{
		// convert the selection
		selection_c *prev_sel = edit.Selected;
		edit.Selected = new selection_c(edit.mode);

		ConvertSelection(prev_sel, edit.Selected);
		delete prev_sel;
	}

	edit.Selected->frob_range(0, total-1, BOP_TOGGLE);

	RedrawMap();
}


void CMD_Quit()
{
	Main_Quit();
}


void CMD_Undo()
{
	if (! BA_Undo())
	{
		Beep("No operation to undo");
		return;
	}

	RedrawMap();
	main_win->UpdatePanelObj();
}


void CMD_Redo()
{
	if (! BA_Redo())
	{
		Beep("No operation to redo");
		return;
	}

	RedrawMap();
	main_win->UpdatePanelObj();
}


static void SetGamma(int new_val)
{
	usegamma = CLAMP(0, new_val, 4);

	W_UpdateGamma();

	Status_Set("Gamma level %d", usegamma);

	RedrawMap();
}


void CMD_SetVar()
{
	const char *var_name = EXEC_Param[0];
	const char *value    = EXEC_Param[1];

	if (! var_name[0])
	{
		Beep("Set: missing var name");
		return;
	}

	if (! value[0])
	{
		Beep("Set: missing value");
		return;
	}

	 int  int_val = atoi(value);
	bool bool_val = (int_val > 0);


	if (y_stricmp(var_name, "3d") == 0)
	{
		Render3D_Enable(bool_val);
	}
	else if (y_stricmp(var_name, "browser") == 0)
	{
		Editor_ClearAction();

		int want_vis   = bool_val ? 1 : 0;
		int is_visible = main_win->browser->visible() ? 1 : 0;

		if (want_vis != is_visible)
			main_win->BrowserMode('/');
	}
	else if (y_stricmp(var_name, "grid") == 0)
	{
		grid.SetShown(bool_val);
	}
	else if (y_stricmp(var_name, "snap") == 0)
	{
		grid.SetSnap(bool_val);
	}
	else if (y_stricmp(var_name, "sprites") == 0)
	{
		edit.thing_render_mode = int_val;
		RedrawMap();
	}
	else if (y_stricmp(var_name, "obj_nums") == 0)
	{
		edit.show_object_numbers = bool_val;
		RedrawMap();
	}
	else if (y_stricmp(var_name, "gamma") == 0)
	{
		SetGamma(int_val);
	}
	else if (y_stricmp(var_name, "sec_render") == 0)
	{
		int_val = CLAMP(0, int_val, (int)SREND_SoundProp);
		edit.sector_render_mode = (sector_rendering_mode_e) int_val;
		RedrawMap();
	}
	else
	{
		Beep("Set: unknown var: %s", var_name);
	}
}


void CMD_ToggleVar()
{
	const char *var_name = EXEC_Param[0];

	if (! var_name[0])
	{
		Beep("Toggle: missing var name");
		return;
	}

	if (y_stricmp(var_name, "3d") == 0)
	{
		Render3D_Enable(! edit.render3d);
	}
	else if (y_stricmp(var_name, "browser") == 0)
	{
		Editor_ClearAction();

		main_win->BrowserMode('/');
	}
	else if (y_stricmp(var_name, "recent") == 0)
	{
		main_win->browser->ToggleRecent();
	}
	else if (y_stricmp(var_name, "grid") == 0)
	{
		grid.ToggleShown();
	}
	else if (y_stricmp(var_name, "snap") == 0)
	{
		grid.ToggleSnap();
	}
	else if (y_stricmp(var_name, "sprites") == 0)
	{
		edit.thing_render_mode = ! edit.thing_render_mode;
		RedrawMap();
	}
	else if (y_stricmp(var_name, "obj_nums") == 0)
	{
		edit.show_object_numbers = ! edit.show_object_numbers;
		RedrawMap();
	}
	else if (y_stricmp(var_name, "gamma") == 0)
	{
		SetGamma((usegamma >= 4) ? 0 : usegamma + 1);
	}
	else if (y_stricmp(var_name, "sec_render") == 0)
	{
		if (edit.sector_render_mode >= SREND_SoundProp)
			edit.sector_render_mode = SREND_Nothing;
		else
			edit.sector_render_mode = (sector_rendering_mode_e)(1 + (int)edit.sector_render_mode);
		RedrawMap();
	}
	else
	{
		Beep("Toggle: unknown var: %s", var_name);
	}
}


void CMD_BrowserMode()
{
	if (! EXEC_Param[0][0])
	{
		Beep("BrowserMode: missing mode");
		return;
	}

	char mode = toupper(EXEC_Param[0][0]);

	if (! (mode == 'L' || mode == 'S' || mode == 'O' ||
	       mode == 'T' || mode == 'F' || mode == 'G'))
	{
		Beep("Unknown browser mode: %s", EXEC_Param[0]);
		return;
	}

	// if that browser is already open, close it now
	if (main_win->browser->visible() &&
		main_win->browser->GetMode() == mode &&
		! Exec_HasFlag("/force") &&
		! Exec_HasFlag("/recent"))
	{
		main_win->BrowserMode(0);
		return;
	}

	main_win->BrowserMode(mode);

	if (Exec_HasFlag("/recent"))
	{
		main_win->browser->ToggleRecent(true /* force */);
	}
}


void CMD_Scroll()
{
	// these are percentages
	float delta_x = atof(EXEC_Param[0]);
	float delta_y = atof(EXEC_Param[1]);

	if (delta_x == 0 && delta_y == 0)
	{
		Beep("Bad parameter to Scroll: '%s' %s'", EXEC_Param[0], EXEC_Param[1]);
		return;
	}

	int base_size = (main_win->canvas->w() + main_win->canvas->h()) / 2;

	delta_x = delta_x * base_size / 100.0 / grid.Scale;
	delta_y = delta_y * base_size / 100.0 / grid.Scale;

	grid.Scroll(delta_x, delta_y);
}


static void NAV_Scroll_Left_release(void)
{
	edit.nav_scroll_left = 0;
}

void CMD_NAV_Scroll_Left()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Editor_ClearNav();

	float perc = atof(EXEC_Param[0]);
	int base_size = (main_win->canvas->w() + main_win->canvas->h()) / 2;
	edit.nav_scroll_left = perc * base_size / 100.0 / grid.Scale;

	Nav_SetKey(EXEC_CurKey, &NAV_Scroll_Left_release);
}


static void NAV_Scroll_Right_release(void)
{
	edit.nav_scroll_right = 0;
}

void CMD_NAV_Scroll_Right()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Editor_ClearNav();

	float perc = atof(EXEC_Param[0]);
	int base_size = (main_win->canvas->w() + main_win->canvas->h()) / 2;
	edit.nav_scroll_right = perc * base_size / 100.0 / grid.Scale;

	Nav_SetKey(EXEC_CurKey, &NAV_Scroll_Right_release);
}


static void NAV_Scroll_Up_release(void)
{
	edit.nav_scroll_up = 0;
}

void CMD_NAV_Scroll_Up()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Editor_ClearNav();

	float perc = atof(EXEC_Param[0]);
	int base_size = (main_win->canvas->w() + main_win->canvas->h()) / 2;
	edit.nav_scroll_up = perc * base_size / 100.0 / grid.Scale;

	Nav_SetKey(EXEC_CurKey, &NAV_Scroll_Up_release);
}


static void NAV_Scroll_Down_release(void)
{
	edit.nav_scroll_down = 0;
}

void CMD_NAV_Scroll_Down()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Editor_ClearNav();

	float perc = atof(EXEC_Param[0]);
	int base_size = (main_win->canvas->w() + main_win->canvas->h()) / 2;
	edit.nav_scroll_down = perc * base_size / 100.0 / grid.Scale;

	Nav_SetKey(EXEC_CurKey, &NAV_Scroll_Down_release);
}


static void NAV_MouseScroll_release(void)
{
	Editor_ScrollMap(+1);
}

void CMD_NAV_MouseScroll()
{
	if (edit.render3d)
		return;

	if (! EXEC_CurKey)
		return;

	edit.scroll_speed = atof(EXEC_Param[0]);

	if (! edit.is_navigating)
		Editor_ClearNav();

	if (Nav_SetKey(EXEC_CurKey, &NAV_MouseScroll_release))
	{
		Editor_ScrollMap(-1);
	}
}


// screen position when LMB was pressed
static int mouse_button1_x;
static int mouse_button1_y;

// map location when LMB was pressed
static int button1_map_x;
static int button1_map_y;

static bool click_check_drag;
static bool click_check_select;
static bool click_force_single;


void CheckBeginDrag()
{
	if (! click_check_drag)
		return;

	int pixel_dx = Fl::event_x() - mouse_button1_x;
	int pixel_dy = Fl::event_y() - mouse_button1_y;

	if (edit.clicked.valid() &&
		MAX(abs(pixel_dx), abs(pixel_dy)) >= minimum_drag_pixels)
	{
		Editor_SetAction(ACT_DRAG);

		// if highlighted object is in selection, we drag the selection,
		// otherwise we drag just this one object

		if (click_force_single || ! edit.Selected->get(edit.clicked.num))
			edit.drag_single_obj = edit.clicked.num;
		else
			edit.drag_single_obj = -1;

		int focus_x, focus_y;

		GetDragFocus(&focus_x, &focus_y, button1_map_x, button1_map_y);

		main_win->canvas->DragBegin(focus_x, focus_y, button1_map_x, button1_map_y);
		return;
	}
}


static void ACT_SelectBox_release(void)
{
	// check if cancelled or overridden
	if (edit.action != ACT_SELBOX)
		return;

	Editor_ClearAction();
	Editor_ClearErrorMode();

	int x1, y1, x2, y2;

	main_win->canvas->SelboxFinish(&x1, &y1, &x2, &y2);

	// a mere click and release will unselect everything
	if (x1 == x2 && y1 == y2)
		ExecuteCommand("UnselectAll");
	else
		SelectObjectsInBox(edit.Selected, edit.mode, x1, y1, x2, y2);

	RedrawMap();
}


static void ACT_Drag_release(void)
{
	// check if cancelled or overridden
	if (edit.action != ACT_DRAG)
		return;

	Editor_ClearAction();

	int dx, dy;
	main_win->canvas->DragFinish(&dx, &dy);

	if (dx || dy)
	{
		if (edit.drag_single_obj >= 0)
			DragSingleObject(edit.drag_single_obj, dx, dy);
		else
			MoveObjects(edit.Selected, dx, dy);
	}

	edit.drag_single_obj = -1;

	RedrawMap();
}


static void ACT_Click_release(void)
{
	Objid click_obj(edit.clicked);
	edit.clicked.clear();

	click_check_drag = false;


	if (edit.action == ACT_SELBOX)
	{
		ACT_SelectBox_release();
		return;
	}
	else if (edit.action == ACT_DRAG)
	{
		ACT_Drag_release();
		return;
	}

	// check if cancelled or overridden
	if (edit.action != ACT_CLICK)
		return;


	if (click_check_select && click_obj.valid())
	{
		// check if pointing at the same object as before
		Objid near_obj;

		GetNearObject(near_obj, edit.mode, edit.map_x, edit.map_y);

		if (near_obj == click_obj)
		{
			edit.Selected->toggle(click_obj.num);
		}
	}

	Editor_ClearAction();
	Editor_ClearErrorMode();

	RedrawMap();
}

void CMD_ACT_Click()
{
	if (edit.render3d)
		return;

	if (! EXEC_CurKey)
		return;

	if (! Nav_ActionKey(EXEC_CurKey, &ACT_Click_release))
		return;

	click_check_select = ! Exec_HasFlag("/noselect");
	click_check_drag   = ! Exec_HasFlag("/nodrag");
	click_force_single = false;

	if (click_check_drag)
	{
		// remember some state (for drag detection)
		mouse_button1_x = Fl::event_x();
		mouse_button1_y = Fl::event_y();

		button1_map_x = edit.map_x;
		button1_map_y = edit.map_y;
	}

	// check for splitting a line, and ensure we can drag the vertex
	if (! Exec_HasFlag("/nosplit") &&
		edit.mode == OBJ_VERTICES &&
		edit.split_line.valid() &&
		edit.action != ACT_DRAW_LINE)
	{
		int split_ld = edit.split_line.num;

		click_force_single = true;   // if drag vertex, force single-obj mode
		click_check_select = false;  // do NOT select the new vertex

		// check if both ends are in selection, if so (and only then)
		// shall we select the new vertex
		const LineDef *L = LineDefs[split_ld];

		bool want_select = edit.Selected->get(L->start) && edit.Selected->get(L->end);

		BA_Begin();
		BA_Message("split linedef #%d", split_ld);

		int new_vert = BA_New(OBJ_VERTICES);

		Vertex *V = Vertices[new_vert];

		V->x = edit.split_x;
		V->y = edit.split_y;

		SplitLineDefAtVertex(split_ld, new_vert);

		BA_End();

		if (want_select)
			edit.Selected->set(new_vert);

		edit.clicked = Objid(OBJ_VERTICES, new_vert);

		Editor_SetAction(ACT_CLICK);

		RedrawMap();
		return;
	}

	// find the object under the pointer.
	GetNearObject(edit.clicked, edit.mode, edit.map_x, edit.map_y);

	// clicking on an empty space starts a new selection box
	if (click_check_select && edit.clicked.is_nil())
	{
		Editor_SetAction(ACT_SELBOX);

		main_win->canvas->SelboxBegin(edit.map_x, edit.map_y);
		return;
	}

	Editor_SetAction(ACT_CLICK);
}



void CMD_ACT_SelectBox()
{
	if (edit.render3d)
		return;

	if (! EXEC_CurKey)
		return;

	if (! Nav_ActionKey(EXEC_CurKey, &ACT_SelectBox_release))
		return;

	Editor_SetAction(ACT_SELBOX);

	main_win->canvas->SelboxBegin(edit.map_x, edit.map_y);
}


void CMD_ACT_Drag()
{
	if (edit.render3d)
		return;

	if (! EXEC_CurKey)
		return;

	if (edit.Selected->empty())
	{
		Beep("Nothing to drag");
		return;
	}

	if (! Nav_ActionKey(EXEC_CurKey, &ACT_Drag_release))
		return;

	int focus_x, focus_y;

	GetDragFocus(&focus_x, &focus_y, edit.map_x, edit.map_y);

	Editor_SetAction(ACT_DRAG);
	main_win->canvas->DragBegin(focus_x, focus_y, edit.map_x, edit.map_y);

	edit.drag_single_obj = -1;

	RedrawMap();
}


static void ACT_Transform_release(void)
{
	// check if cancelled or overridden
	if (edit.action != ACT_TRANSFORM)
		return;

	Editor_ClearAction();

	transform_t param;

	main_win->canvas->TransformFinish(param);

	TransformObjects(param);

	RedrawMap();
}

void CMD_ACT_Transform()
{
	if (edit.render3d)
		return;

	if (! EXEC_CurKey)
		return;

	if (edit.Selected->empty())
	{
		Beep("Nothing to scale");
		return;
	}

	const char *keyword = EXEC_Param[0];
	transform_keyword_e  mode;

	if (! keyword[0])
	{
		Beep("ACT_Transform: missing keyword");
		return;
	}
	else if (y_stricmp(keyword, "scale") == 0)
	{
		mode = TRANS_K_Scale;
	}
	else if (y_stricmp(keyword, "stretch") == 0)
	{
		mode = TRANS_K_Stretch;
	}
	else if (y_stricmp(keyword, "rotate") == 0)
	{
		mode = TRANS_K_Rotate;
	}
	else if (y_stricmp(keyword, "rotscale") == 0)
	{
		mode = TRANS_K_RotScale;
	}
	else if (y_stricmp(keyword, "skew") == 0)
	{
		mode = TRANS_K_Skew;
	}
	else
	{
		Beep("ACT_Transform: unknown keyword: %s", keyword);
		return;
	}


	if (! Nav_ActionKey(EXEC_CurKey, &ACT_Transform_release))
		return;


	int middle_x, middle_y;

	Objs_CalcMiddle(edit.Selected, &middle_x, &middle_y);

	main_win->canvas->TransformBegin(edit.map_x, edit.map_y, middle_x, middle_y, mode);

	Editor_SetAction(ACT_TRANSFORM);
}


void CMD_WHEEL_Scroll()
{
	float speed = atof(EXEC_Param[0]);

	if (Exec_HasFlag("/LAX"))
	{
		keycode_t mod = Fl::event_state() & MOD_ALL_MASK;

		if (mod & MOD_SHIFT)
			speed /= 3.0;
		else if (mod & MOD_COMMAND)
			speed *= 3.0;
	}

	float delta_x =     wheel_dx;
	float delta_y = 0 - wheel_dy;

	int base_size = (main_win->canvas->w() + main_win->canvas->h()) / 2;

	speed = speed * base_size / 100.0 / grid.Scale;

	grid.Scroll(delta_x * speed, delta_y * speed);
}


void CMD_Merge()
{
	switch (edit.mode)
	{
		case OBJ_VERTICES:
			CMD_VT_Merge();
			break;

		case OBJ_LINEDEFS:
			CMD_LIN_MergeTwo();
			break;

		case OBJ_SECTORS:
			CMD_SEC_Merge();
			break;

		case OBJ_THINGS:
			CMD_TH_Merge();
			break;

		default:
			Beep("Cannot merge that");
			break;
	}
}


void CMD_Disconnect()
{
	switch (edit.mode)
	{
		case OBJ_VERTICES:
			CMD_VT_Disconnect();
			break;

		case OBJ_LINEDEFS:
			CMD_LIN_Disconnect();
			break;

		case OBJ_SECTORS:
			CMD_SEC_Disconnect();
			break;

		case OBJ_THINGS:
			CMD_TH_Disconnect();
			break;

		default:
			Beep("Cannot disconnect that");
			break;
	}
}


void CMD_Zoom()
{
	int delta = atoi(EXEC_Param[0]);

	if (delta == 0)
	{
		Beep("Zoom: bad or missing value");
		return;
	}

	int mid_x = edit.map_x;
	int mid_y = edit.map_y;

	if (Exec_HasFlag("/center"))
	{
		mid_x = I_ROUND(grid.orig_x);
		mid_y = I_ROUND(grid.orig_y);
	}

	Editor_Zoom(delta, mid_x, mid_y);
}


void CMD_ZoomWholeMap()
{
	ZoomWholeMap();
}


void CMD_ZoomSelection()
{
	if (edit.Selected->empty())
	{
		Beep("No selection to zoom");
		return;
	}

	GoToSelection();
}


void CMD_GoToCamera()
{
	int x, y;
	float angle;

	Render3D_GetCameraPos(&x, &y, &angle);

	grid.MoveTo(x, y);

	RedrawMap();
}


void CMD_PlaceCamera()
{
	if (edit.render3d)
	{
		Beep("Not supported in 3D view");
		return;
	}

	if (! edit.pointer_in_window)
	{
		// IDEA: turn cursor into cross, wait for click in map window

		Beep("Mouse is not over map");
		return;
	}

	int x = edit.map_x;
	int y = edit.map_y;

	Render3D_SetCameraPos(x, y);

	if (Exec_HasFlag("/open3d"))
	{
		Render3D_Enable(true);
	}

	RedrawMap();
}


void CMD_MoveObjects_Dialog()
{
	if (edit.Selected->empty())
	{
		Beep("Nothing to move");
		return;
	}

	UI_MoveDialog * dialog = new UI_MoveDialog();

	dialog->Run();

	delete dialog;
}


void CMD_ScaleObjects_Dialog()
{
	if (edit.Selected->empty())
	{
		Beep("Nothing to scale");
		return;
	}

	UI_ScaleDialog * dialog = new UI_ScaleDialog();

	dialog->Run();

	delete dialog;
}


void CMD_RotateObjects_Dialog()
{
	if (edit.Selected->empty())
	{
		Beep("Nothing to rotate");
		return;
	}

	UI_RotateDialog * dialog = new UI_RotateDialog();

	dialog->Run();

	delete dialog;
}


void CMD_GRID_Bump()
{
	int delta = atoi(EXEC_Param[0]);

	delta = (delta >= 0) ? +1 : -1;

	grid.AdjustStep(delta);
}


void CMD_GRID_Set()
{
	int step = atoi(EXEC_Param[0]);

	if (step < 2 || step > 4096)
	{
		Beep("Bad grid step");
		return;
	}

	grid.SetStep(step);
}


void CMD_GRID_Zoom()
{
	// target scale is positive for NN:1 and negative for 1:NN

	double scale = atof(EXEC_Param[0]);

	if (scale == 0)
	{
		Beep("Bad scale");
		return;
	}

	if (scale < 0)
		scale = -1.0 / scale;

	float S1 = grid.Scale;

	grid.NearestScale(scale);

	grid.RefocusZoom(edit.map_x, edit.map_y, S1);
}


void CMD_BR_CycleCategory()
{
	if (! main_win->browser->visible())
	{
		Beep("Browser not open");
		return;
	}

	int dir = (atoi(EXEC_Param[0]) >= 0) ? +1 : -1;

	main_win->browser->CycleCategory(dir);
}


void CMD_BR_ClearSearch()
{
	if (! main_win->browser->visible())
	{
		Beep("Browser not open");
		return;
	}

	main_win->browser->ClearSearchBox();
}


void CMD_BR_Scroll()
{
	if (! main_win->browser->visible())
	{
		Beep("Browser not open");
		return;
	}

	if (! EXEC_Param[0][0])
	{
		Beep("Missing parameter to BR_Scroll");
		return;
	}

	int delta = atoi(EXEC_Param[0]);

	main_win->browser->Scroll(delta);
}


void CMD_DefaultProps()
{
	main_win->ShowDefaultProps();
}


void CMD_FindDialog()
{
	main_win->ShowFindAndReplace();
}


void CMD_FindNext()
{
	main_win->find_box->FindNext();
}


void CMD_LogViewer()
{
	LogViewer_Open();
}


void CMD_OnlineDocs()
{
	fl_open_uri("http://eureka-editor.sourceforge.net/?n=Docs.Index");
}


void CMD_AboutDialog()
{
	DLG_AboutText();
}


//------------------------------------------------------------------------


static editor_command_t  command_table[] =
{
	/* ---- miscellaneous / UI stuff ---- */

	{	"Nothing", "Misc",
		&CMD_Nothing
	},

	{	"Set", "Misc",
		&CMD_SetVar,
		/* flags */ NULL,
		/* keywords */ "3d browser gamma grid obj_nums sec_render snap sprites"
	},

	{	"Toggle", "Misc",
		&CMD_ToggleVar,
		/* flags */ NULL,
		/* keywords */ "3d browser gamma grid obj_nums sec_render snap recent sprites"
	},

	{	"MetaKey", "Misc",
		&CMD_MetaKey
	},

	{	"EditMode", "Misc",
		&CMD_EditMode,
		/* flags */ NULL,
		/* keywords */ "thing line sector vertex"
	},

	{	"OperationMenu",  "Misc",
		&CMD_OperationMenu
	},

	{	"MapCheck", "Misc",
		&CMD_MapCheck,
		/* flags */ NULL,
		/* keywords */ "all major vertices sectors linedefs things textures tags current"
	},


	/* ----- 2D canvas ----- */

	{	"Scroll",  "2D View",
		&CMD_Scroll
	},

	{	"GRID_Bump",  "2D View",
		&CMD_GRID_Bump
	},

	{	"GRID_Set",  "2D View",
		&CMD_GRID_Set
	},

	{	"GRID_Zoom",  "2D View",
		&CMD_GRID_Zoom
	},

	{	"ACT_SelectBox", "2D View",
		&CMD_ACT_SelectBox
	},

	{	"WHEEL_Scroll",  "2D View",
		&CMD_WHEEL_Scroll
	},

	{	"NAV_Scroll_Left",  "2D View",
		&CMD_NAV_Scroll_Left
	},

	{	"NAV_Scroll_Right",  "2D View",
		&CMD_NAV_Scroll_Right
	},

	{	"NAV_Scroll_Up",  "2D View",
		&CMD_NAV_Scroll_Up
	},

	{	"NAV_Scroll_Down",  "2D View",
		&CMD_NAV_Scroll_Down
	},

	{	"NAV_MouseScroll", "2D View",
		&CMD_NAV_MouseScroll
	},


	/* ----- FILE menu ----- */

	{	"NewProject",  "File",
		&CMD_NewProject
	},

	{	"ManageProject",  "File",
		&CMD_ManageProject
	},

	{	"OpenMap",  "File",
		&CMD_OpenMap
	},

	{	"GivenFile",  "File",
		&CMD_GivenFile,
		/* flags */ NULL,
		/* keywords */ "next prev first last current"
	},

	{	"FlipMap",  "File",
		&CMD_FlipMap,
		/* flags */ NULL,
		/* keywords */ "next prev first last"
	},

	{	"SaveMap",  "File",
		&CMD_SaveMap
	},

	{	"ExportMap",  "File",
		&CMD_ExportMap
	},

	{	"FreshMap",  "File",
		&CMD_FreshMap
	},

	{	"CopyMap",  "File",
		&CMD_CopyMap
	},

	{	"RenameMap",  "File",
		&CMD_RenameMap
	},

	{	"DeleteMap",  "File",
		&CMD_DeleteMap
	},

	{	"TestMap",  "File",
		&CMD_TestMap
	},

	{	"BuildAllNodes",  "File",
		&CMD_BuildAllNodes
	},

	{	"PreferenceDialog",  "File",
		&CMD_Preferences
	},

	{	"Quit",  "File",
		&CMD_Quit
	},


	/* ----- EDIT menu ----- */

	{	"Undo",   "Edit",
		&CMD_Undo
	},

	{	"Redo",   "Edit",
		&CMD_Redo
	},

	{	"Insert",	"Edit",
		&CMD_Insert,
		/* flags */ "/continue /nofill"
	},

	{	"Delete",	"Edit",
		&CMD_Delete,
		/* flags */ "/keep"
	},

	{	"Clipboard_Cut",   "Edit",
		&CMD_Clipboard_Cut
	},

	{	"Clipboard_Copy",   "Edit",
		&CMD_Clipboard_Copy
	},

	{	"Clipboard_Paste",   "Edit",
		&CMD_Clipboard_Paste
	},

	{	"Select",	"Edit",
		&CMD_Select
	},

	{	"SelectAll",	"Edit",
		&CMD_SelectAll
	},

	{	"UnselectAll",	"Edit",
		&CMD_UnselectAll
	},

	{	"InvertSelection",	"Edit",
		&CMD_InvertSelection
	},

	{	"LastSelection",	"Edit",
		&CMD_LastSelection
	},

	{	"CopyAndPaste",   "Edit",
		&CMD_CopyAndPaste
	},

	{	"CopyProperties",   "Edit",
		&CMD_CopyProperties,
		/* flags */ "/reverse"
	},

	{	"PruneUnused",   "Edit",
		&CMD_PruneUnused
	},

	{	"MoveObjectsDialog",   "Edit",
		&CMD_MoveObjects_Dialog
	},

	{	"ScaleObjectsDialog",   "Edit",
		&CMD_ScaleObjects_Dialog
	},

	{	"RotateObjectsDialog",   "Edit",
		&CMD_RotateObjects_Dialog
	},


	/* ----- VIEW menu ----- */

	{	"Zoom",  "View",
		&CMD_Zoom,
		/* flags */ "/center"
	},

	{	"ZoomWholeMap",  "View",
		&CMD_ZoomWholeMap
	},

	{	"ZoomSelection",  "View",
		&CMD_ZoomSelection
	},

	{	"DefaultProps",  "View",
		&CMD_DefaultProps
	},

	{	"FindDialog",  "View",
		&CMD_FindDialog
	},

	{	"FindNext",  "View",
		&CMD_FindNext
	},

	{	"GoToCamera",  "View",
		&CMD_GoToCamera
	},

	{	"PlaceCamera",  "View",
		&CMD_PlaceCamera,
		/* flags */ "/open3d"
	},

	{	"JumpToObject",  "View",
		&CMD_JumpToObject
	},


	/* ------ HELP menu ------ */

	{	"LogViewer",  "Help",
		&CMD_LogViewer
	},

	{	"OnlineDocs",  "Help",
		&CMD_OnlineDocs
	},

	{	"AboutDialog",  "Help",
		&CMD_AboutDialog
	},


	/* ----- general operations ----- */

	{	"Merge",	"General",
		&CMD_Merge,
		/* flags */ "/keep"
	},

	{	"Disconnect",	"General",
		&CMD_Disconnect
	},

	{	"Mirror",	"General",
		&CMD_Mirror,
		/* flags */ NULL,
		/* keywords */ "horiz vert"
	},

	{	"Rotate90",	"General",
		&CMD_Rotate90,
		/* flags */ NULL,
		/* keywords */ "cw acw"
	},

	{	"Enlarge",	"General",
		&CMD_Enlarge
	},

	{	"Shrink",	"General",
		&CMD_Shrink
	},

	{	"Quantize",	"General",
		&CMD_Quantize
	},

	{	"ApplyTag",	"General",
		&CMD_ApplyTag,
		/* flags */ NULL,
		/* keywords */ "fresh last"
	},

	{	"ACT_Click", "General",
		&CMD_ACT_Click,
		/* flags */ "/noselect /nodrag /nosplit"
	},

	{	"ACT_Drag", "General",
		&CMD_ACT_Drag
	},

	{	"ACT_Transform", "General",
		&CMD_ACT_Transform,
		/* flags */ NULL,
		/* keywords */ "scale stretch rotate rotscale skew"
	},


	/* ------ LineDef mode ------ */

	{	"LIN_Align", NULL,
		&CMD_LIN_Align,
		/* flags */ "/x /y /right /clear"
	},

	{	"LIN_Flip", NULL,
		&CMD_LIN_Flip,
		/* flags */ "/force"
	},

	{	"LIN_SwapSides", NULL,
		&CMD_LIN_SwapSides
	},

	{	"LIN_SplitHalf", NULL,
		&CMD_LIN_SplitHalf
	},

	{	"LIN_SelectPath", NULL,
		&CMD_LIN_SelectPath,
		/* flags */ "/fresh /onesided /sametex"
	},


	/* ------ Sector mode ------ */

	{	"SEC_Floor", NULL,
		&CMD_SEC_Floor
	},

	{	"SEC_Ceil", NULL,
		&CMD_SEC_Ceil
	},

	{	"SEC_Light", NULL,
		&CMD_SEC_Light
	},

	{	"SEC_SelectGroup", NULL,
		&CMD_SEC_SelectGroup,
		/* flags */ "/fresh /can_walk /doors /floor_h /floor_tex /ceil_h /ceil_tex /light /tag /special"
	},

	{	"SEC_SwapFlats", NULL,
		&CMD_SEC_SwapFlats
	},


	/* ------ Thing mode ------ */

	{	"TH_Spin", NULL,
		&CMD_TH_SpinThings
	},


	/* ------ Vertex mode ------ */

	{	"VT_ShapeLine", NULL,
		&CMD_VT_ShapeLine
	},

	{	"VT_ShapeArc", NULL,
		&CMD_VT_ShapeArc
	},


	/* -------- Browser -------- */

	{	"BrowserMode", "Browser",
		&CMD_BrowserMode,
		/* flags */ "/recent",
		/* keywords */ "obj tex flat line sec genline"
	},

	{	"BR_CycleCategory", "Browser",
		&CMD_BR_CycleCategory
	},

	{	"BR_ClearSearch", "Browser",
		&CMD_BR_ClearSearch
	},

	{	"BR_Scroll", "Browser",
		&CMD_BR_Scroll
	},

	// end of command list
	{	NULL, NULL, 0, NULL  }
};


void Editor_RegisterCommands()
{
	M_RegisterCommandList(command_table);
}


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
