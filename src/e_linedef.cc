//------------------------------------------------------------------------
//  LINEDEFS
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2001-2019 Andrew Apted
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

#include "Errors.h"
#include "Instance.h"
#include "main.h"

#include "e_cutpaste.h"
#include "e_linedef.h"
#include "e_main.h"
#include "im_img.h"
#include "m_config.h"
#include "m_game.h"
#include "e_objects.h"
#include "w_rawdef.h"
#include "w_texture.h"


// config items
bool config::leave_offsets_alone = true;


//
// If linedef already exists
//
bool LinedefModule::linedefAlreadyExists(int v1, int v2) const
{
	for (int n = 0 ; n < doc.numLinedefs() ; n++)
	{
		const LineDef *L = doc.linedefs[n];

		if (L->start == v1 && L->end == v2) return true;
		if (L->start == v2 && L->end == v1) return true;
	}

	return false;
}


//------------------------------------------------------------------------


//
// Get pointer
//
inline const LineDef * LinedefModule::pointer(const Objid& obj) const
{
	return doc.linedefs[obj.num];
}

//
// Get sidedef pointer
//
inline const SideDef * LinedefModule::sidedefPointer(const Objid& obj) const
{
	Side where = (obj.parts & PART_LF_ALL) ? Side::left : Side::right;

	int sd = pointer(obj)->WhatSideDef(where);

	return (sd >= 0) ? doc.sidedefs[sd] : nullptr;
}


// disabled this partial texture comparison, as it can lead to
// unexpected results.  [ perhaps have an option for it? ]
#if 1
inline static int PartialTexEqual(const SString &a, const SString &b)
{
	return a.noCaseEqual(b);
}
#else
static int PartialTexCmp(const char *A, const char *B)
{
	// only compare the first 6 characters

	char A2[64];
	char B2[64];

	strcpy(A2, A);
	strcpy(B2, B);

	A2[6] = B2[6] = 0;

	return y_stricmp(A2, B2);
}
#endif


bool LinedefModule::partIsVisible(const Objid& obj, char part) const
{
	const LineDef *L  = pointer(obj);
	const SideDef *SD = sidedefPointer(obj);

	if (! L->TwoSided())
		return (part == 'l');

	const Sector *front = L->Right(doc)->SecRef(doc);
	const Sector *back  = L->Left (doc)->SecRef(doc);

	if (obj.parts & PART_LF_ALL)
		std::swap(front, back);

	// ignore sky walls
	if (part == 'u' && is_sky(front->CeilTex()) && is_sky(back->CeilTex()))
		return false;

	if (part == 'l')
	{
		if (is_null_tex(SD->LowerTex()))
			return false;

		return back->floorh > front->floorh;
	}
	else
	{
		if (is_null_tex(SD->UpperTex()))
			return false;

		return back->ceilh < front->ceilh;
	}
}


//
// calculate vertical range that the given surface occupies.
// when part is zero, we use obj.type instead.
//
void LinedefModule::partCalcExtent(const Objid& obj, char part, int *z1, int *z2) const
{
	const LineDef *L  = pointer(obj);
	const SideDef *SD = sidedefPointer(obj);

	if (! L->TwoSided())
	{
		if (SD)
		{
			*z1 = SD->SecRef(doc)->floorh;
			*z2 = SD->SecRef(doc)->ceilh;
		}
		else
		{
			*z1 = *z2 = 0;
		}

		return;
	}

	if (! part)
	{
		if (obj.parts & (PART_RT_UPPER | PART_LF_UPPER))
			part = 'u';
		else if (obj.parts & (PART_RT_RAIL | PART_LF_RAIL))
			part = 'r';
		else
			part = 'l';
	}

	const Sector *front = L->Right(doc)->SecRef(doc);
	const Sector *back  = L->Left (doc)->SecRef(doc);

	if (obj.parts & PART_LF_ALL)
		std::swap(front, back);

	if (part == 'r')
	{
		*z1 = MAX(front->floorh, back->floorh);
		*z2 = MIN(front->ceilh,  back->ceilh);
	}
	else if (part == 'u')
	{
		*z2 = front->ceilh;
		*z1 = MIN(*z2, back->ceilh);
	}
	else  // part == 'l'
	{
		*z1 = front->floorh;
		*z2 = MAX(*z1, back->floorh);
	}
}

//
// Score texture match
//
int LinedefModule::scoreTextureMatch(const Objid& adj, const Objid& cur) const
{
	// result is in the range 1..999

	const LineDef *L  = pointer(cur);
	const SideDef *LS = sidedefPointer(cur);

	const LineDef *N  = pointer(adj);
	const SideDef *NS = sidedefPointer(adj);

	int adj_z1, adj_z2;
	int cur_z1, cur_z2;

	partCalcExtent(adj, 0, &adj_z1, &adj_z2);
	partCalcExtent(cur, 0, &cur_z1, &cur_z2);

	// adjacent surface is not visible?
	if (adj_z2 <= adj_z1)
		return 1;

	// no overlap?
	int overlap = MIN(adj_z2, cur_z2) - MAX(adj_z1, cur_z1);

	if (overlap <= 0)
		return 2;


	SString adj_tex = NS->MidTex();

	if (N->TwoSided())
	{
		if (adj.parts & (PART_RT_LOWER | PART_LF_LOWER))
			adj_tex = NS->LowerTex();
		else if (adj.parts & (PART_RT_UPPER | PART_LF_UPPER))
			adj_tex = NS->UpperTex();
	}

	if (is_null_tex(adj_tex))
		return 3;

	SString cur_tex = LS->MidTex();

	if (L->TwoSided())
	{
		if (cur.parts & (PART_RT_LOWER | PART_LF_LOWER))
			cur_tex = LS->LowerTex();
		else if (cur.parts & (PART_RT_UPPER | PART_LF_UPPER))
			cur_tex = LS->UpperTex();
	}

	if (!PartialTexEqual(cur_tex, adj_tex))
		return 4;

	// return a score based on length of line shared between the
	// two surfaces

	overlap = overlap / 8;

	if (overlap > 900) overlap = 900;

	return 10 + overlap;
}


//
// Evaluate whether the given adjacent surface is a good (or even
// possible) candidate to align with.
//
// Having a matching texture is the primary component of the score.
// The secondary component is the angle between the lines (we prefer
// this angle to be close to 180 degrees).
//
// TODO : have a preference for the same sector ??
//
// Returns < 0 for surfaces that cannot be used.
//
int LinedefModule::scoreAdjoiner(const Objid& adj,
						 const Objid& cur, int align_flags) const
{
	bool do_right = (align_flags & LINALIGN_Right) ? true : false;

	const LineDef *L  = pointer(cur);
	const SideDef *LS = sidedefPointer(cur);

	const LineDef *N  = pointer(adj);
	const SideDef *NS = sidedefPointer(adj);

	// major fail by caller of Line_AlignOffsets()
	if (! LS)
		return -3;

	// no sidedef on adjoining line?
	if (! NS)
		return -2;

	// does the adjoiner sidedef actually mate up with the sidedef
	// we are aligning (and is on the wanted side) ?

	int v1 = (((cur.parts & PART_LF_ALL) ? 0:1) == (do_right ? 1:0)) ? L->end : L->start;
	int v2 = (((adj.parts & PART_LF_ALL) ? 0:1) == (do_right ? 1:0)) ? N->start : N->end;

	if (v1 != v2)
		return -1;

	/* Ok, we have a potential candidate */

	int v0 = (v1 == L->end) ? L->start : L->end;
	int v3 = (v2 == N->end) ? N->start : N->end;

	double ang = angleBetweenLines(v0, v1, v3);

	int score = scoreTextureMatch(adj, cur);

	score = score * 1000 + 500 - (int)fabs(ang - 180.0);

	return score;
}


//
// Determine the adjoining surface to align with.
//
// Returns a nil Objid for none.
//
void LinedefModule::determineAdjoiner(Objid& result,
							  const Objid& cur, int align_flags) const
{
	int best_score = 0;

	const LineDef *L = pointer(cur);

	for (int n = 0 ; n < doc.numLinedefs() ; n++)
	{
		const LineDef *N = doc.linedefs[n];

		if (N == L)
			continue;

		if (N->IsZeroLength(doc))
			continue;

		if (! (N->TouchesVertex(L->start) || N->TouchesVertex(L->end)))
			continue;

		for (int side = 0 ; side < 2 ; side++)
		for (int what = 0 ; what < 3 ; what++)
		{
			Objid adj;

			adj.type  = ObjType::linedefs;
			adj.num   = n;
			adj.parts = (what == 0) ? PART_RT_LOWER : (what == 1) ? PART_RT_UPPER : PART_RT_RAIL;

			if (side == 1)
				adj.parts = adj.parts << 4;

			int score = scoreAdjoiner(adj, cur, align_flags);

			if (score > best_score)
			{
				result     = adj;
				best_score = score;
			}
		}
	}
}


//
// Calculate reference H
//
int LinedefModule::calcReferenceH(const Objid& obj) const
{
	const LineDef *L  = pointer(obj);
	const SideDef *SD = sidedefPointer(obj);

	if (! L->TwoSided())
	{
		if (! SD)
			return 256;

		const Sector *front = SD->SecRef(doc);

		if (L->flags & MLF_LowerUnpegged)
			return front->floorh + W_GetTextureHeight(SD->MidTex());

		return front->ceilh;
	}


	const Sector *front = L->Right(doc)->SecRef(doc);
	const Sector *back  = L->Left (doc)->SecRef(doc);

	if (obj.parts & PART_LF_ALL)
		std::swap(front, back);

	SYS_ASSERT(front);
	SYS_ASSERT(back);

	if (obj.parts & (PART_RT_UPPER | PART_LF_UPPER))
	{
		if (! (L->flags & MLF_UpperUnpegged))
			return back->ceilh + W_GetTextureHeight(SD->UpperTex());

		return front->ceilh;
	}
	else
	{
		// TODO : verify if this correct for RAIL

		if (! (L->flags & MLF_LowerUnpegged))
			return back->floorh;

		return front->ceilh;
	}
}


void LinedefModule::doAlignX(const Objid& cur,
					 const Objid& adj, int align_flags) const
{
	const LineDef *cur_L  = pointer(cur);

	const LineDef *adj_L  = pointer(adj);
	const SideDef *adj_SD = sidedefPointer(adj);

	bool on_left = adj_L->TouchesVertex((cur.parts & PART_LF_ALL) ? cur_L->end : cur_L->start);

	int new_offset = adj_SD->x_offset;

	if (on_left)
	{
		new_offset += I_ROUND(adj_L->CalcLength(doc));

		if (new_offset > 0)
			new_offset &= 1023;
	}
	else
	{
		new_offset -= I_ROUND(cur_L->CalcLength(doc));

		if (new_offset < 0)
			new_offset = - (-new_offset & 1023);
	}

	Side where = (cur.parts & PART_LF_ALL) ? Side::left : Side::right;

	int sd = cur_L->WhatSideDef(where);

	doc.basis.changeSidedef(sd, SideDef::F_X_OFFSET, new_offset);
}


void LinedefModule::doAlignY(const Objid& cur, const Objid& adj, int align_flags) const
{
	const LineDef *L  = pointer(cur);
	const SideDef *SD = sidedefPointer(cur);

//	const LineDef *adj_L  = LD_ptr(adj);
	const SideDef *adj_SD = sidedefPointer(adj);

	bool lower_vis = partIsVisible(cur, 'l');
	bool upper_vis = partIsVisible(cur, 'u');

	bool lower_unpeg = (L->flags & MLF_LowerUnpegged) ? true : false;
	bool upper_unpeg = (L->flags & MLF_UpperUnpegged) ? true : false;


	// check for windows (set the unpeg flags)

	int new_flags = L->flags;

	if ((align_flags & LINALIGN_Unpeg) &&
		L->TwoSided() &&
		lower_vis && upper_vis &&
		(! lower_unpeg || ! upper_unpeg) &&
	    PartialTexEqual(SD->LowerTex(), SD->UpperTex()) &&
	    is_null_tex(SD->MidTex()) /* no rail */)
	{
		new_flags |= MLF_LowerUnpegged;
		new_flags |= MLF_UpperUnpegged;
	}

	// requirement: adj_tex_h + adj_y_off = cur_tex_h + cur_y_off

	int cur_texh = calcReferenceH(cur);
	int adj_texh = calcReferenceH(adj);

	int new_offset = adj_texh + adj_SD->y_offset - cur_texh;


	// normalize value  [TODO: handle BOOM non-power-of-two heights]

	if (new_offset < 0)
		new_offset = - (-new_offset & 255);
	else
		new_offset &= 255;


	Side where = (cur.parts & PART_LF_ALL) ? Side::left : Side::right;

	int sd = L->WhatSideDef(where);

	doc.basis.changeSidedef(sd, SideDef::F_Y_OFFSET, new_offset);

	if (new_flags != L->flags)
	{
		doc.basis.changeLinedef(cur.num, LineDef::F_FLAGS, new_flags);
	}
}

//
// Clear offsets
//
void LinedefModule::doClearOfs(const Objid& cur, int align_flags) const
{
	Side where = (cur.parts & PART_LF_ALL) ? Side::left : Side::right;

	int sd = pointer(cur)->WhatSideDef(where);

	if (sd < 0)  // should not happen
		return;

	if (align_flags & LINALIGN_X)
	{
		// when the /right flag is used, make the texture end at the right side
		// (whereas zero makes it begin at the left side)
		if (align_flags & LINALIGN_Right)
			doc.basis.changeSidedef(sd, SideDef::F_X_OFFSET, 0 - I_ROUND(pointer(cur)->CalcLength(doc)));
		else
			doc.basis.changeSidedef(sd, SideDef::F_X_OFFSET, 0);
	}

	if (align_flags & LINALIGN_Y)
		doc.basis.changeSidedef(sd, SideDef::F_Y_OFFSET, 0);
}

//
// Align the X and/or Y offets on the given surface.
//
// The given flags control which stuff to change, and where
// to look for the surface to align with.
//
bool LinedefModule::alignOffsets(const Objid& obj, int align_flags) const
{
	if (align_flags & LINALIGN_Clear)
	{
		doClearOfs(obj, align_flags);
		return true;
	}

	Objid adj;
	determineAdjoiner(adj, obj, align_flags);

	if (! adj.valid())
		return false;

	if (align_flags & LINALIGN_X)
		doAlignX(obj, adj, align_flags);

	if (align_flags & LINALIGN_Y)
		doAlignY(obj, adj, align_flags);

	return true;
}

// returns true if the surface at 'k' MUST be aligned before the
// surface at 'j'.
bool LinedefModule::alignCheckAdjacent(const std::vector<Objid> & group,
								int j, int k, bool do_right) const
{
	const Objid& ob_j = group[j];
	const Objid& ob_k = group[k];

	int vj = 0;

	if (((ob_j.parts & PART_LF_ALL) ? 0 : 1) == (do_right ? 1 : 0))
		vj = doc.linedefs[ob_j.num]->end;
	else
		vj = doc.linedefs[ob_j.num]->start;

	int vk = 0;

	if (((ob_k.parts & PART_LF_ALL) ? 0 : 1) == (do_right ? 1 : 0))
		vk = doc.linedefs[ob_k.num]->start;
	else
		vk = doc.linedefs[ob_k.num]->end;

	return (vj == vk);
}


//
// find an unvisited surface that has no possible dependency on
// any other unvisited surface.  In the case of loops, we pick
// an arbitrary surface.
//
int LinedefModule::alignPickNextSurface(const std::vector<Objid> & group,
								 const std::vector<byte>& seen, bool do_right) const
{
	int fallback = -1;

	for (int j = 0 ; j < (int)group.size() ; j++)
	{
		if (seen[j]) continue;
		if (! group[j].valid()) continue;

		if (fallback < 0)
			fallback = j;

		bool has_better = false;

		for (int k = 0 ; k < (int)group.size() ; k++)
		{
			if (k == j)  continue;
			if (seen[k]) continue;
			if (! group[k].valid()) continue;

			if (alignCheckAdjacent(group, j, k, do_right))
			{
				has_better = true;
				break;
			}
		}

		if (! has_better)
			return j;
	}

	// this will be -1 when there is no more surfaces left
	return fallback;
}

//
// Align group
//
void LinedefModule::alignGroup(const std::vector<Objid> & group, int align_flags) const
{
	// we will do each surface in the selection one-by-one,
	// and the order is significant when doing X offsets, so
	// mark them off via this array.
	std::vector<byte> seen;

	seen.resize(group.size());

	unsigned int k;

	for (k = 0 ; k < group.size() ; k++)
		seen[k] = 0;

	bool do_right = (align_flags & LINALIGN_Right) ? true : false;

	for (;;)
	{
		// get next unvisited surface
		int n = alignPickNextSurface(group, seen, do_right);

		if (n < 0)
			break;

		// mark it seen
		seen[n] = 1;

		alignOffsets(group[n], align_flags);
	}
}

void LinedefModule::commandAlign(Instance &inst)
{
	// parse the flags
	bool do_X = Exec_HasFlag("/x");
	bool do_Y = Exec_HasFlag("/y");

	if (! (do_X || do_Y))
	{
		inst.Beep("LIN_Align: need x or y flag");
		return;
	}

	bool do_clear = Exec_HasFlag("/clear");
	bool do_right = Exec_HasFlag("/right");
	bool do_unpeg = true;  // TODO a flag to disable

	int align_flags = 0;

	if (do_X) align_flags = align_flags | LINALIGN_X;
	if (do_Y) align_flags = align_flags | LINALIGN_Y;

	if (do_right) align_flags |= LINALIGN_Right;
	if (do_unpeg) align_flags |= LINALIGN_Unpeg;
	if (do_clear) align_flags |= LINALIGN_Clear;


	SelectHighlight unselect = SelectionOrHighlight();
	if (edit.mode != ObjType::linedefs || unselect == SelectHighlight::empty)
	{
		inst.Beep("no lines to align");
		return;
	}

	/* convert selection to group of surfaces */

	std::vector< Objid > group;

	for (sel_iter_c it(edit.Selected) ; !it.done() ; it.next())
	{
		int parts = edit.Selected->get_ext(*it);
		parts &= ~1;

		const LineDef *L = inst.level.linedefs[*it];

		// safety check
		if (L->left  < 0) parts &= ~PART_LF_ALL;
		if (L->right < 0) parts &= ~PART_RT_ALL;

		Objid obj(ObjType::linedefs, *it);

		// handle lines with individual parts selected
		if (parts != 0)
		{
			for (int bit = 0x02 ; bit <= 0x80 ; bit <<= 1)
			{
				if (parts & bit)
				{
					obj.parts = parts & bit;
					group.push_back(obj);
				}
			}
			continue;
		}

		// handle "simply selected" lines, determine parts to use
		for (int pass = 0 ; pass < 2 ; pass++)
		{
			Side where = pass ? Side::left : Side::right;

			if (L->WhatSideDef(where) < 0)
				continue;

			// decide whether to use upper or lower
			// WISH : this could be smarter....

			bool lower_vis = inst.level.linemod.partIsVisible(obj, 'l');
			bool upper_vis = inst.level.linemod.partIsVisible(obj, 'u');

			if (! (lower_vis || upper_vis))
				continue;

			obj.parts = lower_vis ? PART_RT_LOWER : PART_RT_UPPER;
			if (where == Side::left)
				obj.parts = obj.parts << 4;

			group.push_back(obj);
		}
	}

	if (group.empty())
	{
		inst.Beep("no visible surfaces");
		if (unselect == SelectHighlight::unselect)
			Selection_Clear(inst, true /* nosave */);
		return;
	}

	inst.level.basis.begin();

	inst.level.linemod.alignGroup(group, align_flags);

	if (do_clear)
		inst.level.basis.setMessage("cleared offsets");
	else
		inst.level.basis.setMessage("aligned offsets");

	inst.level.basis.end();

	if (unselect == SelectHighlight::unselect)
		Selection_Clear(inst, true /* nosave */);
}

//------------------------------------------------------------------------

//
// Flip vertices of linedef
//
void LinedefModule::flipLine_verts(int ld) const
{
	int old_start = doc.linedefs[ld]->start;
	int old_end   = doc.linedefs[ld]->end;

	doc.basis.changeLinedef(ld, LineDef::F_START, old_end);
	doc.basis.changeLinedef(ld, LineDef::F_END, old_start);
}

//
// Flip sides of linedef
//
void LinedefModule::flipLine_sides(int ld) const
{
	int old_right = doc.linedefs[ld]->right;
	int old_left  = doc.linedefs[ld]->left;

	doc.basis.changeLinedef(ld, LineDef::F_RIGHT, old_left);
	doc.basis.changeLinedef(ld, LineDef::F_LEFT, old_right);
}


//
// Flip linedef
//
void LinedefModule::flipLinedef(int ld) const
{
	flipLine_verts(ld);
	flipLine_sides(ld);
}

void LinedefModule::flipLinedef_safe(int ld) const
{
	// this avoids creating a linedef with only a left side (no right)

	flipLine_verts(ld);

	if (!doc.linedefs[ld]->OneSided())
		flipLine_sides(ld);
}

//
// Flip linedef group
//
void LinedefModule::flipLinedefGroup(const selection_c *flip) const
{
	for (sel_iter_c it(flip) ; !it.done() ; it.next())
	{
		flipLinedef(*it);
	}
}

//
// flip the orientation of some LineDefs
//
void LinedefModule::commandFlip(Instance &inst)
{
	SelectHighlight unselect = SelectionOrHighlight();
	if (unselect == SelectHighlight::empty)
	{
		inst.Beep("No lines to flip");
		return;
	}

	bool force_it = Exec_HasFlag("/force");

	inst.level.basis.begin();
	inst.level.basis.setMessageForSelection("flipped", *edit.Selected);

	for (sel_iter_c it(edit.Selected) ; !it.done() ; it.next())
	{
		if (force_it)
			inst.level.linemod.flipLinedef(*it);
		else
			inst.level.linemod.flipLinedef_safe(*it);
	}

	inst.level.basis.end();

	if (unselect == SelectHighlight::unselect)
		Selection_Clear(inst, true /* nosave */);
}

void LinedefModule::commandSwapSides(Instance &inst)
{
	SelectHighlight unselect = SelectionOrHighlight();
	if (unselect == SelectHighlight::empty)
	{
		inst.Beep("No lines to swap sides");
		return;
	}

	inst.level.basis.begin();
	inst.level.basis.setMessageForSelection("swapped sides on", *edit.Selected);

	for (sel_iter_c it(edit.Selected) ; !it.done() ; it.next())
	{
		inst.level.linemod.flipLine_sides(*it);
	}

	inst.level.basis.end();

	if (unselect == SelectHighlight::unselect)
		Selection_Clear(inst, true /* nosave */);
}

//
// Split linedef at vertex
//
int LinedefModule::splitLinedefAtVertex(int ld, int new_v) const
{
	LineDef * L = doc.linedefs[ld];
	Vertex  * V = doc.vertices[new_v];

	// create new linedef
	int new_l = doc.basis.addNew(ObjType::linedefs);

	LineDef * L2 = doc.linedefs[new_l];

	// it is OK to directly set fields of newly created objects
	*L2 = *L;

	L2->start = new_v;
	L2->end   = L->end;

	// update vertex on original line
	doc.basis.changeLinedef(ld, LineDef::F_END, new_v);

	// compute lengths (to update sidedef X offsets)
	int orig_length = I_ROUND(L->CalcLength(doc));
	int new_length  = I_ROUND(hypot(L->Start(doc)->x() - V->x(), L->Start(doc)->y() - V->y()));

	// update sidedefs

	if (L->Right(doc))
	{
		L2->right = doc.basis.addNew(ObjType::sidedefs);
		*L2->Right(doc) = *L->Right(doc);

		if (! config::leave_offsets_alone)
			L2->Right(doc)->x_offset += new_length;
	}

	if (L->Left(doc))
	{
		L2->left = doc.basis.addNew(ObjType::sidedefs);
		*L2->Left(doc) = *L->Left(doc);

		if (! config::leave_offsets_alone)
		{
			int new_x_ofs = L->Left(doc)->x_offset + orig_length - new_length;

			doc.basis.changeSidedef(L->left, SideDef::F_X_OFFSET, new_x_ofs);
		}
	}

	return new_l;
}

bool LinedefModule::doSplitLineDef(int ld) const
{
	LineDef * L = doc.linedefs[ld];

	// prevent creating tiny lines (especially zero-length)
	if (abs(L->Start(doc)->x() - L->End(doc)->x()) < 4 &&
		abs(L->Start(doc)->y() - L->End(doc)->y()) < 4)
		return false;

	double new_x = (L->Start(doc)->x() + L->End(doc)->x()) / 2;
	double new_y = (L->Start(doc)->y() + L->End(doc)->y()) / 2;

	int new_v = doc.basis.addNew(ObjType::vertices);

	Vertex * V = doc.vertices[new_v];

	V->SetRawXY(new_x, new_y);

	splitLinedefAtVertex(ld, new_v);

	return true;
}

//
// split one or more LineDefs in two, adding new Vertices in the middle
//
void LinedefModule::commandSplitHalf(Instance &inst)
{
	SelectHighlight unselect = SelectionOrHighlight();
	if (unselect == SelectHighlight::empty)
	{
		inst.Beep("No lines to split");
		return;
	}

	int new_first = inst.level.numLinedefs();
	int new_count = 0;

	inst.level.basis.begin();

	for (sel_iter_c it(edit.Selected) ; !it.done() ; it.next())
	{
		if (inst.level.linemod.doSplitLineDef(*it))
			new_count++;
	}

	inst.level.basis.setMessage("halved %d lines", new_count);
	inst.level.basis.end();

	// Hmmmmm -- should abort early if some lines are too short??
	if (new_count < edit.Selected->count_obj())
		inst.Beep("Some lines were too short!");

	if (unselect == SelectHighlight::unselect)
	{
		Selection_Clear(inst, true /* nosave */);
	}
	else if (new_count > 0)
	{
		edit.Selected->frob_range(new_first, new_first + new_count - 1, BitOp::add);
	}
}

//
// Add second sidedef
//
void LinedefModule::addSecondSidedef(int ld, int new_sd, int other_sd) const
{
	const LineDef * L  = doc.linedefs[ld];
	SideDef * SD = doc.sidedefs[new_sd];

	int new_flags = L->flags;

	new_flags |=  MLF_TwoSided;
	new_flags &= ~MLF_Blocking;

	doc.basis.changeLinedef(ld, LineDef::F_FLAGS, new_flags);

	// TODO: make this a global pseudo-constant
	int null_tex = BA_InternaliseString("-");

	const SideDef *other = doc.sidedefs[other_sd];

	if (! is_null_tex(other->MidTex()))
	{
		SD->lower_tex = other->mid_tex;
		SD->upper_tex = other->mid_tex;

		doc.basis.changeSidedef(other_sd, SideDef::F_LOWER_TEX, other->mid_tex);
		doc.basis.changeSidedef(other_sd, SideDef::F_UPPER_TEX, other->mid_tex);

		doc.basis.changeSidedef(other_sd, SideDef::F_MID_TEX, null_tex);
	}
	else
	{
		SD->lower_tex = other->lower_tex;
		SD->upper_tex = other->upper_tex;
	}
}

void LinedefModule::mergedSecondSidedef(int ld) const
{
	// similar to above, but with existing sidedefs

	LineDef * L = doc.linedefs[ld];

	SYS_ASSERT(L->TwoSided());

	int new_flags = L->flags;

	new_flags |=  MLF_TwoSided;
	new_flags &= ~MLF_Blocking;

	doc.basis.changeLinedef(ld, LineDef::F_FLAGS, new_flags);

	// TODO: make this a global pseudo-constant
	int null_tex = BA_InternaliseString("-");

	// determine textures for each side
	int  left_tex = 0;
	int right_tex = 0;

	if (! is_null_tex(L->Left(doc)->MidTex()))
		left_tex = L->Left(doc)->mid_tex;

	if (! is_null_tex(L->Right(doc)->MidTex()))
		right_tex = L->Right(doc)->mid_tex;

	if (! left_tex)  left_tex = right_tex;
	if (! right_tex) right_tex = left_tex;

	// use default texture if both sides are empty
	if (! left_tex)
	{
		left_tex = BA_InternaliseString(default_wall_tex);
		right_tex = left_tex;
	}

	doc.basis.changeSidedef(L->left,  SideDef::F_MID_TEX, null_tex);
	doc.basis.changeSidedef(L->right, SideDef::F_MID_TEX, null_tex);

	doc.basis.changeSidedef(L->left,  SideDef::F_LOWER_TEX, left_tex);
	doc.basis.changeSidedef(L->left,  SideDef::F_UPPER_TEX, left_tex);

	doc.basis.changeSidedef(L->right, SideDef::F_LOWER_TEX, right_tex);
	doc.basis.changeSidedef(L->right, SideDef::F_UPPER_TEX, right_tex);
}

//
// Remove sidedef
//
void LinedefModule::removeSidedef(int ld, Side ld_side) const
{
	const LineDef *L = doc.linedefs[ld];

	int gone_sd  = (ld_side == Side::right) ? L->right : L->left;
	int other_sd = (ld_side == Side::right) ? L->left : L->right;

	if (ld_side == Side::right)
		doc.basis.changeLinedef(ld, LineDef::F_RIGHT, -1);
	else
		doc.basis.changeLinedef(ld, LineDef::F_LEFT, -1);

	if (other_sd < 0)
		return;

	// The line is changing from TWO SIDED --> ONE SIDED.
	// Hence we need to:
	//    (1) clear the Two-Sided flag
	//    (2) set the Impassible flag
	//	  (3) flip the linedef if right side was removed
	//    (4) set the middle texture

	int new_flags = L->flags;

	new_flags &= ~MLF_TwoSided;
	new_flags |=  MLF_Blocking;

	doc.basis.changeLinedef(ld, LineDef::F_FLAGS, new_flags);

	// FIXME: if sidedef is shared, either don't modify it _OR_ duplicate it

	const SideDef *SD = doc.sidedefs[other_sd];

	int new_tex = BA_InternaliseString(default_wall_tex);

	// grab new texture from lower or upper if possible
	if (! is_null_tex(SD->LowerTex()))
		new_tex = SD->lower_tex;
	else if (! is_null_tex(SD->UpperTex()))
		new_tex = SD->upper_tex;
	else if (gone_sd >= 0)
	{
		SD = doc.sidedefs[gone_sd];

		if (! is_null_tex(SD->LowerTex()))
			new_tex = SD->lower_tex;
		else if (! is_null_tex(SD->UpperTex()))
			new_tex = SD->upper_tex;
	}

	doc.basis.changeSidedef(other_sd, SideDef::F_MID_TEX, new_tex);
}


void LinedefModule::commandMergeTwo(Instance &inst)
{
	if (edit.Selected->count_obj() == 1 && edit.highlight.valid())
	{
		Selection_Add(edit.highlight);
	}

	if (edit.Selected->count_obj() != 2)
	{
		inst.Beep("Need 2 linedefs to merge (got %d)", edit.Selected->count_obj());
		return;
	}

	// we will merge the second into the first

	int ld2 = edit.Selected->find_first();
	int ld1 = edit.Selected->find_second();

	const LineDef * L1 = inst.level.linedefs[ld1];
	const LineDef * L2 = inst.level.linedefs[ld2];

	if (! (L1->OneSided() && L2->OneSided()))
	{
		inst.Beep("Linedefs to merge must be single sided.");
		return;
	}

	Selection_Clear(inst, true);


	inst.level.basis.begin();

	// ld2 steals the sidedef from ld1

	inst.level.basis.changeLinedef(ld2, LineDef::F_LEFT, L1->right);
	inst.level.basis.changeLinedef(ld1, LineDef::F_RIGHT, -1);

	inst.level.linemod.mergedSecondSidedef(ld2);

	// fix existing lines connected to ld1 : reconnect to ld2

	for (int n = 0 ; n < inst.level.numLinedefs(); n++)
	{
		if (n == ld1 || n == ld2)
			continue;

		const LineDef * L = inst.level.linedefs[n];

		if (L->start == L1->start)
			inst.level.basis.changeLinedef(n, LineDef::F_START, L2->end);
		else if (L->start == L1->end)
			inst.level.basis.changeLinedef(n, LineDef::F_START, L2->start);

		if (L->end == L1->start)
			inst.level.basis.changeLinedef(n, LineDef::F_END, L2->end);
		else if (L->end == L1->end)
			inst.level.basis.changeLinedef(n, LineDef::F_END, L2->start);
	}

	// delete ld1 and any unused vertices

	selection_c del_line(ObjType::linedefs);

	del_line.set(ld1);

	DeleteObjects_WithUnused(inst.level, &del_line, false, false, false);

	inst.level.basis.setMessage("merged two linedefs");
	inst.level.basis.end();
}

//
// Move coordinate into linedef
//
void LinedefModule::moveCoordOntoLinedef(int ld, double *x, double *y) const
{
	const LineDef *L = doc.linedefs[ld];

	double x1 = L->Start(doc)->x();
	double y1 = L->Start(doc)->y();
	double x2 = L->End(doc)->x();
	double y2 = L->End(doc)->y();

	double dx = x2 - x1;
	double dy = y2 - y1;

	double len_squared = dx*dx + dy*dy;

	SYS_ASSERT(len_squared > 0);

	// compute along distance
	double along = (*x - x1) * dx + (*y - y1) * dy;

	// result = start + along * line unit vector
	*x = x1 + along * dx / len_squared;
	*y = y1 + along * dy / len_squared;
}

//
// Linedef start will be moved
//
bool LinedefModule::linedefStartWillBeMoved(int ld, selection_c &list) const
{
	for (sel_iter_c it(list) ; !it.done() ; it.next())
	{
		const LineDef *L = doc.linedefs[*it];

		if (*it != ld && L->end == doc.linedefs[ld]->start)
			return true;
	}

	return false;
}

//
// Linedef end will be moved
//
bool LinedefModule::linedefEndWillBeMoved(int ld, selection_c &list) const
{
	for (sel_iter_c it(list) ; !it.done() ; it.next())
	{
		const LineDef *L = doc.linedefs[*it];

		if (*it != ld && L->start == doc.linedefs[ld]->end)
			return true;
	}

	return false;
}

//
// Pick linedef to extend
//
int LinedefModule::pickLinedefToExtend(selection_c &list, bool moving_start) const
{
	// We want a line whose new length is not going to be wrecked
	// by a change to a later linedef.  However we must handle loops!

	for (sel_iter_c it(list) ; !it.done() ; it.next())
	{
		if (moving_start)
		{
			if (! linedefEndWillBeMoved(*it, list))
				return *it;
		}
		else
		{
			if (! linedefStartWillBeMoved(*it, list))
				return *it;
		}
	}

	return list.find_first();

}


//
// Set linedef length
//
void LinedefModule::linedefSetLength(int ld, int new_len, double angle) const
{
	// the 'new_len' parameter can be negative, which means move
	// the start vertex instead of the end vertex.

	const LineDef *L = doc.linedefs[ld];

	double dx = abs(new_len) * cos(angle);
	double dy = abs(new_len) * sin(angle);

	int idx = I_ROUND(dx);
	int idy = I_ROUND(dy);

	if (idx == 0 && idy == 0)
	{
		if (dx < 0) idx = (int)floor(dx); else idx = (int)ceil(dx);
		if (dy < 0) idy = (int)floor(dy); else idy = (int)ceil(dy);
	}

	if (idx == 0 && idy == 0)
		idx = 1;

	if (new_len < 0)
	{
		doc.basis.changeVertex(L->start, Vertex::F_X, L->End(doc)->raw_x - INT_TO_COORD(idx));
		doc.basis.changeVertex(L->start, Vertex::F_Y, L->End(doc)->raw_y - INT_TO_COORD(idy));
	}
	else
	{
		doc.basis.changeVertex(L->end, Vertex::F_X, L->Start(doc)->raw_x + INT_TO_COORD(idx));
		doc.basis.changeVertex(L->end, Vertex::F_Y, L->Start(doc)->raw_y + INT_TO_COORD(idy));
	}
}

//
// Set linedef length
//
void LinedefModule::setLinedefsLength(int new_len) const
{
	// this works on the current selection (caller must set it up)

	// use a copy of the selection
	selection_c list(ObjType::linedefs);
	ConvertSelection(doc, edit.Selected, &list);

	if (list.empty())
		return;

	// remember angles
	std::vector<double> angles(doc.numLinedefs());

	for (int n = 0 ; n < doc.numLinedefs() ; n++)
	{
		const LineDef *L = doc.linedefs[n];

		angles[n] = atan2(L->End(doc)->y() - L->Start(doc)->y(), L->End(doc)->x() - L->Start(doc)->x());
	}

	doc.basis.begin();
	doc.basis.setMessageForSelection("set length of", list);

	while (! list.empty())
	{
		int ld = pickLinedefToExtend(list, new_len < 0 /* moving_start */);

		list.clear(ld);

		linedefSetLength(ld, new_len, angles[ld]);
	}

	doc.basis.end();
}

//
// Fix for a lost side
//
void LinedefModule::fixForLostSide(int ld) const
{
	LineDef * L = doc.linedefs[ld];

	SYS_ASSERT(L->Right(doc));

	int tex;

	if (! is_null_tex(L->Right(doc)->LowerTex()))
		tex = L->Right(doc)->lower_tex;
	else if (! is_null_tex(L->Right(doc)->UpperTex()))
		tex = L->Right(doc)->upper_tex;
	else
		tex = BA_InternaliseString(default_wall_tex);

	doc.basis.changeSidedef(L->right, SideDef::F_MID_TEX, tex);
}

//
// Compute the angle between lines AB and BC, going anticlockwise.
// result is in degrees in the range [0, 360).
//
// A, B and C are VERTEX indices.
//
// -AJA- 2001-05-09
//
double LinedefModule::angleBetweenLines(int A, int B, int C) const
{
	double a_dx = doc.vertices[B]->x() - doc.vertices[A]->x();
	double a_dy = doc.vertices[B]->y() - doc.vertices[A]->y();

	double c_dx = doc.vertices[B]->x() - doc.vertices[C]->x();
	double c_dy = doc.vertices[B]->y() - doc.vertices[C]->y();

	double AB_angle = (a_dx == 0) ? (a_dy >= 0 ? 90 : -90) : atan2(a_dy, a_dx) * 180 / M_PI;
	double CB_angle = (c_dx == 0) ? (c_dy >= 0 ? 90 : -90) : atan2(c_dy, c_dx) * 180 / M_PI;

	double result = CB_angle - AB_angle;

	while (result >= 360.0)
		result -= 360.0;

	while (result < 0)
		result += 360.0;

#if 0  // DEBUGGING
	DebugPrintf("ANGLE %1.6f  (%d,%d) -> (%d,%d) -> (%d,%d)\n", result,
			Vertices[A].x, Vertices[A].y,
			Vertices[B].x, Vertices[B].y,
			Vertices[C].x, Vertices[C].y);
#endif

	return result;
}


SString LD_RatioName(fixcoord_t idx, fixcoord_t idy, bool number_only)
{
	idx = abs(idx);
	idy = abs(idy);

	if (idx == 0 && idy == 0)
	{
		if (number_only)
			return "0:0";
		else
			return "zero-len";
	}
	else if (idx == 0)
	{
		if (number_only)
			return "1:0";
		else
			return "vertical";
	}
	else if (idy == 0)
	{
		if (number_only)
			return "0:1";
		else
			return "horizontal";
	}

	// compute the greatest common divisor
	int a = idx;
	int b = idy;
	int gcd = 1;

	for (;;)
	{
		if (a > b)
			a -= b;
		else if (b > a)
			b -= a;
		else
		{
			gcd = a;
			break;
		}
	}

	idx /= gcd;
	idy /= gcd;

	// if ratio is really complex, it isn't worth showing
	if (idx > 999 || idy > 999)
		return SString("---");

	char buffer[256];
	snprintf(buffer, sizeof(buffer), "%s%d:%d", number_only ? "" : "ratio ", idx, idy);

	return SString(buffer);
}


bool LD_RailHeights(int& z1, int& z2, const LineDef *L, const SideDef *sd,
					const Sector *front, const Sector *back)
{
	SString rail_tex = sd->MidTex();
	if (is_null_tex(rail_tex))
		return false;

	z1 = MAX(front->floorh, back->floorh);
	z2 = MIN(front->ceilh,  back->ceilh);

	if (z2 <= z1)
		return false;

	int img_h = W_GetTextureHeight(rail_tex);

	if (L->flags & MLF_LowerUnpegged)
	{
		z1 = z1 + sd->y_offset;
		z2 = z1 + img_h;
	}
	else
	{
		z2 = z2 + sd->y_offset;
		z1 = z2 - img_h;
	}

	return true;
}


//  SideDef packing logic -- raw from glBSP
#if 0

static int SidedefCompare(const void *p1, const void *p2)
{
	int comp;

	int side1 = ((const u16_t *) p1)[0];
	int side2 = ((const u16_t *) p2)[0];

	sidedef_t *A = lev_sidedefs[side1];
	sidedef_t *B = lev_sidedefs[side2];

	if (side1 == side2)
		return 0;

	// don't merge sidedefs on special lines
	if (A->on_special || B->on_special)
		return side1 - side2;

	if (A->sector != B->sector)
	{
		if (A->sector == NULL) return -1;
		if (B->sector == NULL) return +1;

		return (A->sector->index - B->sector->index);
	}

	if ((int)A->x_offset != (int)B->x_offset)
		return A->x_offset - (int)B->x_offset;

	if ((int)A->y_offset != B->y_offset)
		return (int)A->y_offset - (int)B->y_offset;

	// compare textures

	comp = memcmp(A->upper_tex, B->upper_tex, sizeof(A->upper_tex));
	if (comp) return comp;

	comp = memcmp(A->lower_tex, B->lower_tex, sizeof(A->lower_tex));
	if (comp) return comp;

	comp = memcmp(A->mid_tex, B->mid_tex, sizeof(A->mid_tex));
	if (comp) return comp;

	// sidedefs must be the same
	return 0;
}

void DetectDuplicateSidedefs(void)
{
	int i;
	u16_t *array = (u16_t *)UtilCalloc(num_sidedefs * sizeof(u16_t));

	GB_DisplayTicker();

	// sort array of indices
	for (i=0; i < num_sidedefs; i++)
		array[i] = i;

	qsort(array, num_sidedefs, sizeof(u16_t), SidedefCompare);

	// now mark them off
	for (i=0; i < num_sidedefs - 1; i++)
	{
		// duplicate ?
		if (SidedefCompare(array + i, array + i+1) == 0)
		{
			sidedef_t *A = lev_sidedefs[array[i]];
			sidedef_t *B = lev_sidedefs[array[i+1]];

			// found a duplicate !
			B->equiv = A->equiv ? A->equiv : A;
		}
	}

	UtilFree(array);

	// update all linedefs
	for (i=0, new_num=0; i < num_linedefs; i++)
	{
		linedef_t *L = lev_linedefs[i];

		// handle duplicated sidedefs
		while (L->right && L->right->equiv)
		{
			L->right->ref_count--;
			L->right = L->right->equiv;
			L->right->ref_count++;
		}

		while (L->left && L->left->equiv)
		{
			L->left->ref_count--;
			L->left = L->left->equiv;
			L->left->ref_count++;
		}
	}
}
#endif


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
