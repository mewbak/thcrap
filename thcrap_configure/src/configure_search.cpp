/**
  * Touhou Community Reliant Automatic Patcher
  * Cheap command-line patch stack configuration tool
  *
  * ----
  *
  * Game searching front-end code.
  */

#include <thcrap.h>
#include "configure.h"

static const char games_js_fn[] = "config/games.js";

static const char* ChooseLocation(const char *id, json_t *locs)
{
	size_t num_versions = json_object_size(locs);
	if(num_versions == 1) {
		const char *loc = json_object_iter_key(json_object_iter(locs));
		const char *variety = json_object_get_string(locs, loc);

		log_printf("Found %s (%s) at %s\n", id, variety, loc);

		return loc;
	} else if(num_versions > 1) {
		const char *loc;
		json_t *val;
		size_t i = 0;
		size_t loc_num;

		log_printf("Found %d versions of %s:\n\n", num_versions, id);

		json_object_foreach(locs, loc, val) {
			++i;
			con_clickable(i); log_printf(" [%2d] %s: %s\n", i, loc, json_string_value(val));
		}
		printf("\n");
		while(1) {
			char buf[16];
			con_printf("Pick a version to run the patch on: (1 - %u): ", num_versions);

			console_read(buf, sizeof (buf));
			if(
				(sscanf(buf, "%u", &loc_num) == 1) &&
				(loc_num <= num_versions) &&
				(loc_num >= 1)
			) {
				break;
			}
		}
		i = 0;
		json_object_foreach(locs, loc, val) {
			if(++i == loc_num) {
				return loc;
			}
		}
	}
	return NULL;
}

// Work around a bug in Windows 7 and later by sending
// BFFM_SETSELECTION a second time.
// https://connect.microsoft.com/VisualStudio/feedback/details/518103/bffm-setselection-does-not-work-with-shbrowseforfolder-on-windows-7
typedef struct {
	ITEMIDLIST *path;
	int attempts;
} initial_path_t;

int CALLBACK SetInitialBrowsePathProc(HWND hWnd, UINT uMsg, LPARAM lp, LPARAM pData)
{
	initial_path_t *ip = (initial_path_t *)pData;
	if(ip) {
		switch(uMsg) {
		case BFFM_INITIALIZED:
			ip->attempts = 0;
			// fallthrough
		case BFFM_SELCHANGED:
			if(ip->attempts < 2) {
				SendMessageW(hWnd, BFFM_SETSELECTION, FALSE, (LPARAM)ip->path);
				ip->attempts++;
			}
		}
	}
	return 0;
}

#define UnkRelease(p) do { IUnknown** __p = (IUnknown**)(p); (*__p)->Release(); (*__p) = NULL; } while(0)

static int SelectFolderVista(PIDLIST_ABSOLUTE initial_path, PIDLIST_ABSOLUTE& pidl, const wchar_t* window_title) {
	// Those two functions are absent in XP, so we have to load them dynamically
	HMODULE shell32 = GetModuleHandle(L"Shell32.dll");
	auto pSHCreateItemFromIDList = (HRESULT(WINAPI *)(PCIDLIST_ABSOLUTE, REFIID, void**))GetProcAddress(shell32, "SHCreateItemFromIDList");
	auto pSHGetIDListFromObject = (HRESULT(WINAPI *)(IUnknown*, PIDLIST_ABSOLUTE*))GetProcAddress(shell32, "SHGetIDListFromObject");
	if (!pSHCreateItemFromIDList || !pSHGetIDListFromObject) {
		return -1;
	}

	IFileDialog *pfd = NULL;
	CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
	if (!pfd) return -1;

	IShellItem* psi = NULL;
	pSHCreateItemFromIDList(initial_path, IID_PPV_ARGS(&psi));
	if (!psi) {
		UnkRelease(&pfd);
		return -1;
	}
	pfd->SetDefaultFolder(psi);
	UnkRelease(&psi);

	pfd->SetOptions(
		FOS_NOCHANGEDIR | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM
		| FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST | FOS_DONTADDTORECENT);
	pfd->SetTitle(window_title);
	HRESULT hr = pfd->Show(con_hwnd());
	if (SUCCEEDED(hr)) {
		if (SUCCEEDED(pfd->GetResult(&psi))) {
			pSHGetIDListFromObject(psi, &pidl);
			UnkRelease(&psi);
			UnkRelease(&pfd);
			return 0;
		}
	}

	UnkRelease(&pfd);
	if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
		return 0;
	}
	return -1;
}

static int SelectFolderXP(PIDLIST_ABSOLUTE initial_path, PIDLIST_ABSOLUTE& pidl, const wchar_t* window_title) {
	BROWSEINFOW bi = { 0 };
	initial_path_t ip = { 0 };
	ip.path = initial_path;

	bi.lpszTitle = window_title;
	bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NONEWFOLDERBUTTON | BIF_USENEWUI;
	bi.hwndOwner = con_hwnd();
	bi.lpfn = SetInitialBrowsePathProc;
	bi.lParam = (LPARAM)&ip;
	pidl = SHBrowseForFolderW(&bi);
	return 0;
}
PIDLIST_ABSOLUTE SelectFolder(PIDLIST_ABSOLUTE initial_path, const wchar_t* window_title) {
	PIDLIST_ABSOLUTE pidl = NULL;
	if (-1 == SelectFolderVista(initial_path, pidl, window_title)) {
		SelectFolderXP(initial_path, pidl, window_title);
	}
	return pidl;
}

json_t* ConfigureLocateGames(const char *games_js_path)
{
	json_t *games;
	int repeat = 0;

	cls(0);

	log_printf("--------------\n");
	log_printf("Locating games\n");
	log_printf("--------------\n");
	log_printf(
		"\n"
		"\n"
	);

	games = json_load_file_report("config/games.js");
	if(json_object_size(games) != 0) {
		log_printf("You already have a %s with the following contents:\n\n", games_js_fn);
		json_dump_log(games, JSON_INDENT(2) | JSON_SORT_KEYS);
		log_printf(
			"\n"
			"\n"
			"Patch data will be downloaded or updated for all the games listed.\n"
			"\n"
		);
		con_clickable("a"); log_printf("\t* (A)dd new games to this list and keep existing ones?\n");
		con_clickable("r"); log_printf("\t* Clear this list and (r)escan?\n");
		con_clickable("k"); log_printf("\t* (K)eep this list and continue?\n");
		log_printf("\n");
		char ret = Ask<3>(nullptr, { 'a', 'r', 'k' | DEFAULT_ANSWER });
		if(ret == 'k') {
			return games;
		} else if(ret == 'r') {
			json_object_clear(games);
		}
	} else {
		games = json_object();
		log_printf(
			"You don't have a %s yet.\n"
			"\n"
			"Thus, I now need to search for patchable games installed on your system.\n"
			"This only has to be done once - unless, of course, you later move the games to\n"
			"different directories.\n"
			"\n"
			"Depending on the number of drives and your directory structure,\n"
			"this may take a while. You can speed up this process by giving me a\n"
			"common root path shared by all games you want to patch.\n"
			"\n"
			"For example, if you have 'Double Dealing Character' in \n"
			"\n"
				"\tC:\\Games\\Touhou\\DDC\\\n"
			"\n"
			"and 'Imperishable Night' in\n"
			"\n"
				"\tC:\\Games\\Touhou\\IN\\\n"
			"\n"
			"you would now specify \n"
			"\n"
				"\tC:\\Games\\Touhou\\\n"
			"\n"
			"... unless, of course, your Touhou games are spread out all over your drives -\n"
			"in which case there's no way around a complete search.\n"
			"\n"
			"\n",
			games_js_fn
		);
		pause();
	}

	PIDLIST_ABSOLUTE initial_path = NULL;
	// BFFM_SETSELECTION does this anyway if we pass a string, so we
	// might as well do the slash conversion in win32_utf8's wrapper
	// while we're at it.
	SHParseDisplayNameU(games_js_path, NULL, &initial_path, 0, NULL);
	CoInitialize(NULL);
	do {
		wchar_t search_path_w[MAX_PATH] = {0};
		json_t *found = NULL;

		PIDLIST_ABSOLUTE pidl = SelectFolder(initial_path, L"Root path for game search (cancel to search entire system):");
		if (pidl && SHGetPathFromIDListW(pidl, search_path_w)) {
			PathAddBackslashW(search_path_w);
			CoTaskMemFree(pidl);
		}

		int search_path_len = wcslen(search_path_w)*UTF8_MUL + 1;
		VLA(char, search_path, search_path_len);
		StringToUTF8(search_path, search_path_w, search_path_len);
		repeat = 0;
		log_printf(
			"Searching games%s%s... this may take a while...\n\n",
			search_path[0] ? " in " : " on the entire system",
			search_path[0] ? search_path: ""
		);
		console_print_percent(-1);
		found = SearchForGames(search_path, games);
		VLA_FREE(search_path);
		if(json_object_size(found)) {
			char *games_js_str = NULL;
			const char *id;
			json_t *locs;

			json_object_foreach(found, id, locs) {
				const char *loc = ChooseLocation(id, locs);
				json_object_set_new(games, id, json_string(loc));
				printf("\n");
			}

			SetCurrentDirectory(games_js_path);

			games_js_str = json_dumps(games, JSON_INDENT(2) | JSON_SORT_KEYS);
			if(!file_write_text(games_js_fn, games_js_str)) {
				log_printf("The following game locations have been identified and written to %s:\n", games_js_fn);
				log_printf(games_js_str);
				log_printf("\n");
			} else if(!file_write_error(games_js_fn)) {
				games = json_decref_safe(games);
			}
			SAFE_FREE(games_js_str);
		} else if(json_object_size(games)) {
			log_printf("No new game locations found.\n");
		} else {
			log_printf("No game locations found.\n");
			if(search_path_w[0]) {
				repeat = console_ask_yn("Search in a different directory?") == 'y';
			}
			if(!repeat) {
				log_printf(
					"No patch shortcuts will be created.\n"
					"Please re-run this configuration tool after you have acquired some games\n"
					"supported by the patches.\n"
				);
				pause();
			}
		}
		json_decref(found);
	} while(repeat);
	CoUninitialize();
	CoTaskMemFree(initial_path);
	return games;
}
