//------------------------------------------------------------------------
//  CONFIG FILE
//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2001-2018 Andrew Apted
//  Copyright (C) 1997-2003 André Majorel et al
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
//  in the public domain in 1994 by Raphaël Quinet and Brendon Wyber.
//
//------------------------------------------------------------------------

#include "Instance.h"

#include "lib_adler.h"
#include "m_config.h"
#include "m_parse.h"
#include "m_streams.h"

//------------------------------------------------------------------------

//
//  Structures for command line arguments and config settings
//
enum class OptType
{
	// End of the options description
	end,

	// Boolean (toggle)
	// Receptacle is of type: bool
	boolean,

	// Integer number,
	// Receptacle is of type: int
	integer,

	// A color value
	// Receptacle is of type: rgb_color_t
	color,

	// String (not leaking)
	// Receptacle is of type: SString
	string,

	// List of strings (not leaking)
	// Receptacle is of type: std::vector<SString>
	stringList,
};

enum
{
	OptFlag_pass1 = 1 << 0,
	OptFlag_helpNewline = 1 << 1,
	OptFlag_preference = 1 << 2,
	OptFlag_warp = 1 << 3,
	OptFlag_hide = 1 << 4,
};

struct opt_desc_t
{
	const char *long_name;  // Command line arg. or keyword
	const char *short_name; // Abbreviated command line argument

	OptType opt_type;    // Type of this option
	unsigned flags;    // Flags for this option :
	// '1' : process only on pass 1 of parse_command_line_options()
	// '<' : print extra newline after this option (when dumping)
	// 'v' : a real variable (preference setting)
	// 'w' : warp hack -- accept two numeric args
	// 'H' : hide option from --help display

	const char *desc;   // Description of the option
	const char *arg_desc;  // Description of the argument (NULL --> none or default)

	void *data_ptr;   // Pointer to the data
};


static const opt_desc_t options[] =
{
	//
	// A few options must be handled in an early pass
	//

	{	"home",
		0,
        OptType::string,
		OptFlag_pass1,
		"Home directory",
		"<dir>",
		&global::home_dir
	},

	{	"install",
		0,
        OptType::string,
		OptFlag_pass1,
		"Installation directory",
		"<dir>",
		&global::install_dir
	},

	{	"log",
		0,
        OptType::string,
		OptFlag_pass1,
		"Log messages to specified file",
		"<file>",
		&global::log_file
	},

	{	"config",
		0,
        OptType::string,
		OptFlag_pass1 | OptFlag_helpNewline,
		"Config file to load / save",
		"<file>",
		&global::config_file
	},

	{	"help",
		"h",
        OptType::boolean,
		OptFlag_pass1,
		"Show usage summary",
		NULL,
		&global::show_help
	},

	{	"version",
		"v",
        OptType::boolean,
		OptFlag_pass1,
		"Show the version",
		NULL,
		&global::show_version
	},

	{	"debug",
		"d",
        OptType::boolean,
		OptFlag_pass1,
		"Enable debugging messages",
		NULL,
		&global::Debugging
	},

	{	"quiet",
		"q",
        OptType::boolean,
		OptFlag_pass1,
		"Quiet mode (no messages on stdout)",
		NULL,
		&global::Quiet
	},

	//
	// Normal options from here on....
	//

	{	"file",
		"f",
        OptType::stringList,
		0,
		"Wad file(s) to edit",
		"<file>...",
		&global::Pwad_list
	},

	{	"merge",
		"m",
		OptType::stringList,
		0,
		"Resource file(s) to load",
		"<file>...",
		&gInstance.loaded.resourceList
	},

	{	"iwad",
		"i",
        OptType::string,
		0,
		"The name of the IWAD (game data)",
		"<file>",
		&gInstance.loaded.iwadName	// TODO: same deal
	},

	{	"port",
		"p",
        OptType::string,
		0,
		"Port (engine) name",
		"<name>",
		&gInstance.loaded.portName	// TODO: same deal
	},

	{	"warp",
		"w",
        OptType::string,
		OptFlag_warp | OptFlag_helpNewline,
		"Select level to edit",
		"<map>",
		&gInstance.loaded.levelName	// TODO: this will need to work only for first instance
	},

	{	"udmftest",
		0,
        OptType::boolean,
		OptFlag_hide,
		"Enable the unfinished UDMF support",
		NULL,
		&global::udmf_testing
	},

	/* ------------ Preferences ------------ */

	{	"auto_load_recent",
		0,
        OptType::boolean,
		OptFlag_preference,
		"When no given files, load the most recent one saved",
		NULL,
		&config::auto_load_recent
	},

	{	"begin_maximized",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Maximize the window when Eureka starts",
		NULL,
		&config::begin_maximized
	},

	{	"backup_max_files",
		0,
        OptType::integer,
		OptFlag_preference,
		"Maximum copies to make when backing up a wad",
		NULL,
		&config::backup_max_files
	},

	{	"backup_max_space",
		0,
        OptType::integer,
		OptFlag_preference,
		"Maximum space to use (in MB) when backing up a wad",
		NULL,
		&config::backup_max_space
	},

	{	"browser_combine_tex",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Combine flats and textures in a single browser",
		NULL,
		&config::browser_combine_tex
	},

	{	"browser_small_tex",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Show smaller (more compact) textures in the browser",
		NULL,
		&config::browser_small_tex
	},

	{	"bsp_on_save",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Node building: always build the nodes after saving",
		NULL,
		&config::bsp_on_save
	},

	{	"bsp_fast",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Node building: enable fast mode (may be lower quality)",
		NULL,
		&config::bsp_fast
	},

	{	"bsp_warnings",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Node building: show all warning messages",
		NULL,
		&config::bsp_warnings
	},

	{	"bsp_split_factor",
		0,
        OptType::integer,
		OptFlag_preference,
		"Node building: seg splitting factor",
		NULL,
		&config::bsp_split_factor
	},

	{	"bsp_gl_nodes",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Node building: build GL-Nodes",
		NULL,
		&config::bsp_gl_nodes
	},

	{	"bsp_force_v5",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Node building: force V5 of GL-Nodes",
		NULL,
		&config::bsp_force_v5
	},

	{	"bsp_force_zdoom",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Node building: force ZDoom format for normal nodes",
		NULL,
		&config::bsp_force_zdoom
	},

	{	"bsp_compressed",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Node building: force zlib compression of ZDoom nodes",
		NULL,
		&config::bsp_compressed
	},

	{	"default_gamma",
		0,
        OptType::integer,
		OptFlag_preference,
		"Default gamma for images and 3D view (0..4)",
		NULL,
		&config::usegamma
	},

	{	"default_edit_mode",
		0,
        OptType::integer,
		OptFlag_preference,
		"Default editing mode: 0..3 = Th / Lin / Sec / Vt",
		NULL,
		&config::default_edit_mode
	},

	{	"default_port",
		0,
        OptType::string,
		OptFlag_preference,
		"Default port (engine) name",
		NULL,
		&config::default_port
	},

	{	"dotty_axis_col",
		0,
        OptType::color,
		OptFlag_preference,
		"axis color for the dotty style grid",
		NULL,
		&config::dotty_axis_col
	},

	{	"dotty_major_col",
		0,
        OptType::color,
		OptFlag_preference,
		"major color for the dotty style grid",
		NULL,
		&config::dotty_major_col
	},

	{	"dotty_minor_col",
		0,
        OptType::color,
		OptFlag_preference,
		"minor color for the dotty style grid",
		NULL,
		&config::dotty_minor_col
	},

	{	"dotty_point_col",
		0,
        OptType::color,
		OptFlag_preference,
		"point color for the dotty style grid",
		NULL,
		&config::dotty_point_col
	},

	{	"floor_bump_small",
		0,
        OptType::integer,
		OptFlag_preference,
		"distance for '+' and '-' buttons in sector panel while SHIFT is pressed",
		NULL,
		&config::floor_bump_small
	},

	{	"floor_bump_medium",
		0,
        OptType::integer,
		OptFlag_preference,
		"distance for '+' and '-' buttons in sector panel without any modifier keys",
		NULL,
		&config::floor_bump_medium
	},

	{	"floor_bump_large",
		0,
        OptType::integer,
		OptFlag_preference,
		"distance for '+' and '-' buttons in sector panel while CTRL is pressed",
		NULL,
		&config::floor_bump_large
	},

	{	"grid_default_mode",
		0,
        OptType::integer,
		OptFlag_preference,
		"Default grid mode: 0 = OFF, 1 = dotty, 2 = normal",
		NULL,
		&config::grid_default_mode
	},

	{	"grid_default_size",
		0,
        OptType::integer,
		OptFlag_preference,
		"Default grid size",
		NULL,
		&config::grid_default_size
	},

	{	"grid_default_snap",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Default grid snapping",
		NULL,
		&config::grid_default_snap
	},

	{	"grid_hide_in_free_mode",
		0,
        OptType::boolean,
		OptFlag_preference,
		"hide the grid in FREE mode",
		NULL,
		&config::grid_hide_in_free_mode
	},

	{	"grid_ratio_high",
		0,
        OptType::integer,
		OptFlag_preference,
		"custom grid ratio : high value (numerator)",
		NULL,
		&config::grid_ratio_high
	},

	{	"grid_ratio_low",
		0,
        OptType::integer,
		OptFlag_preference,
		"custom grid ratio : low value (denominator)",
		NULL,
		&config::grid_ratio_low
	},

	{	"grid_snap_indicator",
		0,
        OptType::boolean,
		OptFlag_preference,
		"show a cross at the grid-snapped location",
		NULL,
		&config::grid_snap_indicator
	},

	{	"grid_style",
		0,
        OptType::integer,
		OptFlag_preference,
		"grid style : 0 = squares, 1 = dotty",
		NULL,
		&config::grid_style
	},

	{	"gui_theme",
		0,
        OptType::integer,
		OptFlag_preference,
		"GUI widget theme: 0 = fltk, 1 = gtk+, 2 = plastic",
		NULL,
		&config::gui_scheme
	},

	{	"gui_color_set",
		0,
        OptType::integer,
		OptFlag_preference,
		"GUI color set: 0 = fltk default, 1 = bright, 2 = custom",
		NULL,
		&config::gui_color_set
	},

	{	"gui_custom_bg",
		0,
        OptType::color,
		OptFlag_preference,
		"GUI custom background color",
		NULL,
		&config::gui_custom_bg
	},

	{	"gui_custom_ig",
		0,
        OptType::color,
		OptFlag_preference,
		"GUI custom input color",
		NULL,
		&config::gui_custom_ig
	},

	{	"gui_custom_fg",
		0,
        OptType::color,
		OptFlag_preference,
		"GUI custom foreground (text) color",
		NULL,
		&config::gui_custom_fg
	},

	{	"highlight_line_info",
		0,
        OptType::integer,
		OptFlag_preference,
		"Info drawn near a highlighted line (0 = nothing)",
		NULL,
		&config::highlight_line_info
	},

	{	"leave_offsets_alone",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Do not adjust offsets when splitting lines (etc)",
		NULL,
		&config::leave_offsets_alone
	},

	{	"light_bump_small",
		0,
        OptType::integer,
		OptFlag_preference,
		"light step for '+' and '-' buttons in sector panel while SHIFT is pressed",
		NULL,
		&config::light_bump_small
	},

	{	"light_bump_medium",
		0,
        OptType::integer,
		OptFlag_preference,
		"light step for '+' and '-' buttons in sector panel without any modifier keys",
		NULL,
		&config::light_bump_medium
	},

	{	"light_bump_large",
		0,
        OptType::integer,
		OptFlag_preference,
		"light step for '+' and '-' buttons in sector panel while CTRL is pressed",
		NULL,
		&config::light_bump_large
	},

	{	"map_scroll_bars",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Enable scroll-bars for the map view",
		NULL,
		&config::map_scroll_bars
	},

	{	"minimum_drag_pixels",
		0,
        OptType::integer,
		OptFlag_preference,
		"Minimum distance to move mouse to drag an object (in pixels)",
		NULL,
		&config::minimum_drag_pixels
	},

	{	"new_sector_size",
		0,
        OptType::integer,
		OptFlag_preference,
		"Size of sector rectangles created outside of the map",
		NULL,
		&config::new_sector_size
	},

	{	"normal_axis_col",
		0,
        OptType::color,
		OptFlag_preference,
		"axis color for the normal grid",
		NULL,
		&config::normal_axis_col
	},

	{	"normal_main_col",
		0,
        OptType::color,
		OptFlag_preference,
		"main color for the normal grid",
		NULL,
		&config::normal_main_col
	},

	{	"normal_flat_col",
		0,
        OptType::color,
		OptFlag_preference,
		"flat color for the normal grid",
		NULL,
		&config::normal_flat_col
	},

	{	"normal_small_col",
		0,
        OptType::color,
		OptFlag_preference,
		"small color for the normal grid",
		NULL,
		&config::normal_small_col
	},

	{	"panel_gamma",
		0,
        OptType::integer,
		OptFlag_preference,
		"Gamma for images in the panels and the browser (0..4)",
		NULL,
		&config::panel_gamma
	},

	{	"render_pix_aspect",
		0,
        OptType::integer,
		OptFlag_preference,
		"Aspect ratio of pixels for 3D view (100 * width / height)",
		NULL,
		&config::render_pixel_aspect
	},

	{	"render_far_clip",
		0,
        OptType::integer,
		OptFlag_preference,
		"Distance of far clip plane for 3D rendering",
		NULL,
		&config::render_far_clip
	},

	{	"render_high_detail",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Use highest detail when rendering 3D view (software mode)",
		NULL,
		&config::render_high_detail
	},

	{	"render_lock_gravity",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Locked gravity in 3D view -- cannot move up or down",
		NULL,
		&config::render_lock_gravity
	},

	{	"render_missing_bright",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Render the missing texture as fullbright",
		NULL,
		&config::render_missing_bright
	},

	{	"render_unknown_bright",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Render the unknown texture as fullbright",
		NULL,
		&config::render_unknown_bright
	},

	{	"same_mode_clears_selection",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Clear the selection when entering the same mode",
		NULL,
		&config::same_mode_clears_selection
	},

	{	"sector_render_default",
		0,
        OptType::integer,
		OptFlag_preference,
		"Default sector rendering mode: 0 = NONE, 1 = floor, 2 = ceiling",
		NULL,
		&config::sector_render_default
	},

	{	"show_full_one_sided",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Show all textures on one-sided lines in the Linedef panel",
		NULL,
		&config::show_full_one_sided
	},

	{	"sidedef_add_del_buttons",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Show the ADD and DEL buttons in Sidedef panels",
		NULL,
		&config::sidedef_add_del_buttons
	},

	{	"thing_render_default",
		0,
        OptType::integer,
		OptFlag_preference,
		"Default thing rendering mode: 0 = boxes, 1 = sprites",
		NULL,
		&config::thing_render_default
	},

	{	"transparent_col",
		0,
        OptType::color,
		OptFlag_preference,
		"color used to represent transparent pixels in textures",
		NULL,
		&config::transparent_col
	},

	{	"swap_sidedefs",
		0,
        OptType::boolean,
		OptFlag_preference,
		"Swap upper and lower sidedefs in the Linedef panel",
		NULL,
		&config::swap_sidedefs
	},

	//
	// That's all there is
	//

	{	0,
		0,
        OptType::end,
		0,
		0,
		0,
		0
	}
};


//------------------------------------------------------------------------

//
// Parse config line from file
//
static int parse_config_line_from_file(const SString &cline, const SString &basename, int lnum)
{
	SString line(cline);

	// skip leading whitespace
	line.trimLeadingSpaces();

	// skip comments
	if(line[0] == '#')
		return 0;

	// remove trailing newline and whitespace
	line.trimTrailingSpaces();

	// skip empty lines
	if(line.empty())
		return 0;

	// grab the name
	size_t pos = line.find_first_not_of(IDENT_SET);
	if(pos == std::string::npos || !isspace(line[pos]))
	{
		gLog.printf("WARNING: %s(%u): bad line, no space after keyword.\n",
					basename.c_str(), lnum);
		return 0;
	}

	SString value;
	line.cutWithSpace(pos, &value);

	// find the option value (occupies rest of the line)
	value.trimLeadingSpaces();
	if(value.empty())
	{
		gLog.printf("WARNING: %s(%u): bad line, missing option value.\n",
					basename.c_str(), lnum);
		return 0;
	}

	// find the option keyword
	const opt_desc_t * opt;

	for (opt = options ; ; opt++)
	{
		if (opt->opt_type == OptType::end)
		{
			gLog.printf("WARNING: %s(%u): invalid option '%s', skipping\n",
						basename.c_str(), lnum, line.c_str());
			return 0;
		}

		if(!opt->long_name || line != opt->long_name)
			continue;

		// pre-pass options (like --help) don't make sense in a config file
		if (opt->flags & OptFlag_pass1)
		{
			gLog.printf("WARNING: %s(%u): cannot use option '%s' in config "
						"files.\n", basename.c_str(), lnum, line.c_str());
			return 0;
		}

		// found it
		break;
	}

	switch (opt->opt_type)
	{
        case OptType::boolean:
			if(value.noCaseEqual("no") || value.noCaseEqual("false") ||
			   value.noCaseEqual("off") || value.noCaseEqual("0"))
			{
				*((bool *) (opt->data_ptr)) = false;
			}
			else  // anything else is TRUE
			{
				*((bool *) (opt->data_ptr)) = true;
			}
			break;

        case OptType::integer:
			*((int *) opt->data_ptr) = atoi(value);
			break;

        case OptType::color:
			*((rgb_color_t *) opt->data_ptr) = ParseColor(value);
			break;

        case OptType::string:
			*static_cast<SString *>(opt->data_ptr) = value;
			break;

        case OptType::stringList:
			while(value.good())
			{
				size_t spacepos = value.findSpace();
				auto list = static_cast<std::vector<SString> *>(opt->data_ptr);
				if(spacepos == std::string::npos)
				{
					list->push_back(value);
					value.clear();
				}
				else
				{
					SString word = value;
					word.erase(spacepos, SString::npos);
					list->push_back(word);

					value.erase(0, spacepos);
					value.trimLeadingSpaces();
				}
			}
			break;

		default:
			BugError("INTERNAL ERROR: unknown option type %d\n", (int) opt->opt_type);
			return -1;
	}

	return 0;  // OK
}


//
//  try to parse a config file by pathname.
//
//  Return 0 on success, negative value on failure.
//
static int parse_a_config_file(std::istream &is, const SString &filename)
{
	SString basename = GetBaseName(filename);

	// handle one line on each iteration
	SString line;
	for(int lnum = 1; M_ReadTextLine(line, is); lnum++)
	{
		int ret = parse_config_line_from_file(line, basename, lnum);

		if (ret != 0)
			return ret;
	}

	return 0;  // OK
}


inline static SString default_config_file() noexcept(false)
{
	if(global::home_dir.empty())
		ThrowException("Home directory not set.");

	return global::home_dir + "/config.cfg";
}


//
//  parses the config file (either a user-specific one or the default one).
//
//  return 0 on success, negative value on error.
//
int M_ParseConfigFile() noexcept(false)
{
	if (global::config_file.empty())
	{
		global::config_file = default_config_file();
	}

	std::ifstream is(global::config_file.get());

	gLog.printf("Reading config file: %s\n", global::config_file.c_str());

	if (!is.is_open())
	{
		gLog.printf("--> %s\n", GetErrorMessage(errno).c_str());
		return -1;
	}

	return parse_a_config_file(is, global::config_file);
}


int M_ParseDefaultConfigFile()
{
	SString filename = global::install_dir + "/defaults.cfg";

	std::ifstream is(filename.get());

	gLog.printf("Reading config file: %s\n", filename.c_str());

	if (!is.is_open())
	{
		gLog.printf("--> %s\n", GetErrorMessage(errno).c_str());
		return -1;
	}

	return parse_a_config_file(is, filename);
}


//
// check certain environment variables...
//
void M_ParseEnvironmentVars()
{
#if 0
	char *value;

	value = getenv ("EUREKA_GAME");
	if (value != NULL)
		Game = value;
#endif
}


static void M_AddPwadName(const char *filename)
{
	global::Pwad_list.push_back(filename);
}


//
// parses the command line options
//
// If <pass> is set to 1, ignores all options except those
// that have the "1" flag (the "early" options).
//
// Otherwise, ignores all options that have the "1" flag.
//
void M_ParseCommandLine(int argc, const char *const *argv, CommandLinePass pass)
{
	const opt_desc_t *o;

	while (argc > 0)
	{
		bool ignore;

		// is it actually an option?
		if (argv[0][0] != '-')
		{
			// this is a loose file, handle it now
			if (pass == CommandLinePass::normal)
				M_AddPwadName(argv[0]);

			argv++;
			argc--;
			continue;
		}

		// Which option is this?
		for (o = options; ; o++)
		{
			if (o->opt_type == OptType::end)
			{
				ThrowException("unknown option: '%s'\n", argv[0]);
				/* NOT REACHED */
			}

			if ( (o->short_name && strcmp (argv[0]+1, o->short_name) == 0) ||
				 (o->long_name  && strcmp (argv[0]+1, o->long_name ) == 0) ||
				 (o->long_name  && argv[0][1] == '-' && strcmp (argv[0]+2, o->long_name ) == 0) )
				break;
		}

		// ignore options which are not meant for this pass
		ignore = !!(o->flags & OptFlag_pass1) !=
				(pass == CommandLinePass::early);

		switch (o->opt_type)
		{
            case OptType::boolean:
				// -AJA- permit a following value
				if (argc >= 2 && argv[1][0] != '-')
				{
					argv++;
					argc--;

					if (ignore)
						break;

					if (y_stricmp(argv[0], "no")    == 0 ||
					    y_stricmp(argv[0], "false") == 0 ||
						y_stricmp(argv[0], "off")   == 0 ||
						y_stricmp(argv[0], "0")     == 0)
					{
						*((bool *) (o->data_ptr)) = false;
					}
					else  // anything else is TRUE
					{
						*((bool *) (o->data_ptr)) = true;
					}
				}
				else if (! ignore)
				{
					*((bool *) o->data_ptr) = true;
				}
				break;

            case OptType::integer:
				if (argc < 2)
				{
					ThrowException("missing argument after '%s'\n", argv[0]);
					/* NOT REACHED */
				}

				argv++;
				argc--;

				if (! ignore)
				{
					*((int *) o->data_ptr) = atoi(argv[0]);
				}
				break;

            case OptType::color:
				if (argc < 2)
				{
					ThrowException("missing argument after '%s'\n", argv[0]);
					/* NOT REACHED */
				}

				argv++;
				argc--;

				if (! ignore)
				{
					*((rgb_color_t *) o->data_ptr) = ParseColor(argv[0]);
				}
				break;

            case OptType::string:
				if (argc < 2)
				{
					ThrowException("missing argument after '%s'\n", argv[0]);
					/* NOT REACHED */
				}
				argv++;
				argc--;
				if(!ignore)
					*static_cast<SString *>(o->data_ptr) = argv[0];
				// support two numeric values after -warp
				if (o->flags & OptFlag_warp && isdigit(argv[0][0]) &&
					argc > 1 && isdigit(argv[1][0]))
				{
					if (! ignore)
					{
						*static_cast<SString *>(o->data_ptr) = SString(argv[0]) + argv[1];
					}

					argv++;
					argc--;
				}

				break;


            case OptType::stringList:
				if (argc < 2)
				{
					ThrowException("missing argument after '%s'\n", argv[0]);
					/* NOT REACHED */
				}
				while (argc > 1 && argv[1][0] != '-' && argv[1][0] != '+')
				{
					argv++;
					argc--;

					if (! ignore)
					{
						auto list = static_cast<std::vector<SString> *>(o->data_ptr);
						list->push_back(argv[0]);
					}
				}
				break;

			default:
				BugError("INTERNAL ERROR: unknown option type (%d)\n", (int) o->opt_type);
				/* NOT REACHED */
		}

		argv++;
		argc--;
	}
}


//
// print a list of all command line options (usage message).
//
void M_PrintCommandLineOptions()
{
	const opt_desc_t *o;
	int name_maxlen = 0;

	for (o = options; o->opt_type != OptType::end; o++)
	{
		int len;

		if (o->flags & (OptFlag_preference | OptFlag_hide))
			continue;

		if (o->long_name)
		{
			len = (int)strlen (o->long_name);
			name_maxlen = std::max(name_maxlen, len);
		}

		if (o->arg_desc)
			len = (int)strlen (o->arg_desc);
	}

	for (int pass = 0 ; pass < 2 ; pass++)
	for (o = options; o->opt_type != OptType::end; o++)
	{
		if (o->flags & (OptFlag_preference | OptFlag_hide))
			continue;

		if ((o->flags & OptFlag_pass1 ? 1 : 0) != pass)
			continue;

		if (o->short_name)
			printf ("  -%-3s ", o->short_name);
		else
			printf ("       ");

		if (o->long_name)
			printf ("--%-*s   ", name_maxlen, o->long_name);
		else
			printf ("%*s  ", name_maxlen + 2, "");

		if (o->arg_desc)
			printf ("%-12s", o->arg_desc);
		else switch (o->opt_type)
		{
            case OptType::boolean:       printf ("            "); break;
            case OptType::integer:       printf ("<value>     "); break;
            case OptType::color:         printf ("<color>     "); break;

            case OptType::string:      printf ("<string>    "); break;
            case OptType::stringList:   printf ("<string> ..."); break;
            case OptType::end: ;  // This line is here only to silence a GCC warning.
		}

		printf (" %s\n", o->desc);

		if (o->flags & OptFlag_helpNewline)
			printf ("\n");
	}
}


int M_WriteConfigFile()
{
	if(global::config_file.empty())
		ThrowException("Configuration file not initialized.");

	gLog.printf("Writing config file: %s\n", global::config_file.c_str());

	std::ofstream os(global::config_file.get(), std::ios::trunc);

	if (! os.is_open())
	{
		gLog.printf("--> %s\n", GetErrorMessage(errno).c_str());
		return -1;
	}
	os << "# Eureka configuration (local)\n";

	const opt_desc_t *o;

	for (o = options; o->opt_type != OptType::end; o++)
	{
		if (!(o->flags & OptFlag_preference))
			continue;

		if (! o->long_name)
			continue;

		os << o->long_name << ' ';

		switch (o->opt_type)
		{
            case OptType::boolean:
				os << (*((bool *)o->data_ptr) ? "1" : "0");
				break;

            case OptType::string:
			{
				const SString *str = static_cast<SString *>(o->data_ptr);
				os << (str ? *str : "''");
				break;
			}
            case OptType::integer:
				os << *((int *)o->data_ptr);
				break;

            case OptType::color:
				os << SString::printf("%06x", *((rgb_color_t *)o->data_ptr) >> 8);
				break;

            case OptType::stringList:
			{
				auto list = static_cast<std::vector<SString> *>(o->data_ptr);

				if (list->empty())
					os << "{}";
				else for (const SString &item : *list)
					os << item << ' ';
			}

			default:
				break;
		}

		os << '\n';
	}

	return 0;  // OK
}


//------------------------------------------------------------------------
//   USER STATE HANDLING
//------------------------------------------------------------------------

static SString PersistFilename(const crc32_c& crc)
{
	return SString::printf("%s/cache/%08X%08X.dat", global::cache_dir.c_str(), crc.extra, crc.raw);
}


#define MAX_TOKENS  10


bool Instance::M_LoadUserState()
{
	crc32_c crc;

	level.getLevelChecksum(crc);

	SString filename = PersistFilename(crc);

	LineFile file(filename);
	if (! file.isOpen())
		return false;

	gLog.printf("Loading user state from: %s\n", filename.c_str());

	SString line;

	std::vector<SString> tokens;

	while (file.readLine(line))
	{
		int num_tok = M_ParseLine(line, tokens, ParseOptions::haveStrings);

		if (num_tok == 0)
			continue;

		if (num_tok < 0)
		{
			gLog.printf("Error in persistent data: %s\n", line.c_str());
			continue;
		}

		if (  Editor_ParseUser(tokens) ||
		        Grid_ParseUser(tokens) ||
		    Render3D_ParseUser(tokens) ||
		     Browser_ParseUser(*this, tokens) ||
		       Props_ParseUser(*this, tokens) ||
		     RecUsed_ParseUser(tokens))
		{
			// Ok
		}
		else
		{
			gLog.printf("Unknown persistent data: %s\n", line.c_str());
		}
	}

	file.close();

	Props_LoadValues(*this);

	return true;
}

//
// user state persistence (stuff like camera pos, grid settings, ...)
//
bool Instance::M_SaveUserState() const
{
	crc32_c crc;

	level.getLevelChecksum(crc);

	SString filename = PersistFilename(crc);

	gLog.printf("Save user state to: %s\n", filename.c_str());

	std::ofstream os(filename.get(), std::ios::trunc);

	if (! os.is_open())
	{
		gLog.printf("--> FAILED! (%s)\n", GetErrorMessage(errno).c_str());
		return false;
	}

	Editor_WriteUser(os);
	Grid_WriteUser(os);
	Render3D_WriteUser(os);
	Browser_WriteUser(os);
	Props_WriteUser(os);
	RecUsed_WriteUser(os);

	return true;
}


void Instance::M_DefaultUserState()
{
	grid.Init();

	ZoomWholeMap();

	Render3D_Setup();

	Editor_DefaultState();
}


//--- editor settings ---
// vi:ts=4:sw=4:noexpandtab
