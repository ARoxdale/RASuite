//-----------------------------------------------------------------------------
// MEKA
// Sega Master System / Game Gear / SG-1000 / SC-3000 / SF-7000 / ColecoVision emulator
//-----------------------------------------------------------------------------
// Omar Cornut (Bock) & MEKA team 1998-2016
// Sound engine by Hiromitsu Shioya (Hiroshi) in 1998-1999
// Z80 CPU core by Marat Faizullin, 1994-1998
//-----------------------------------------------------------------------------
// MEKA - meka.c
// Entry points and various initialization functions
//-----------------------------------------------------------------------------

#include "shared.h"
#include "app_filebrowser.h"
#include "app_options.h"
#include "app_tileview.h"
#include "bios.h"
#include "blit.h"
#include "blitintf.h"
#include "capture.h"
#include "config.h"
#include "coleco.h"
#include "db.h"
#include "debugger.h"
#include "desktop.h"
#include "effects.h"
#include "fdc765.h"
#include "file.h"
#include "fskipper.h"
#include "glasses.h"
#include "inputs_f.h"
#include "inputs_i.h"
#include "mappers.h"
#include "palette.h"
#include "patch.h"
#include "setup.h"
#include "textbox.h"
#include "tvtype.h"
#include "video.h"
#include "video_m5.h"
#include "vlfn.h"
#ifdef ARCH_WIN32
#include <commctrl.h>
#endif
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_ttf.h>


//-----------------------------------------------------------------------------
// Retro Achievements (Cheevos)
//-----------------------------------------------------------------------------

#include "RA_Resource.h"
#include "RA_Interface.h"
#include "RA_Implementation.h"

char RA_rootDir[2048];

#include <allegro5/allegro_windows.h> //we need to blit ?
#include <allegro5/allegro_native_dialog.h> // Just want a menu that doesn't break everything.
//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------

extern "C" // C-style mangling for ASM linkage
{
OPT_TYPE				opt;
TGFX_TYPE				tgfx;
SMS_TYPE				sms;
TSMS_TYPE				tsms;
}

t_machine				g_machine;
t_meka_env				g_env;
t_media_image			g_media_rom;			// FIXME-WIP: Not fully used
t_meka_configuration	g_configuration;

extern "C"	// C-style mangling for ASM linkage
{
u8      RAM[0x10000];               // RAM
u8      SRAM[0x8000];               // Save RAM
u8      VRAM[0x4000];               // Video RAM
u8      PRAM[0x40];                 // Palette RAM
u8 *    ROM;                        // Emulated ROM
u8 *    Game_ROM;                   // Cartridge ROM
u8 *    Game_ROM_Computed_Page_0;   // Cartridge ROM computed first page
u8 *    Mem_Pages [8];              // Pointer to memory pages
}

ALLEGRO_DISPLAY*		g_display = NULL;
ALLEGRO_EVENT_QUEUE*	g_display_event_queue = NULL;
ALLEGRO_LOCKED_REGION*	g_screenbuffer_locked_region = NULL;
ALLEGRO_BITMAP*         g_screenbuffer_locked_buffer = NULL;
int						g_screenbuffer_format = 0;
int						g_gui_buffer_format = 0;

ALLEGRO_BITMAP *screenbuffer = NULL, *screenbuffer_next = NULL;
ALLEGRO_BITMAP *screenbuffer_1 = NULL, *screenbuffer_2 = NULL;
ALLEGRO_BITMAP *fs_out = NULL;
ALLEGRO_BITMAP *gui_buffer = NULL;
ALLEGRO_BITMAP *gui_background = NULL;

// Note: non floating-point constructors seems to fail at static construction.
ALLEGRO_COLOR COLOR_BLACK = al_map_rgb_f(0.0f,0.0f,0.0f);
ALLEGRO_COLOR COLOR_WHITE = al_map_rgb_f(1.0f,1.0f,1.0f);
ALLEGRO_COLOR COLOR_DEBUG_BACKDROP = al_map_rgb_f(222.0f/255.0f,222.0f/255.0f,101.0f/255.0f);

//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// FIXME: this function is pretty old and is basically a left-over or
// everything that was not moved elsewhere.
//-----------------------------------------------------------------------------
static void Init_Emulator()
{
    Video_Init();

    memset(RAM, 0, 0x10000);        // RAM: 64 Kb (max=SF-7000)
    memset(SRAM, 0, 0x8000);        // SRAM: 32 Kb (max)
    memset(VRAM, 0, 0x4000);        // VRAM: 16 Kb
    memset(PRAM, 0, 0x0040);        // PRAM: 64 bytes
    ROM = Game_ROM_Computed_Page_0 = (u8*)Memory_Alloc(0x4000); // 16 kbytes (one page)
    memset(Game_ROM_Computed_Page_0, 0, 0x4000);
    Game_ROM = NULL;

    tsms.Pages_Count_8k = 1;
    tsms.Pages_Count_16k = 1;

    RdZ80 = RdZ80_NoHook = Read_Default;
    drv_set (g_machine.driver_id);
}

static void Init_LookUpTables()
{
    Coleco_Init_Table_Inputs();
	Mapper_InitializeLookupTables();
}

// Initialize default configuration settings
static void Init_Default_Values()
{
    g_env.debug_dump_infos = false;

    // IPeriod
    opt.IPeriod = opt.Cur_IPeriod = 228;
    opt.IPeriod_Coleco = 228; // 215
    opt.IPeriod_Sg1000_Sc3000 = 228;

    opt.Layer_Mask = LAYER_BACKGROUND | LAYER_SPRITES;

    opt.GUI_Inited = false;
    opt.Current_Key_Pressed = 0;
    opt.State_Current = 0;
    opt.State_Load = -1;
    opt.Setup_Interactive_Execute = false;
    opt.Force_Quit = false;

    // Machine
    g_machine.driver_id = DRV_SMS;

	g_configuration.loaded_configuration_file	  = false;

    g_configuration.country                       = COUNTRY_EXPORT;
    g_configuration.sprite_flickering             = SPRITE_FLICKERING_AUTO;
    g_configuration.enable_BIOS                   = true;
    g_configuration.show_product_number           = false;
    g_configuration.show_fullscreen_messages      = true;
    g_configuration.enable_NES                    = false;
    g_configuration.allow_opposite_directions     = false;
    g_configuration.start_in_gui                  = true;

    // Applet: Game Screen
    g_configuration.game_window_scale             = 1.0f;

    // Applet: File Browser
    g_configuration.fb_close_after_load           = false;
    g_configuration.fb_uses_DB                    = true;
    g_configuration.fullscreen_after_load         = true;

    // Applet: Debugger
    g_configuration.debugger_disassembly_display_labels = true;
    g_configuration.debugger_log_enabled          = true;

    // Applet: Memory Editor
    g_configuration.memory_editor_lines				= 16;
    g_configuration.memory_editor_columns			= 16;

    // Video
	g_configuration.video_driver					= g_video_driver_default;
	g_configuration.video_fullscreen				= false;
	g_configuration.video_game_format_request		= ALLEGRO_PIXEL_FORMAT_ANY_16_NO_ALPHA;
	g_configuration.video_gui_format_request		= ALLEGRO_PIXEL_FORMAT_ANY_NO_ALPHA;

	g_configuration.video_mode_game_vsync			= false;
    g_configuration.video_mode_gui_res_x			= 1024;
    g_configuration.video_mode_gui_res_y			= 768;
    g_configuration.video_mode_gui_refresh_rate		= 0;    // Auto
    g_configuration.video_mode_gui_vsync			= false;

	// Capture
	g_configuration.capture_filename_template		= "%s-%02d.png";
	g_configuration.capture_crop_scrolling_column	= true;
	g_configuration.capture_crop_align_8x8			= false;
	g_configuration.capture_include_gui				= true;

	// Fonts
	g_configuration.font_menus						= FONTID_LARGE; // FONTID_CRISP;
	g_configuration.font_messages					= FONTID_PROGGY_CLEAN; //FONTID_PROGGYCLEAN;
	g_configuration.font_options					= FONTID_PROGGY_CLEAN; //FONTID_PROGGYCLEAN;
	g_configuration.font_debugger					= FONTID_PROGGY_SQUARE; //FONTID_PROGGYCLEAN;
	g_configuration.font_documentation				= FONTID_PROGGY_CLEAN; //FONTID_PROGGYCLEAN;
	g_configuration.font_techinfo					= FONTID_PROGGY_SQUARE; //FONTID_PROGGYCLEAN;
	g_configuration.font_filebrowser				= FONTID_PROGGY_CLEAN; //FONTID_PROGGYCLEAN;
	g_configuration.font_about						= FONTID_PROGGY_CLEAN; //FONTID_PROGGYCLEAN;

    // Media
    // FIXME: yet not fully used
    g_media_rom.type        = MEDIA_IMAGE_ROM;
    g_media_rom.data        = NULL;
    g_media_rom.data_size   = 0;
    g_media_rom.mekacrc.v[0]= 0;
    g_media_rom.mekacrc.v[1]= 0;
    g_media_rom.crc32       = 0;

    g_machine_pause_requests = 0;

    Blitters_Init_Values();
    Frame_Skipper_Init_Values();

    strcpy(FB.current_directory, ".");

    TB_Message_Init_Values();
    Sound_Init_Config();
    TVType_Init_Values();
    Glasses_Init_Values();
    TileViewer_Init_Values();
    Skins_Init_Values();

    #ifdef MEKA_Z80_DEBUGGER
        Debugger_Init_Values();
    #endif
}

static void Free_Memory (void)
{
    free(Game_ROM_Computed_Page_0);
    BIOS_Free_Roms();
    if (Game_ROM)
    {
        free(Game_ROM);
        Game_ROM = NULL;
    }
}

static void Close_Emulator (void)
{
    Sound_Close         ();
    Desktop_Close       ();
    Fonts_Close         ();
    FDC765_Close        ();
    Palette_Close       ();
    Inputs_Sources_Close();
#ifdef MEKA_JOYPAD
	Inputs_Joystick_Close();
#endif
    GUI_Close           ();
    Free_Memory         ();
    FB_Close            ();
    DB_Close            ();
    Blitters_Close      ();
    Glasses_Close       ();
    Data_Close          ();
}

// Remove Allegro installed callback
// This is the first function to call on shutdown, to avoid getting called
// during the shutdown process (which sometimes makes things crash).
/*
static void Close_Callback (void)
{
    al_set_close_button_callback(NULL);
}
*/

// Change to starting directory
// This function is registered in the atexit() table to be called on quit
static void Close_Emulator_Starting_Dir()
{
    chdir(g_env.Paths.StartingDirectory);
}

static int Init_Allegro()
{
    ConsolePrint(Msg_Get(MSG_Init_Allegro));

    // Initialize timer BEFORE allegro
    // OSD_Timer_Initialize();

    al_init();
	al_init_image_addon();
	al_init_font_addon();
	al_init_ttf_addon();
	al_init_primitives_addon();

    // Keyboard, mouse
    al_install_keyboard();
    g_env.mouse_installed = al_install_mouse();

	// Create event queues
	g_display_event_queue = al_create_event_queue();

	// Get Allegro version and print it in console
	const unsigned int allegro_version = al_get_allegro_version();
	ConsolePrintf(" version %d.%d.%d (release %d)\n", (allegro_version >> 24), (allegro_version >> 16) & 0xFF, (allegro_version >> 8) & 0xFF, (allegro_version & 0xFF));

    return (1);
}

static void Close_Allegro()
{
	al_shutdown_primitives_addon();
	al_shutdown_ttf_addon();
	al_shutdown_font_addon();
	al_shutdown_image_addon();

    al_uninstall_mouse();
	al_uninstall_audio();
	al_uninstall_system();
}

static void Init_GUI(void)
{
    ConsolePrintf ("%s\n", Msg_Get(MSG_Init_GUI));
    GUI_Init();
}

// MAIN FUNCTION --------------------------------------------------------------
int main(int argc, char **argv)
{
    #ifdef ARCH_WIN32
        // Need for XP manifest stuff
#pragma comment(lib, "comctl32.lib")
        InitCommonControls();
    #endif

#ifdef ARCH_WIN32
    //_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif

    ConsoleInit(); // First thing to do
    #ifdef ARCH_WIN32
    //RA
	//RA_Init(ConsoleHWND(), RA_Meka, "0.000535"); //faking as RA_Gens otherwise RA_Integration will crash on unknown client

	//Run init here or meka will screw up the directories for loading libraries, etc.

	//HWND allegroHWND = al_get_win_window_handle(g_display);
	//RA_Init(allegroHWND, RA_Meka, "0.00535");
	
	//RA Setup Code
	{ 
		char meka_currDir[2048];
		GetCurrentDirectory(2048, meka_currDir); // "where'd you get the multithreaded code, Ted?"
		
		//See Meka.rc for Console parameters.
		//disable close window for console because I am lazy.
		EnableMenuItem(GetSystemMenu(ConsoleHWND(), FALSE), SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);


		//Make placeholder menu for console (We will put the RA menu on the console because this is the easiest way for me right now)
		HMENU MainMenu = CreateMenu();

		HMENU RAMenu = CreatePopupMenu();
		AppendMenu(RAMenu, MF_POPUP | MF_STRING, 100001, "(RA Not Yet Loaded)");
		AppendMenu(MainMenu, MF_STRING | MF_POPUP, (UINT)RAMenu, "&RetroAchievements");
		SetMenu(ConsoleHWND(), MainMenu);
		InvalidateRect(ConsoleHWND(), NULL, TRUE);
		DrawMenuBar(ConsoleHWND());

		RA_Init(ConsoleHWND(), RA_Meka, "0.000535");
		//RA_Init(ConsoleHWND(), RA_Gens, "0.500535");

		RA_InitShared();
		RA_InitDirectX();
		RA_UpdateAppTitle("");


		RebuildMenu();
		RA_HandleHTTPResults();
		RA_AttemptLogin(); // Doesn't RA_AttemptLogin() call RebuildMenu() anyway at some point in its code?

		RebuildMenu();


		GetCurrentDirectory(2048, RA_rootDir); // "Father Todd Unctious?"

		SetCurrentDirectory(meka_currDir); // "Cowboys Ted! They're a bunch of cowboys!"
	}
	


	/*{ //(What's this all for?)

		//##RA
		while (RA_HTTPRequestExists("requestlogin.php"))
		{
			RA_HandleHTTPResults();
			Sleep(16);
		}
	}
	RA_HandleHTTPResults();
	*/

       /*
		HMENU MainMenu;
		HMENU AboutMenu;
		

		MainMenu = CreateMenu();
		AboutMenu = CreatePopupMenu();

		AppendMenu(AboutMenu, MF_STRING, 100001, "&About Dud");
		AppendMenu(MainMenu, MF_STRING | MF_POPUP, (UINT)AboutMenu, "&About");
		SetMenu(ConsoleHWND(), MainMenu);

		ConsolePrintf("%s (built %s %s)\n(c) %s %s\n--\n", MEKA_NAME_VERSION, MEKA_BUILD_DATE, MEKA_BUILD_TIME, MEKA_DATE, MEKA_AUTHORS);
		SetWindowText(ConsoleHWND(), "Testing Meka RA Build Really");

		//disable close window for console because I am lazy
		EnableMenuItem(GetSystemMenu(ConsoleHWND(), FALSE), SC_CLOSE,MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
		DrawMenuBar(ConsoleHWND());*/
		/*

		Flags = MF_BYPOSITION | MF_POPUP | MF_STRING;

		MENU_L(MainMenu, 0, AboutMenu, (UINT)AboutMenu, "About", "", "&About");

		Flags = MF_BYPOSITION | MF_STRING;
		MENU_L(AboutMenu, 0, Flags, 10001, "About Dud", "", "&About Dud");
		InsertMenu(AboutMenu, 1, MF_SEPARATOR, NULL, NULL);
		*/

		
		ConsolePrintf("%s (built %s %s)\n(c) %s %s\n--\n", MEKA_NAME_VERSION, MEKA_BUILD_DATE, MEKA_BUILD_TIME, MEKA_DATE, MEKA_AUTHORS);

	#else
        ConsolePrintf ("\n%s (c) %s %s\n--\n", MEKA_NAME_VERSION, MEKA_DATE, MEKA_AUTHORS);
    #endif



    // Wait for Win32 console signal
    if (!ConsoleWaitForAnswer(true))
        return (0);


    // Save command line parameters
    g_env.argc = argc;
    g_env.argv = (char**)malloc (sizeof(char *) * (g_env.argc + 1));
	int i;
    for (i = 0; i != g_env.argc; i++)
        g_env.argv[i] = strdup(argv[i]);
    g_env.argv[i] = NULL;

    // FIXME: add 'init system' here

    // Initializations
    g_env.state = MEKA_STATE_INIT;
    Filenames_Init         (); // Set Filenames Values
    Messages_Init          (); // Load MEKA.MSG and init messaging system
    Init_Default_Values    (); // Set Defaults Variables
    Command_Line_Parse     (); // Parse Command Line (1)
    Init_Allegro           (); // Initialize Allegro Library
	Fonts_Init             (); // Initialize Fonts system
	Capture_Init           (); // Initialize Screen capture
    Configuration_Load     (); // Load Configuration File
    atexit (Close_Emulator_Starting_Dir);

    Setup_Interactive_Init (); // Show Interactive Setup if asked to
    Frame_Skipper_Init     (); // Initialize Auto Frame Skipper
    DB_Init                (g_env.Paths.DataBaseFile); // Initialize and load DataBase file
    Patches_List_Init      (); // Load Patches List
    VLFN_Init              (); // Load Virtual Long Filename List
    Skins_Init             (); // Load Skin List
    Blitters_Init          (); // Load Blitter List
    Inputs_Init            (); // Initialize Inputs and load inputs sources list
    Blit_Init              (); // Initialize Blitter
    Random_Init            (); // Initialize Random Number Generator
	Effects_TV_Init        ();	// Initialize TV snow effect
    FDC765_Init            (); // Initialize Floppy Disk emulation
    Data_Init              (); // Load datafile
    Init_Emulator          (); // Initialize Emulation
    Palette_Init           (); // Initialize Palette system
    Init_LookUpTables	   (); // Initialize Look-up tables
    Machine_Init           (); // Initialize Virtual Machine

    Sound_Init             (); // Initialize Sound
#ifdef MEKA_JOYPAD
    Inputs_Joystick_Init   (); // Initialize Joysticks. 
#endif

    // Initialization complete
    ConsolePrintf ("%s\n--\n", Msg_Get(MSG_Init_Completed));


	HWND MekaWND = al_get_win_window_handle(g_display);



	// Save configuration file early on (so that bad drivers, will still create a default .cfg file etc.)
	if (!g_configuration.loaded_configuration_file)
		Configuration_Save();

	// Setup display (fullscreen/GUI)
    if ((g_machine_flags & MACHINE_RUN) == MACHINE_RUN && !g_configuration.start_in_gui)
        g_env.state = MEKA_STATE_GAME;
    else
        g_env.state = MEKA_STATE_GUI;
    Video_Setup_State();

	/*HWND allegro_window = al_get_win_window_handle(g_display);

	HMENU MainMenu;
	HMENU AboutMenu;


	MainMenu = CreateMenu();
	AboutMenu = CreatePopupMenu();

	AppendMenu(AboutMenu, MF_STRING, 100001, "&About Dud");
	AppendMenu(MainMenu, MF_STRING | MF_POPUP, (UINT)AboutMenu, "&About");
	//SetMenu(allegro_window, MainMenu);
	
	ConsolePrintf("%s (built %s %s)\n(c) %s %s\n--\n", MEKA_NAME_VERSION, MEKA_BUILD_DATE, MEKA_BUILD_TIME, MEKA_DATE, MEKA_AUTHORS);
	SetWindowText(allegro_window, "Testing Meka RA Build Really");
	*/

	/*
	ALLEGRO_MENU *menu = al_create_menu();
	ALLEGRO_MENU *file_menu = al_create_menu();
	al_append_menu_item(file_menu, "Exit", 1, 0, NULL, NULL);
	al_append_menu_item(menu, "File", 0, 0, NULL, file_menu);
	al_set_display_menu(g_display, menu);
	*/

    Machine_Reset          (); // Reset Emulated Machine (set default values)
	Init_GUI               (); // Initialize Graphical User Interface
	FB_Init_2              (); // Finish initializing the file browser

    // Load ROM from command line if necessary
    Load_ROM_Command_Line();

    // Wait for Win32 console signal
    if (!ConsoleWaitForAnswer(true))
        return (0);



    //ConsoleClose(); // Close Console

    // Start main program loop
    // Everything runs from there.
    // Z80_Opcodes_Usage_Reset();


	





    Main_Loop(); 
    // Z80_Opcodes_Usage_Print();




    // Shutting down emulator...
    g_env.state = MEKA_STATE_SHUTDOWN;
    Video_Setup_State      (); // Switch back to text mode
    BMemory_Save           (); // Write Backed Memory if necessary
    Configuration_Save     (); // Write Configuration File
    Write_Inputs_Src_List  (); // Write Inputs Definition File
    VLFN_Close             (); // Write Virtual Long Filename List
    Close_Emulator         (); // Close Emulator
    Show_End_Message       (); // Show End Message

    Messages_Close();
    Close_Allegro();

	//RA
	RA_HandleHTTPResults();
	RA_Shutdown();

    return (0);
}

//-----------------------------------------------------------------------------
