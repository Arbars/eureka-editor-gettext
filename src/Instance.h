//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2020 Ioan Chera
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

#ifndef INSTANCE_H_
#define INSTANCE_H_

#include "bsp.h"
#include "Document.h"
#include "e_main.h"
#include "im_img.h"
#include "m_game.h"
#include "m_loadsave.h"
#include "main.h"
#include "MasterDirectory.h"
#include "r_grid.h"
#include "r_render.h"
#include "r_subdiv.h"
#include "w_texture.h"
#include "w_wad.h"
#include "WadData.h"

#include <unordered_map>

class Fl_RGB_Image;
class Lump_c;
class UI_NodeDialog;
class UI_ProjectSetup;

//
// An instance with a document, holding all other associated data, such as the window reference, the
// wad list.
//
class Instance
{
public:
	// E_BASIS
	fixcoord_t MakeValidCoord(double x) const;

	// E_COMMANDS
	void ACT_Click_release();
	void ACT_Drag_release();
	void ACT_SelectBox_release();
	void ACT_Transform_release();
	void ACT_AdjustOfs_release();
	void CheckBeginDrag();
	void CMD_AboutDialog();
	void CMD_ACT_Drag();
	void CMD_ACT_Click();
	void CMD_ACT_SelectBox();
	void CMD_ACT_Transform();
	void CMD_AddBehaviorLump();
	void CMD_ApplyTag();
	void CMD_BR_ClearSearch();
	void CMD_BR_CycleCategory();
	void CMD_BR_Scroll();
	void CMD_BrowserMode();
	void CMD_BuildAllNodes();
	void CMD_Clipboard_Copy();
	void CMD_Clipboard_Cut();
	void CMD_Clipboard_Paste();
	void CMD_CopyAndPaste();
	void CMD_CopyMap();
	void CMD_CopyProperties();
	void CMD_DefaultProps();
	void CMD_Delete();
	void CMD_DeleteMap();
	void CMD_Disconnect();
	void CMD_EditLump();
	void CMD_EditMode();
	void CMD_Enlarge();
	void CMD_ExportMap();
	void CMD_FindDialog();
	void CMD_FindNext();
	void CMD_FlipMap();
	void CMD_FreshMap();
	void CMD_GivenFile();
	void CMD_GoToCamera();
	void CMD_GRID_Bump();
	void CMD_GRID_Set();
	void CMD_GRID_Zoom();
	void CMD_InvertSelection();
	void CMD_JumpToObject();
	void CMD_LastSelection();
	void CMD_LIN_Align();
	void CMD_LIN_Flip();
	void CMD_LIN_SelectPath();
	void CMD_LIN_SplitHalf();
	void CMD_LIN_SwapSides();
	void CMD_LogViewer();
	void CMD_ManageProject();
	void CMD_MapCheck();
	void CMD_Merge();
	void CMD_MetaKey();
	void CMD_Mirror();
	void CMD_MoveObjects_Dialog();
	void CMD_NAV_MouseScroll();
	void CMD_NAV_Scroll_Down();
	void CMD_NAV_Scroll_Left();
	void CMD_NAV_Scroll_Right();
	void CMD_NAV_Scroll_Up();
	void CMD_NewProject();
	void CMD_Nothing();
	void CMD_ObjectInsert();
	void CMD_OnlineDocs();
	void CMD_OpenMap();
	void CMD_OperationMenu();
	void CMD_PlaceCamera();
	void CMD_Preferences();
	void CMD_PruneUnused();
	void CMD_Quantize();
	void CMD_Quit();
	void CMD_RecalcSectors();
	void CMD_RenameMap();
	void CMD_Redo();
	void CMD_Rotate90();
	void CMD_RotateObjects_Dialog();
	void CMD_SaveMap();
	void CMD_ScaleObjects_Dialog();
	void CMD_Scroll();
	void CMD_SEC_Ceil();
	void CMD_SEC_Floor();
	void CMD_SEC_Light();
	void CMD_SEC_SelectGroup();
	void CMD_SEC_SwapFlats();
	void CMD_Select();
	void CMD_SelectAll();
	void CMD_SetVar();
	void CMD_Shrink();
	void CMD_TestMap();
	void CMD_TH_SpinThings();
	void CMD_ToggleVar();
	void CMD_Undo();
	void CMD_UnselectAll();
	void CMD_VT_ShapeArc();
	void CMD_VT_ShapeLine();
	void CMD_WHEEL_Scroll();
	void CMD_Zoom();
	void CMD_ZoomSelection();
	void CMD_ZoomWholeMap();
	void NAV_MouseScroll_release();
	void NAV_Scroll_Left_release();
	void NAV_Scroll_Right_release();
	void NAV_Scroll_Up_release();
	void NAV_Scroll_Down_release();
	void R3D_ACT_AdjustOfs();
	void R3D_Backward();
	void R3D_Forward();
	void R3D_Left();
	void R3D_NAV_Forward();
	void R3D_NAV_Forward_release();
	void R3D_NAV_Back();
	void R3D_NAV_Back_release();
	void R3D_NAV_Right();
	void R3D_NAV_Right_release();
	void R3D_NAV_Left();
	void R3D_NAV_Left_release();
	void R3D_NAV_Up();
	void R3D_NAV_Up_release();
	void R3D_NAV_Down();
	void R3D_NAV_Down_release();
	void R3D_NAV_TurnLeft();
	void R3D_NAV_TurnLeft_release();
	void R3D_NAV_TurnRight();
	void R3D_NAV_TurnRight_release();
	void R3D_Down();
	void R3D_DropToFloor();
	void R3D_Right();
	void R3D_Set();
	void R3D_Toggle();
	void R3D_Turn();
	void R3D_Up();
	void R3D_WHEEL_Move();
	void Transform_Update();

	// E_CUTPASTE
	int Texboard_GetFlatNum() const;
	int Texboard_GetTexNum() const;
	void Texboard_SetFlat(const SString &new_flat) const;
	void Texboard_SetTex(const SString &new_tex) const;

	// E_LINEDEF
	bool LD_RailHeights(int &z1, int &z2, const LineDef *L, const SideDef *sd,
		const Sector *front, const Sector *back) const;

	// E_MAIN
	void CalculateLevelBounds();
	void Editor_ChangeMode(char mode_char);
	void Editor_ChangeMode_Raw(ObjType new_mode);
	void Editor_ClearAction();
	void Editor_DefaultState();
	void Editor_Init();
	bool Editor_ParseUser(const std::vector<SString> &tokens);
	void Editor_WriteUser(std::ostream &os) const;
	void MapStuff_NotifyBegin();
	void MapStuff_NotifyChange(ObjType type, int objnum, int field);
	void MapStuff_NotifyDelete(ObjType type, int objnum);
	void MapStuff_NotifyEnd();
	void MapStuff_NotifyInsert(ObjType type, int objnum);
	void ObjectBox_NotifyBegin();
	void ObjectBox_NotifyChange(ObjType type, int objnum, int field);
	void ObjectBox_NotifyDelete(ObjType type, int objnum);
	void ObjectBox_NotifyEnd() const;
	void ObjectBox_NotifyInsert(ObjType type, int objnum);
	bool RecUsed_ParseUser(const std::vector<SString> &tokens);
	void RecUsed_WriteUser(std::ostream &os) const;
	void RedrawMap();
	void Selection_Add(Objid &obj) const;
	void Selection_Clear(bool no_save = false);
	void Selection_InvalidateLast();
	void Selection_NotifyBegin();
	void Selection_NotifyDelete(ObjType type, int objnum);
	void Selection_NotifyEnd();
	void Selection_NotifyInsert(ObjType type, int objnum);
	void Selection_Push();
	void Selection_Toggle(Objid &obj) const;
	SelectHighlight SelectionOrHighlight();
	void UpdateHighlight();
	void ZoomWholeMap();

	// E_PATH
	void GoToErrors();
	void GoToObject(const Objid &objid);
	void GoToSelection();
	const byte *SoundPropagation(int start_sec);

	// IM_IMG
	std::unique_ptr<Img_c> IM_ConvertRGBImage(const Fl_RGB_Image *src) const;
	std::unique_ptr<Img_c> IM_ConvertTGAImage(const rgba_color_t *data, int W,
											  int H) const;
	Img_c *IM_CreateDogSprite() const;
	Img_c *IM_CreateLightSprite() const;
	Img_c *IM_CreateMapSpotSprite(int base_r, int base_g, int base_b) const;
	Img_c *IM_DigitFont_11x14();
	Img_c *IM_DigitFont_14x19();
	Img_c *IM_MissingTex();
	Img_c *IM_SpecialTex();
	Img_c *IM_UnknownFlat();
	Img_c *IM_UnknownSprite();
	Img_c *IM_UnknownTex();
	void W_UnloadAllTextures() const;
	void IM_UnloadDummyTextures() const;

	// this applies a constant gamma.
	// for textures/flats/things in the browser and panels.
	inline void IM_DecodePixel_medium(img_pixel_t p, byte &r, byte &g, byte &b) const
	{
		if(p & IS_RGB_PIXEL)
		{
			r = wad.rgb555_medium[IMG_PIXEL_RED(p)];
			g = wad.rgb555_medium[IMG_PIXEL_GREEN(p)];
			b = wad.rgb555_medium[IMG_PIXEL_BLUE(p)];
		}
		else
		{
			const rgb_color_t col = wad.palette_medium[p];

			r = RGB_RED(col);
			g = RGB_GREEN(col);
			b = RGB_BLUE(col);
		}
	}

	// M_CONFIG
	void M_DefaultUserState();
	bool M_LoadUserState();
	bool M_SaveUserState() const;

	// M_EVENTS
	void ClearStickyMod();
	void Editor_ClearNav();
	void Editor_ScrollMap(int mode, int dx = 0, int dy = 0, keycode_t mod = 0);
	void Editor_SetAction(editor_action_e new_action);
	void EV_EscapeKey();
	int EV_HandleEvent(int event);
	void M_LoadOperationMenus();
	bool Nav_ActionKey(keycode_t key, nav_release_func_t func);
	void Nav_Clear();
	void Nav_Navigate();
	bool Nav_SetKey(keycode_t key, nav_release_func_t func);
	unsigned Nav_TimeDiff();

	// M_FILES
	bool M_ParseEurekaLump(Wad_file *wad, bool keep_cmd_line_args = false);
	SString M_PickDefaultIWAD() const;
	bool M_TryOpenMostRecent();
	void M_WriteEurekaLump(Wad_file *wad) const;

	// M_GAME
	bool is_sky(const SString &flat) const;
	char M_GetFlatType(const SString &name) const;
	const linetype_t &M_GetLineType(int type) const;
	const sectortype_t &M_GetSectorType(int type) const;
	char M_GetTextureType(const SString &name) const;
	const thingtype_t &M_GetThingType(int type) const;
	SString M_LineCategoryString(SString &letters) const;
	SString M_TextureCategoryString(SString &letters, bool do_flats) const;
	SString M_ThingCategoryString(SString &letters) const;

	// M_KEYS
	void Beep(EUR_FORMAT_STRING(const char *fmt), ...) EUR_PRINTF(2, 3);
	bool Exec_HasFlag(const char *flag) const;
	bool ExecuteCommand(const editor_command_t *cmd, const SString &param1 = "", const SString &param2 = "", const SString &param3 = "", const SString &param4 = "");
	bool ExecuteCommand(const SString &name, const SString &param1 = "", const SString &param2 = "", const SString &param3 = "", const SString &param4 = "");
	bool ExecuteKey(keycode_t key, key_context_e context);

	// M_LOADSAVE
	Lump_c *Load_LookupAndSeek(const char *name) const;
	void LoadLevel(Wad_file *wad, const SString &level);
	void LoadLevelNum(Wad_file *wad, int lev_num);
	bool MissingIWAD_Dialog();
	void RemoveEditWad();
	void ReplaceEditWad(Wad_file* new_wad);
	bool M_SaveMap();
	void ValidateVertexRefs(LineDef *ld, int num);
	void ValidateSectorRef(SideDef *sd, int num);
	void ValidateSidedefRefs(LineDef *ld, int num);
	
	// M_NODES
	void BuildNodesAfterSave(int lev_idx);
	void GB_PrintMsg(EUR_FORMAT_STRING(const char *str), ...) const EUR_PRINTF(2, 3);

	// M_TESTMAP
	bool M_PortSetupDialog(const SString& port, const SString& game);

	// M_UDMF
	void UDMF_LoadLevel();
	void UDMF_SaveLevel() const;

	// MAIN
	bool Main_ConfirmQuit(const char *action) const;
	SString Main_FileOpFolder() const;
	void Main_LoadResources(LoadingData &loading);

	// R_RENDER
	void Render3D_CB_Copy() ;
	void Render3D_GetCameraPos(double *x, double *y, float *angle) const;
	void Render3D_MouseMotion(int x, int y, keycode_t mod, int dx, int dy);
	bool Render3D_ParseUser(const std::vector<SString> &tokens);
	void Render3D_SetCameraPos(double new_x, double new_y);
	void Render3D_Setup();
	void Render3D_UpdateHighlight();
	void Render3D_WriteUser(std::ostream &os) const;

	// R_SOFTWARE
	bool SW_QueryPoint(Objid &hl, int qx, int qy);
	void SW_RenderWorld(int ox, int oy, int ow, int oh);

	// R_SUBDIV
	sector_3dfloors_c *Subdiv_3DFloorsForSector(int num);
	void Subdiv_InvalidateAll();
	bool Subdiv_SectorOnScreen(int num, double map_lx, double map_ly, double map_hx, double map_hy);
	sector_subdivision_c *Subdiv_PolygonsForSector(int num);

	// UI_BROWSER
	void Browser_WriteUser(std::ostream &os) const;

	// UI_DEFAULT
	void Props_WriteUser(std::ostream &os) const;

	// UI_INFOBAR
	void Status_Set(EUR_FORMAT_STRING(const char *fmt), ...) const EUR_PRINTF(2, 3);
	void Status_Clear() const;

	// UI_MENU
	Fl_Sys_Menu_Bar *Menu_Create(int x, int y, int w, int h);

	// W_LOADPIC
	std::unique_ptr<Img_c> LoadImage_JPEG(Lump_c *lump,
										  const SString &name) const;
	std::unique_ptr<Img_c> LoadImage_PNG(Lump_c *lump,
										 const SString &name) const;
	std::unique_ptr<Img_c> LoadImage_TGA(Lump_c *lump,
										 const SString &name) const;
	bool LoadPicture(Img_c &dest, Lump_c *lump, const SString &pic_name, int pic_x_offset, int pic_y_offset, int *pic_width = nullptr, int *pic_height = nullptr) const;

	// W_TEXTURE
	bool W_FlatIsKnown(const SString &name) const;
	Img_c *W_GetFlat(const SString &name, bool try_uppercase = false) const;
	Img_c *W_GetSprite(int type);
	Img_c *W_GetTexture(const SString &name, bool try_uppercase = false) const;
	int W_GetTextureHeight(const SString &name) const;
	void W_LoadTextures();
	void W_LoadTextures_TX_START(Wad_file *wf);
	bool W_TextureCausesMedusa(const SString &name) const;
	bool W_TextureIsKnown(const SString &name) const;

	// W_WAD
	Lump_c *W_FindSpriteLump(const SString &name) const;

private:
	// New private methods
	void navigationScroll(float *editNav, nav_release_func_t func);
	void navigation3DMove(float *editNav, nav_release_func_t func, bool fly);
	void navigation3DTurn(float *editNav, nav_release_func_t func);

	// E_COMMANDS
	void DoBeginDrag();

	// E_CUTPASTE
	bool Clipboard_DoCopy();
	bool Clipboard_DoPaste();
	void ReselectGroup();
	int Texboard_GetThing() const;

	// E_LINEDEF
	void commandLinedefMergeTwo();

	// E_MAIN
	void Editor_ClearErrorMode();
	void UpdateDrawLine();
	void zoom_fit();

	// E_SECTOR
	void commandSectorMerge();

	// E_VERTEX
	void commandLineDisconnect();
	void commandSectorDisconnect();
	void commandVertexDisconnect();
	void commandVertexMerge();

	// M_EVENTS
	void Editor_Zoom(int delta, int mid_x, int mid_y);
	void EV_EnterWindow();
	void EV_LeaveWindow();
	void EV_MouseMotion(int x, int y, keycode_t mod, int dx, int dy);
	int EV_RawButton(int event);
	int EV_RawKey(int event);
	int EV_RawMouse(int event);
	int EV_RawWheel(int event);
	void M_AddOperationMenu(const SString &context, Fl_Menu_Button *menu);
	bool M_ParseOperationFile();

	// M_KEYS
	void DoExecuteCommand(const editor_command_t *cmd);

	// M_LOADSAVE
	void CreateFallbackSector();
	void CreateFallbackSideDef();
	void EmptyLump(const char *name) const;
	void FreshLevel();
	void LoadBehavior();
	void LoadHeader();
	void LoadLineDefs();
	void LoadLineDefs_Hexen();
	void LoadScripts();
	void LoadSectors();
	void LoadSideDefs();
	void LoadThings();
	void LoadThings_Hexen();
	void LoadVertices();
	bool M_ExportMap();
	void Navigate2D();
	void Project_ApplyChanges(UI_ProjectSetup *dialog);
	bool Project_AskFile(SString& filename) const;
	void SaveBehavior();
	void SaveHeader(const SString &level);
	void SaveLevel(const SString &level);
	void SaveLineDefs();
	void SaveLineDefs_Hexen();
	void SaveThings();
	void SaveThings_Hexen();
	void SaveScripts();
	void SaveSectors();
	void SaveSideDefs();
	void SaveVertices();
	void ShowLoadProblem() const;

	// M_NODES
	build_result_e BuildAllNodes(nodebuildinfo_t *info);

	// M_UDMF
	void ValidateLevel_UDMF();

	// R_GRID
	bool Grid_ParseUser(const std::vector<SString> &tokens);
	void Grid_WriteUser(std::ostream &os) const;

	// R_RENDER
	int GrabSelectedFlat();
	int GrabSelectedTexture();
	int GrabSelectedThing();
	int LD_GrabTex(const LineDef *L, int part) const;
	void Render3D_CB_Cut();
	void Render3D_CB_Paste();
	void Render3D_Navigate();
	void StoreDefaultedFlats();
	void StoreSelectedFlat(int new_tex);
	void StoreSelectedTexture(int new_tex);
	void StoreSelectedThing(int new_type);

	// W_TEXTURE
	void LoadTextureEntry_DOOM(byte *tex_data, int tex_length, int offset,
							   const byte *pnames, int pname_size,
							   bool skip_first);
	void LoadTextureEntry_Strife(const byte *tex_data, int tex_length,
								 int offset, const byte *pnames, int pname_size,
								 bool skip_first);
	void LoadTexturesLump(Lump_c *lump, const byte *pnames, int pname_size,
						  bool skip_first);
	void W_ClearSprites();

public:	// will be private when we encapsulate everything
	Document level{*this};	// level data proper

	UI_MainWindow *main_win = nullptr;
	Editor_State_t edit = {};

	//
	// Wad settings
	//

	// the current PWAD, or NULL for none.
	// when present it is also at master_dir.back()
	Wad_file *edit_wad = nullptr;
	Wad editWad;
	bool haveEditWad = false;
	Wad gameWad;
	SString Pwad_name;	// Filename of current wad

	MasterDirectory master;

	LoadingData loaded;

	//
	// Game-dependent (thus instance dependent) defaults
	//
	ConfigData conf;

	//
	// Panel stuff
	//
	bool changed_panel_obj = false;
	bool changed_recent_list = false;
	bool invalidated_last_sel = false;
	bool invalidated_panel_obj = false;
	bool invalidated_selection = false;
	bool invalidated_totals = false;

	//
	// Selection stuff
	//
	selection_c *last_Sel = nullptr;

	//
	// Document stuff
	//
	bool MadeChanges = false;
	double Map_bound_x1 = 32767;   /* minimum X value of map */
	double Map_bound_y1 = 32767;   /* minimum Y value of map */
	double Map_bound_x2 = -32767;   /* maximum X value of map */
	double Map_bound_y2 = -32767;   /* maximum Y value of map */
	int moved_vertex_count = 0;
	int new_vertex_minimum = 0;
	bool recalc_map_bounds = false;
	// the containers for the textures (etc)
	Recently_used recent_flats{ *this };
	Recently_used recent_textures{ *this };
	Recently_used recent_things{ *this };
	int bad_linedef_count = 0;
	int bad_sector_refs = 0;
	int bad_sidedef_refs = 0;
	// this is only used to prevent a M_SaveMap which happens inside
	// CMD_BuildAllNodes from building that saved level twice.
	bool inhibit_node_build = false;
	int last_given_file = 0;
	Wad_file *load_wad = nullptr;
	int loading_level = 0;
	int saving_level = 0;
	UI_NodeDialog *nodeialog = nullptr;
	nodebuildinfo_t *nb_info = nullptr;
	sprite_map_t sprites;

	WadData wad;

	//
	// Path stuff
	//
	bool sound_propagation_invalid = false;
	std::vector<byte> sound_prop_vec;
	std::vector<byte> sound_temp1_vec;
	std::vector<byte> sound_temp2_vec;
	int sound_start_sec = 0;

	//
	// Image stuff
	//
	Img_c *digit_font_11x14 = nullptr;
	Img_c *digit_font_14x19 = nullptr;
	Img_c *missing_tex_image = nullptr;
	Img_c *special_tex_image = nullptr;
	Img_c *unknown_flat_image = nullptr;
	Img_c *unknown_sprite_image = nullptr;
	Img_c *unknown_tex_image = nullptr;

	//
	// IO stuff
	//
	nav_active_key_t cur_action_key = {};
	bool in_operation_menu = false;
	int mouse_last_x = 0;
	int mouse_last_y = 0;
	nav_active_key_t nav_actives[MAX_NAV_ACTIVE_KEYS] = {};
	unsigned nav_time = 0;
	bool no_operation_cfg = false;
	std::unordered_map<SString, Fl_Menu_Button *> op_all_menus;
	// these are grabbed from FL_MOUSEWHEEL events
	int wheel_dx = 0;
	int wheel_dy = 0;

	// key or mouse button pressed for command, 0 when none
	keycode_t EXEC_CurKey = {};
	// result from command function, 0 is OK
	int EXEC_Errno = 0;
	SString EXEC_Flags[MAX_EXEC_PARAM] = {};
	SString EXEC_Param[MAX_EXEC_PARAM] = {};

	//
	// Rendering
	//
	Grid_State_c grid{ *this };
	Render_View_t r_view{ *this };
	sector_info_cache_c sector_info_cache{ *this };
};

// this one applies the current gamma.
// for rendering the 3D view or the 2D sectors and sprites.
inline void IM_DecodePixel(const WadData &wad, img_pixel_t p, byte &r, byte &g,
						   byte &b)
{
	if(p & IS_RGB_PIXEL)
	{
		r = wad.rgb555_gamma[IMG_PIXEL_RED(p)];
		g = wad.rgb555_gamma[IMG_PIXEL_GREEN(p)];
		b = wad.rgb555_gamma[IMG_PIXEL_BLUE(p)];
	}
	else
	{
		const rgb_color_t col = wad.palette[p];

		r = RGB_RED(col);
		g = RGB_GREEN(col);
		b = RGB_BLUE(col);
	}
}

extern Instance gInstance;	// for now we run with one instance, will have more for the MDI.

#endif
