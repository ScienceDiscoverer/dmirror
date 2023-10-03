// CONSOLE
#include <sddb>
#include <stdp>
#include <stdr>
#include <ftools>
#include <conint>

// TODO GetFileSecurity FOR EVERY RECURCIVELY CREATED FOLDER AND COPY IT TO THE MIRRORED FOLDER INSTEAD OF USING [NULL]
// TODO FIGURE OUT HOW TO CREATE EMPTY FOLDERS IN DST AND REMOVE FOLDERS WHEN DELETING WORTHLESS FILES IN DST -> FILE_FLAG_BACKUP_SEMANTICS MUST BE SET TO GET DIRECTORY HANDLE WITH CREATEFILE https://learn.microsoft.com/en-us/windows/win32/fileio/obtaining-a-handle-to-a-directory

// NTODO FOLDER TIMESTAMPS WILL NOT BE PRESERVED, TOO MUCH OVERHEAD AND USELESS ANYWAYS

struct HardLinkGroup
{
	wtxta hl;
	ui64 id;
};

class HardLinkGroups : public darr
{
public:
	HardLinkGroups()
	{ 
		ts = 16384;
		s = 0;
		d = (HardLinkGroup *)Alloc(ts * sizeof(HardLinkGroup));
		memset(d, 0, ts * sizeof(HardLinkGroup)); // All texts invalidated
	}
	HardLinkGroups(const HardLinkGroups &o) = delete;

	~HardLinkGroups()
	{
		for(ui64 i = 0; i < s; ++i)
		{
			d[i].hl.~wtxta();
		}
	}

	HardLinkGroup & operator[](ui64 i) { DARR_ASSERT_RANGE(i) return d[i]; }
	const HardLinkGroup & operator[](ui64 i) const { DARR_ASSERT_RANGE(i) return d[i]; }
	
	HardLinkGroups & Add(const wtxt &fn, ui64 fid)
	{
		for(ui64 i = 0; i < s; ++i)
		{
			if(d[i].id == fid)
			{
				d[i].hl << fn;
				return *this;
			}
		}
		
		ui64 ns = s + 1;
		if(ts < ns)
		{
			d = (HardLinkGroup *)ReAlloc(d, ns, sizeof(HardLinkGroup));
			memset(&d[s], 0, (ts - s) * sizeof(HardLinkGroup)); // All new texts invalidated
		}

		d[s].hl << fn;
		d[s].id = fid;
		s = ns;
		
		return *this;
	}

private:
	HardLinkGroup *d;
};

wtxt base_src = MAX_PATH, base_dst = MAX_PATH;

wtxt base_dir = MAX_PATH;
wtxt sub_dir = MAX_PATH;
wtxt base_dir_orig = MAX_PATH;

void createSubDirs(const wtxt &fn)
{
	ui64 path_sz = txtfe(fn, TEND, '\\'); // Path size excluding slash
	base_dir = fn;
	while(~base_dir > 6) // [\\?\D:]
	{
		removeLastDir(base_dir);
		if(fileExists(base_dir))
		{
			break;
		}
	}
	
	while(~base_dir < path_sz)
	{
		base_dir += '\\';
		base_dir += txtsp(sub_dir, fn, ~base_dir, txtf(fn, ~base_dir, '\\') - 1);
		CreateDirectoryW(base_dir, NULL);
		base_dir_orig = base_dir;
		txtr(base_dir_orig, 0, ~base_dst, base_src);
		DWORD attrs = GetFileAttributesW(base_dir_orig);
		SetFileAttributesW(base_dir, attrs);
	}
}

bool64 fileUnchanged(const wtxt &src, const wtxt &dst)
{
	BY_HANDLE_FILE_INFORMATION src_bhfi, dst_bhfi;
	getFileHandleInfoReparse(src, src_bhfi);
	getFileHandleInfoReparse(dst, dst_bhfi);
	if(
	*((ui64 *)&src_bhfi.ftLastWriteTime)	==	*((ui64 *)&dst_bhfi.ftLastWriteTime)	&&
	src_bhfi.nFileSizeHigh					==	dst_bhfi.nFileSizeHigh					&&
	src_bhfi.nFileSizeLow					==	dst_bhfi.nFileSizeLow)
	{
		return true;
	}
	
	return false;
}

ui64 prev_bt;
DWORD ticks;

#pragma warning( disable : 4100 )
DWORD NOTHROW copyProgress(
	LARGE_INTEGER fs,
	LARGE_INTEGER bt,
	LARGE_INTEGER ss,
	LARGE_INTEGER sbt,
	DWORD sn,
	DWORD reason,
	HANDLE src,
	HANDLE dest,
	LPVOID data)
{
	if(fs.QuadPart < 134217728) // 128 MB
	{
		return PROGRESS_CONTINUE;
	}
	
	if(reason == CALLBACK_CHUNK_FINISHED)
	{
		printProgBar((ui64)fs.QuadPart/1048576, (ui64)bt.QuadPart/1048576, 0, 2, 80);
		DWORD new_ticks = GetTickCount();
		ui64 spd = 0;
		ui64 ms = new_ticks - ticks;
		if(ms >= 1000)
		{
			spd = bt.QuadPart - prev_bt;
			ticks = new_ticks;
			prev_bt = (ui64)bt.QuadPart;
			p|CLL|(ui64)spd/1024|" KB/S"|CR;
		}
	}
	return PROGRESS_CONTINUE;
}
#pragma warning( default : 4100 )

inline void cloneFile(const wtxt &src, const wtxt &dst)
{
	p|SCP(0,2)|CLL|SCP(0,3)|CLL; // Clear previous file's progress [mb_copied/mb_total] + copy speed
	BOOL res = CopyFileExW(src, dst, copyProgress, NULL, NULL, COPY_FILE_COPY_SYMLINK);
	if(!res)
	{
		p|PE|S|" CopyFileExW "|src|" --> "|dst|P;
		return;
	}
	copyFileTime(src, dst);
}

wtxt short_path = MAX_PATH;

wtxt & rmLngPrfx(const wtxt &path)
{
	short_path = path;
	txtd(short_path, 0, 4);
	return short_path;
}

i32 wmain(ui64 argc, wchar_t **argv)
{
	if(argc != 3)
	{
		return 1;
	}
	
	p|DC;
	
	wtxta src_fl = 262144, dst_fl = 262144;
	HardLinkGroups hlinks;
	
	wtxt long_path_prfx = WL("\\\\?\\");
	//base_src = WL("\\\\?\\D:\\tst\\web"), base_dst = WL("\\\\?\\D:\\tst\\dst");
	base_src = long_path_prfx + cwstr({ argv[1], strl(argv[1]) }), base_dst = long_path_prfx + cwstr({ argv[2], strl(argv[2]) });
	
	p|"SCANNING "|G|rmLngPrfx(base_src)|"..."|N;
	SWSET
	getFileList(base_src, src_fl);
	SWSTOP
	
	SWPRINT
	SWRESET
	
	p|"SRC FILES FOUND: "|G|~src_fl|N;
	
	p|"SCANNING "|Y|rmLngPrfx(base_dst)|"..."|N;
	SWSET
	getFileList(base_dst, dst_fl);
	SWSTOP
	
	SWPRINT
	SWRESET
	
	p|"DST FILES FOUND: "|Y|~dst_fl|N;
	
	p|Y|"!WARNING! "|"All files in "|B|rmLngPrfx(base_dst)|" will be overwritten/deleted. "|RD|"Are you sure about that?"|N;
	p|"Type ["|GD|"yes"|"] to confirm: "|EC;
	txt confirmator = 15;
	r > confirmator;
	if(confirmator != L("yes"))
	{
		p|RC;
		return 0;
	}
	
	p|DC|CLS;
	
	ui64 dst_deleted = 0;
	
	p|N|"Deleting worthless DST files..."|N;
	
	ui64 dst_mod = ~dst_fl/1000 + 1;
	wtxt orig_fp = MAX_PATH;
	
	SWSET
	
	for(ui64 i = 0; i < ~dst_fl; ++i)
	{
		orig_fp = dst_fl[i];
		txtr(orig_fp, 0, ~base_dst, base_src);
		
		if(!fileExists(orig_fp))
		{
			BOOL res = DeleteFileW(dst_fl[i]);
			if(!res)
			{
				p|PE|" DeleteFileW "|dst_fl[i]|N;
			}
			else
			{
				++dst_deleted;
			}
		}
		
		printProgBarMod(dst_mod, ~dst_fl, i, 0, 0, 80);
	}
	
	printProgBar(~dst_fl, ~dst_fl, 0, 0, 80);
	
	SWSTOP
	SWPRINT
	SWRESET
	
	p|"DST FILES DELETED: "|R|dst_deleted|N|P|SCP(0,0)|CLL;
	
	BY_HANDLE_FILE_INFORMATION bhfi;
	ui64 hlid = 0; // Hard Link ID
	
	p|N|CLL|"Finding all hardlinks..."|N|CLL;
	
	ui64 src_mod = ~dst_fl/1000 + 1;
	
	SWSET
	
	ui64 tot_hlinks = 0;
	for(ui64 i = 0; i < ~src_fl; ++i)
	{
		getFileHandleInfoReparse(src_fl[i], bhfi);
		if(bhfi.nNumberOfLinks > 1 /*&& !(bhfi.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)*/)
		{
			hlid = (ui64)bhfi.nFileIndexHigh << 32 | (ui64)bhfi.nFileIndexLow;
			hlinks.Add(src_fl[i], hlid);
			txtclr(src_fl[i]);
			++tot_hlinks;
		}
		
		printProgBarMod(src_mod, ~src_fl, i, 0, 0, 80); // TODO CHECK IF PROGRESS BAR SLOWS DOWN PROCESS TOO MUCH 1M 7S VS 18 SECONDS WITHOUT?
	}// TODO PROGRESS BAR MOD MIGHT NOT CALC CORRECTLY MIRRORING HARDLINKS HAS MOD OF 1 OR WTF IS GOIN GON?!?
	
	printProgBar(~src_fl, ~src_fl, 0, 0, 80);
	
	SWSTOP
	SWPRINT
	SWRESET
	
	p|CLL|"HL GROUPS: "|Y|~hlinks|" TOTAL: "|G|tot_hlinks|N|P|CLS;
	
	wtxt base_hl = MAX_PATH, hl = MAX_PATH;
	ui64 tot_processed = 0, tot_cpy = 0, tot_upd = 0, tot_skip = 0;
	
	p|SCP(0,4)|"Mirroring hardlinks...";
	
	printProgBarMod(src_mod, ~src_fl, tot_processed, 0, 0, 80);
	
	SWSET
	
	ui64 i = 0;
	for(; i < ~hlinks; ++i) // First, copy all hardlinks by only copying 1 file from the group, then creating remaining hl-s
	{
		bool64 hl_found = false;
		for(ui64 j = 1; j < ~hlinks[i].hl; ++j)
		{
			base_hl = hlinks[i].hl[j];
			txtr(base_hl, 0, ~base_src, base_dst);
			if(fileExists(base_hl))
			{
				if(fileUnchanged(hlinks[i].hl[j], base_hl)) // Use this file as base for all other hardlinks
				{
					hl_found = true;
					tot_skip += ~hlinks[i].hl;
					break;
				}
				else // Overwrite this file and use it as base
				{
					cloneFile(hlinks[i].hl[j], base_hl);
					hl_found = true;
					tot_upd += ~hlinks[i].hl;
					break;
				}
			}
		}
		
		if(!hl_found)
		{
			createSubDirs(base_hl);
			cloneFile(hlinks[i].hl[0], base_hl);
			tot_cpy += ~hlinks[i].hl;
		}
		
		for(ui64 j = 0; j < ~hlinks[i].hl; ++j)
		{
			hl = hlinks[i].hl[j];
			txtr(hl, 0, ~base_src, base_dst);
			if(!fileExists(hl))
			{
				createSubDirs(hl);
				CreateHardLinkW(hl, base_hl, NULL); // TODO MB COPY SECURITY INFO
			}
		}
		
		tot_processed += ~hlinks[i].hl;
		printProgBarMod(src_mod, ~src_fl, tot_processed, 0, 0, 80);
	}
	
	p|SCP(0,4)|CLL|"Mirroring normal files and symbolic links...";
	
	i = 0;
	for(; i < ~src_fl; ++i) // Finally, copy all normal files and symbolic links
	{
		if(src_fl[i] == empty)
		{
			continue;
		}
		
		base_hl = src_fl[i];
		txtr(base_hl, 0, ~base_src, base_dst);
		if(fileExists(base_hl))
		{
			if(fileUnchanged(src_fl[i], base_hl))
			{
				++tot_skip;
				goto skip_main_loop;
			}
			else
			{
				cloneFile(src_fl[i], base_hl);
				++tot_upd;
				goto skip_main_loop;
			}
		}
		
		createSubDirs(base_hl);
		cloneFile(src_fl[i], base_hl);
		++tot_cpy;
		
	skip_main_loop:
		printProgBarMod(src_mod, ~src_fl, ++tot_processed, 0, 0, 80);
	}
	
	printProgBar(~src_fl, ~src_fl, 0, 0, 80);
	
	SWSTOP
	p|SCP(0,4)|CLL;
	SWPRINT
	
	p|"TOTAL COPIED "|G|tot_cpy|" UPDATED "|Y|tot_upd|" UNCHANGED "|V|tot_skip|" DELETED "|R|dst_deleted|N;
	
	ui64 tot_all = tot_cpy + tot_upd + tot_skip;
	if(tot_all != ~src_fl || tot_processed != ~src_fl || tot_all != tot_processed)
	{
		p|R|"MISTMATCH FOUND!"|N;
		p|"tot_all "|tot_all|N|"~src_fl "|~src_fl|N|"tot_processed "|tot_processed|N;
	}
	
	p|RC;
	return 0;
}