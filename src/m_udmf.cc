//------------------------------------------------------------------------
//  UDMF PARSING / WRITING
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2019 Andrew Apted
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

#include "m_udmf.h"
#include "m_game.h"
#include "w_rawdef.h"
#include "w_texture.h"
#include "w_wad.h"

#include "ui_window.h"


extern short loading_level;
extern Lump_c * Load_LookupAndSeek(const char *name);


class Udmf_Token
{
private:
	// empty means EOF
	std::string text;

public:
	Udmf_Token(const char *str) : text(str)
	{ }

	Udmf_Token(const char *str, int len) : text(str, 0, len)
	{ }

	const char *c_str()
	{
		return text.c_str();
	}

	bool IsEOF() const
	{
		return text.empty();
	}

	bool IsIdentifier() const
	{
		if (text.size() == 0)
			return false;

		char ch = text[0];

		return isalpha(ch) || ch == '_';
	}

	bool IsString() const
	{
		return text.size() > 0 && text[0] == '"';
	}

	bool Match(const char *name) const
	{
		return y_stricmp(text.c_str(), name) == 0;
	}

	int DecodeInt() const
	{
		return atoi(text.c_str());
	}

	int DecodeFloat() const
	{
		double f = atof(text.c_str());

		// TODO multiply by PRECISION

		return I_ROUND(f);
	}

	int DecodeTexture() const
	{
		char buffer[16];

		if (! IsString())
		{
			// TODO warning
			strcpy(buffer, "-");
		}
		else
		{
			int use_len = 8;

			if (text.size() < 10)
				use_len = (int)text.size() - 2;

			strncpy(buffer, text.c_str() + 1, use_len);
			buffer[use_len] = 0;
		}

		return BA_InternaliseString(NormalizeTex(buffer));
	}
};


// since UDMF lumps can be very large, we read chunks of it
// as-needed instead of loading the whole thing into memory.
// the buffer size should be over 2x maximum token length.
#define U_BUF_SIZE  16384

class Udmf_Parser
{
private:
	Lump_c *lump;

	// reached EOF or a file read error
	bool done;

	// we have seen a "/*" but not the closing "*/"
	bool in_comment;

	// number of remaining bytes
	int remaining;

	// read buffer
	char buffer[U_BUF_SIZE];

	// position in buffer and used size of buffer
	int b_pos;
	int b_size;

public:
	Udmf_Parser(Lump_c *_lump) :
		lump(_lump),
		done(false), in_comment(false),
		b_pos(0), b_size(0)
	{
		remaining = lump->Length();
	}

	Udmf_Token Next()
	{
		for (;;)
		{
			if (done)
				return Udmf_Token("");

			// when position reaches half-way point, shift buffer down
			if (b_pos >= U_BUF_SIZE/2)
			{
				memmove(buffer, buffer + U_BUF_SIZE/2, U_BUF_SIZE/2);

				b_pos  -= U_BUF_SIZE/2;
				b_size -= U_BUF_SIZE/2;
			}

			// top up the buffer
			if (remaining > 0 && b_size < U_BUF_SIZE)
			{
				int want = U_BUF_SIZE - b_size;
				if (want > remaining)
					want = remaining;

				if (! lump->Read(buffer + b_size, want))
				{
					// TODO mark error somewhere, show dialog later
					done = true;
					continue;
				}

				remaining -= want;
				b_size    += want;
			}

			// end of file?
			if (remaining <= 0 && b_pos >= b_size)
			{
				done = true;
				continue;
			}

			if (in_comment)
			{
				// end of multi-line comment?
				if (b_pos+2 <= b_size &&
					buffer[b_pos] == '*' &&
					buffer[b_pos+1] == '/')
				{
					in_comment = false;
					b_pos += 2;
					continue;
				}

				b_pos++;
				continue;
			}

			// check for multi-line comment
			if (b_pos+2 <= b_size &&
				buffer[b_pos] == '/' &&
				buffer[b_pos+1] == '*')
			{
				in_comment = true;
				b_pos += 2;
				continue;
			}

			// check for single-line comment
			if (b_pos+2 <= b_size &&
				buffer[b_pos] == '/' &&
				buffer[b_pos+1] == '/')
			{
				SkipToEOLN();
				continue;
			}

			// skip whitespace (assumes ASCII)
			int start = b_pos;
			unsigned char ch = buffer[b_pos];

			if ((ch <= 32) || (ch >= 127 && ch <= 160))
			{
				b_pos++;
				continue;
			}

			// an actual token, yay!

			// is it a string?
			if (ch == '"')
			{
				b_pos++;

				while (b_pos < b_size)
				{
					// skip escapes
					if (buffer[b_pos] == '\\' && b_pos+1 < b_size)
					{
						b_pos += 2;
						continue;
					}

					if (buffer[b_pos] == '"')
					{
						// include trailing double quote
						b_pos++;
						break;
					}

					b_pos++;
				}

				return Udmf_Token(buffer+start, b_pos - start);
			}

			// is it a identifier or number?
			if (isalnum(ch) || ch == '_' || ch == '-' || ch == '+')
			{
				b_pos++;

				while (b_pos < b_size)
				{
					char ch = buffer[b_pos];
					if (isalnum(ch) || ch == '_' || ch == '-' || ch == '+' || ch == '.')
					{
						b_pos++;
						continue;
					}
					break;
				}

				return Udmf_Token(buffer+start, b_pos - start);
			}

			// it must be a symbol, such as '{' or '}'
			b_pos++;

			return Udmf_Token(buffer+start, 1);
		}
	}

	bool Expect(const char *name)
	{
		Udmf_Token tok = Next();
		return tok.Match(name);
	}

	void SkipToEOLN()
	{
		while (b_pos < b_size && buffer[b_pos] != '\n')
			b_pos++;
	}
};


static void ParseUDMF_GlobalVar(Udmf_Parser& parser, Udmf_Token& name)
{
	Udmf_Token value = parser.Next();
	if (value.IsEOF())
	{
		// TODO mark error
		return;
	}
	if (!parser.Expect(";"))
	{
		// TODO mark error
		parser.SkipToEOLN();
		return;
	}

	if (name.Match("namespace"))
	{
		// TODO : do something with the namespace value
	}
	else if (name.Match("ee_compat"))
	{
		// odd Eternity thing, ignore it
	}
	else
	{
		LogPrintf("skipping unknown global '%s' in UDMF\n", name.c_str());
	}
}


static void ParseUDMF_ThingField(Thing *T, Udmf_Token& field, Udmf_Token& value)
{
	// just ignore any setting with the "false" keyword
	if (value.Match("false"))
		return;

	// TODO hexen options

	// TODO strife options

	if (field.Match("x"))
		T->x = value.DecodeFloat();
	else if (field.Match("y"))
		T->y = value.DecodeFloat();
	else if (field.Match("height"))
		T->z = value.DecodeFloat();
	else if (field.Match("type"))
		T->type = value.DecodeInt();
	else if (field.Match("angle"))
		T->angle = value.DecodeInt();

	else if (field.Match("id"))
		T->tid = value.DecodeInt();
	else if (field.Match("special"))
		T->special = value.DecodeInt();
	else if (field.Match("arg0"))
		T->arg1 = value.DecodeInt();
	else if (field.Match("arg1"))
		T->arg2 = value.DecodeInt();
	else if (field.Match("arg2"))
		T->arg3 = value.DecodeInt();
	else if (field.Match("arg3"))
		T->arg4 = value.DecodeInt();
	else if (field.Match("arg4"))
		T->arg5 = value.DecodeInt();

	else if (field.Match("skill2"))
		T->options |= MTF_Easy;
	else if (field.Match("skill3"))
		T->options |= MTF_Medium;
	else if (field.Match("skill4"))
		T->options |= MTF_Hard;
	else if (field.Match("ambush"))
		T->options |= MTF_Ambush;
	else if (field.Match("friend"))
		T->options |= MTF_Friend;
	else if (field.Match("single"))
		T->options &= ~MTF_Not_SP;
	else if (field.Match("coop"))
		T->options &= ~MTF_Not_COOP;
	else if (field.Match("dm"))
		T->options &= ~MTF_Not_DM;

	else
	{
		DebugPrintf("thing #%d: unknown field '%s'\n", NumThings-1, field.c_str());
	}
}

static void ParseUDMF_VertexField(Vertex *V, Udmf_Token& field, Udmf_Token& value)
{
	if (field.Match("x"))
		V->x = value.DecodeFloat();
	else if (field.Match("y"))
		V->y = value.DecodeFloat();
	else
	{
		DebugPrintf("vertex #%d: unknown field '%s'\n", NumVertices-1, field.c_str());
	}
}

static void ParseUDMF_LinedefField(LineDef *LD, Udmf_Token& field, Udmf_Token& value)
{
	// Note: vertex and sidedef numbers are validated later on

	// just ignore any setting with the "false" keyword
	if (value.Match("false"))
		return;

	// TODO hexen flags

	// TODO strife flags

	if (field.Match("v1"))
		LD->start = value.DecodeInt();
	else if (field.Match("v2"))
		LD->end = value.DecodeInt();
	else if (field.Match("sidefront"))
		LD->right = value.DecodeInt();
	else if (field.Match("sideback"))
		LD->left = value.DecodeInt();
	else if (field.Match("special"))
		LD->type = value.DecodeInt();

	else if (field.Match("arg0"))
		LD->tag = value.DecodeInt();
	else if (field.Match("arg1"))
		LD->arg2 = value.DecodeInt();
	else if (field.Match("arg2"))
		LD->arg3 = value.DecodeInt();
	else if (field.Match("arg3"))
		LD->arg4 = value.DecodeInt();
	else if (field.Match("arg4"))
		LD->arg5 = value.DecodeInt();

	else if (field.Match("blocking"))
		LD->flags |= MLF_Blocking;
	else if (field.Match("blockmonsters"))
		LD->flags |= MLF_BlockMonsters;
	else if (field.Match("twosided"))
		LD->flags |= MLF_TwoSided;
	else if (field.Match("dontpegtop"))
		LD->flags |= MLF_UpperUnpegged;
	else if (field.Match("dontpegbottom"))
		LD->flags |= MLF_LowerUnpegged;
	else if (field.Match("secret"))
		LD->flags |= MLF_Secret;
	else if (field.Match("blocksound"))
		LD->flags |= MLF_SoundBlock;
	else if (field.Match("dontdraw"))
		LD->flags |= MLF_DontDraw;
	else if (field.Match("mapped"))
		LD->flags |= MLF_Mapped;

	else if (field.Match("passuse"))
		LD->flags |= MLF_Boom_PassThru;

	else
	{
		DebugPrintf("linedef #%d: unknown field '%s'\n", NumVertices-1, field.c_str());
	}
}

static void ParseUDMF_SidedefField(SideDef *SD, Udmf_Token& field, Udmf_Token& value)
{
	// Note: sector numbers are validated later on

	// TODO: consider how to handle "offsetx_top" (etc), if at all

	if (field.Match("sector"))
		SD->sector = value.DecodeInt();
	else if (field.Match("texturetop"))
		SD->upper_tex = value.DecodeTexture();
	else if (field.Match("texturebottom"))
		SD->lower_tex = value.DecodeTexture();
	else if (field.Match("texturemiddle"))
		SD->mid_tex = value.DecodeTexture();
	else if (field.Match("offsetx"))
		SD->x_offset = value.DecodeInt();
	else if (field.Match("offsety"))
		SD->y_offset = value.DecodeInt();
	else
	{
		DebugPrintf("sidedef #%d: unknown field '%s'\n", NumVertices-1, field.c_str());
	}
}

static void ParseUDMF_SectorField(Sector *S, Udmf_Token& field, Udmf_Token& value)
{
	if (field.Match("heightfloor"))
		S->floorh = value.DecodeInt();
	else if (field.Match("heightceiling"))
		S->ceilh = value.DecodeInt();
	else if (field.Match("texturefloor"))
		S->floor_tex = value.DecodeTexture();
	else if (field.Match("textureceiling"))
		S->ceil_tex = value.DecodeTexture();
	else if (field.Match("lightlevel"))
		S->light = value.DecodeInt();
	else if (field.Match("special"))
		S->type = value.DecodeInt();
	else if (field.Match("id"))
		S->tag = value.DecodeInt();
	else
	{
		DebugPrintf("sector #%d: unknown field '%s'\n", NumVertices-1, field.c_str());
	}
}

static void ParseUDMF_Object(Udmf_Parser& parser, Udmf_Token& name)
{
	// create a new object of the specified type
	Objid kind;

	Thing   *new_T  = NULL;
	Vertex  *new_V  = NULL;
	LineDef *new_LD = NULL;
	SideDef *new_SD = NULL;
	Sector  *new_S  = NULL;

	if (name.Match("thing"))
	{
		kind = Objid(OBJ_THINGS, 1);
		new_T = new Thing;
		new_T->options = MTF_Not_SP | MTF_Not_COOP | MTF_Not_DM;
		Things.push_back(new_T);
	}
	else if (name.Match("vertex"))
	{
		kind = Objid(OBJ_VERTICES, 1);
		new_V = new Vertex;
		Vertices.push_back(new_V);
	}
	else if (name.Match("linedef"))
	{
		kind = Objid(OBJ_LINEDEFS, 1);
		new_LD = new LineDef;
		LineDefs.push_back(new_LD);
	}
	else if (name.Match("sidedef"))
	{
		kind = Objid(OBJ_SIDEDEFS, 1);
		new_SD = new SideDef;
		new_SD->mid_tex = BA_InternaliseString("-");
		new_SD->lower_tex = new_SD->mid_tex;
		new_SD->upper_tex = new_SD->upper_tex;
		SideDefs.push_back(new_SD);
	}
	else if (name.Match("sector"))
	{
		kind = Objid(OBJ_SECTORS, 1);
		new_S = new Sector;
		new_S->light = 160;
		Sectors.push_back(new_S);
	}

	if (!kind.valid())
	{
		// unknown object kind
		LogPrintf("skipping unknown block '%s' in UDMF\n", name.c_str());
	}

	for (;;)
	{
		Udmf_Token tok = parser.Next();
		if (tok.IsEOF())
			break;

		if (tok.Match("}"))
			break;

		if (! parser.Expect("="))
		{
			// TODO mark error
			parser.SkipToEOLN();
			continue;
		}

		Udmf_Token value = parser.Next();
		if (value.IsEOF())
			break;

		if (! parser.Expect(";"))
		{
			// TODO mark error
			parser.SkipToEOLN();
			continue;
		}

		if (new_T)
			ParseUDMF_ThingField(new_T, tok, value);

		if (new_V)
			ParseUDMF_VertexField(new_V, tok, value);

		if (new_LD)
			ParseUDMF_LinedefField(new_LD, tok, value);

		if (new_SD)
			ParseUDMF_SidedefField(new_SD, tok, value);

		if (new_S)
			ParseUDMF_SectorField(new_S, tok, value);
	}
}

void LoadLevel_UDMF()
{
	Lump_c *lump = Load_LookupAndSeek("TEXTMAP");
	// we assume this cannot happen
	if (! lump)
		return;

	Udmf_Parser parser(lump);

	for (;;)
	{
		Udmf_Token tok = parser.Next();
		if (tok.IsEOF())
			break;

		if (! tok.IsIdentifier())
		{
			// something has gone wrong
			// TODO mark the error somehow, pop-up dialog later
			parser.SkipToEOLN();
			continue;
		}

		Udmf_Token tok2 = parser.Next();
		if (tok2.IsEOF())
			break;

		if (tok2.Match("="))
		{
			ParseUDMF_GlobalVar(parser, tok);
			continue;
		}
		if (tok2.Match("{"))
		{
			ParseUDMF_Object(parser, tok);
			continue;
		}

		// unexpected symbol
		// TODO mark the error somehow, show dialog later
		parser.SkipToEOLN();
	}

	// TODO TODO validate fields  [ esp. verts/sides in linedef, sector in sidedef ]
}


//----------------------------------------------------------------------

static inline void WrFlag(Lump_c *lump, int flags, const char *name, int mask)
{
	if ((flags & mask) != 0)
	{
		lump->Printf("%s = true;\n", name);
	}
}

static void WriteUDMF_Info(Lump_c *lump)
{
	// TODO other namespaces

	lump->Printf("namespace = \"Doom\";\n\n");
}

static void WriteUDMF_Things(Lump_c *lump)
{
	for (int i = 0 ; i < NumThings ; i++)
	{
		lump->Printf("thing // %d\n", i);
		lump->Printf("{\n");

		const Thing *th = Things[i];

		lump->Printf("x = %d.000;\n", th->x);
		lump->Printf("y = %d.000;\n", th->y);

		if (th->z != 0)
			lump->Printf("height = %d.000;\n", th->z);

		lump->Printf("angle = %d;\n", th->angle);
		lump->Printf("type = %d;\n", th->type);

		// thing options
		WrFlag(lump, th->options, "skill1", MTF_Easy);
		WrFlag(lump, th->options, "skill2", MTF_Easy);
		WrFlag(lump, th->options, "skill3", MTF_Medium);
		WrFlag(lump, th->options, "skill4", MTF_Hard);
		WrFlag(lump, th->options, "skill5", MTF_Hard);

		WrFlag(lump, 0 ^ th->options, "single", MTF_Not_SP);
		WrFlag(lump, 0 ^ th->options, "coop",   MTF_Not_COOP);
		WrFlag(lump, 0 ^ th->options, "dm",     MTF_Not_DM);

		WrFlag(lump, th->options, "ambush", MTF_Ambush);

		if (game_info.friend_flag)
			WrFlag(lump, th->options, "friend", MTF_Friend);

		// TODO Hexen flags

		// TODO Strife flags

		// TODO Hexen special and args

		lump->Printf("}\n\n");
	}
}

static void WriteUDMF_Vertices(Lump_c *lump)
{
	for (int i = 0 ; i < NumVertices ; i++)
	{
		lump->Printf("vertex // %d\n", i);
		lump->Printf("{\n");

		const Vertex *vert = Vertices[i];

		lump->Printf("x = %d.000;\n", vert->x);
		lump->Printf("y = %d.000;\n", vert->y);

		lump->Printf("}\n\n");
	}
}

static void WriteUDMF_LineDefs(Lump_c *lump)
{
	for (int i = 0 ; i < NumLineDefs ; i++)
	{
		lump->Printf("linedef // %d\n", i);
		lump->Printf("{\n");

		const LineDef *ld = LineDefs[i];

		lump->Printf("v1 = %d;\n", ld->start);
		lump->Printf("v2 = %d;\n", ld->end);

		if (ld->right >= 0)
			lump->Printf("sidefront = %d;\n", ld->right);
		if (ld->left >= 0)
			lump->Printf("sideback = %d;\n", ld->left);

		if (ld->type != 0)
			lump->Printf("special = %d;\n", ld->type);

		if (ld->tag != 0)
			lump->Printf("arg0 = %d;\n", ld->tag);
		if (ld->arg2 != 0)
			lump->Printf("arg1 = %d;\n", ld->arg2);
		if (ld->arg3 != 0)
			lump->Printf("arg2 = %d;\n", ld->arg3);
		if (ld->arg4 != 0)
			lump->Printf("arg3 = %d;\n", ld->arg4);
		if (ld->arg5 != 0)
			lump->Printf("arg4 = %d;\n", ld->arg5);

		// linedef flags
		WrFlag(lump, ld->flags, "blocking",      MLF_Blocking);
		WrFlag(lump, ld->flags, "blockmonsters", MLF_BlockMonsters);
		WrFlag(lump, ld->flags, "twosided",      MLF_TwoSided);
		WrFlag(lump, ld->flags, "dontpegtop",    MLF_UpperUnpegged);
		WrFlag(lump, ld->flags, "dontpegbottom", MLF_LowerUnpegged);
		WrFlag(lump, ld->flags, "secret",        MLF_Secret);
		WrFlag(lump, ld->flags, "blocksound",    MLF_SoundBlock);
		WrFlag(lump, ld->flags, "dontdraw",      MLF_DontDraw);
		WrFlag(lump, ld->flags, "mapped",        MLF_Mapped);

		if (game_info.pass_through)
			WrFlag(lump, ld->flags, "passuse", MLF_Boom_PassThru);

		if (game_info.midtex_3d)
			WrFlag(lump, ld->flags, "midtex3d", MLF_Eternity_3DMidTex);

		// TODO : hexen stuff (SPAC flags, etc)

		// TODO : strife stuff

		// TODO : zdoom stuff

		lump->Printf("}\n\n");
	}
}

static void WriteUDMF_SideDefs(Lump_c *lump)
{
	for (int i = 0 ; i < NumSideDefs ; i++)
	{
		lump->Printf("sidedef // %d\n", i);
		lump->Printf("{\n");

		const SideDef *side = SideDefs[i];

		lump->Printf("sector = %d;\n", side->sector);

		if (side->x_offset != 0)
			lump->Printf("offsetx = %d;\n", side->x_offset);
		if (side->y_offset != 0)
			lump->Printf("offsety = %d;\n", side->y_offset);

		// use NormalizeTex to ensure no double quote

		if (strcmp(side->UpperTex(), "-") != 0)
			lump->Printf("texturetop = \"%s\";\n", NormalizeTex(side->UpperTex()));
		if (strcmp(side->LowerTex(), "-") != 0)
			lump->Printf("texturebottom = \"%s\";\n", NormalizeTex(side->LowerTex()));
		if (strcmp(side->MidTex(), "-") != 0)
			lump->Printf("texturemiddle = \"%s\";\n", NormalizeTex(side->MidTex()));

		lump->Printf("}\n\n");
	}
}

static void WriteUDMF_Sectors(Lump_c *lump)
{
	for (int i = 0 ; i < NumSectors ; i++)
	{
		lump->Printf("sector // %d\n", i);
		lump->Printf("{\n");

		const Sector *sec = Sectors[i];

		lump->Printf("heightfloor = %d;\n", sec->floorh);
		lump->Printf("heightceiling = %d;\n", sec->ceilh);

		// use NormalizeTex to ensure no double quote

		lump->Printf("texturefloor = \"%s\";\n", NormalizeTex(sec->FloorTex()));
		lump->Printf("textureceiling = \"%s\";\n", NormalizeTex(sec->CeilTex()));

		lump->Printf("lightlevel = %d;\n", sec->light);
		if (sec->type != 0)
			lump->Printf("special = %d;\n", sec->type);
		if (sec->tag != 0)
			lump->Printf("id = %d;\n", sec->tag);

		lump->Printf("}\n\n");
	}
}

void SaveLevel_UDMF()
{
	Lump_c *lump = edit_wad->AddLump("TEXTMAP");

	WriteUDMF_Info(lump);
	WriteUDMF_Things(lump);
	WriteUDMF_Vertices(lump);
	WriteUDMF_LineDefs(lump);
	WriteUDMF_SideDefs(lump);
	WriteUDMF_Sectors(lump);

	lump->Finish();

	lump = edit_wad->AddLump("ENDMAP");
	lump->Finish();
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
