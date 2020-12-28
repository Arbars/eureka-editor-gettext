//------------------------------------------------------------------------
//  VERTEX OPERATIONS
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

#include "Errors.h"
#include "Instance.h"
#include "main.h"

#include "e_cutpaste.h"
#include "e_hover.h"
#include "e_linedef.h"
#include "e_main.h"
#include "e_objects.h"
#include "e_vertex.h"
#include "m_bitvec.h"
#include "r_grid.h"
#include "m_game.h"
#include "e_objects.h"
#include "w_rawdef.h"

#include <algorithm>


int VertexModule::findExact(fixcoord_t fx, fixcoord_t fy) const
{
	for (int i = 0 ; i < doc.numVertices() ; i++)
	{
		if (doc.vertices[i]->Matches(fx, fy))
			return i;
	}

	return -1;  // not found
}


int VertexModule::findDragOther(int v_num) const
{
	// we always return the START of a linedef if possible, but
	// if that doesn't occur then return the END of a linedef.

	int fallback = -1;

	for (int i = 0 ; i < doc.numLinedefs() ; i++)
	{
		const LineDef *L = doc.linedefs[i];

		if (L->end == v_num)
			return L->start;

		if (L->start == v_num && fallback < 0)
			fallback = L->end;
	}

	return fallback;
}


int VertexModule::howManyLinedefs(int v_num) const
{
	int count = 0;

	for (int n = 0 ; n < doc.numLinedefs() ; n++)
	{
		LineDef *L = doc.linedefs[n];

		if (L->start == v_num || L->end == v_num)
			count++;
	}

	return count;
}


//
// two linedefs are being sandwiched together.
// vertex 'v' is the shared vertex (the "hinge").
// to prevent an overlap, we merge ld1 into ld2.
//
void VertexModule::mergeSandwichLines(int ld1, int ld2, int v, selection_c& del_lines) const
{
	LineDef *L1 = doc.linedefs[ld1];
	LineDef *L2 = doc.linedefs[ld2];

	bool ld1_onesided = L1->OneSided();
	bool ld2_onesided = L2->OneSided();

	int new_mid_tex = (ld1_onesided) ? L1->Right(doc)->mid_tex :
					  (ld2_onesided) ? L2->Right(doc)->mid_tex : 0;

	// flip L1 so it would be parallel with L2 (after merging the other
	// endpoint) but going the opposite direction.
	if ((L2->end == v) == (L1->end == v))
	{
		doc.linemod.flipLinedef(ld1);
	}

	bool same_left  = (L2->WhatSector(Side::left, doc)  == L1->WhatSector(Side::left, doc));
	bool same_right = (L2->WhatSector(Side::right, doc) == L1->WhatSector(Side::right, doc));

	if (same_left && same_right)
	{
		// the merged line would have the same thing on both sides
		// (possibly VOID space), so the linedefs both "vanish".

		del_lines.set(ld1);
		del_lines.set(ld2);
		return;
	}

	if (same_left)
	{
		doc.basis.changeLinedef(ld2, LineDef::F_LEFT, L1->right);
	}
	else if (same_right)
	{
		doc.basis.changeLinedef(ld2, LineDef::F_RIGHT, L1->left);
	}
	else
	{
		// geometry was broken / unclosed sector(s)
	}

	del_lines.set(ld1);


	// fix orientation of remaining linedef if needed
	if (L2->Left(doc) && ! L2->Right(doc))
	{
		doc.linemod.flipLinedef(ld2);
	}

	if (L2->OneSided() && new_mid_tex > 0)
	{
		doc.basis.changeSidedef(L2->right, SideDef::F_MID_TEX, new_mid_tex);
	}

	// fix flags of remaining linedef
	int new_flags = L2->flags;

	if (L2->TwoSided())
	{
		new_flags |=  MLF_TwoSided;
		new_flags &= ~MLF_Blocking;
	}
	else
	{
		new_flags &= ~MLF_TwoSided;
		new_flags |=  MLF_Blocking;
	}

	doc.basis.changeLinedef(ld2, LineDef::F_FLAGS, new_flags);
}


//
// merge v1 into v2
//
void VertexModule::doMergeVertex(int v1, int v2, selection_c& del_lines) const
{
	SYS_ASSERT(v1 >= 0 && v2 >= 0);
	SYS_ASSERT(v1 != v2);

	// check if two linedefs would overlap after the merge
	// [ but ignore lines already marked for deletion ]

	for (int n = 0 ; n < doc.numLinedefs() ; n++)
	{
		const LineDef *L = doc.linedefs[n];

		if (! L->TouchesVertex(v1))
			continue;

		if (del_lines.get(n))
			continue;

		int v3 = (L->start == v1) ? L->end : L->start;

		int found = -1;

		for (int k = 0 ; k < doc.numLinedefs(); k++)
		{
			if (k == n)
				continue;

			const LineDef *K = doc.linedefs[k];

			if ((K->start == v3 && K->end == v2) ||
				(K->start == v2 && K->end == v3))
			{
				found = k;
				break;
			}
		}

		if (found >= 0 && ! del_lines.get(found))
		{
			mergeSandwichLines(n, found, v3, del_lines);
			break;
		}
	}

	// update all linedefs which use V1 to use V2 instead, and
	// delete any line that exists between the two vertices.

	for (int n = 0 ; n < doc.numLinedefs() ; n++)
	{
		const LineDef *L = doc.linedefs[n];

		// change *ALL* references, this is critical
		// [ to-be-deleted lines will get start == end, that is OK ]

		if (L->start == v1)
			doc.basis.changeLinedef(n, LineDef::F_START, v2);

		if (L->end == v1)
			doc.basis.changeLinedef(n, LineDef::F_END, v2);

		if (L->start == v2 && L->end == v2)
			del_lines.set(n);
	}
}


//
// the first vertex is kept, all the other vertices are deleted
// (after fixing the attached linedefs).
//
void VertexModule::mergeList(selection_c *verts) const
{
	if (verts->count_obj() < 2)
		return;

	int v = verts->find_first();

#if 0
	double new_x, new_y;
	Objs_CalcMiddle(verts, &new_x, &new_y);

	BA_ChangeVT(v, Vertex::F_X, MakeValidCoord(new_x));
	BA_ChangeVT(v, Vertex::F_Y, MakeValidCoord(new_y));
#endif

	verts->clear(v);

	selection_c del_lines(ObjType::linedefs);

	// this prevents unnecessary sandwich mergers
	ConvertSelection(doc, verts, &del_lines);

	for (sel_iter_c it(verts) ; !it.done() ; it.next())
	{
		doMergeVertex(*it, v, del_lines);
	}

	// all these vertices will be unused now, hence this call
	// shouldn't kill any other objects.
	doc.objects.del(verts);

	// we NEED to keep unused vertices here, otherwise we can merge
	// all vertices of an isolated sector and end up with NOTHING!
	DeleteObjects_WithUnused(doc, &del_lines, false /* keep_things */, true /* keep_verts */, false /* keep_lines */);

	verts->clear_all();
}


void VertexModule::commandMerge(Instance &inst)
{
	if (inst.edit.Selected->count_obj() == 1 && inst.edit.highlight.valid())
	{
		inst.Selection_Add(inst.edit.highlight);
	}

	if (inst.edit.Selected->count_obj() < 2)
	{
		inst.Beep("Need 2 or more vertices to merge");
		return;
	}

	inst.level.basis.begin();
	inst.level.basis.setMessageForSelection("merged", *inst.edit.Selected);

	inst.level.vertmod.mergeList(inst.edit.Selected);

	inst.level.basis.end();

	Selection_Clear(inst, true /* no_save */);
}


bool VertexModule::tryFixDangler(int v_num) const
{
	// see if this vertex is sitting on another one (or very close to it)
	int v_other  = -1;
	int max_dist = 2;

	for (int i = 0 ; i < doc.numVertices() ; i++)
	{
		if (i == v_num)
			continue;

		double dx = doc.vertices[v_num]->x() - doc.vertices[i]->x();
		double dy = doc.vertices[v_num]->y() - doc.vertices[i]->y();

		if (abs(dx) <= max_dist && abs(dy) <= max_dist &&
			!doc.linemod.linedefAlreadyExists(v_num, v_other))
		{
			v_other = i;
			break;
		}
	}


	// check for a dangling vertex
	if (howManyLinedefs(v_num) != 1)
	{
		if (v_other >= 0 && howManyLinedefs(v_other) == 1)
			std::swap(v_num, v_other);
		else
			return false;
	}


	if (v_other >= 0)
	{
		Selection_Clear(inst, true /* no_save */);

		// delete highest numbered one  [ so the other index remains valid ]
		if (v_num < v_other)
			std::swap(v_num, v_other);

#if 0 // DEBUG
		fprintf(stderr, "Vertex_TryFixDangler : merge vert %d onto %d\n", v_num, v_other);
#endif

		doc.basis.begin();
		doc.basis.setMessage("merged dangling vertex #%d\n", v_num);

		selection_c list(ObjType::vertices);

		list.set(v_other);	// first one is the one kept
		list.set(v_num);

		mergeList(&list);

		doc.basis.end();

		inst.edit.Selected->set(v_other);

		inst.Beep("Merged a dangling vertex");
		return true;
	}


#if 0
	// find the line joined to this vertex
	int joined_ld = -1;

	for (int n = 0 ; n < doc.numLinedefs(); n++)
	{
		if (LineDefs[n]->TouchesVertex(v_num))
		{
			joined_ld = n;
			break;
		}
	}

	SYS_ASSERT(joined_ld >= 0);
#endif


	// see if vertex is sitting on a line

	Objid line_obj = doc.hover.findSplitLineForDangler(v_num);

	if (! line_obj.valid())
		return false;

#if 0 // DEBUG
	fprintf(stderr, "Vertex_TryFixDangler : split linedef %d with vert %d\n", line_obj.num, v_num);
#endif

	doc.basis.begin();
	doc.basis.setMessage("split linedef #%d\n", line_obj.num);

	doc.linemod.splitLinedefAtVertex(line_obj.num, v_num);

	doc.basis.end();

	// no vertices were added or removed, hence can continue Insert_Vertex
	return false;
}


void VertexModule::calcDisconnectCoord(const LineDef *L, int v_num, double *x, double *y) const
{
	const Vertex * V = doc.vertices[v_num];

	double dx = L->End(doc)->x() - L->Start(doc)->x();
	double dy = L->End(doc)->y() - L->Start(doc)->y();

	if (L->end == v_num)
	{
		dx = -dx;
		dy = -dy;
	}

	if (abs(dx) < 4 && abs(dy) < 4)
	{
		dx = dx / 2;
		dy = dy / 2;
	}
	else if (abs(dx) < 16 && abs(dy) < 16)
	{
		dx = dx / 4;
		dy = dy / 4;
	}
	else if (abs(dx) >= abs(dy))
	{
		dy = dy * 8 / abs(dx);
		dx = (dx < 0) ? -8 : 8;
	}
	else
	{
		dx = dx * 8 / abs(dy);
		dy = (dy < 0) ? -8 : 8;
	}

	*x = V->x() + dx;
	*y = V->y() + dy;
}


void VertexModule::doDisconnectVertex(int v_num, int num_lines) const
{
	int which = 0;

	for (int n = 0 ; n < doc.numLinedefs() ; n++)
	{
		LineDef *L = doc.linedefs[n];

		if (L->start == v_num || L->end == v_num)
		{
			double new_x, new_y;
			calcDisconnectCoord(L, v_num, &new_x, &new_y);

			// the _LAST_ linedef keeps the current vertex, the rest
			// need a new one.
			if (which != num_lines-1)
			{
				int new_v = doc.basis.addNew(ObjType::vertices);

				doc.vertices[new_v]->SetRawXY(inst, new_x, new_y);

				if (L->start == v_num)
					doc.basis.changeLinedef(n, LineDef::F_START, new_v);
				else
					doc.basis.changeLinedef(n, LineDef::F_END, new_v);
			}
			else
			{
				doc.basis.changeVertex(v_num, Vertex::F_X, inst.MakeValidCoord(new_x));
				doc.basis.changeVertex(v_num, Vertex::F_Y, inst.MakeValidCoord(new_y));
			}

			which++;
		}
	}
}


void VertexModule::commandDisconnect(Instance &inst)
{
	if (inst.edit.Selected->empty())
	{
		if (inst.edit.highlight.is_nil())
		{
			inst.Beep("Nothing to disconnect");
			return;
		}

		inst.Selection_Add(inst.edit.highlight);
	}

	bool seen_one = false;

	inst.level.basis.begin();
	inst.level.basis.setMessageForSelection("disconnected", *inst.edit.Selected);

	for (sel_iter_c it(inst.edit.Selected) ; !it.done() ; it.next())
	{
		int v_num = *it;

		// nothing to do unless vertex has 2 or more linedefs
		int num_lines = inst.level.vertmod.howManyLinedefs(*it);

		if (num_lines < 2)
			continue;

		inst.level.vertmod.doDisconnectVertex(v_num, num_lines);

		seen_one = true;
	}

	if (! seen_one)
		inst.Beep("Nothing was disconnected");

	inst.level.basis.end();

	Selection_Clear(inst, true);
}


void VertexModule::doDisconnectLinedef(int ld, int which_vert, bool *seen_one) const
{
	const LineDef *L = doc.linedefs[ld];

	int v_num = which_vert ? L->end : L->start;

	// see if there are any linedefs NOT in the selection which are
	// connected to this vertex.

	bool touches_non_sel = false;

	for (int n = 0 ; n < doc.numLinedefs(); n++)
	{
		if (inst.edit.Selected->get(n))
			continue;

		LineDef *N = doc.linedefs[n];

		if (N->start == v_num || N->end == v_num)
		{
			touches_non_sel = true;
			break;
		}
	}

	if (! touches_non_sel)
		return;

	double new_x, new_y;
	calcDisconnectCoord(doc.linedefs[ld], v_num, &new_x, &new_y);

	int new_v = doc.basis.addNew(ObjType::vertices);

	doc.vertices[new_v]->SetRawXY(inst, new_x, new_y);

	// fix all linedefs in the selection to use this new vertex
	for (sel_iter_c it(inst.edit.Selected) ; !it.done() ; it.next())
	{
		LineDef *L2 = doc.linedefs[*it];

		if (L2->start == v_num)
			doc.basis.changeLinedef(*it, LineDef::F_START, new_v);

		if (L2->end == v_num)
			doc.basis.changeLinedef(*it, LineDef::F_END, new_v);
	}

	*seen_one = true;
}


void VertexModule::commandLineDisconnect(Instance &inst)
{
	// Note: the logic here is significantly different than the logic
	//       in VT_Disconnect, since we want to keep linedefs in the
	//       selection connected, and only disconnect from linedefs
	//       NOT in the selection.
	//
	// Hence need separate code for this.

	SelectHighlight unselect = inst.SelectionOrHighlight();
	if (unselect == SelectHighlight::empty)
	{
		inst.Beep("Nothing to disconnect");
		return;
	}

	bool seen_one = false;

	inst.level.basis.begin();
	inst.level.basis.setMessageForSelection("disconnected", *inst.edit.Selected);

	for (sel_iter_c it(inst.edit.Selected) ; !it.done() ; it.next())
	{
		inst.level.vertmod.doDisconnectLinedef(*it, 0, &seen_one);
		inst.level.vertmod.doDisconnectLinedef(*it, 1, &seen_one);
	}

	inst.level.basis.end();

	if (! seen_one)
		inst.Beep("Nothing was disconnected");

	if (unselect == SelectHighlight::unselect)
		Selection_Clear(inst, true /* no save */);
}


void VertexModule::verticesOfDetachableSectors(selection_c &verts) const
{
	SYS_ASSERT(doc.numVertices() > 0);

	bitvec_c  in_verts(doc.numVertices());
	bitvec_c out_verts(doc.numVertices());

	for (int n = 0 ; n < doc.numLinedefs() ; n++)
	{
		const LineDef * L = doc.linedefs[n];

		// only process lines which touch a selected sector
		bool  left_in = L->Left(doc)  && inst.edit.Selected->get(L->Left(doc)->sector);
		bool right_in = L->Right(doc) && inst.edit.Selected->get(L->Right(doc)->sector);

		if (! (left_in || right_in))
			continue;

		bool innie = false;
		bool outie = false;

		if (L->Right(doc))
		{
			if (right_in)
				innie = true;
			else
				outie = true;
		}

		if (L->Left(doc))
		{
			if (left_in)
				innie = true;
			else
				outie = true;
		}

		if (innie)
		{
			in_verts.set(L->start);
			in_verts.set(L->end);
		}

		if (outie)
		{
			out_verts.set(L->start);
			out_verts.set(L->end);
		}
	}

	for (int k = 0 ; k < doc.numVertices() ; k++)
	{
		if (in_verts.get(k) && out_verts.get(k))
			verts.set(k);
	}
}


void VertexModule::DETSEC_SeparateLine(int ld_num, int start2, int end2, Side in_side) const
{
	const LineDef * L1 = doc.linedefs[ld_num];

	int new_ld = doc.basis.addNew(ObjType::linedefs);
	int lost_sd;

	LineDef * L2 = doc.linedefs[new_ld];

	if (in_side == Side::left)
	{
		L2->start = end2;
		L2->end   = start2;
		L2->right = L1->left;

		lost_sd = L1->left;
	}
	else
	{
		L2->start = start2;
		L2->end   = end2;
		L2->right = L1->right;

		lost_sd = L1->right;

		doc.linemod.flipLinedef(ld_num);
	}

	doc.basis.changeLinedef(ld_num, LineDef::F_LEFT, -1);


	// determine new flags

	int new_flags = L1->flags;

	new_flags &= ~MLF_TwoSided;
	new_flags |=  MLF_Blocking;

	doc.basis.changeLinedef(ld_num, LineDef::F_FLAGS, new_flags);

	L2->flags = L1->flags;


	// fix the first line's textures

	int tex = BA_InternaliseString(inst.default_wall_tex);

	const SideDef * SD = doc.sidedefs[L1->right];

	if (! is_null_tex(SD->LowerTex()))
		tex = SD->lower_tex;
	else if (! is_null_tex(SD->UpperTex()))
		tex = SD->upper_tex;

	doc.basis.changeSidedef(L1->right, SideDef::F_MID_TEX, tex);


	// now fix the second line's textures

	SD = doc.sidedefs[lost_sd];

	if (! is_null_tex(SD->LowerTex()))
		tex = SD->lower_tex;
	else if (! is_null_tex(SD->UpperTex()))
		tex = SD->upper_tex;

	doc.basis.changeSidedef(lost_sd, SideDef::F_MID_TEX, tex);
}


void VertexModule::DETSEC_CalcMoveVector(selection_c * detach_verts, double * dx, double * dy) const
{
	double det_mid_x, sec_mid_x;
	double det_mid_y, sec_mid_y;

	doc.objects.calcMiddle(detach_verts,  &det_mid_x, &det_mid_y);
	doc.objects.calcMiddle(inst.edit.Selected, &sec_mid_x, &sec_mid_y);

	*dx = sec_mid_x - det_mid_x;
	*dy = sec_mid_y - det_mid_y;

	// avoid moving perfectly horizontal or vertical
	// (also handes the case of dx == dy == 0)

	if (abs(*dx) > abs(*dy))
	{
		*dx = (*dx < 0) ? -9 : +9;
		*dy = (*dy < 0) ? -5 : +5;
	}
	else
	{
		*dx = (*dx < 0) ? -5 : +5;
		*dy = (*dy < 0) ? -9 : +9;
	}

	if (abs(*dx) < 2) *dx = (*dx < 0) ? -2 : +2;
	if (abs(*dy) < 4) *dy = (*dy < 0) ? -4 : +4;

	double mul = 1.0 / CLAMP(0.25, grid.Scale, 1.0);

	*dx = (*dx) * mul;
	*dy = (*dy) * mul;
}


void VertexModule::commandSectorDisconnect(Instance &inst)
{
	if (inst.level.numVertices() == 0)
	{
		inst.Beep("No sectors to disconnect");
		return;
	}

	SelectHighlight unselect = inst.SelectionOrHighlight();
	if (unselect == SelectHighlight::empty)
	{
		inst.Beep("No sectors to disconnect");
		return;
	}

	int n;

	// collect all vertices which need to be detached
	selection_c detach_verts(ObjType::vertices);
	inst.level.vertmod.verticesOfDetachableSectors(detach_verts);

	if (detach_verts.empty())
	{
		inst.Beep("Already disconnected");
		if (unselect == SelectHighlight::unselect)
			Selection_Clear(inst, true /* nosave */);
		return;
	}


	// determine vector to move the detach coords
	double move_dx, move_dy;
	inst.level.vertmod.DETSEC_CalcMoveVector(&detach_verts, &move_dx, &move_dy);


	inst.level.basis.begin();
	inst.level.basis.setMessageForSelection("disconnected", *inst.edit.Selected);

	// create new vertices, and a mapping from old --> new

	int * mapping = new int[inst.level.numVertices()];

	for (n = 0 ; n < inst.level.numVertices(); n++)
		mapping[n] = -1;

	for (sel_iter_c it(detach_verts) ; !it.done() ; it.next())
	{
		int new_v = inst.level.basis.addNew(ObjType::vertices);

		mapping[*it] = new_v;

		Vertex *newbie = inst.level.vertices[new_v];

		*newbie = *inst.level.vertices[*it];
	}

	// update linedefs, creating new ones where necessary
	// (go backwards so we don't visit newly created lines)

	for (n = inst.level.numLinedefs() -1 ; n >= 0 ; n--)
	{
		const LineDef * L = inst.level.linedefs[n];

		// only process lines which touch a selected sector
		bool  left_in = L->Left(inst.level)  && inst.edit.Selected->get(L->Left(inst.level)->sector);
		bool right_in = L->Right(inst.level) && inst.edit.Selected->get(L->Right(inst.level)->sector);

		if (! (left_in || right_in))
			continue;

		bool between_two = (left_in && right_in);

		int start2 = mapping[L->start];
		int end2   = mapping[L->end];

		if (start2 >= 0 && end2 >= 0 && L->TwoSided() && ! between_two)
		{
			inst.level.vertmod.DETSEC_SeparateLine(n, start2, end2, left_in ? Side::left : Side::right);
		}
		else
		{
			if (start2 >= 0)
				inst.level.basis.changeLinedef(n, LineDef::F_START, start2);

			if (end2 >= 0)
				inst.level.basis.changeLinedef(n, LineDef::F_END, end2);
		}
	}

	delete[] mapping;

	// finally move all vertices of selected sectors

	selection_c all_verts(ObjType::vertices);

	ConvertSelection(inst.level, inst.edit.Selected, &all_verts);

	for (sel_iter_c it(all_verts) ; !it.done() ; it.next())
	{
		const Vertex * V = inst.level.vertices[*it];

		inst.level.basis.changeVertex(*it, Vertex::F_X, V->raw_x + inst.MakeValidCoord(move_dx));
		inst.level.basis.changeVertex(*it, Vertex::F_Y, V->raw_y + inst.MakeValidCoord(move_dy));
	}

	inst.level.basis.end();

	if (unselect == SelectHighlight::unselect)
		Selection_Clear(inst, true /* nosave */);
}


//------------------------------------------------------------------------
//   RESHAPING STUFF
//------------------------------------------------------------------------


static double WeightForVertex(const Vertex *V, /* bbox: */ double x1, double y1, double x2, double y2,
							  double width, double height, int side)
{
	double dist;
	double extent;

	if (width >= height)
	{
		dist = (side < 0) ? (V->x() - x1) : (x2 - V->x());
		extent = width;
	}
	else
	{
		dist = (side < 0) ? (V->y() - y1) : (y2 - V->y());
		extent = height;
	}

	if (dist > extent * 0.66)
		return 0;

	if (dist > extent * 0.33)
		return 0.25;

	return 1.0;
}


struct vert_along_t
{
	int vert_num;

	double along;

public:
	vert_along_t(int num, double _along) : vert_num(num), along(_along)
	{ }

	struct CMP
	{
		inline bool operator() (const vert_along_t &A, const vert_along_t& B) const
		{
			return A.along < B.along;
		}
	};
};


void VertexModule::commandShapeLine(Instance &inst)
{
	if (inst.edit.Selected->count_obj() < 3)
	{
		inst.Beep("Need 3 or more vertices to shape");
		return;
	}

	// determine orientation and position of the line

	double x1, y1, x2, y2;
	inst.level.objects.calcBBox(inst.edit.Selected, &x1, &y1, &x2, &y2);

	double width  = x2 - x1;
	double height = y2 - y1;

	if (width < 4 && height < 4)
	{
		inst.Beep("Too small");
		return;
	}

	// The method here is where split the vertices into two groups and
	// use the center of each group to form the infinite line.  I have
	// modified that a bit, the vertices in a band near the middle all
	// participate in the sum but at 0.25 weighting.  -AJA-

	double ax = 0;
	double ay = 0;
	double a_total = 0;

	double bx = 0;
	double by = 0;
	double b_total = 0;

	for (sel_iter_c it(inst.edit.Selected) ; !it.done() ; it.next())
	{
		const Vertex *V = inst.level.vertices[*it];

		double weight = WeightForVertex(V, x1,y1, x2,y2, width,height, -1);

		if (weight > 0)
		{
			ax += V->x() * weight;
			ay += V->y() * weight;

			a_total += weight;
		}

		weight = WeightForVertex(V, x1,y1, x2,y2, width,height, +1);

		if (weight > 0)
		{
			bx += V->x() * weight;
			by += V->y() * weight;

			b_total += weight;
		}
	}

	SYS_ASSERT(a_total > 0);
	SYS_ASSERT(b_total > 0);

	ax /= a_total;
	ay /= a_total;

	bx /= b_total;
	by /= b_total;


	// check the two end points are not too close
	double unit_x = (bx - ax);
	double unit_y = (by - ay);

	double unit_len = hypot(unit_x, unit_y);

	if (unit_len < 2)
	{
		inst.Beep("Cannot determine line");
		return;
	}

	unit_x /= unit_len;
	unit_y /= unit_len;


	// collect all vertices and determine where along the line they are,
	// then sort them based on their along value.

	std::vector< vert_along_t > along_list;

	for (sel_iter_c it(inst.edit.Selected) ; !it.done() ; it.next())
	{
		const Vertex *V = inst.level.vertices[*it];

		vert_along_t ALONG(*it, AlongDist(V->x(), V->y(), ax,ay, bx,by));

		along_list.push_back(ALONG);
	}

	std::sort(along_list.begin(), along_list.end(), vert_along_t::CMP());


	// compute proper positions for start and end of the line
	const Vertex *V1 = inst.level.vertices[along_list.front().vert_num];
	const Vertex *V2 = inst.level.vertices[along_list. back().vert_num];

	double along1 = along_list.front().along;
	double along2 = along_list. back().along;

	if ((true) /* don't move first and last vertices */)
	{
		ax = V1->x();
		ay = V1->y();

		bx = V2->x();
		by = V2->y();
	}
	else
	{
		bx = ax + along2 * unit_x;
		by = ay + along2 * unit_y;

		ax = ax + along1 * unit_x;
		ay = ay + along1 * unit_y;
	}


	inst.level.basis.begin();
	inst.level.basis.setMessage("shaped %d vertices", (int)along_list.size());

	for (unsigned int i = 0 ; i < along_list.size() ; i++)
	{
		double frac;

		if ((true) /* regular spacing */)
			frac = i / (double)(along_list.size() - 1);
		else
			frac = (along_list[i].along - along1) / (along2 - along1);

		// ANOTHER OPTION: use distances between neighbor verts...

		double nx = ax + (bx - ax) * frac;
		double ny = ay + (by - ay) * frac;

		inst.level.basis.changeVertex(along_list[i].vert_num, Thing::F_X, inst.MakeValidCoord(nx));
		inst.level.basis.changeVertex(along_list[i].vert_num, Thing::F_Y, inst.MakeValidCoord(ny));
	}

	inst.level.basis.end();
}


static double BiggestGapAngle(std::vector< vert_along_t > &along_list,
							  unsigned int *start_idx)
{
	double best_diff = 0;
	double best_low  = 0;

	*start_idx = 0;

	for (unsigned int i = 0 ; i < along_list.size() ; i++)
	{
		unsigned int k = i + 1;

		if (k >= along_list.size())
			k = 0;

		double low  = along_list[i].along;
		double high = along_list[k].along;

		if (high < low)
			high = high + M_PI * 2.0;

		double ang_diff = high - low;

		if (ang_diff > best_diff)
		{
			best_diff  = ang_diff;
			best_low   = low;
			*start_idx = k;
		}
	}

	double best = best_low + best_diff * 0.5;

	return best;
}


double VertexModule::evaluateCircle(double mid_x, double mid_y, double r,
	std::vector< vert_along_t > &along_list,
	unsigned int start_idx, double arc_rad,
	double ang_offset /* radians */,
	bool move_vertices) const
{
	double cost = 0;

	bool partial_circle = (arc_rad < M_PI * 1.9);

	for (unsigned int i = 0 ; i < along_list.size() ; i++)
	{
		unsigned int k = (start_idx + i) % along_list.size();

		const Vertex *V = doc.vertices[along_list[k].vert_num];

		double frac = i / (double)(along_list.size() - (partial_circle ? 1 : 0));

		double ang = arc_rad * frac + ang_offset;

		double new_x = mid_x + cos(ang) * r;
		double new_y = mid_y + sin(ang) * r;

		if (move_vertices)
		{
			doc.basis.changeVertex(along_list[k].vert_num, Thing::F_X, inst.MakeValidCoord(new_x));
			doc.basis.changeVertex(along_list[k].vert_num, Thing::F_Y, inst.MakeValidCoord(new_y));
		}
		else
		{
			double dx = new_x - V->x();
			double dy = new_y - V->y();

			cost = cost + (dx*dx + dy*dy);
		}
	}

	return cost;
}


void VertexModule::commandShapeArc(Instance &inst)
{
	if (EXEC_Param[0].empty())
	{
		inst.Beep("VT_ShapeArc: missing angle parameter");
		return;
	}

	int arc_deg = atoi(EXEC_Param[0]);

	if (arc_deg < 30 || arc_deg > 360)
	{
		inst.Beep("VT_ShapeArc: bad angle: %s", EXEC_Param[0].c_str());
		return;
	}

	double arc_rad = arc_deg * M_PI / 180.0;


	if (inst.edit.Selected->count_obj() < 3)
	{
		inst.Beep("Need 3 or more vertices to shape");
		return;
	}


	// determine middle point for circle
	double x1, y1, x2, y2;
	inst.level.objects.calcBBox(inst.edit.Selected, &x1, &y1, &x2, &y2);

	double width  = x2 - x1;
	double height = y2 - y1;

	if (width < 4 && height < 4)
	{
		inst.Beep("Too small");
		return;
	}

	double mid_x = (x1 + x2) * 0.5;
	double mid_y = (y1 + y2) * 0.5;


	// collect all vertices and determine their angle (in radians),
	// and sort them.
	//
	// also determine radius of circle -- average of distances between
	// the computed mid-point and each vertex.

	double r = 0;

	std::vector< vert_along_t > along_list;

	for (sel_iter_c it(inst.edit.Selected) ; !it.done() ; it.next())
	{
		const Vertex *V = inst.level.vertices[*it];

		double dx = V->x() - mid_x;
		double dy = V->y() - mid_y;

		double dist = hypot(dx, dy);

		if (dist < 4)
		{
			inst.Beep("Strange shape");
			return;
		}

		r += dist;

		double angle = atan2(dy, dx);

		vert_along_t ALONG(*it, angle);

		along_list.push_back(ALONG);
	}

	r /= (double)along_list.size();

	std::sort(along_list.begin(), along_list.end(), vert_along_t::CMP());


	// where is the biggest gap?
	unsigned int start_idx;
	unsigned int end_idx;

	double gap_angle = BiggestGapAngle(along_list, &start_idx);

	double gap_dx = cos(gap_angle);
	double gap_dy = sin(gap_angle);


	if (start_idx > 0)
		end_idx = start_idx - 1;
	else
		end_idx = static_cast<unsigned>(along_list.size() - 1);

	const Vertex * start_V = inst.level.vertices[along_list[start_idx].vert_num];
	const Vertex * end_V   = inst.level.vertices[along_list[  end_idx].vert_num];

	double start_end_dist = hypot(end_V->x() - start_V->x(), end_V->y() - start_V->y());


	// compute new mid-point and radius (except for a full circle)
	// and also compute starting angle.

	double best_offset = 0;
	double best_cost = 1e30;

	if (arc_deg < 360)
	{
		mid_x = (start_V->x() + end_V->x()) * 0.5;
		mid_y = (start_V->y() + end_V->y()) * 0.5;

		r = start_end_dist * 0.5;

		double dx = gap_dx;
		double dy = gap_dy;

		if (arc_deg > 180)
		{
			dx = -dx;
			dy = -dy;
		}

		double theta = fabs(arc_rad - M_PI) / 2.0;

		double away = r * tan(theta);

		mid_x += dx * away;
		mid_y += dy * away;

		r = hypot(r, away);

		best_offset = atan2(start_V->y() - mid_y, start_V->x() - mid_x);
	}
	else
	{
		// find the best orientation, the one that minimises the distances
		// which vertices move.  We try 1000 possibilities.

		for (int pos = 0 ; pos < 1000 ; pos++)
		{
			double ang_offset = pos * M_PI * 2.0 / 1000.0;

			double cost = inst.level.vertmod.evaluateCircle(mid_x, mid_y, r, along_list,
										 start_idx, arc_rad, ang_offset, false);

			if (cost < best_cost)
			{
				best_offset = ang_offset;
				best_cost   = cost;
			}
		}
	}


	// actually move stuff now

	inst.level.basis.begin();
	inst.level.basis.setMessage("shaped %d vertices", (int)along_list.size());

	inst.level.vertmod.evaluateCircle(mid_x, mid_y, r, along_list, start_idx, arc_rad,
				   best_offset, true);

	inst.level.basis.end();
}


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
