//------------------------------------------------------------------------
//  TEXTURES / FLATS / SPRITES
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2001-2020 Andrew Apted
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

#include <map>
#include <algorithm>
#include <string>

#include "m_game.h"      /* yg_picture_format */
#include "w_loadpic.h"
#include "w_rawdef.h"
#include "w_texture.h"


//----------------------------------------------------------------------
//    TEXTURE HANDLING
//----------------------------------------------------------------------

void WadData::clearTextures()
{
	textures.clear();

	medusa_textures.clear();
}


void WadData::addTexture(const SString &name, std::unique_ptr<Img_c> &&img,
						 bool is_medusa)
{
	// free any existing one with the same name

	SString tex_str = name;

	textures[tex_str] = std::move(img);

	medusa_textures[tex_str] = is_medusa ? 1 : 0;
}


static bool CheckTexturesAreStrife(const byte *tex_data, int tex_length,
								   int num_tex, bool skip_first)
{
	// we follow the ZDoom logic here: check ALL the texture entries
	// assuming DOOM format, and if any have a patch_count of zero or
	// the last two bytes of columndir are non-zero then assume Strife.

	auto tex_data_s32 = reinterpret_cast<const s32_t *>(tex_data);

	for (int n = skip_first ? 1 : 0 ; n < num_tex ; n++)
	{
		int offset = LE_S32(tex_data_s32[1 + n]);

		// ignore invalid offsets here  [ caught later ]
		if (offset < 4 * num_tex || offset >= tex_length)
			continue;

		auto raw = reinterpret_cast<const raw_texture_t *>(tex_data + offset);

		if (LE_S16(raw->patch_count) <= 0)
			return true;

		if (raw->column_dir[1] != 0)
			return true;
	}

	return false;
}


void Instance::LoadTextureEntry_Strife(const byte *tex_data, int tex_length,
									   int offset, const byte *pnames,
									   int pname_size, bool skip_first)
{
	auto raw = reinterpret_cast<const raw_strife_texture_t *>(tex_data +
															  offset);

	// create the new image
	int width  = LE_U16(raw->width);
	int height = LE_U16(raw->height);

	gLog.debugPrintf("Texture [%.8s] : %dx%d\n", raw->name, width, height);

	if (width == 0 || height == 0)
	{
		ParseException::raise("%s: Texture '%.8s' has zero size\n", __func__,
							  raw->name);
	}

	auto img = std::make_unique<Img_c>(width, height, false);
	bool is_medusa = false;

	// apply all the patches
	int num_patches = LE_S16(raw->patch_count);

	if (! num_patches)
	{
		ParseException::raise("%s: Texture '%.8s' has no patches\n", __func__,
							  raw->name);
	}

	const raw_strife_patchdef_t *patdef =  &raw->patches[0];

	if (num_patches >= 2)
		is_medusa = true;

	for (int j = 0 ; j < num_patches ; j++, patdef++)
	{
		int xofs = LE_S16(patdef->x_origin);
		int yofs = LE_S16(patdef->y_origin);
		int pname_idx = LE_U16(patdef->pname);

		if (yofs < 0)
			yofs = 0;

		if (pname_idx >= pname_size)
		{
			gLog.printf("Invalid pname in texture '%.8s'\n", raw->name);
			continue;
		}

		char picname[16];
		memcpy(picname, pnames + 8*pname_idx, 8);
		picname[8] = 0;

		Lump_c *lump = master.findGlobalLump(picname);

		if (! lump ||
			! LoadPicture(*img, lump, picname, xofs, yofs))
		{
			gLog.printf("texture '%.8s': patch '%.8s' not found.\n", raw->name,
						picname);
		}
	}

	// store the new texture
	char namebuf[16];
	memcpy(namebuf, raw->name, 8);
	namebuf[8] = 0;

	wad.addTexture(namebuf, std::move(img), is_medusa);
}


void Instance::LoadTextureEntry_DOOM(byte *tex_data, int tex_length,
									 int offset, const byte *pnames,
									 int pname_size, bool skip_first)
{
	const raw_texture_t *raw = (const raw_texture_t *)(tex_data + offset);

	// create the new image
	int width  = LE_U16(raw->width);
	int height = LE_U16(raw->height);

	gLog.debugPrintf("Texture [%.8s] : %dx%d\n", raw->name, width, height);

	if (width == 0 || height == 0)
		ThrowException("W_LoadTextures: Texture '%.8s' has zero size\n", raw->name);

	auto img = std::make_unique<Img_c>(width, height, false);
	bool is_medusa = false;

	// apply all the patches
	int num_patches = LE_S16(raw->patch_count);

	if (! num_patches)
		ThrowException("W_LoadTextures: Texture '%.8s' has no patches\n", raw->name);

	const raw_patchdef_t *patdef = (const raw_patchdef_t *) & raw->patches[0];

	// andrewj: this is not strictly correct, the Medusa Effect is only
	//          triggered when multiple patches occupy a single column of
	//          the texture.  But checking for that is a major pain since
	//          we don't know the width of each patch here....
	if (num_patches >= 2)
		is_medusa = true;

	for (int j = 0 ; j < num_patches ; j++, patdef++)
	{
		int xofs = LE_S16(patdef->x_origin);
		int yofs = LE_S16(patdef->y_origin);
		int pname_idx = LE_U16(patdef->pname);

		if (pname_idx >= pname_size)
		{
			gLog.printf("Invalid pname in texture '%.8s'\n", raw->name);
			continue;
		}

		char picname[16];
		memcpy(picname, pnames + 8*pname_idx, 8);
		picname[8] = 0;

//gLog.debugPrintf("-- %d patch [%s]\n", j, picname);
		Lump_c *lump = master.findGlobalLump(picname);

		if (! lump ||
			! LoadPicture(*img, lump, picname, xofs, yofs))
		{
			gLog.printf("texture '%.8s': patch '%.8s' not found.\n", raw->name, picname);
		}
	}

	// store the new texture
	char namebuf[16];
	memcpy(namebuf, raw->name, 8);
	namebuf[8] = 0;

	wad.addTexture(namebuf, std::move(img), is_medusa);
}


void Instance::LoadTexturesLump(Lump_c *lump, const byte *pnames,
								int pname_size, bool skip_first)
{
	// TODO : verify size word at front of PNAMES ??

	// skip size word at front of PNAMES
	pnames += 4;

	pname_size /= 8;

	// load TEXTUREx data into memory for easier processing
	std::vector<byte> tex_data;
	int tex_length = loadLumpData(*lump, tex_data);

	// at the front of the TEXTUREx lump are some 4-byte integers
	auto tex_data_s32 = reinterpret_cast<const s32_t *>(tex_data.data());

	int num_tex = LE_S32(tex_data_s32[0]);

	// it seems having a count of zero is valid
	if (num_tex == 0)
		return;

	if (num_tex < 0 || num_tex > (1<<20))
	{
		ParseException::raise("%s: TEXTURE1/2 lump is corrupt, bad count.",
							  __func__);
	}

	bool is_strife = CheckTexturesAreStrife(tex_data.data(), tex_length,
											num_tex, skip_first);

	// Note: we skip the first entry (e.g. AASHITTY) which is not really
    //       usable (in the DOOM engine the #0 texture means "do not draw").

	for (int n = skip_first ? 1 : 0 ; n < num_tex ; n++)
	{
		int offset = LE_S32(tex_data_s32[1 + n]);

		if (offset < 4 * num_tex || offset >= tex_length)
		{
			ParseException::raise("%s: TEXTURE1/2 lump is corrupt, bad offset.",
								  __func__);
		}

		if (is_strife)
		{
			LoadTextureEntry_Strife(tex_data.data(), tex_length, offset,
									pnames, pname_size, skip_first);
		}
		else
		{
			LoadTextureEntry_DOOM(tex_data.data(), tex_length, offset, pnames,
								  pname_size, skip_first);
		}
	}
}


void Instance::W_LoadTextures_TX_START(Wad_file *wf)
{
	for(const LumpRef &lumpRef : wf->directory)
	{
		if(lumpRef.ns != WadNamespace::TextureLumps)
			continue;
		Lump_c *lump = lumpRef.lump;

		char img_fmt = W_DetectImageFormat(*lump);
		const SString &name = lump->Name();
		std::unique_ptr<Img_c> img = NULL;

		switch (img_fmt)
		{
			case 'd': /* Doom patch */
				img = std::make_unique<Img_c>();
				if (! LoadPicture(*img, lump, name, 0, 0))
				{
					img = nullptr;
				}
				break;

			case 'p': /* PNG */
				img = LoadImage_PNG(*lump, name);
				break;

			case 't': /* TGA */
				img = LoadImage_TGA(lump, name);
				break;

			case 'j': /* JPEG */
				img = LoadImage_JPEG(lump, name);
				break;

			case 0:
				gLog.printf("Unknown texture format in '%s' lump\n", name.c_str());
				break;

			default:
				gLog.printf("Unsupported texture format in '%s' lump\n", lump->Name().c_str());
				break;
		}

		// if we successfully loaded the texture, add it
		if (img)
		{
			wad.addTexture(name, std::move(img), false /* is_medusa */);
		}
	}
}


void Instance::W_LoadTextures()
{
	wad.clearTextures();

	for (int i = 0 ; i < (int)master.dir.size() ; i++)
	{
		gLog.printf("Loading Textures from WAD #%d\n", i+1);

		Lump_c *pnames   = master.dir[i]->FindLumpInNamespace("PNAMES", WadNamespace::Global);
		Lump_c *texture1 = master.dir[i]->FindLumpInNamespace("TEXTURE1", WadNamespace::Global);
		Lump_c *texture2 = master.dir[i]->FindLumpInNamespace("TEXTURE2", WadNamespace::Global);

		// Note that we _require_ the PNAMES lump to exist along
		// with the TEXTURE1/2 lump which uses it.  Probably a
		// few wads exist which lack the PNAMES lump (relying on
		// the one in the IWAD), however this practice is too
		// error-prone (using the wrong IWAD will break it),
		// so I think supporting it is a bad idea.  -- AJA

		if (pnames)
		{
			std::vector<byte> pname_data;
			int pname_size = loadLumpData(*pnames, pname_data);

			if (texture1)
				LoadTexturesLump(texture1, pname_data.data(), pname_size, true);

			if (texture2)
				LoadTexturesLump(texture2, pname_data.data(), pname_size, false);
		}

		if (conf.features.tx_start)
		{
			W_LoadTextures_TX_START(master.dir[i]);
		}
	}
}


Img_c * Instance::W_GetTexture(const SString &name, bool try_uppercase) const
{
	if (is_null_tex(name))
		return NULL;

	if (name.empty())
		return NULL;

	SString t_str = name;
	std::map<SString, std::unique_ptr<Img_c>>::const_iterator P = wad.textures.find(t_str);

	if (P != wad.textures.end())
		return P->second.get();

	if (try_uppercase)
	{
		return W_GetTexture(NormalizeTex(name), false);
	}

	if (conf.features.mix_textures_flats)
	{
		std::map<SString, std::unique_ptr<Img_c>>::const_iterator P =
				wad.flats.find(t_str);

		if (P != wad.flats.end())
			return P->second.get();
	}

	return NULL;
}


int Instance::W_GetTextureHeight(const SString &name) const
{
	Img_c *img = W_GetTexture(name);

	if (! img)
		return 128;

	return img->height();
}

// accepts "-", "#xxxx" or an existing texture name
bool Instance::W_TextureIsKnown(const SString &name) const
{
	if (is_null_tex(name) || is_special_tex(name))
		return true;

	if (name.empty())
		return false;

	std::map<SString, std::unique_ptr<Img_c>>::const_iterator P = wad.textures.find(name);

	if (P != wad.textures.end())
		return true;

	if (conf.features.mix_textures_flats)
	{
		std::map<SString, std::unique_ptr<Img_c>>::const_iterator P = wad.flats.find(name);

		if (P != wad.flats.end())
			return true;
	}

	return false;
}


bool Instance::W_TextureCausesMedusa(const SString &name) const
{
	std::map<SString, int>::const_iterator P = wad.medusa_textures.find(name);

	return (P != wad.medusa_textures.end() && P->second > 0);
}


SString NormalizeTex(const SString &name)
{
	if (name[0] == 0)
		return "-";

	SString buffer;

	for (size_t i = 0 ; i < WAD_TEX_NAME && name[i]; i++)
	{
		buffer.push_back(name[i]);

		// remove double quotes
		if (buffer[i] == '"')
			buffer[i] = '_';
		else
			buffer[i] = static_cast<char>(toupper(buffer[i]));
	}

	return buffer;
}


//----------------------------------------------------------------------
//    FLAT HANDLING
//----------------------------------------------------------------------

void WadData::clearFlats()
{
	flats.clear();
}


void WadData::addFlat(const SString &name, Img_c *img)
{
	// find any existing one with same name, and free it

	SString flat_str = name;
	flats[flat_str] = std::unique_ptr<Img_c>(img);
}


static std::unique_ptr<Img_c> LoadFlatImage(const WadData &wad,
											const SString &name, Lump_c *lump)
{
	// TODO: check size == 64*64

	auto img = std::make_unique<Img_c>(64, 64, false);

	int size = 64 * 64;

	std::vector<byte> raw;
	raw.resize(size);

	if (! (lump->Seek() && lump->Read(raw.data(), size)))
		throw ParseException("Error reading flat from WAD.\n");

	for (int i = 0 ; i < size ; i++)
	{
		img_pixel_t pix = raw[i];

		if (pix == TRANS_PIXEL)
			pix = static_cast<img_pixel_t>(wad.trans_replace);

		img->wbuf() [i] = pix;
	}

	return img;
}


void WadData::loadFlats(const MasterDirectory &master)
{
	clearFlats();

	for (int i = 0 ; i < (int)master.dir.size() ; i++)
	{
		gLog.printf("Loading Flats from WAD #%d\n", i+1);

		const Wad_file *wf = master.dir[i];

		for(const LumpRef &lumpRef : wf->directory)
		{
			if(lumpRef.ns != WadNamespace::Flats)
				continue;
			Lump_c *lump = lumpRef.lump;

			std::unique_ptr<Img_c> img = LoadFlatImage(*this, lump->Name(),
													   lump);

			// TODO: use unique_ptr
			if (img)
				addFlat(lump->Name(), img.release());
		}
	}
}


Img_c * Instance::W_GetFlat(const SString &name, bool try_uppercase) const
{
	std::map<SString, std::unique_ptr<Img_c>>::const_iterator P = wad.flats.find(name);

	if (P != wad.flats.end())
		return P->second.get();

	if (conf.features.mix_textures_flats)
	{
		std::map<SString, std::unique_ptr<Img_c>>::const_iterator P = wad.textures.find(name);

		if (P != wad.textures.end())
			return P->second.get();
	}

	if (try_uppercase)
	{
		return W_GetFlat(NormalizeTex(name), false);
	}

	return NULL;
}


bool Instance::W_FlatIsKnown(const SString &name) const
{
	// sectors do not support "-" (but our code can make it)
	if (is_null_tex(name))
		return false;

	if (name.empty())
		return false;

	std::map<SString, std::unique_ptr<Img_c>>::const_iterator P = wad.flats.find(name);

	if (P != wad.flats.end())
		return true;

	if (conf.features.mix_textures_flats)
	{
		std::map<SString, std::unique_ptr<Img_c>>::const_iterator P = wad.textures.find(name);

		if (P != wad.textures.end())
			return true;
	}

	return false;
}


//----------------------------------------------------------------------
//    SPRITE HANDLING
//----------------------------------------------------------------------

static void DeleteSprite(const sprite_map_t::value_type& P)
{
	// Note that P.second can be NULL here
	delete P.second;
}

void Instance::W_ClearSprites()
{
	std::for_each(sprites.begin(), sprites.end(), DeleteSprite);

	sprites.clear();
}


// find sprite by prefix
static Lump_c * Sprite_loc_by_root (const Instance &inst, const SString &name)
{
	// first look for one in the sprite namespace (S_START..S_END),
	// only if that fails do we check the whole wad.

	SString buffer;
	buffer.reserve(16);
	buffer = name;
	if(buffer.length() == 4)
		buffer += 'A';
	if(buffer.length() == 5)
		buffer += '0';

	Lump_c *lump = inst.W_FindSpriteLump(buffer);

	if (! lump)
	{
		if(buffer.length() >= 6)
			buffer[5] = '1';
		lump = inst.W_FindSpriteLump(buffer);
	}

	if (! lump)
	{
		buffer += "D1";
		lump = inst.W_FindSpriteLump(buffer);
	}

	if (lump)
		return lump;

	// check outside of the sprite namespace...

	if (inst.conf.features.lax_sprites)
	{
		buffer = name;
		if(buffer.length() == 4)
			buffer += 'A';
		if(buffer.length() == 5)
			buffer += '0';

		lump = inst.master.findGlobalLump(buffer);

		if (! lump)
		{
			if(buffer.length() >= 6)
				buffer[5] = '1';
			lump = inst.master.findGlobalLump(buffer);
		}

		// TODO: verify lump is OK (size etc)
		if (lump)
		{
			gLog.printf("WARNING: using sprite '%s' outside of S_START..S_END\n", name.c_str());
		}
	}

	if (!lump)
	{
		// Still no lump? Try direct lookup
		// TODO: verify lump is OK (size etc)
		lump = inst.master.findGlobalLump(name);
	}

	return lump;
}


Img_c *Instance::W_GetSprite(int type)
{
	sprite_map_t::const_iterator P = sprites.find(type);

	if (P != sprites.end())
		return P->second;

	// sprite not in the list yet.  Add it.

	const thingtype_t &info = M_GetThingType(type);

	Img_c *result = NULL;

	if (info.desc.startsWith("UNKNOWN"))
	{
		// leave as NULL
	}
	else if (info.sprite.noCaseEqual("_LYT"))
	{
		result = IM_CreateLightSprite();
	}
	else if (info.sprite.noCaseEqual("_MSP"))
	{
		result = IM_CreateMapSpotSprite(0, 255, 0);
	}
	else if (info.sprite.noCaseEqual("NULL"))
	{
		result = IM_CreateMapSpotSprite(70, 70, 255);
	}
	else
	{
		Lump_c *lump = Sprite_loc_by_root(*this, info.sprite);
		if (! lump)
		{
			// for the MBF dog, create our own sprite for it, since
			// it is defined in the Boom definition file and the
			// missing sprite looks ugly in the thing browser.

			if (info.sprite.noCaseEqual("DOGS"))
				result = IM_CreateDogSprite();
			else
				gLog.printf("Sprite not found: '%s'\n", info.sprite.c_str());
		}
		else
		{
			result = new Img_c();

			if (! LoadPicture(*result, lump, info.sprite, 0, 0))
			{
				delete result;
				result = NULL;
			}
		}
	}

	// player color remapping
	// [ FIXME : put colors into game definition file ]
	if (result && info.group == 'p')
	{
		Img_c *new_img = NULL;

		switch (type)
		{
			case 1:
				// no change
				break;

			case 2:
				new_img = result->color_remap(0x70, 0x7f, 0x60, 0x6f);
				break;

			case 3:
				new_img = result->color_remap(0x70, 0x7f, 0x40, 0x4f);
				break;

			case 4:
				new_img = result->color_remap(0x70, 0x7f, 0x20, 0x2f);
				break;

			// blue for the extra coop starts
			case 4001:
			case 4002:
			case 4003:
			case 4004:
				new_img = result->color_remap(0x70, 0x7f, 0xc4, 0xcf);
				break;
		}

		if (new_img)
		{
			std::swap(result, new_img);
			delete new_img;
		}
	}

	// note that a NULL image is OK.  Our renderer will just ignore the
	// missing sprite.

	sprites[type] = result;
	return result;
}


//----------------------------------------------------------------------

static void UnloadTex(const std::map<SString, std::unique_ptr<Img_c>>::value_type& P)
{
	if (P.second != NULL)
		P.second->unload_gl(false);
}

static void UnloadFlat(const std::map<SString, std::unique_ptr<Img_c>>::value_type& P)
{
	if (P.second != NULL)
		P.second->unload_gl(false);
}

static void UnloadSprite(const sprite_map_t::value_type& P)
{
	if (P.second != NULL)
		P.second->unload_gl(false);
}

void Instance::W_UnloadAllTextures() const
{
	std::for_each(wad.textures.begin(), wad.textures.end(), UnloadTex);
	std::for_each(wad.flats.begin(), wad.flats.end(), UnloadFlat);
	std::for_each( sprites.begin(),  sprites.end(), UnloadSprite);

	IM_UnloadDummyTextures();
}

//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
