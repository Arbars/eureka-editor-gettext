//------------------------------------------------------------------------
//  3D RENDERING
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

#include "main.h"

#include <map>
#include <algorithm>

#include "im_color.h"
#include "im_img.h"
#include "e_hover.h"
#include "e_linedef.h"
#include "e_main.h"
#include "e_things.h"
#include "e_objects.h"
#include "m_game.h"
#include "w_rawdef.h"
#include "w_loadpic.h"
#include "w_texture.h"

#include "m_events.h"
#include "r_render.h"

#include "ui_window.h"


#define INFO_BAR_H	30

#define INFO_TEXT_COL	fl_rgb_color(192, 192, 192)
#define INFO_DIM_COL	fl_rgb_color(128, 128, 128)


// config items
bool render_high_detail    = false;
bool render_lock_gravity   = false;
bool render_missing_bright = true;
bool render_unknown_bright = true;

// in original DOOM pixels were 20% taller than wide, giving 0.83
// as the pixel aspect ratio.
int  render_pixel_aspect = 83;  //  100 * width / height

rgb_color_t transparent_col = RGB_MAKE(0, 255, 255);


struct r_editing_info_t
{
public:
	// current highlighted wotsit
	Obj3d_t hl;

	// current selection
	std::vector< Obj3d_t > sel;

	obj3d_type_e sel_type;  // valid when sel.size() > 0

	// a remembered highlight (for operation menu)
	Obj3d_t saved_hl;

	// state for adjusting offsets via the mouse
	std::vector<int> adjust_sides;
	std::vector<int> adjust_lines;

	float adjust_dx, adjust_dx_factor;
	float adjust_dy, adjust_dy_factor;

	std::vector<int> saved_x_offsets;
	std::vector<int> saved_y_offsets;

public:
	r_editing_info_t() :
		hl(),
		sel(),
		sel_type(OB3D_Thing),
		adjust_sides(), adjust_lines(),
		saved_x_offsets(), saved_y_offsets()
	{ }

	~r_editing_info_t()
	{ }

public:
	bool SelectIsCompat(obj3d_type_e new_type) const
	{
		return (sel_type <= OB3D_Floor && new_type <= OB3D_Floor) ||
			   (sel_type == OB3D_Thing && new_type == OB3D_Thing) ||
			   (sel_type >= OB3D_Lower && new_type >= OB3D_Lower);
	}

	// this needed since we allow invalid objects in sel
	bool SelectEmpty() const
	{
		for (unsigned int k = 0 ; k < sel.size() ; k++)
			if (sel[k].valid())
				return false;

		return true;
	}

	bool SelectGet(const Obj3d_t& obj) const
	{
		for (unsigned int k = 0 ; k < sel.size() ; k++)
			if (sel[k] == obj)
				return true;

		return false;
	}

	void SelectToggle(const Obj3d_t& obj)
	{
		// when type of surface is radically different, clear selection
		if (! sel.empty() && ! SelectIsCompat(obj.type))
			sel.clear();

		if (sel.empty())
		{
			sel_type = obj.type;
			sel.push_back(obj);
			return;
		}

		// if object already selected, unselect it
		// [ we are lazy and leave a NIL object in the vector ]
		for (unsigned int k = 0 ; k < sel.size() ; k++)
		{
			if (sel[k] == obj)
			{
				sel[k].num = NIL_OBJ;
				return;
			}
		}

		sel.push_back(obj);
	}

	int GrabClipboard()
	{
		obj3d_type_e type = SelectEmpty() ? hl.type : sel_type;

		if (type == OB3D_Thing)
			return r_clipboard.GetThing();

		if (type == OB3D_Floor || type == OB3D_Ceil)
			return r_clipboard.GetFlatNum();

		return r_clipboard.GetTexNum();
	}

	void StoreClipboard(int new_val)
	{
		obj3d_type_e type = SelectEmpty() ? hl.type : sel_type;

		if (type == OB3D_Thing)
		{
			r_clipboard.SetThing(new_val);
			return;
		}

		const char *name = BA_GetString(new_val);

		if (type == OB3D_Floor || type == OB3D_Ceil)
			r_clipboard.SetFlat(name);
		else
			r_clipboard.SetTex(name);
	}

	void AddAdjustSide(const Obj3d_t& obj)
	{
		const LineDef *L = LineDefs[obj.num];

		int sd = (obj.side < 0) ? L->left : L->right;

		// this should not happen
		if (sd < 0)
			return;

		// ensure it is not already there
		// (e.g. when a line's upper and lower are both selected)
		for (unsigned int k = 0 ; k < adjust_sides.size() ; k++)
			if (adjust_sides[k] == sd)
				return;

		adjust_sides.push_back(sd);
		adjust_lines.push_back(obj.num);
	}

	float AdjustDistFactor(float view_x, float view_y)
	{
		if (adjust_lines.empty())
			return 128.0;

		double total = 0;

		for (unsigned int k = 0 ; k < adjust_lines.size() ; k++)
		{
			const LineDef *L = LineDefs[adjust_lines[k]];
			total += ApproxDistToLineDef(L, view_x, view_y);
		}

		return total / (double)adjust_lines.size();
	}

	void SaveOffsets()
	{
		unsigned int total = adjust_sides.size();

		if (total == 0)
			return;

		if (saved_x_offsets.size() != total)
		{
			saved_x_offsets.resize(total);
			saved_y_offsets.resize(total);
		}

		for (unsigned int k = 0 ; k < total ; k++)
		{
			SideDef *SD = SideDefs[adjust_sides[k]];

			saved_x_offsets[k] = SD->x_offset;
			saved_y_offsets[k] = SD->y_offset;

			// change it temporarily (just for the render)
			SD->x_offset += (int)adjust_dx;
			SD->y_offset += (int)adjust_dy;
		}
	}

	void RestoreOffsets()
	{
		unsigned int total = adjust_sides.size();

		for (unsigned int k = 0 ; k < total ; k++)
		{
			SideDef *SD = SideDefs[adjust_sides[k]];

			SD->x_offset = saved_x_offsets[k];
			SD->y_offset = saved_y_offsets[k];
		}
	}

};

static r_editing_info_t  r_edit;


struct R_View
{
public:
	// player type and position.
	int p_type, px, py;

	// view position.
	float x, y, z;

	// view direction.  angle is in radians
	float angle;
	float Sin, Cos;

	// screen image.
	int sw, sh;
	img_pixel_t *screen;

	float aspect_sh;
	float aspect_sw;  // sw * aspect_ratio

	bool texturing;
	bool sprites;
	bool lighting;

	bool gravity;  // when true, walk on ground

	std::vector<int> thing_sectors;
	int thsec_sector_num;
	bool thsec_invalidated;

	// navigation loop info
	bool is_scrolling;
	float scroll_speed;

	unsigned int nav_time;

	float nav_fwd, nav_back;
	float nav_left, nav_right;
	float nav_up, nav_down;
	float nav_turn_L, nav_turn_R;

public:
	R_View() :
		p_type(0), px(), py(),
		x(), y(), z(),
		angle(), Sin(), Cos(),
		sw(), sh(), screen(NULL),
		texturing(false), sprites(false), lighting(false),
		gravity(true),
	    thing_sectors(),
		thsec_sector_num(0),
		thsec_invalidated(false),
		is_scrolling(false),
		nav_time(0)
	{ }

	~R_View()
	{ }

	void SetAngle(float new_ang)
	{
		angle = new_ang;

		if (angle >= 2*M_PI)
			angle -= 2*M_PI;
		else if (angle < 0)
			angle += 2*M_PI;

		Sin = sin(angle);
		Cos = cos(angle);
	}

	void FindGroundZ()
	{
		// test a grid of points on the player's bounding box, and
		// use the maximum floor of all contacted sectors.

		int max_floor = INT_MIN;

		for (int dx = -2 ; dx <= 2 ; dx++)
		for (int dy = -2 ; dy <= 2 ; dy++)
		{
			Objid o;

			GetNearObject(o, OBJ_SECTORS, int(x + dx*8), int(y + dy*8));

			if (o.num >= 0)
				max_floor = MAX(max_floor, Sectors[o.num]->floorh);
		}

		if (max_floor != INT_MIN)
			z = max_floor + game_info.view_height;
	}

	void CalcAspect()
	{
		aspect_sw = sw;	 // things break if these are different

		aspect_sh = sw / (render_pixel_aspect / 100.0);
	}

	void UpdateScreen(int ow, int oh)
	{
		// in low detail mode, setup size so that expansion always covers
		// our window (i.e. we draw a bit more than we need).

		int new_sw = render_high_detail ? ow : (ow + 1) / 2;
		int new_sh = render_high_detail ? oh : (oh + 1) / 2;

		if (!screen || sw != new_sw || sh != new_sh)
		{
			sw = new_sw;
			sh = new_sh;

			if (screen)
				delete[] screen;

			screen = new img_pixel_t [sw * sh];
		}

		CalcAspect();
	}

	void ClearScreen()
	{
		// color #0 is black (DOOM, Heretic, Hexen)
		memset(screen, 0, sw * sh * sizeof(screen[0]));
	}

	void FindThingSectors()
	{
		thing_sectors.resize(NumThings);

		for (int i = 0 ; i < NumThings ; i++)
		{
			Objid obj;

			GetNearObject(obj, OBJ_SECTORS, Things[i]->x, Things[i]->y);

			thing_sectors[i] = obj.num;
		}

		thsec_sector_num  = NumSectors;
		thsec_invalidated = false;
	}

	inline int R_DoomLightingEquation(int L, float dist)
	{
		/* L in the range 0 to 63 */
		int min_L = CLAMP(0, 36 - L, 31);

		int index = (59 - L) - int(1280 / MAX(1, dist));

		/* result is colormap index (0 bright .. 31 dark) */
		return CLAMP(min_L, index, 31);
	}

	img_pixel_t DoomLightRemap(int light, float dist, img_pixel_t pixel)
	{
		int map = R_DoomLightingEquation(light >> 2, dist);

		if (pixel & IS_RGB_PIXEL)
		{
			map = (map ^ 31) + 1;

			int r = IMG_PIXEL_RED(pixel);
			int g = IMG_PIXEL_GREEN(pixel);
			int b = IMG_PIXEL_BLUE(pixel);

			r = (r * map) >> 5;
			g = (g * map) >> 5;
			b = (b * map) >> 5;

			return IMG_PIXEL_MAKE_RGB(r, g, b);
		}
		else
		{
			return raw_colormap[map][pixel];
		}
	}

	void PrepareToRender(int ow, int oh)
	{
		if (thsec_invalidated || !screen ||
			NumThings  != (int)thing_sectors.size() ||
			NumSectors != thsec_sector_num)
		{
			FindThingSectors();
		}

		UpdateScreen(ow, oh);

		if (gravity)
			FindGroundZ();
	}
};

static R_View view;


struct DrawSurf
{
public:
	enum
	{
		K_INVIS = 0,
		K_FLAT,
		K_TEXTURE
	};
	int kind;

	// heights for the surface (h1 is below h2)
	int h1, h2, tex_h;

	Img_c *img;
	img_pixel_t col;  /* used when no image */

	enum
	{
		SOLID_ABOVE = 1,
		SOLID_BELOW = 2
	};
	int y_clip;

	bool fullbright;

public:
	DrawSurf() : kind(K_INVIS), h1(), h2(), tex_h(),
				 img(NULL), col(), y_clip(),
				 fullbright(false)
	{ }

	~DrawSurf()
	{ }

	int hashed_color(const char *name, const int *cols)
	{
		int hash = name[0]*17 + name[2]*7  + name[4]*3 +
		           name[5]*13 + name[6]*47 + name[7];

		hash ^= (hash >> 5);

		int c1 = cols[0];
		int c2 = cols[1];

		if (c1 > c2)
			std::swap(c1, c2);

		if (c1 == c2)
			return c1;

		return c1 + hash % (c2 - c1 + 1);
	}

	void FindFlat(const char * fname, Sector *sec)
	{
		fullbright = false;

		if (is_sky(fname))
		{
			col = game_info.sky_color;
			fullbright = true;
			return;
		}

		if (view.texturing)
		{
			img = W_GetFlat(fname);

			if (! img)
			{
				img = IM_UnknownFlat();
				fullbright = render_unknown_bright;
			}

			return;
		}

		col = hashed_color(fname, game_info.floor_colors);
	}

	void FindTex(const char * tname, LineDef *ld)
	{
		fullbright = false;

		if (view.texturing)
		{
			if (is_null_tex(tname))
			{
				img = IM_MissingTex();
				fullbright = render_missing_bright;
				return;
			}

			img = W_GetTexture(tname);

			if (! img)
			{
				img = IM_UnknownTex();
				fullbright = render_unknown_bright;
			}

			return;
		}

		col = hashed_color(tname, game_info.wall_colors);
	}
};


struct DrawWall
{
public:
	typedef std::vector<struct DrawWall *> vec_t;

	// when 'th' is >= 0, this is actually a sprite, and 'ld' and
	// 'sd' will be NULL.  Sprites use the info in the 'ceil' surface.
	int th;

	LineDef *ld;
	SideDef *sd;
	Sector *sec;

	// which side this wall faces (SIDE_LEFT or SIDE_RIGHT)
	// for sprites: a copy of the thinginfo flags
	int side;

	// lighting for wall, adjusted for N/S and E/W walls
	int wall_light;

	// line constants
	float delta_ang;
	float dist, t_dist;
	float normal;	// scale for things

	// distance values (inverted, so they can be lerped)
	double iz1, iz2;
	double diz, cur_iz;
	double mid_iz;

	// translate coord, for sprite
	float spr_tx1;

	// screen X coordinates
	int sx1, sx2;

	// for sprites, the remembered open space to clip to
	int oy1, oy2;

	/* surfaces */

	DrawSurf ceil;
	DrawSurf upper;
	DrawSurf lower;
	DrawSurf floor;
	DrawSurf rail;

#define IZ_EPSILON  1e-6

   /* PREDICATES */

	struct MidDistCmp
	{
		inline bool operator() (const DrawWall * A, const DrawWall * B) const
		{
			return A->mid_iz > B->mid_iz;
		}
	};

	struct DistCmp
	{
		inline bool operator() (const DrawWall * A, const DrawWall * B) const
		{
			if (fabs(A->cur_iz - B->cur_iz) >= IZ_EPSILON)
			{
				// this is the normal case
				return A->cur_iz > B->cur_iz;
			}

			// this case usually occurs at a column where two walls share a vertex.
			//
			// hence we check if they actually share a vertex, and if so then
			// we test whether A is behind B or not -- by checking which side
			// of B the camera and the other vertex of A are on.

			if (A->ld && B->ld)
			{
				// find the vertex of A _not_ shared with B
				int A_other = -1;

				if (B->ld->TouchesVertex(A->ld->start)) A_other = A->ld->end;
				if (B->ld->TouchesVertex(A->ld->end))   A_other = A->ld->start;

				if (A_other >= 0)
				{
					int ax = Vertices[A_other]->x;
					int ay = Vertices[A_other]->y;

					int bx1 = B->ld->Start()->x;
					int by1 = B->ld->Start()->y;
					int bx2 = B->ld->End()->x;
					int by2 = B->ld->End()->y;

					int cx = (int)view.x;  // camera
					int cy = (int)view.y;

					int A_side = PointOnLineSide(ax, ay, bx1, by1, bx2, by2);
					int C_side = PointOnLineSide(cx, cy, bx1, by1, bx2, by2);

					return (A_side * C_side >= 0);
				}
			}

			// a pretty good fallback:
			return A->mid_iz > B->mid_iz;
		}
	};

	struct SX1Cmp
	{
		inline bool operator() (const DrawWall * A, const DrawWall * B) const
		{
			return A->sx1 < B->sx1;
		}

		inline bool operator() (const DrawWall * A, int x) const
		{
			return A->sx1 < x;
		}

		inline bool operator() (int x, const DrawWall * A) const
		{
			return x < A->sx1;
		}
	};

	struct SX2Less
	{
		int x;

		SX2Less(int _x) : x(_x) { }

		inline bool operator() (const DrawWall * A) const
		{
			return A->sx2 < x;
		}
	};

	/* methods */

	void ComputeWallSurface()
	{
		Sector *front = sec;
		Sector *back  = NULL;

		SideDef *back_sd = (side == SIDE_LEFT) ? ld->Right() : ld->Left();
		if (back_sd)
			back = Sectors[back_sd->sector];

		bool sky_upper = back && is_sky(front->CeilTex()) && is_sky(back->CeilTex());
		bool self_ref  = (front == back) ? true : false;

		if ((front->ceilh > view.z || is_sky(front->CeilTex()))
		    && ! sky_upper && ! self_ref)
		{
			ceil.kind = DrawSurf::K_FLAT;
			ceil.h1 = front->ceilh;
			ceil.h2 = +99999;
			ceil.tex_h = ceil.h1;
			ceil.y_clip = DrawSurf::SOLID_ABOVE;

			ceil.FindFlat(front->CeilTex(), front);
		}

		if (front->floorh < view.z && ! self_ref)
		{
			floor.kind = DrawSurf::K_FLAT;
			floor.h1 = -99999;
			floor.h2 = front->floorh;
			floor.tex_h = floor.h2;
			floor.y_clip = DrawSurf::SOLID_BELOW;

			floor.FindFlat(front->FloorTex(), front);
		}

		if (! back)
		{
			/* ONE-sided line */

			lower.kind = DrawSurf::K_TEXTURE;
			lower.h1 = front->floorh;
			lower.h2 = front->ceilh;
			lower.y_clip = DrawSurf::SOLID_ABOVE | DrawSurf::SOLID_BELOW;

			lower.FindTex(sd->MidTex(), ld);

			if (lower.img && (ld->flags & MLF_LowerUnpegged))
				lower.tex_h = lower.h1 + lower.img->height();
			else
				lower.tex_h = lower.h2;

			lower.tex_h += sd->y_offset;
			return;
		}

		/* TWO-sided line */

		if (back->ceilh < front->ceilh && ! sky_upper && ! self_ref)
		{
			upper.kind = DrawSurf::K_TEXTURE;
			upper.h1 = back->ceilh;
			upper.h2 = front->ceilh;
			upper.y_clip = DrawSurf::SOLID_ABOVE;

			upper.FindTex(sd->UpperTex(), ld);

			if (upper.img && ! (ld->flags & MLF_UpperUnpegged))
				upper.tex_h = upper.h1 + upper.img->height();
			else
				upper.tex_h = upper.h2;

			upper.tex_h += sd->y_offset;
		}

		if (back->floorh > front->floorh && ! self_ref)
		{
			lower.kind = DrawSurf::K_TEXTURE;
			lower.h1 = front->floorh;
			lower.h2 = back->floorh;
			lower.y_clip = DrawSurf::SOLID_BELOW;

			lower.FindTex(sd->LowerTex(), ld);

			// note "sky_upper" here, needed to match original DOOM behavior
			if (ld->flags & MLF_LowerUnpegged)
				lower.tex_h = sky_upper ? back->ceilh : front->ceilh;
			else
				lower.tex_h = lower.h2;

			lower.tex_h += sd->y_offset;
		}

		/* Mid-Masked texture */

		if (! view.texturing)
			return;

		if (is_null_tex(sd->MidTex()))
			return;

		rail.FindTex(sd->MidTex(), ld);
		if (! rail.img)
			return;

		int c_h = MIN(front->ceilh,  back->ceilh);
		int f_h = MAX(front->floorh, back->floorh);
		int r_h = rail.img->height();

		if (f_h >= c_h)
			return;

		if (ld->flags & MLF_LowerUnpegged)
		{
			rail.h1 = f_h + sd->y_offset;
			rail.h2 = rail.h1 + r_h;
		}
		else
		{
			rail.h2 = c_h + sd->y_offset;
			rail.h1 = rail.h2 - r_h;
		}

		rail.kind = DrawSurf::K_TEXTURE;
		rail.y_clip = 0;
		rail.tex_h = rail.h2;

		// clip railing, unless sectors on both sides are identical or
		// we have a sky upper

		if (! (sky_upper ||
				(back->ceilh == front->ceilh &&
				 back->ceil_tex == front->ceil_tex &&
				 back->light == front->light)))
		{
			rail.h2 = MIN(c_h, rail.h2);
		}

		if (! (back->floorh == front->floorh &&
			   back->floor_tex == front->floor_tex &&
			   back->light == front->light))
		{
			rail.h1 = MAX(f_h, rail.h1);
		}
	}
};


struct RendInfo
{
public:
	// complete set of walls/sprites to draw.
	DrawWall::vec_t walls;

	// the active list.  Pointers here are always duplicates of ones in
	// the walls list (no need to 'delete' any of them).
	DrawWall::vec_t active;

	// query state
	int query_mode;  // 0 for normal render
	int query_sx;
	int query_sy;

	DrawWall     *query_wall;  // the hit wall
	obj3d_type_e  query_part;  // the part of the hit wall

	// inverse distances over X range, 0 when empty.
	std::vector<double> depth_x;

	int open_y1;
	int open_y2;

	// these used by HighlightGeometry()
	int hl_ox, hl_oy;

private:
	static void DeleteWall(DrawWall *P)
	{
		delete P;
	}

public:
	RendInfo() :
		walls(), active(),
		query_mode(0), query_sx(), query_sy(),
		depth_x(), open_y1(), open_y2()
	{ }

	~RendInfo()
	{
		std::for_each(walls.begin(), walls.end(), DeleteWall);

		walls.clear ();
		active.clear ();
	}

	void InitDepthBuf (int width)
	{
		depth_x.resize(width);

		std::fill_n(depth_x.begin(), width, 0);
	}

	void AddRenderLine(int sx1, int sy1, int sx2, int sy2, short thick, Fl_Color color)
	{
		if (! render_high_detail)
		{
			sx1 *= 2;  sy1 *= 2;
			sx2 *= 2;  sy2 *= 2;
		}

		fl_color(color);

		if (thick)
			fl_line_style(FL_SOLID, 2);

		fl_line(hl_ox + sx1, hl_oy + sy1, hl_ox + sx2, hl_oy + sy2);

		if (thick)
			fl_line_style(0);
	}

	void AddRenderLine(int sx1, int sy1, int sx2, int sy2, bool is_selected)
	{
		AddRenderLine(sx1, sy1, sx2, sy2,
				is_selected ? 2 : 0,
				is_selected ? FL_RED : HI_COL);
	}

	static inline float PointToAngle(float x, float y)
	{
		if (-0.01 < x && x < 0.01)
			return (y > 0) ? M_PI/2 : (3 * M_PI/2);

		float angle = atan2(y, x);

		if (angle < 0)
			angle += 2*M_PI;

		return angle;
	}

	static inline int AngleToX(float ang)
	{
		float t = tan(M_PI/2 - ang);

		int x = int(view.aspect_sw * t);

		x = (view.sw + x) / 2;

		if (x < 0)
			x = 0;
		else if (x > view.sw)
			x = view.sw;

		return x;
	}

	static inline float XToAngle(int x)
	{
		x = x * 2 - view.sw;

		float ang = M_PI/2 + atan(x / view.aspect_sw);

		if (ang < 0)
			ang = 0;
		else if (ang > M_PI)
			ang = M_PI;

		return ang;
	}

	static inline int DeltaToX(double iz, float tx)
	{
		int x = int(view.aspect_sw * tx * iz);

		x = (x + view.sw) / 2;

		return x;
	}

	static inline float XToDelta(int x, double iz)
	{
		x = x * 2 - view.sw;

		float tx = x / iz / view.aspect_sw;

		return tx;
	}

	static inline int DistToY(double iz, int sec_h)
	{
		if (sec_h > 32770)
			return -9999;

		if (sec_h < -32770)
			return +9999;

		int y = int(view.aspect_sh * (sec_h - view.z) * iz);

		return (view.sh - y) / 2;
	}

	static inline float YToDist(int y, int sec_h)
	{
		y = view.sh - y * 2;

		if (y == 0)
			return 999999;

		return view.aspect_sh * (sec_h - view.z) / y;
	}

	static inline float YToSecH(int y, double iz)
	{
		y = y * 2 - view.sh;

		return view.z - (float(y) / view.aspect_sh / iz);
	}

	void AddLine(int ld_index)
	{
		LineDef *ld = LineDefs[ld_index];

		if (! is_vertex(ld->start) || ! is_vertex(ld->end))
			return;

		if (! ld->Right())
			return;

		float x1 = ld->Start()->x - view.x;
		float y1 = ld->Start()->y - view.y;
		float x2 = ld->End()->x - view.x;
		float y2 = ld->End()->y - view.y;

		float tx1 = x1 * view.Sin - y1 * view.Cos;
		float ty1 = x1 * view.Cos + y1 * view.Sin;
		float tx2 = x2 * view.Sin - y2 * view.Cos;
		float ty2 = x2 * view.Cos + y2 * view.Sin;

		// reject line if complete behind viewplane
		if (ty1 <= 0 && ty2 <= 0)
			return;

		float angle1 = PointToAngle(tx1, ty1);
		float angle2 = PointToAngle(tx2, ty2);
		float span = angle1 - angle2;

		if (span < 0)
			span += 2*M_PI;

		int side = SIDE_RIGHT;

		if (span >= M_PI)
			side = SIDE_LEFT;

		// ignore the line when there is no facing sidedef
		SideDef *sd = (side == SIDE_LEFT) ? ld->Left() : ld->Right();

		if (! sd)
			return;

		if (side == SIDE_LEFT)
		{
			float tmp = angle1;
			angle1 = angle2;
			angle2 = tmp;
		}

		// clip angles to view volume

		float base_ang = angle1;

		float leftclip  = (3 * M_PI / 4);
		float rightclip = M_PI / 4;

		float tspan1 = angle1 - rightclip;
		float tspan2 = leftclip - angle2;

		if (tspan1 < 0) tspan1 += 2*M_PI;
		if (tspan2 < 0) tspan2 += 2*M_PI;

		if (tspan1 > M_PI/2)
		{
			// Totally off the left edge?
			if (tspan2 >= M_PI)
				return;

			angle1 = leftclip;
		}

		if (tspan2 > M_PI/2)
		{
			// Totally off the left edge?
			if (tspan1 >= M_PI)
				return;

			angle2 = rightclip;
		}

		// convert angles to on-screen X positions
		int sx1 = AngleToX(angle1);
		int sx2 = AngleToX(angle2) - 1;

		if (sx1 > sx2)
			return;

		// optimisation for query mode
		if (query_mode && (sx2 < query_sx || sx1 > query_sx))
			return;

		// compute distance from eye to wall
		float wdx = x2 - x1;
		float wdy = y2 - y1;

		float wlen = sqrt(wdx * wdx + wdy * wdy);
		float dist = fabs((y1 * wdx / wlen) - (x1 * wdy / wlen));

		if (dist < 0.01)
			return;

		// compute normal of wall (translated coords)
		float normal;

		if (side == SIDE_LEFT)
			normal = PointToAngle(ty2 - ty1, tx1 - tx2);
		else
			normal = PointToAngle(ty1 - ty2, tx2 - tx1);

		// compute inverse distances
		double iz1 = cos(normal - angle1) / dist / cos(M_PI/2 - angle1);
		double iz2 = cos(normal - angle2) / dist / cos(M_PI/2 - angle2);

		double diz = (iz2 - iz1) / MAX(1, sx2 - sx1);

		// create drawwall structure

		DrawWall *dw = new DrawWall;

		dw->th = -1;
		dw->ld = ld;
		dw->sd = sd;
		dw->sec = sd->SecRef();

		dw->side = side;

		dw->wall_light = dw->sec->light;

		if (ld->Start()->x == ld->End()->x)
			dw->wall_light += 16;
		else if (ld->Start()->y == ld->End()->y)
			dw->wall_light -= 16;

		dw->delta_ang = angle1 + XToAngle(sx1) - normal;

		dw->dist = dist;
		dw->normal = normal;
		dw->t_dist = tan(base_ang - normal) * dist;

		dw->iz1 = iz1;
		dw->iz2 = iz2;
		dw->diz = diz;
		dw->mid_iz = iz1 + (sx2 - sx1 + 1) * diz / 2;

		dw->sx1 = sx1;
		dw->sx2 = sx2;

		walls.push_back(dw);
	}

	void AddThing(int th_index)
	{
		Thing *th = Things[th_index];

		const thingtype_t *info = M_GetThingType(th->type);

		float x = th->x - view.x;
		float y = th->y - view.y;

		float tx = x * view.Sin - y * view.Cos;
		float ty = x * view.Cos + y * view.Sin;

		// reject sprite if complete behind viewplane
		if (ty < 4)
			return;

		bool is_unknown = false;

		float scale = info->scale;

		Img_c *sprite = W_GetSprite(th->type);
		if (! sprite)
		{
			sprite = IM_UnknownSprite();
			is_unknown = true;
		}

		float tx1 = tx - sprite->width() * scale / 2.0;
		float tx2 = tx + sprite->width() * scale / 2.0;

		double iz = 1 / ty;

		int sx1 = DeltaToX(iz, tx1);
		int sx2 = DeltaToX(iz, tx2) - 1;

		if (sx1 < 0)
			sx1 = 0;

		if (sx2 >= view.sw)
			sx2 = view.sw - 1;

		if (sx1 > sx2)
			return;

		int thsec = view.thing_sectors[th_index];

		int h1, h2;

		if (info && (info->flags & THINGDEF_CEIL))
		{
			// IOANCH 9/2015: also add z
			h2 = (is_sector(thsec) ? Sectors[thsec]->ceilh : 192) - th->z;
			h1 = h2 - sprite->height() * scale;
		}
		else
		{
			h1 = (is_sector(thsec) ? Sectors[thsec]->floorh : 0) + th->z;
			h2 = h1 + sprite->height() * scale;
		}

		// create drawwall structure

		DrawWall *dw = new DrawWall;

		dw->th  = th_index;
		dw->ld  = NULL;
		dw->sd  = NULL;
		dw->sec = NULL;

		dw->side = info ? info->flags : 0;

		if (is_unknown && render_unknown_bright)
			dw->side |= THINGDEF_LIT;
		else if (r_edit.hl.isThing() && th_index == r_edit.hl.num)
			dw->side |= THINGDEF_LIT;

		dw->spr_tx1 = tx1;

		dw->normal = scale;

		dw->iz1 = dw->mid_iz = iz;
		dw->diz = 0;

		dw->sx1 = sx1;
		dw->sx2 = sx2;

		dw->ceil.img = sprite;
		dw->ceil.h1  = h1;
		dw->ceil.h2  = h2;

		walls.push_back(dw);
	}

	void ComputeSurfaces()
	{
		DrawWall::vec_t::iterator S;

		for (S = walls.begin() ; S != walls.end() ; S++)
		{
			if ((*S)->ld)
				(*S)->ComputeWallSurface();
		}
	}

	void Highlight_WallPart(obj3d_type_e part, const DrawWall *dw,
							bool is_selected)
	{
		int h1, h2;

		if (! dw->ld->TwoSided())
		{
			h1 = dw->sd->SecRef()->floorh;
			h2 = dw->sd->SecRef()->ceilh;
		}
		else
		{
			const Sector *front = dw->ld->Right()->SecRef();
			const Sector *back  = dw->ld-> Left()->SecRef();

			if (part == OB3D_Lower)
			{
				h1 = MIN(front->floorh, back->floorh);
				h2 = MAX(front->floorh, back->floorh);
			}
			else  /* part == OB3D_Upper */
			{
				h1 = MIN(front->ceilh, back->ceilh);
				h2 = MAX(front->ceilh, back->ceilh);
			}
		}

		int x1 = dw->sx1 - 1;
		int x2 = dw->sx2 + 1;

		int ly1 = DistToY(dw->iz1, h2);
		int ly2 = DistToY(dw->iz1, h1);

		int ry1 = DistToY(dw->iz2, h2);
		int ry2 = DistToY(dw->iz2, h1);

		// workaround for crappy line clipping in X windows
		if (ly1 < -5000 || ly2 < -5000 || ly1 >  5000 || ly2 >  5000 ||
			ry1 < -5000 || ry2 < -5000 || ry1 >  5000 || ry2 >  5000)
			return;

		// keep the lines thin, makes aligning textures easier
		AddRenderLine(x1, ly1, x1, ly2, is_selected);
		AddRenderLine(x2, ry1, x2, ry2, is_selected);
		AddRenderLine(x1, ly1, x2, ry1, is_selected);
		AddRenderLine(x1, ly2, x2, ry2, is_selected);
	}

	void Highlight_Line(obj3d_type_e part, int ld, int side, bool is_selected)
	{
		const LineDef *L = LineDefs[ld];

		DrawWall::vec_t::iterator S;

		for (S = walls.begin() ; S != walls.end() ; S++)
		{
			const DrawWall *dw = (*S);

			if (dw->ld == L && dw->side == side)
				Highlight_WallPart(part, dw, is_selected);
		}
	}

	void Highlight_Sector(obj3d_type_e part, int sec_num, bool is_selected)
	{
		int sec_h;

		if (part == OB3D_Floor)
		{
			sec_h = Sectors[sec_num]->floorh;

			if (sec_h >= view.z)
				return;
		}
		else  /* OB3D_Ceil */
		{
			sec_h = Sectors[sec_num]->ceilh;

			if (sec_h <= view.z)
				return;
		}

		DrawWall::vec_t::iterator S;

		for (S = walls.begin() ; S != walls.end() ; S++)
		{
			const DrawWall *dw = (*S);

			if (dw->ld && dw->ld->TouchesSector(sec_num))
			{
				int sy1 = DistToY(dw->iz1, sec_h);
				int sy2 = DistToY(dw->iz2, sec_h);

				// workaround for crappy line clipping in X windows
				if (sy1 < -5000 || sy2 < -5000 ||
					sy1 >  5000 || sy2 >  5000)
					continue;

				AddRenderLine(dw->sx1, sy1, dw->sx2, sy2, is_selected);
			}
		}
	}

	void Highlight_Thing(int th, bool is_selected)
	{
		DrawWall::vec_t::iterator S;

		for (S = walls.begin() ; S != walls.end() ; S++)
		{
			const DrawWall *dw = (*S);

			if (! (dw->th >= 0 && dw->th == th))
				continue;

			int h1 = dw->ceil.h1 - 1;
			int h2 = dw->ceil.h2 + 1;

			int x1 = dw->sx1 - 1;
			int x2 = dw->sx2 + 1;

			int y1 = DistToY(dw->iz1, h2);
			int y2 = DistToY(dw->iz1, h1);

			AddRenderLine(x1, y1, x1, y2, is_selected);
			AddRenderLine(x2, y1, x2, y2, is_selected);
			AddRenderLine(x1, y1, x2, y1, is_selected);
			AddRenderLine(x1, y2, x2, y2, is_selected);
			break;
		}
	}

	inline void Highlight_Object(Obj3d_t& obj, bool is_selected)
	{
		if (obj.isThing())
		{
			Highlight_Thing(obj.num, is_selected);
		}
		else if (obj.isSector())
		{
			Highlight_Sector(obj.type, obj.num, is_selected);
		}
		else if (obj.isLine())
		{
			Highlight_Line(obj.type, obj.num, obj.side, is_selected);
		}
	}

	void HighlightGeometry(int ox, int oy)
	{
		hl_ox = ox;
		hl_oy = oy;

		/* do the selection */

		bool saw_hl = false;

		for (unsigned int k = 0 ; k < r_edit.sel.size() ; k++)
		{
			if (! r_edit.sel[k].valid())
				continue;

			if (r_edit.hl.valid() && r_edit.hl == r_edit.sel[k])
				saw_hl = true;

			Highlight_Object(r_edit.sel[k], true);
		}

		/* do the highlight */

		if (! saw_hl)
			Highlight_Object(r_edit.hl, false);
	}

	void ClipSolids()
	{
		// perform a rough depth sort of the walls and sprites.

		std::sort(walls.begin(), walls.end(), DrawWall::MidDistCmp());

		// go forwards, from closest to furthest away

		DrawWall::vec_t::iterator S;

		for (S = walls.begin() ; S != walls.end() ; S++)
		{
			DrawWall *dw = (*S);

			if (! dw)
				continue;

			int one_sided = dw->ld && ! dw->ld->Left();
			int vis_count = dw->sx2 - dw->sx1 + 1;

			for (int x = dw->sx1 ; x <= dw->sx2 ; x++)
			{
				double iz = dw->iz1 + (dw->diz * (x - dw->sx1));

				if (iz < depth_x[x])
					vis_count--;
				else if (one_sided)
					depth_x[x] = iz;
			}

			if (vis_count == 0)
			{
				delete dw;
				(*S) = NULL;
			}
		}

		// remove null pointers

		S = std::remove(walls.begin(), walls.end(), (DrawWall *) NULL);

		walls.erase(S, walls.end());
	}

	void RenderFlatColumn(DrawWall *dw, DrawSurf& surf,
			int x, int y1, int y2)
	{
		img_pixel_t *dest = view.screen;

		const img_pixel_t *src = surf.img->buf();

		int tw = surf.img->width();
		int th = surf.img->height();

		float ang = XToAngle(x);
		float modv = cos(ang - M_PI/2);

		float t_cos = cos(M_PI + -view.angle + ang) / modv;
		float t_sin = sin(M_PI + -view.angle + ang) / modv;

		dest += x + y1 * view.sw;

		int light = dw->sec->light;

		for ( ; y1 <= y2 ; y1++, dest += view.sw)
		{
			float dist = YToDist(y1, surf.tex_h);

			int tx = int( view.x - t_sin * dist) & (tw - 1);
			int ty = int(-view.y + t_cos * dist) & (th - 1);

			*dest = src[ty * tw + tx];

			if (view.lighting && ! surf.fullbright)
				*dest = view.DoomLightRemap(light, dist, *dest);
		}
	}

	void RenderTexColumn(DrawWall *dw, DrawSurf& surf,
			int x, int y1, int y2)
	{
		img_pixel_t *dest = view.screen;

		const img_pixel_t *src = surf.img->buf();

		int tw = surf.img->width();
		int th = surf.img->height();

		int  light = dw->wall_light;
		float dist = 1.0 / dw->cur_iz;

		/* compute texture X coord */

		float cur_ang = dw->delta_ang - XToAngle(x);

		int tx = int(dw->t_dist - tan(cur_ang) * dw->dist);

		tx = (dw->sd->x_offset + tx) & (tw - 1);

		/* compute texture Y coords */

		float hh = surf.tex_h - YToSecH(y1, dw->cur_iz);
		float dh = surf.tex_h - YToSecH(y2, dw->cur_iz);

		dh = (dh - hh) / MAX(1, y2 - y1);
		hh += 0.2;

		src  += tx;
		dest += x + y1 * view.sw;

		for ( ; y1 <= y2 ; y1++, hh += dh, dest += view.sw)
		{
			int ty = int(floor(hh)) % th;

			// handle negative values (use % twice)
			ty = (ty + th) % th;

			img_pixel_t pix = src[ty * tw];

			if (pix == TRANS_PIXEL)
				continue;

			if (view.lighting && ! surf.fullbright)
				*dest = view.DoomLightRemap(light, dist, pix);
			else
				*dest = pix;
		}
	}

	void SolidFlatColumn(DrawWall *dw, DrawSurf& surf, int x, int y1, int y2)
	{
		img_pixel_t *dest = view.screen;

		dest += x + y1 * view.sw;

		int light = dw->sec->light;

		for ( ; y1 <= y2 ; y1++, dest += view.sw)
		{
			float dist = YToDist(y1, surf.tex_h);

			if (view.lighting && ! surf.fullbright)
				*dest = view.DoomLightRemap(light, dist, game_info.floor_colors[1]);
			else
				*dest = surf.col;
		}
	}

	void SolidTexColumn(DrawWall *dw, DrawSurf& surf, int x, int y1, int y2)
	{
		int  light = dw->wall_light;
		float dist = 1.0 / dw->cur_iz;

		img_pixel_t *dest = view.screen;

		dest += x + y1 * view.sw;

		for ( ; y1 <= y2 ; y1++, dest += view.sw)
		{
			if (view.lighting && ! surf.fullbright)
				*dest = view.DoomLightRemap(light, dist, game_info.wall_colors[1]);
			else
				*dest = surf.col;
		}
	}

	inline void RenderWallSurface(DrawWall *dw, DrawSurf& surf, int x,
								  obj3d_type_e part)
	{
		if (surf.kind == DrawSurf::K_INVIS)
			return;

		int y1 = DistToY(dw->cur_iz, surf.h2);
		int y2 = DistToY(dw->cur_iz, surf.h1) - 1;

		if (y1 < open_y1)
			y1 = open_y1;

		if (y2 > open_y2)
			y2 = open_y2;

		if (y1 > y2)
			return;

		/* clip the open region */

		if (surf.y_clip & DrawSurf::SOLID_ABOVE)
			if (open_y1 < y2)
				open_y1 = y2;

		if (surf.y_clip & DrawSurf::SOLID_BELOW)
			if (open_y2 > y1)
				open_y2 = y1;

		/* query mode : is mouse over this wall part? */

		if (query_mode & 1)
		{
			if (y1 <= query_sy && query_sy <= y2)
			{
				query_wall = dw;
				query_part = part;
			}

			return;
		}

		/* fill pixels */

		if (! surf.img)
		{
			if (surf.kind == DrawSurf::K_FLAT)
				SolidFlatColumn(dw, surf, x, y1, y2);
			else
				SolidTexColumn(dw, surf, x, y1, y2);
		}
		else switch (surf.kind)
		{
			case DrawSurf::K_FLAT:
				RenderFlatColumn(dw, surf, x, y1, y2);
				break;

			case DrawSurf::K_TEXTURE:
				RenderTexColumn(dw, surf, x, y1, y2);
				break;
		}
	}

	inline void RenderSprite(DrawWall *dw, int x)
	{
		int y1 = DistToY(dw->cur_iz, dw->ceil.h2);
		int y2 = DistToY(dw->cur_iz, dw->ceil.h1) - 1;

		if (y1 < dw->oy1)
			y1 = dw->oy1;

		if (y2 > dw->oy2)
			y2 = dw->oy2;

		if (y1 > y2)
			return;

		/* fill pixels */

		img_pixel_t *dest = view.screen;

		const img_pixel_t *src = dw->ceil.img->buf();

		int tw = dw->ceil.img->width();
		int th = dw->ceil.img->height();

		float scale = dw->normal;

		int tx = int((XToDelta(x, dw->cur_iz) - dw->spr_tx1) / scale);

		if (tx < 0 || tx >= tw)
			return;

		float hh = dw->ceil.h2 - YToSecH(y1, dw->cur_iz);
		float dh = dw->ceil.h2 - YToSecH(y2, dw->cur_iz);

		dh = (dh - hh) / MAX(1, y2 - y1);

		src  += tx;
		dest += x + y1 * view.sw;

		int thsec = view.thing_sectors[dw->th];
		int light = is_sector(thsec) ? Sectors[thsec]->light : 255;
		float dist = 1.0 / dw->cur_iz;

		if (query_mode & 2)
		{
			if (y1 <= query_sy && query_sy <= y2)
			{
				query_wall = dw;
				query_part = OB3D_Thing;
			}

			return;
		}

		for ( ; y1 <= y2 ; y1++, hh += dh, dest += view.sw)
		{
			int ty = int(hh / scale);

			if (ty < 0 || ty >= th)
				continue;

			img_pixel_t pix = src[ty * tw];

			if (pix == TRANS_PIXEL)
				continue;

			if (dw->side & THINGDEF_INVIS)
			{
				if (*dest & IS_RGB_PIXEL)
					*dest = IS_RGB_PIXEL | ((*dest & 0x7bde) >> 1);
				else
					*dest = raw_colormap[14][*dest];
				continue;
			}

			*dest = pix;

			if (view.lighting && ! (dw->side & THINGDEF_LIT))
				*dest = view.DoomLightRemap(light, dist, *dest);
		}
	}

	inline void RenderMidMasker(DrawWall *dw, DrawSurf& surf, int x)
	{
		if (surf.kind == DrawSurf::K_INVIS)
			return;

		if (! surf.img)
			return;

		int y1 = DistToY(dw->cur_iz, surf.h2);
		int y2 = DistToY(dw->cur_iz, surf.h1) - 1;

		if (y1 < dw->oy1)
			y1 = dw->oy1;

		if (y2 > dw->oy2)
			y2 = dw->oy2;

		if (y1 > y2)
			return;

		/* fill pixels */

		RenderTexColumn(dw, surf, x, y1, y2);
	}

	void UpdateActiveList(int x)
	{
		DrawWall::vec_t::iterator S, E, P;

		bool changes = false;

		// remove walls that have finished.

		S = active.begin();
		E = active.end();

		S = std::remove_if (S, E, DrawWall::SX2Less(x));

		if (S != E)
		{
			active.erase(S, E);
			changes = true;
		}

		// add new walls that start in this column.

		S = walls.begin();
		E = walls.end();

		S = std::lower_bound(S, E, x, DrawWall::SX1Cmp());
		E = std::upper_bound(S, E, x, DrawWall::SX1Cmp());

		if (S != E)
			changes = true;

		for ( ; S != E ; S++)
		{
			active.push_back(*S);
		}

		// calculate new depth values

		S = active.begin();
		E = active.end();

		for (P=S ; (P != E) ; P++)
		{
			DrawWall *dw = (*P);

			dw->cur_iz = dw->iz1 + dw->diz * (x - dw->sx1);

			if (P != S && (*(P-1))->cur_iz < dw->cur_iz + IZ_EPSILON)
				changes = true;
		}

		// if there are changes, re-sort the active list...

		if (changes)
		{
			std::sort(active.begin(), active.end(), DrawWall::DistCmp());
		}
	}

	void RenderWalls()
	{
		// sort walls by their starting column, to allow binary search.

		std::sort(walls.begin(), walls.end(), DrawWall::SX1Cmp());

		active.clear();

		for (int x=0 ; x < view.sw ; x++)
		{
			// clear vertical depth buffer

			open_y1 = 0;
			open_y2 = view.sh - 1;

			UpdateActiveList(x);

			// in query mode, only care about a single column
			if (query_mode && x != query_sx)
				continue;

			// render, front to back

			DrawWall::vec_t::iterator S, E, P;

			S = active.begin();
			E = active.end();

			for (P=S ; P != E ; P++)
			{
				DrawWall *dw = (*P);

				// for things, just remember the open space
				{
					dw->oy1 = open_y1;
					dw->oy2 = open_y2;
				}
				if (dw->th >= 0)
					continue;

				RenderWallSurface(dw, dw->ceil,  x, OB3D_Ceil);
				RenderWallSurface(dw, dw->floor, x, OB3D_Floor);
				RenderWallSurface(dw, dw->upper, x, OB3D_Upper);
				RenderWallSurface(dw, dw->lower, x, OB3D_Lower);

				if (open_y1 >= open_y2)
					break;
			}

			// now render things, back to front
			// (mid-masked textures are done here too)

			if (P == E)
				P--;

			for ( ; P != (S-1) ; P--)
			{
				DrawWall *dw = (*P);

				if (dw->th >= 0)
					RenderSprite(dw, x);
				else
					RenderMidMasker(dw, dw->rail, x);
			}
		}
	}

	void DoRender3D()
	{
		view.ClearScreen();

		InitDepthBuf(view.sw);

		r_edit.SaveOffsets();

		for (int i=0 ; i < NumLineDefs ; i++)
			AddLine(i);

		if (view.sprites)
			for (int k=0 ; k < NumThings ; k++)
				AddThing(k);

		ClipSolids();

		ComputeSurfaces();

		RenderWalls();

		r_edit.RestoreOffsets();
	}

	void DoQuery(int qx, int qy)
	{
		query_mode = 3;

		query_sx   = qx;
		query_sy   = qy;

		query_wall = NULL;

		DoRender3D();

		query_mode = 0;
	}
};


static Thing *FindPlayer(int typenum)
{
	// need to search backwards (to handle Voodoo dolls properly)

	for (int i = NumThings-1 ; i >= 0 ; i--)
		if (Things[i]->type == typenum)
			return Things[i];

	return NULL;  // not found
}


//------------------------------------------------------------------------

static Thing *player;


UI_Render3D::UI_Render3D(int X, int Y, int W, int H) :
	Fl_Widget(X, Y, W, H)
{ }


UI_Render3D::~UI_Render3D()
{ }


void UI_Render3D::draw()
{
	int ox = x();
	int oy = y() + INFO_BAR_H;
	int ow = w();
	int oh = h() - INFO_BAR_H;

	view.PrepareToRender(ow, oh);

	RendInfo rend;

	rend.DoRender3D();

	if (render_high_detail)
		BlitHires(ox, oy, ow, oh);
	else
		BlitLores(ox, oy, ow, oh);

	fl_push_clip(ox, oy, ow, oh);

	rend.HighlightGeometry(ox, oy);

	fl_pop_clip();

	DrawInfoBar();
}


bool UI_Render3D::query(Obj3d_t& hl, int sx, int sy)
{
	hl.clear();

	if (! edit.pointer_in_window)
		return false;

	int ow = w();
	int oh = h();

	view.PrepareToRender(ow, oh);

	int qx = sx - x();
	int qy = sy - y() - INFO_BAR_H / 2;

	if (! render_high_detail)
	{
		qx = qx / 2;
		qy = qy / 2;
	}

	RendInfo rend;

	rend.DoQuery(qx, qy);

	if (! rend.query_wall)
	{
		// nothing was hit
		return false;
	}

	hl.type = rend.query_part;

	if (hl.type == OB3D_Thing)
	{
		hl.num = rend.query_wall->th;
	}
	else if (hl.type == OB3D_Floor || hl.type == OB3D_Ceil)
	{
		// ouch -- fix?
		for (int n = 0 ; n < NumSectors ; n++)
			if (rend.query_wall->sec == Sectors[n])
				hl.num = n;
	}
	else
	{
		hl.side = rend.query_wall->side;

		// ouch -- fix?
		for (int n = 0 ; n < NumLineDefs ; n++)
			if (rend.query_wall->ld == LineDefs[n])
				hl.num = n;
	}

	return hl.valid();
}


void UI_Render3D::BlitHires(int ox, int oy, int ow, int oh)
{
	for (int ry = 0 ; ry < view.sh ; ry++)
	{
		u8_t line_rgb[view.sw * 3];

		u8_t *dest = line_rgb;
		u8_t *dest_end = line_rgb + view.sw * 3;

		const img_pixel_t *src = view.screen + ry * view.sw;

		for ( ; dest < dest_end  ; dest += 3, src++)
		{
			IM_DecodePixel(*src, dest[0], dest[1], dest[2]);
		}

		fl_draw_image(line_rgb, ox, oy+ry, view.sw, 1);
	}
}


void UI_Render3D::BlitLores(int ox, int oy, int ow, int oh)
{
	for (int ry = 0 ; ry < view.sh ; ry++)
	{
		const img_pixel_t *src = view.screen + ry * view.sw;

		// if destination width is odd, we store an extra pixel here
		u8_t line_rgb[(ow + 1) * 3];

		u8_t *dest = line_rgb;
		u8_t *dest_end = line_rgb + ow * 3;

		for (; dest < dest_end ; dest += 6, src++)
		{
			IM_DecodePixel(*src, dest[0], dest[1], dest[2]);
			IM_DecodePixel(*src, dest[3], dest[4], dest[5]);
		}

		fl_draw_image(line_rgb, ox, oy + ry*2, ow, 1);

		if (ry * 2 + 1 < oh)
		{
			fl_draw_image(line_rgb, ox, oy + ry*2 + 1, ow, 1);
		}
	}
}


void UI_Render3D::DrawInfoBar()
{
	int cx = x();
	int cy = y();

	fl_push_clip(x(), cy, w(), INFO_BAR_H);

	if (r_edit.SelectEmpty())
		fl_color(FL_BLACK);
	else
		fl_color(fl_rgb_color(104,0,0));

	fl_rectf(x(), cy, w(), INFO_BAR_H);

	cx += 10;
	cy += 20;

	fl_font(FL_COURIER, 16);

	int ang = I_ROUND(view.angle * 180 / M_PI);
	if (ang < 0) ang += 360;

	IB_Number(cx, cy, "angle", ang, 3);
	cx += 8;

	IB_Number(cx, cy, "z", I_ROUND(view.z) - game_info.view_height, 4);

	IB_Number(cx, cy, "gamma", usegamma, 1);
	cx += 10;

	IB_Flag(cx, cy, view.gravity, "GRAVITY", "gravity");

	IB_Flag(cx, cy, true, "|", "|");

	IB_Highlight(cx, cy);

	fl_pop_clip();
}


void UI_Render3D::IB_Number(int& cx, int& cy, const char *label, int value, int size)
{
	char buffer[256];

	// negative size means we require a sign
	if (size < 0)
		sprintf(buffer, "%s:%-+*d ", label, -size + 1, value);
	else
		sprintf(buffer, "%s:%-*d ", label, size, value);

	fl_color(INFO_TEXT_COL);

	fl_draw(buffer, cx, cy);

	cx = cx + fl_width(buffer);
}

void UI_Render3D::IB_Flag(int& cx, int& cy, bool value, const char *label_on, const char *label_off)
{
	const char *label = value ? label_on : label_off;

	fl_color(value ? INFO_TEXT_COL : INFO_DIM_COL);

	fl_draw(label, cx, cy);

	cx = cx + fl_width(label) + 20;
}


static int GrabTextureFromObject(const Obj3d_t& obj);

void UI_Render3D::IB_Highlight(int& cx, int& cy)
{
	char buffer[256];

	if (! r_edit.hl.valid())
	{
		fl_color(INFO_DIM_COL);

		strcpy(buffer, "no highlight");
	}
	else
	{
		fl_color(INFO_TEXT_COL);

		if (r_edit.hl.isThing())
		{
			const Thing *th = Things[r_edit.hl.num];
			const thingtype_t *info = M_GetThingType(th->type);

			snprintf(buffer, sizeof(buffer), "thing #%d  %s",
					 r_edit.hl.num, info->desc);

		}
		else if (r_edit.hl.isSector())
		{
			int tex = GrabTextureFromObject(r_edit.hl);

			snprintf(buffer, sizeof(buffer), " sect #%d  %-8s",
					 r_edit.hl.num,
					 (tex < 0) ? "??????" : BA_GetString(tex));
		}
		else
		{
			int tex = GrabTextureFromObject(r_edit.hl);

			snprintf(buffer, sizeof(buffer), " line #%d  %-8s",
					 r_edit.hl.num,
					 (tex < 0) ? "??????" : BA_GetString(tex));
		}
	}

	fl_draw(buffer, cx, cy);

	cx = cx + fl_width(buffer);
}


int UI_Render3D::handle(int event)
{
	if (EV_HandleEvent(event))
		return 1;

	return Fl_Widget::handle(event);
}


void Render3D_Setup()
{
	if (! view.p_type)
	{
		view.p_type = THING_PLAYER1;
		view.px = 99999;
	}

	player = FindPlayer(view.p_type);

	if (! player)
	{
		if (view.p_type != THING_DEATHMATCH)
			view.p_type = THING_DEATHMATCH;

		player = FindPlayer(view.p_type);
	}

	if (player && (view.px != player->x || view.py != player->y))
	{
		// if player moved, re-create view parameters

		view.x = view.px = player->x;
		view.y = view.py = player->y;

		view.FindGroundZ();

		view.SetAngle(player->angle * M_PI / 180.0);
	}
	else
	{
		view.x = 0;
		view.y = 0;
		view.z = 64;

		view.SetAngle(0);
	}

	/* create image */

	view.sw = -1;
	view.sh = -1;

	view.texturing  = true;
	view.sprites    = true;
	view.lighting   = true;
}


void Render3D_Enable(bool _enable)
{
	Editor_ClearAction();
	Render3D_ClearSelection();

	edit.render3d = _enable;

	// give keyboard focus to the appropriate large widget
	if (edit.render3d)
	{
		Fl::focus(main_win->render);

		main_win->info_bar->SetMouse(view.x, view.y);
	}
	else
	{
		Fl::focus(main_win->canvas);

		main_win->canvas->PointerPos();
		main_win->info_bar->SetMouse(edit.map_x, edit.map_y);
	}

	RedrawMap();
}


void Render3D_RBScroll(int mode, int dx = 0, int dy = 0, keycode_t mod = 0)
{
	// started?
	if (mode < 0)
	{
		view.is_scrolling = true;
		main_win->SetCursor(FL_CURSOR_HAND);
		return;
	}

	// finished?
	if (mode > 0)
	{
		view.is_scrolling = false;
		main_win->SetCursor(FL_CURSOR_DEFAULT);
		return;
	}

	if (dx == 0 && dy == 0)
		return;

	// we separate the movement into either turning or moving up/down
	// (never both at the same time : CONFIG IT THOUGH).

	bool force_one_dir = true;

	if (force_one_dir)
	{
		if (abs(dx) >= abs(dy))
			dy = 0;
		else
			dx = 0;
	}

	bool is_strafe = (mod & MOD_ALT) ? true : false;

	float mod_factor = 1.0;
	if (mod & MOD_SHIFT)   mod_factor = 0.4;
	if (mod & MOD_COMMAND) mod_factor = 2.5;

	float speed = view.scroll_speed * mod_factor;

	if (is_strafe)
	{
		view.x += view.Sin * dx * mod_factor;
		view.y -= view.Cos * dx * mod_factor;
	}
	else  // turn camera
	{
		double d_ang = dx * speed * M_PI / 480.0;

		view.SetAngle(view.angle - d_ang);
	}

	dy = -dy;  //TODO CONFIG ITEM

	if (is_strafe)
	{
		view.x += view.Cos * dy * mod_factor;
		view.y += view.Sin * dy * mod_factor;
	}
	else if (! (render_lock_gravity && view.gravity))
	{
		view.z += dy * speed * 0.75;

		view.gravity = false;
	}

	main_win->info_bar->SetMouse(view.x, view.y);
	RedrawMap();
}


void Render3D_AdjustOffsets(int mode, int dx, int dy)
{
	// started?
	if (mode < 0)
	{
		r_edit.adjust_sides.clear();
		r_edit.adjust_lines.clear();

		r_edit.adjust_dx = 0;
		r_edit.adjust_dy = 0;

		// find the sidedefs to adjust
		if (! r_edit.SelectEmpty())
		{
			if (r_edit.sel_type < OB3D_Lower)
			{
				Beep("cannot adjust that");
				return;
			}

			for (unsigned int k = 0 ; k < r_edit.sel.size() ; k++)
			{
				const Obj3d_t& obj = r_edit.sel[k];

				if (obj.isLine())
					r_edit.AddAdjustSide(obj);
			}
		}
		else  // no selection, use the highlight
		{
			if (! r_edit.hl.valid())
			{
				Beep("nothing to adjust");
				return;
			}
			else if (! r_edit.hl.isLine())
			{
				Beep("cannot adjust that");
				return;
			}

			r_edit.AddAdjustSide(r_edit.hl);
		}

		if (r_edit.adjust_sides.empty())  // WTF?
			return;

		float dist = r_edit.AdjustDistFactor(view.x, view.y);
		dist = CLAMP(20, dist, 1000);

		r_edit.adjust_dx_factor = dist / view.aspect_sw;
		r_edit.adjust_dy_factor = dist / view.aspect_sh;

		Editor_SetAction(ACT_ADJUST_OFS);
		return;
	}


	if (edit.action != ACT_ADJUST_OFS)
		return;


	// finished?
	if (mode > 0)
	{
		// apply the offset deltas now
		dx = (int)r_edit.adjust_dx;
		dy = (int)r_edit.adjust_dy;

		if (dx || dy)
		{
			BA_Begin();

			for (unsigned int k = 0 ; k < r_edit.adjust_sides.size() ; k++)
			{
				int sd = r_edit.adjust_sides[k];

				const SideDef * SD = SideDefs[sd];

				BA_ChangeSD(sd, SideDef::F_X_OFFSET, SD->x_offset + dx);
				BA_ChangeSD(sd, SideDef::F_Y_OFFSET, SD->y_offset + dy);
			}

			BA_Message("adjusted offsets");
			BA_End();
		}

		r_edit.adjust_sides.clear();
		r_edit.adjust_lines.clear();

		Editor_ClearAction();
		return;
	}


	if (dx == 0 && dy == 0)
		return;


	bool force_one_dir = true;

	if (force_one_dir)
	{
		if (abs(dx) >= abs(dy))
			dy = 0;
		else
			dx = 0;
	}


	keycode_t mod = M_ReadLaxModifiers();

	float factor = (mod & MOD_SHIFT) ? 0.25 : 1.0;

	if (render_high_detail)
		factor = factor * 2.0;

	r_edit.adjust_dx -= dx * factor * r_edit.adjust_dx_factor;
	r_edit.adjust_dy -= dy * factor * r_edit.adjust_dy_factor;

	RedrawMap();
}


void Render3D_MouseMotion(int x, int y, keycode_t mod, int dx, int dy)
{
	if (view.is_scrolling)
	{
		Render3D_RBScroll(0, dx, dy, mod);
		return;
	}
	else if (edit.action == ACT_ADJUST_OFS)
	{
		Render3D_AdjustOffsets(0, dx, dy);
		return;
	}

	Obj3d_t old(r_edit.hl);

	main_win->render->query(r_edit.hl, x, y);

	if (old == r_edit.hl)
		return;

	main_win->render->redraw();
}


void Render3D_UpdateHighlight()
{
	// this is mainly to clear the highlight when mouse pointer
	// leaves the 3D viewport.

	if (r_edit.hl.valid() && ! edit.pointer_in_window)
	{
		r_edit.hl.clear();
		main_win->render->redraw();
	}
}


void Render3D_ClearNav()
{
	view.nav_fwd  = view.nav_back  = 0;
	view.nav_left = view.nav_right = 0;
	view.nav_up   = view.nav_down  = 0;

	view.nav_turn_L = view.nav_turn_R = 0;
}


void Render3D_Navigate()
{
	float delay_ms = Nav_TimeDiff();

	delay_ms = delay_ms / 1000.0;

	keycode_t mod = M_ReadLaxModifiers();

	float mod_factor = 1.0;
	if (mod & MOD_SHIFT)   mod_factor = 0.5;
	if (mod & MOD_COMMAND) mod_factor = 2.0;

	if (view.nav_fwd || view.nav_back || view.nav_right || view.nav_left)
	{
		float fwd   = view.nav_fwd   - view.nav_back;
		float right = view.nav_right - view.nav_left;

		float dx = view.Cos * fwd + view.Sin * right;
		float dy = view.Sin * fwd - view.Cos * right;

		dx = dx * mod_factor * mod_factor;
		dy = dy * mod_factor * mod_factor;

		view.x += dx * delay_ms;
		view.y += dy * delay_ms;
	}

	if (view.nav_up || view.nav_down)
	{
		float dz = (view.nav_up - view.nav_down);

		view.z += dz * mod_factor * delay_ms;
	}

	if (view.nav_turn_L || view.nav_turn_R)
	{
		float dang = (view.nav_turn_L - view.nav_turn_R);

		dang = dang * mod_factor * delay_ms;
		dang = CLAMP(-90, dang, 90);

		view.SetAngle(view.angle + dang);
	}

	main_win->info_bar->SetMouse(view.x, view.y);
	RedrawMap();
}


static int GrabTextureFromObject(const Obj3d_t& obj)
{
	if (obj.type == OB3D_Floor)
		return Sectors[obj.num]->floor_tex;

	if (obj.type == OB3D_Ceil)
		return Sectors[obj.num]->ceil_tex;

	if (! obj.isLine())
		return -1;

	const LineDef *LD = LineDefs[obj.num];

	if (LD->OneSided())
	{
		return LD->Right()->mid_tex;
	}

	const SideDef *SD = (obj.side == SIDE_RIGHT) ? LD->Right() : LD->Left();

	if (! SD)
		return -1;

	switch (obj.type)
	{
		case OB3D_Lower:
			return SD->lower_tex;

		case OB3D_Upper:
			return SD->upper_tex;

		case OB3D_Rail:
			return SD->mid_tex;

		default:
			return -1;
	}
}


//
// grab the texture or flat (as offset into string table) from the
// current 3D selection.  returns -1 if selection is empty, -2 if
// there multiple selected and some were different.
//
static int GrabTextureFrom3DSel()
{
	if (r_edit.SelectEmpty())
	{
		return GrabTextureFromObject(r_edit.hl);
	}

	int result = -1;

	for (unsigned int k = 0 ; k < r_edit.sel.size() ; k++)
	{
		const Obj3d_t& obj = r_edit.sel[k];

		if (! obj.valid())
			continue;

		int cur_tex = GrabTextureFromObject(obj);
		if (cur_tex < 0)
			continue;

		// more than one distinct texture?
		if (result >= 0 && result != cur_tex)
			return -2;

		result = cur_tex;
	}

	return result;
}


static void StoreTextureToObject(const Obj3d_t& obj, int new_tex)
{
	if (obj.type == OB3D_Floor)
	{
		BA_ChangeSEC(obj.num, Sector::F_FLOOR_TEX, new_tex);
		return;
	}
	else if (obj.type == OB3D_Ceil)
	{
		BA_ChangeSEC(obj.num, Sector::F_CEIL_TEX, new_tex);
		return;
	}

	if (! obj.isLine())
		return;

	const LineDef *LD = LineDefs[obj.num];

	int sd = LD->WhatSideDef(obj.side);

	if (sd < 0)
		return;

	if (LD->OneSided())
	{
		BA_ChangeSD(sd, SideDef::F_MID_TEX, new_tex);
		return;
	}

	switch (obj.type)
	{
		case OB3D_Lower:
			BA_ChangeSD(sd, SideDef::F_LOWER_TEX, new_tex);
			break;

		case OB3D_Upper:
			BA_ChangeSD(sd, SideDef::F_UPPER_TEX, new_tex);
			break;

		case OB3D_Rail:
			BA_ChangeSD(sd, SideDef::F_MID_TEX,   new_tex);
			break;

		// shut the compiler up
		default: break;
	}
}


static void StoreTextureTo3DSel(int new_tex)
{
	BA_Begin();

	if (r_edit.SelectEmpty())
	{
		StoreTextureToObject(r_edit.hl, new_tex);
	}
	else
	{
		for (unsigned int k = 0 ; k < r_edit.sel.size() ; k++)
		{
			const Obj3d_t& obj = r_edit.sel[k];

			if (! obj.valid())
				continue;

			StoreTextureToObject(obj, new_tex);
		}
	}

	BA_Message("pasted texture: %s", BA_GetString(new_tex));
	BA_End();
}


static void Render3D_Cut()
{
	// this is equivalent to setting the default texture

	obj3d_type_e type = r_edit.SelectEmpty() ? r_edit.hl.type : r_edit.sel_type;

	if (type == OB3D_Thing)
		return;

	const char *name = default_wall_tex;

	if (type == OB3D_Floor)
		name = default_floor_tex;
	else if (type == OB3D_Ceil)
		name = default_ceil_tex;

	StoreTextureTo3DSel(BA_InternaliseString(name));

	Status_Set("Cut texture to default");
}


static void Render3D_Copy()
{
	int new_tex = GrabTextureFrom3DSel();
	if (new_tex < 0)
	{
		Beep("multiple textures present");
		return;
	}

	r_edit.StoreClipboard(new_tex);

	Status_Set("Copied %s", BA_GetString(new_tex));
}


static void Render3D_Paste()
{
	int new_tex = r_edit.GrabClipboard();

	StoreTextureTo3DSel(new_tex);

	Status_Set("Pasted %s", BA_GetString(new_tex));
}


static void Render3D_Delete()
{
	obj3d_type_e type = r_edit.SelectEmpty() ? r_edit.hl.type : r_edit.sel_type;

	if (type == OB3D_Thing)
		return;

	if (type == OB3D_Floor || type == OB3D_Ceil)
	{
		StoreTextureTo3DSel(BA_InternaliseString(game_info.sky_flat));
		return;
	}

	StoreTextureTo3DSel(BA_InternaliseString("-"));

	Status_Set("Removed textures");
}


bool Render3D_ClipboardOp(char what)
{
	if (r_edit.SelectEmpty() && ! r_edit.hl.valid())
		return false;

	switch (what)
	{
		case 'c':
			Render3D_Copy();
			break;

		case 'v':
			Render3D_Paste();
			break;

		case 'x':
			Render3D_Cut();
			break;

		case 'd':
			Render3D_Delete();
			break;
	}

	return true;
}


void Render3D_ClearSelection()
{
	if (! r_edit.SelectEmpty())
		RedrawMap();

	r_edit.sel.clear();
}


void Render3D_SaveHighlight()
{
	r_edit.saved_hl = r_edit.hl;
}

void Render3D_RestoreHighlight()
{
	r_edit.hl = r_edit.saved_hl;
}


bool Render3D_BrowsedItem(char kind, int number, const char *name, int e_state)
{
	// do not check the highlight here, as mouse pointer will be
	// over an item in the browser.

	if (r_edit.SelectEmpty())
		return false;

	if (kind == 'O' && r_edit.sel_type == OB3D_Thing)
	{
		StoreTextureTo3DSel(number);
		return true;
	}
	else if (kind == 'F' && r_edit.sel_type <= OB3D_Floor)
	{
		int new_flat = BA_InternaliseString(name);
		StoreTextureTo3DSel(new_flat);
		return true;
	}
	else if (kind == 'T' && r_edit.sel_type >= OB3D_Lower)
	{
		int new_tex = BA_InternaliseString(name);
		StoreTextureTo3DSel(new_tex);
		return true;
	}

	// mismatched usage
	fl_beep();

	// we still eat it
	return true;
}


void Render3D_SetCameraPos(int new_x, int new_y)
{
	view.x = new_x;
	view.y = new_y;

	view.FindGroundZ();
}


void Render3D_GetCameraPos(int *x, int *y, float *angle)
{
	*x = view.x;
	*y = view.y;

	// convert angle from radians to degrees
	*angle = view.angle * 180.0 / M_PI;
}


bool Render3D_ParseUser(const char ** tokens, int num_tok)
{
	if (strcmp(tokens[0], "camera") == 0 && num_tok >= 5)
	{
		view.x = atof(tokens[1]);
		view.y = atof(tokens[2]);
		view.z = atof(tokens[3]);

		view.SetAngle(atof(tokens[4]));
		return true;
	}

	if (strcmp(tokens[0], "r_modes") == 0 && num_tok >= 4)
	{
		view.texturing = atoi(tokens[1]) ? true : false;
		view.sprites   = atoi(tokens[2]) ? true : false;
		view.lighting  = atoi(tokens[3]) ? true : false;

		return true;
	}

	if (strcmp(tokens[0], "r_gravity") == 0 && num_tok >= 2)
	{
		view.gravity = atoi(tokens[1]) ? true : false;
		return true;
	}

	if (strcmp(tokens[0], "low_detail") == 0 && num_tok >= 2)
	{
		// ignored for compatibility
		return true;
	}

	if (strcmp(tokens[0], "gamma") == 0 && num_tok >= 2)
	{
		usegamma = MAX(0, atoi(tokens[1])) % 5;

		W_UpdateGamma();
		return true;
	}

	if (r_clipboard.ParseUser(tokens, num_tok))
		return true;

	return false;
}


void Render3D_WriteUser(FILE *fp)
{
	fprintf(fp, "camera %1.2f %1.2f %1.2f %1.2f\n",
	        view.x, view.y, view.z, view.angle);

	fprintf(fp, "r_modes %d %d %d\n",
	        view.texturing  ? 1 : 0,
			view.sprites    ? 1 : 0,
			view.lighting   ? 1 : 0);

	fprintf(fp, "r_gravity %d\n",
	        view.gravity ? 1 : 0);

	fprintf(fp, "gamma %d\n",
	        usegamma);

	r_clipboard.WriteUser(fp);
}


//------------------------------------------------------------------------
//  COMMAND FUNCTIONS
//------------------------------------------------------------------------

void R3D_Click()
{
	if (! r_edit.hl.valid())
	{
		Beep("nothing there");
		return;
	}

	if (r_edit.hl.type == OB3D_Thing)
		return;

	r_edit.SelectToggle(r_edit.hl);

	// unselect any texture boxes in the panel
	main_win->UnselectPics();

	RedrawMap();
}


void R3D_Forward()
{
	float dist = atof(EXEC_Param[0]);

	view.x += view.Cos * dist;
	view.y += view.Sin * dist;

	main_win->info_bar->SetMouse(view.x, view.y);
	RedrawMap();
}

void R3D_Backward()
{
	float dist = atof(EXEC_Param[0]);

	view.x -= view.Cos * dist;
	view.y -= view.Sin * dist;

	main_win->info_bar->SetMouse(view.x, view.y);
	RedrawMap();
}

void R3D_Left()
{
	float dist = atof(EXEC_Param[0]);

	view.x -= view.Sin * dist;
	view.y += view.Cos * dist;

	main_win->info_bar->SetMouse(view.x, view.y);
	RedrawMap();
}

void R3D_Right()
{
	float dist = atof(EXEC_Param[0]);

	view.x += view.Sin * dist;
	view.y -= view.Cos * dist;

	main_win->info_bar->SetMouse(view.x, view.y);
	RedrawMap();
}

void R3D_Up()
{
	if (view.gravity && render_lock_gravity)
	{
		Beep("Gravity is on");
		return;
	}

	view.gravity = false;

	float dist = atof(EXEC_Param[0]);

	view.z += dist;

	RedrawMap();
}

void R3D_Down()
{
	if (view.gravity && render_lock_gravity)
	{
		Beep("Gravity is on");
		return;
	}

	view.gravity = false;

	float dist = atof(EXEC_Param[0]);

	view.z -= dist;

	RedrawMap();
}


void R3D_Turn()
{
	float angle = atof(EXEC_Param[0]);

	// convert to radians
	angle = angle * M_PI / 180.0;

	view.SetAngle(view.angle + angle);

	RedrawMap();
}


void R3D_DropToFloor()
{
	view.FindGroundZ();

	RedrawMap();
}


static void R3D_NAV_Forward_release()
{
	view.nav_fwd = 0;
}

void R3D_NAV_Forward()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	view.nav_fwd = atof(EXEC_Param[0]);

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_Forward_release);
}


static void R3D_NAV_Back_release(void)
{
	view.nav_back = 0;
}

void R3D_NAV_Back()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	view.nav_back = atof(EXEC_Param[0]);

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_Back_release);
}


static void R3D_NAV_Right_release(void)
{
	view.nav_right = 0;
}

void R3D_NAV_Right()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	view.nav_right = atof(EXEC_Param[0]);

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_Right_release);
}


static void R3D_NAV_Left_release(void)
{
	view.nav_left = 0;
}

void R3D_NAV_Left()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	view.nav_left = atof(EXEC_Param[0]);

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_Left_release);
}


static void R3D_NAV_Up_release(void)
{
	view.nav_up = 0;
}

void R3D_NAV_Up()
{
	if (! EXEC_CurKey)
		return;

	if (view.gravity && render_lock_gravity)
	{
		Beep("Gravity is on");
		return;
	}

	view.gravity = false;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	view.nav_up = atof(EXEC_Param[0]);

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_Up_release);
}


static void R3D_NAV_Down_release(void)
{
	view.nav_down = 0;
}

void R3D_NAV_Down()
{
	if (! EXEC_CurKey)
		return;

	if (view.gravity && render_lock_gravity)
	{
		Beep("Gravity is on");
		return;
	}

	view.gravity = false;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	view.nav_down = atof(EXEC_Param[0]);

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_Down_release);
}


static void R3D_NAV_TurnLeft_release(void)
{
	view.nav_turn_L = 0;
}

void R3D_NAV_TurnLeft()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	float turn = atof(EXEC_Param[0]);

	// convert to radians
	view.nav_turn_L = turn * M_PI / 180.0;

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_TurnLeft_release);
}


static void R3D_NAV_TurnRight_release(void)
{
	view.nav_turn_R = 0;
}

void R3D_NAV_TurnRight()
{
	if (! EXEC_CurKey)
		return;

	if (! edit.is_navigating)
		Render3D_ClearNav();

	float turn = atof(EXEC_Param[0]);

	// convert to radians
	view.nav_turn_R = turn * M_PI / 180.0;

	Nav_SetKey(EXEC_CurKey, &R3D_NAV_TurnRight_release);
}


static void R3D_NAV_MouseMove_release(void)
{
	Render3D_RBScroll(+1);
}

void R3D_NAV_MouseMove()
{
	if (! EXEC_CurKey)
		return;

	view.scroll_speed = atof(EXEC_Param[0]);

	if (! edit.is_navigating)
		Editor_ClearNav();

	if (Nav_SetKey(EXEC_CurKey, &R3D_NAV_MouseMove_release))
	{
		Render3D_RBScroll(-1);
	}
}



static void ACT_AdjustOfs_release(void)
{
	// check if cancelled or overridden
	if (edit.action != ACT_ADJUST_OFS)
		return;

	Render3D_AdjustOffsets(+1);
}

void R3D_ACT_AdjustOfs()
{
	if (! EXEC_CurKey)
		return;

	if (Nav_ActionKey(EXEC_CurKey, &ACT_AdjustOfs_release))
	{
		Render3D_AdjustOffsets(-1);
	}
}


void R3D_Set()
{
	const char *var_name = EXEC_Param[0];
	const char *value    = EXEC_Param[1];

	if (! var_name[0])
	{
		Beep("3D_Set: missing var name");
		return;
	}

	if (! value[0])
	{
		Beep("3D_Set: missing value");
		return;
	}

	 int  int_val = atoi(value);
	bool bool_val = (int_val > 0);


	if (y_stricmp(var_name, "tex") == 0)
	{
		view.texturing = bool_val;
	}
	else if (y_stricmp(var_name, "obj") == 0)
	{
		view.sprites = bool_val;
		view.thsec_invalidated = true;
	}
	else if (y_stricmp(var_name, "light") == 0)
	{
		view.lighting = bool_val;
	}
	else if (y_stricmp(var_name, "grav") == 0)
	{
		view.gravity = bool_val;
	}
	else
	{
		Beep("3D_Set: unknown var: %s", var_name);
		return;
	}

	RedrawMap();
}


void R3D_Toggle()
{
	const char *var_name = EXEC_Param[0];

	if (! var_name[0])
	{
		Beep("3D_Toggle: missing var name");
		return;
	}

	if (y_stricmp(var_name, "tex") == 0)
	{
		view.texturing = ! view.texturing;
	}
	else if (y_stricmp(var_name, "obj") == 0)
	{
		view.sprites = ! view.sprites;
		view.thsec_invalidated = true;
	}
	else if (y_stricmp(var_name, "light") == 0)
	{
		view.lighting = ! view.lighting;
	}
	else if (y_stricmp(var_name, "grav") == 0)
	{
		view.gravity = ! view.gravity;
	}
	else
	{
		Beep("3D_Toggle: unknown var: %s", var_name);
		return;
	}

	RedrawMap();
}


//
// Align texture on sidedef(s)
//
// Parameter:
//     x : align X offset
//     y : align Y offset
//    xy : align both X and Y
//
// Flags:
//    /clear : clear offset(s) instead of aligning
//    /right : align to line on the right of this one (instead of left)
//
void R3D_Align()
{
	if (! edit.render3d)
	{
		Beep("3D mode required");
		return;
	}

	// parse the flags
	bool do_X = Exec_HasFlag("/x");
	bool do_Y = Exec_HasFlag("/y");

	// TODO : this is for backwards compatibility, remove it later
	{
		const char *param = EXEC_Param[0];

		if (strchr(param, 'x')) do_X = true;
		if (strchr(param, 'y')) do_Y = true;
	}

	if (! (do_X || do_Y))
	{
		Beep("3D_Align: need x or y flag");
		return;
	}

	bool do_clear = Exec_HasFlag("/clear");
	bool do_right = Exec_HasFlag("/right");
	bool do_unpeg = true;

	int align_flags = 0;

	if (do_X) align_flags = align_flags | LINALIGN_X;
	if (do_Y) align_flags = align_flags | LINALIGN_Y;

	if (do_right) align_flags |= LINALIGN_Right;
	if (do_unpeg) align_flags |= LINALIGN_Unpeg;
	if (do_clear) align_flags |= LINALIGN_Clear;


	// if selection is empty, add the highlight to it
	// (and clear it when we are done).
	bool did_select = false;

	if (r_edit.SelectEmpty())
	{
		if (! r_edit.hl.valid())
		{
			Beep("nothing to align");
			return;
		}
		else if (! r_edit.hl.isLine())
		{
			Beep("cannot align that");
			return;
		}

		r_edit.SelectToggle(r_edit.hl);
		did_select = true;
	}
	else
	{
		if (r_edit.sel_type < OB3D_Lower)
		{
			Beep("cannot align that");
			return;
		}
	}


	BA_Begin();

	Line_AlignGroup(r_edit.sel, align_flags);

	if (do_clear)
		BA_Message("cleared offsets");
	else
		BA_Message("aligned offsets");

	BA_End();

	if (did_select)
		r_edit.sel.clear();

	RedrawMap();
}


void R3D_WHEEL_Move()
{
	float dx = Fl::event_dx();
	float dy = Fl::event_dy();

	dy = 0 - dy;

	float speed = atof(EXEC_Param[0]);

	if (Exec_HasFlag("/LAX"))
	{
		keycode_t mod = Fl::event_state() & MOD_ALL_MASK;

		if (mod == MOD_SHIFT)
			speed /= 4.0;
		else if (mod == MOD_COMMAND)
			speed *= 4.0;
	}

	view.x += speed * (view.Cos * dy + view.Sin * dx);
	view.y += speed * (view.Sin * dy - view.Cos * dx);

	main_win->info_bar->SetMouse(view.x, view.y);
	RedrawMap();
}


//------------------------------------------------------------------------


static editor_command_t  render_commands[] =
{
	{	"3D_Click", NULL,
		&R3D_Click
	},

	{	"3D_Set", NULL,
		&R3D_Set,
		/* flags */ NULL,
		/* keywords */ "tex obj light grav"
	},

	{	"3D_Toggle", NULL,
		&R3D_Toggle,
		/* flags */ NULL,
		/* keywords */ "tex obj light grav"
	},

	{	"3D_Align", NULL,
		&R3D_Align,
		/* flags */ "/x /y /right /clear"
	},

	{	"3D_Forward", NULL,
		&R3D_Forward
	},

	{	"3D_Backward", NULL,
		&R3D_Backward
	},

	{	"3D_Left", NULL,
		&R3D_Left
	},

	{	"3D_Right", NULL,
		&R3D_Right
	},

	{	"3D_Up", NULL,
		&R3D_Up
	},

	{	"3D_Down", NULL,
		&R3D_Down
	},

	{	"3D_Turn", NULL,
		&R3D_Turn
	},

	{	"3D_DropToFloor", NULL,
		&R3D_DropToFloor
	},

	{	"3D_ACT_AdjustOfs", NULL,
		&R3D_ACT_AdjustOfs
	},

	{	"3D_WHEEL_Move", NULL,
		&R3D_WHEEL_Move
	},

	{	"3D_NAV_Forward", NULL,
		&R3D_NAV_Forward
	},

	{	"3D_NAV_Back", NULL,
		&R3D_NAV_Back
	},

	{	"3D_NAV_Right", NULL,
		&R3D_NAV_Right
	},

	{	"3D_NAV_Left", NULL,
		&R3D_NAV_Left
	},

	{	"3D_NAV_Up", NULL,
		&R3D_NAV_Up
	},

	{	"3D_NAV_Down", NULL,
		&R3D_NAV_Down
	},

	{	"3D_NAV_TurnLeft", NULL,
		&R3D_NAV_TurnLeft
	},

	{	"3D_NAV_TurnRight", NULL,
		&R3D_NAV_TurnRight
	},

	{	"3D_NAV_MouseMove", NULL,
		&R3D_NAV_MouseMove
	},

	// end of command list
	{	NULL, NULL, 0, NULL  }
};


void Render3D_RegisterCommands()
{
	M_RegisterCommandList(render_commands);
}


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
