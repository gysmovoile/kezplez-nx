#include "util.h"


#ifdef LOGGING_ENABLED
const char log_path[256] = "/switch/kezplez-nx/log.txt\0";
#endif

const char keyfile_path[256] = "/switch/kezplez-nx/keys.txt\0";
const char hekate_fusedump_path[256] = "/dumps/fuses.bin\0";
const char hekate_tsecdump_old_path[256] = "/dumps/tsec_key.bin\0";
const char hekate_tsecdump_new_path[256] = "/dumps/tsec_keys.bin\0";

bool prefix_discovered = false;
char hekate_dump_prefix[256];

application_ctx* err_appstate;

bool thread_dummied = false;
FILE* dbg_f = NULL;

//all credit to @shchmue for adding hekate 4.0+ support
char* get_hekate_dump_prefix()
{
	if (!prefix_discovered)
	{
		DIR* dir;
		struct dirent* ent;
		char dirname[256] = "/backup";
		dir = opendir(dirname);
		hekate_dump_prefix[1] = 0x00;
		bool found_backup = false;
		while ((ent = readdir(dir)))
		{
			char statbuf[256];
			strcpy(statbuf, dirname);
			strcat(statbuf, "/");
			strcat(statbuf, ent->d_name);
			
			struct stat sb;
			stat(statbuf, &sb);
			
			if (strcmp(ent->d_name, "dumps") == 0 && S_ISDIR(sb.st_mode)) // /backup/dumps pre-hekate 4.0
			{
				strcpy(hekate_dump_prefix, dirname);
				found_backup = true;
				break;
			}
			else if (S_ISDIR(sb.st_mode)) // /backup/<eMMC serial> post-hekate 4.0
			{
				bool is_valid_emmc_serial = true;
				for (int i = 0; i < strlen(ent->d_name); i++) //checking for a proper hex string
				{
					bool is_number               = (ent->d_name[i] >= 0x30) && (ent->d_name[i] <= 0x39);
					bool is_lowercase_hex_letter = (ent->d_name[i] >= 0x41) && (ent->d_name[i] <= 0x46);
					bool is_uppercase_hex_letter = (ent->d_name[i] >= 0x61) && (ent->d_name[i] <= 0x66);
					if (is_number || is_lowercase_hex_letter || is_uppercase_hex_letter)
					{
						continue;
					}
					else
					{
						is_valid_emmc_serial = false;
					}
				}
				
				if (is_valid_emmc_serial)
				{
					strcat(dirname, "/");
					strcat(dirname, ent->d_name);
					strcpy(hekate_dump_prefix, dirname);
					found_backup = true;
					break;
				}
			}
		}
		closedir(dir);
		if (!found_backup)
		{
			fatal_error("Couldn't find a hekate dump directory!  Please at least dump your tsec keys and fuses before running this application.");
		}
		else
		{
			prefix_discovered = true;
		}
	}
	
	// debug_log(hekate_dump_prefix);
	return hekate_dump_prefix;
}

void prepend_hdp(char* suffix, char* dest)
{
	strcpy(dest, get_hekate_dump_prefix());
	strcat(dest, suffix);
	
	// debug_log(path);
}


Thread* util_thread_func(void (*func)(application_ctx*), application_ctx* appstate)
{
	if (!thread_dummied) { thread_dummy_run(appstate); }
	
	debug_log("setting up thread-");
	Thread* func_thread = malloc(sizeof(Thread));
	
	appstate->thread_started = false;
	
	debug_log("a");
	u64* thread_args = malloc(sizeof(u64*) * 2);
	thread_args[0] = (u64) appstate;
	thread_args[1] = (u64) func;
	
	
	debug_log("b-%08x-%08x-%08x-", thread_args, thread_args[0], thread_args[1]);
	Result rc = threadCreate(func_thread, (void (*)(void*)) thread_tester, thread_args, 0x2000000, 0x2C, -2);
	
	debug_log("c%08x-", rc);
	
	if (rc != 0x0)
	{
		int thread_attempts = 0;
		while (thread_attempts < 20 && rc != 0)
		{
			usleep(50000);
			rc = threadCreate(func_thread, (void (*)(void*)) thread_tester, thread_args, 0x2000000, 0x2C, -2);
			thread_attempts++;
		}
		if (thread_attempts == 20 && rc != 0)
		{
			fatal_error("Unable to create thread, error code: %08x", rc);
			return NULL;
		}
	}
	
	rc = threadStart(func_thread);
	debug_log("c2%08x", rc);
	
	if (rc != 0x0)
	{
		int thread_attempts = 0;
		while (thread_attempts < 20 && rc != 0)
		{
			usleep(50000);
			rc = threadStart(func_thread);
			thread_attempts++;
		}
		if (thread_attempts == 20 && rc != 0)
		{
			fatal_error("Unable to start thread, error code: %08x", rc);
			return NULL;
		}
	}
	
	usleep(250000);
	while(!appstate->thread_started) {  } //Wait until thread has started and made local argument copies to free the arguments

	free(thread_args);
	
	return func_thread;
}

//don't even fuckin ask
void thread_dummy_run(application_ctx* appstate)
{
	Thread* func_thread = malloc(sizeof(Thread));
	
	threadCreate(func_thread, (void (*)(void*)) thread_dummy, NULL, 0x2000000, 0x2C, -2);
	threadStart(func_thread);
	
	usleep(250000);
	
	threadClose(func_thread);
	free(func_thread);
	
	thread_dummied = true;
}

void thread_dummy(void* args)
{
	return;
}

void thread_tester(void* args)
{
	u64* thread_args = (u64*) args;
	
	application_ctx* appstate = (application_ctx*) thread_args[0];
	appstate->step_completed = false;
	appstate->thread_started = true;
	
	void (*func)(application_ctx*) = (void (*)(application_ctx*)) thread_args[1];
	
	usleep(100000);
	debug_log("thread started, received args are-%08x-%08x-%08x\n", thread_args, appstate, func);
	usleep(100000);
	
	func(appstate);
	
	usleep(100000);
	appstate->step_completed = true;
	return;
}

u32 read_u32_le(FILE* targetfile)
{
	u8 buf[4];
	fread(buf, 4, 1, targetfile);
	return (buf[3] << 24) + (buf[2] << 16) + (buf[1] << 8) + (buf[0]);
}

u32 read_u32_be(FILE* targetfile)
{
	u8 buf[4];
	fread(buf, 4, 1, targetfile);
	return (buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + (buf[3]);
}

FILE* safe_open_key_file()
{
	FILE* attempted_keyfile = fopen(keyfile_path, FMODE_APPEND);
	if (attempted_keyfile == NULL)
	{
		attempted_keyfile = fopen(keyfile_path, FMODE_WRITE);
		fclose(attempted_keyfile);
		attempted_keyfile = fopen(keyfile_path, FMODE_APPEND);
	}
	
	return attempted_keyfile;
}

void hactool_init(application_ctx* appstate)
{
	memset(&appstate->tool_ctx, 0, sizeof(appstate->tool_ctx));
	appstate->tool_ctx.action = ACTION_EXTRACT;
	// key init
	FILE* keyfile = safe_fopen(keyfile_path, FMODE_READ);
	
	pki_initialize_keyset(&appstate->tool_ctx.settings.keyset, KEYSET_RETAIL);
	extkeys_initialize_keyset(&appstate->tool_ctx.settings.keyset, keyfile);
	
	appstate->nca_ctx.tool_ctx = &appstate->tool_ctx;
	
	pki_derive_keys(&appstate->tool_ctx.settings.keyset);
	fclose(keyfile);
}


void debug_log_toscreen(application_ctx* appstate, char* log_text, ...)
{

#ifdef LOGGING_ENABLED
	char log_buffer[256]; log_buffer[255] = 0x00;
	va_list args;
	va_start(args, log_text);
	vsnprintf(log_buffer, 255, log_text, args);
	va_end(args);
	
	if (dbg_f == NULL) { dbg_f = safe_fopen(log_path, FMODE_APPEND); }
	if (dbg_f == NULL) { return; }
	
	fwrite(log_buffer, strlen(log_buffer), 1, dbg_f);
	memset(appstate->log_buffer, 0x00, 256);
	strncpy(appstate->log_buffer, log_buffer, 256);
	
	fflush(dbg_f);
#endif

}

void debug_log(char* log_text, ...)
{

#ifdef LOGGING_ENABLED
	char log_buffer[256]; log_buffer[255] = 0x00;
	va_list args;
	va_start(args, log_text);
	vsnprintf(log_buffer, 255, log_text, args);
	va_end(args);

	if (dbg_f == NULL) { dbg_f = safe_fopen(log_path, FMODE_APPEND); }
	if (dbg_f == NULL) { return; }
	
	fwrite(log_buffer, strlen(log_buffer), 1, dbg_f);
	
	fflush(dbg_f);
#endif

}

FILE* safe_fopen(const char* file_path, char* mode)
{
	FILE* targetfile = fopen(file_path, mode);
	
	if (targetfile == NULL)
	{
		targetfile = fopen(file_path, FMODE_WRITE);
		fclose(targetfile);
		targetfile = fopen(file_path, mode);
	}
	if (targetfile == NULL)
	{
		fatal_error("Unable to open file: %s", file_path);
	}
	
	return targetfile;
}

void util_hold_appstate(application_ctx* appstate)
{
	err_appstate = appstate;
}

void fatal_error(char* err_text, ...)
{
	err_appstate->state_id = -1;
	
	char log_buffer[256]; log_buffer[255] = 0x00;
	va_list args;
	va_start(args, err_text);
	vsnprintf(log_buffer, 255, err_text, args);
	va_end(args);
	
	if (dbg_f == NULL) { dbg_f = safe_fopen(log_path, FMODE_APPEND); }
	if (dbg_f != NULL)
	{
		fwrite(log_buffer, strlen(log_buffer), 1, dbg_f);
		memset(err_appstate->log_buffer, 0x00, 256);
		strncpy(err_appstate->log_buffer, log_buffer, 256);
		fflush(dbg_f);
	}
	
	debug_log("loop not done\n");
	while (appletMainLoop())
	{
		hidScanInput();
		u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);
		if (kDown & KEY_PLUS) { break; }
		
		u32 screenwidth;;
		u32 screenheight;
		u32* framebuf_ptr = (u32*) gfxGetFramebuffer(&screenwidth, &screenheight);
		for (int cury = 0; cury < screenheight; cury++)
		{
			for (int curx = 0; curx < screenwidth; curx++)
			{
				int framebuf_targetpos = (cury * screenwidth) + curx;
				framebuf_ptr[framebuf_targetpos] = 0xFF000000;
			}
		}
		font_drawtext(5, 10, 10, 0xFF0000FF, "ERROR");
		font_drawtext(4, 10, 50, 0xFF0000FF, err_appstate->log_buffer);
		gfxFlushBuffers();
		gfxSwapBuffers();
		gfxWaitForVsync();
	}
	debug_log("loop done\n");
	
	font_exit();
	debug_log("font done\n");

	plExit();
	debug_log("pl done\n");
	gfxExit();
	debug_log("gfx done\n");
	setsysExit();
	debug_log("setsys done\n");
	
	exit(0);
	debug_log("applet done\n");
}