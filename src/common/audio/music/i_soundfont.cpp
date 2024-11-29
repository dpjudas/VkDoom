/*
** i_soundfont.cpp
** The sound font manager for the MIDI synths
**
**---------------------------------------------------------------------------
** Copyright 2018 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include <ctype.h>
#include <assert.h>
#include "i_soundfont.h"
#include "i_soundinternal.h"
#include "cmdlib.h"
#include "i_system.h"
#include "filereadermusicinterface.h"
#include <zmusic.h>
#include "fs_filesystem.h"
#include "version.h"
#include "fs_findfile.h"
#include "i_interface.h"
#include "configfile.h"
#include "printf.h"

//==========================================================================
//
//
//
//==========================================================================

FSoundFontManager sfmanager;

//==========================================================================
//
// returns both a file reader and the full name of the looked up file
//
//==========================================================================

std::pair<FileReader, FString> FSoundFontReader::LookupFile(const char *name)
{
	if (!IsAbsPath(name))
	{
		for(int i = mPaths.Size()-1; i>=0; i--)
		{
			FString fullname = mPaths[i] + name;
			auto fr = OpenFile(fullname.GetChars());
			if (fr.isOpen()) return std::make_pair(std::move(fr), fullname);
		}
	}
	auto fr = OpenFile(name);
	if (!fr.isOpen()) name = "";
	return std::make_pair(std::move(fr), name);
}

//==========================================================================
//
// This adds a directory to the path list
//
//==========================================================================

void FSoundFontReader::AddPath(const char *strp)
{
	if (*strp == 0) return;
	if (!mAllowAbsolutePaths && IsAbsPath(strp)) return;	// of no use so we may just discard it right away
	int i = 0;
	FString str = strp;
	FixPathSeperator(str);
	if (str.Back() != '/') str += '/';	// always let it end with a slash.
	for (auto &s : mPaths)
	{
		if (pathcmp(s.GetChars(), str.GetChars()) == 0)
		{
			// move string to the back.
			mPaths.Delete(i);
			mPaths.Push(str);
			return;
		}
		i++;
	}
	mPaths.Push(str);
}

int FSoundFontReader::pathcmp(const char *p1, const char *p2)
{
	return mCaseSensitivePaths? strcmp(p1, p2) : stricmp(p1, p2);
}

//==========================================================================
//
//
//
//==========================================================================

FileReader FSoundFontReader::Open(const char *name, std::string& filename)
{
	FileReader fr;
	if (name == nullptr)
	{
		fr = OpenMainConfigFile();
		filename = MainConfigFileName().GetChars();
	}
	else
	{
		auto res = LookupFile(name);
		fr = std::move(res.first);
		filename = res.second.GetChars();
	}
	return fr;
}

//==========================================================================
//
//
//
//==========================================================================

ZMusicCustomReader* FSoundFontReader::open_interface(const char* name)
{
	std::string filename;

	FileReader fr = Open(name, filename);
	if (!fr.isOpen()) return nullptr;
	auto fri = GetMusicReader(fr);
	return fri;
}


//==========================================================================
//
// Note that the file type has already been checked
//
//==========================================================================

FSF2Reader::FSF2Reader(const char *fn)
{
	mMainConfigForSF2.Format("soundfont \"%s\"\n", fn);
	mFilename = fn;
}

//==========================================================================
//
//
//
//==========================================================================

FileReader FSF2Reader::OpenMainConfigFile()
{
	FileReader fr;
	if (mMainConfigForSF2.IsNotEmpty())
	{
		fr.OpenMemory(mMainConfigForSF2.GetChars(), mMainConfigForSF2.Len());
	}
	return fr;
}

FileReader FSF2Reader::OpenFile(const char *name)
{
	FileReader fr;
	if (mFilename.CompareNoCase(name) == 0)
	{
		fr.OpenFile(name);
	}
	return fr;
}

//==========================================================================
//
//
//
//==========================================================================

FZipPatReader::FZipPatReader(const char *filename)
{
	mAllowAbsolutePaths = true;
	resf = FResourceFile::OpenResourceFile(filename);
}

FZipPatReader::~FZipPatReader()
{
	if (resf != nullptr) delete resf;
}

FileReader FZipPatReader::OpenMainConfigFile()
{
	return OpenFile("timidity.cfg");
}

FileReader FZipPatReader::OpenFile(const char *name)
{
	FileReader fr;
	if (resf != nullptr)
	{
		auto lump = resf->FindEntry(name);
		if (lump >= 0)
		{
			return resf->GetEntryReader(lump, FileSys::READER_NEW, FileSys::READERFLAG_SEEKABLE);
		}
	}
	fr.OpenFile(name);
	return fr;
}

//==========================================================================
//
//
//
//==========================================================================

FPatchSetReader::FPatchSetReader(const char *filename)
{
#ifndef _WIN32
	mCaseSensitivePaths = true;
	const char *paths[] = {
		"/usr/local/lib/timidity",
		"/etc/timidity",
		"/etc"
	};
#else
	const char *paths[] = {
		"C:/TIMIDITY",
		"/TIMIDITY",
		progdir.GetChars()
	};
#endif
	mAllowAbsolutePaths = true;
	FileReader fr;
	if (fr.OpenFile(filename))
	{
		mFullPathToConfig = filename;
	}
	else if (!IsAbsPath(filename))
	{
		for(auto c : paths)
		{
			FStringf fullname("%s/%s", c, filename);
			if (fr.OpenFile(fullname.GetChars()))
			{
				mFullPathToConfig = fullname;
			}
		}
	}
	if (mFullPathToConfig.Len() > 0)
	{
		FixPathSeperator(mFullPathToConfig);
		mBasePath = ExtractFilePath(mFullPathToConfig.GetChars());
		if (mBasePath.Len() > 0 && mBasePath.Back() != '/') mBasePath += '/';
	}
}


FileReader FPatchSetReader::OpenMainConfigFile()
{
	FileReader fr;
	fr.OpenFile(mFullPathToConfig.GetChars());
	return fr;
}

FileReader FPatchSetReader::OpenFile(const char *name)
{
	FString path;
	if (IsAbsPath(name)) path = name;
	else path = mBasePath + name;
	FileReader fr;
	fr.OpenFile(path.GetChars());
	return fr;
}

//==========================================================================
//
//
//
//==========================================================================

FLumpPatchSetReader::FLumpPatchSetReader(const char *filename)
{
	mLumpIndex = fileSystem.FindFile(filename);

	mBasePath = filename;
	FixPathSeperator(mBasePath);
	mBasePath = ExtractFilePath(mBasePath.GetChars());
	if (mBasePath.Len() > 0 && mBasePath.Back() != '/') mBasePath += '/';
}

FileReader FLumpPatchSetReader::OpenMainConfigFile()
{
	return fileSystem.ReopenFileReader(mLumpIndex);
}

FileReader FLumpPatchSetReader::OpenFile(const char *name)
{
	FString path;
	if (IsAbsPath(name)) return FileReader();	// no absolute paths in the virtual file system.
	path = mBasePath + name;
	auto index = fileSystem.FindFile(path.GetChars());
	if (index < 0) return FileReader();
	return fileSystem.ReopenFileReader(index);
}

//==========================================================================
//
// collects everything out of the soundfonts directory.
// This may either be .sf2 files or zipped GUS patch sets with a
// 'timidity.cfg' in the root directory.
// Other compression types are not supported, in particular not 7z because
// due to the solid nature of its archives would be too slow.
//
//==========================================================================

void FSoundFontManager::ProcessOneFile(const char* fn)
{
	auto fb = ExtractFileBase(fn, false);
	auto fbe = ExtractFileBase(fn, true);
	for (auto &sfi : soundfonts)
	{
		// We already got a soundfont with this name. Do not add again.
		if (!sfi.mName.CompareNoCase(fb)) return;
	}

	FileReader fr;
	if (fr.OpenFile(fn))
	{
		// Try to identify. We only accept .sf2 and .zip by content. All other archives are intentionally ignored.
		char head[16] = { 0};
		fr.Read(head, 16);
		if (!memcmp(head, "RIFF", 4) && !memcmp(head+8, "sfbkLIST", 8))
		{
			FSoundFontInfo sft = { fb, fbe, fn, SF_SF2 };
			soundfonts.Push(sft);
		}
		if (!memcmp(head, "WOPL3-BANK\0", 11))
		{
			FSoundFontInfo sft = { fb, fbe, fn, SF_WOPL };
			soundfonts.Push(sft);
		}
		if (!memcmp(head, "WOPN2-BANK\0", 11) || !memcmp(head, "WOPN2-B2NK\0", 11))
		{
			FSoundFontInfo sft = { fb, fbe, fn, SF_WOPN };
			soundfonts.Push(sft);
		}
		else if (!memcmp(head, "PK", 2))
		{
			auto zip = FResourceFile::OpenResourceFile(fn, true);
			if (zip != nullptr)
			{
				if (zip->EntryCount() > 1)	// Anything with just one lump cannot possibly be a packed GUS patch set so skip it right away and simplify the lookup code
				{
					auto zipl = zip->FindEntry("timidity.cfg");
					if (zipl >= 0)
					{
						// It seems like this is what we are looking for
						FSoundFontInfo sft = { fb, fbe, fn, SF_GUS };
						soundfonts.Push(sft);
					}
				}
				delete zip;
			}
		}
	}
}

//==========================================================================
//
//
//
//==========================================================================

void FSoundFontManager::CollectSoundfonts()
{
	FConfigFile* GameConfig = sysCallbacks.GetConfig ? sysCallbacks.GetConfig() : nullptr;
	if (GameConfig != NULL && GameConfig->SetSection ("SoundfontSearch.Directories"))
	{
		const char *key;
		const char *value;

		while (GameConfig->NextInSection (key, value))
		{
			if (stricmp (key, "Path") == 0)
			{
				FileSys::FileList list;

				FString dir;

				dir = NicePath(value);
				FixPathSeperator(dir);
				if (dir.IsNotEmpty())
				{
					if (FileSys::ScanDirectory(list, dir.GetChars(), "*", true))
					{
						for(auto& entry : list)
						{
							if (!entry.isDirectory)
							{
								ProcessOneFile(entry.FilePath.c_str());
							}
						}
					}
				}
			}
		}
	}

	if (soundfonts.Size() == 0)
	{
		ProcessOneFile(NicePath("$PROGDIR/soundfonts/" GAMENAMELOWERCASE ".sf2").GetChars());
	}
}

//==========================================================================
//
//
//
//==========================================================================

const FSoundFontInfo *FSoundFontManager::FindSoundFont(const char *name, int allowed) const
{
	for(auto &sfi : soundfonts)
	{
		// an empty name will pick the first one in a compatible format.
		if (allowed & sfi.type && (name == nullptr || *name == 0 || !sfi.mName.CompareNoCase(name) || !sfi.mNameExt.CompareNoCase(name)))
		{
			DPrintf(DMSG_NOTIFY, "Found compatible soundfont %s\n", sfi.mNameExt.GetChars());
			return &sfi;
		}
	}
	// We did not find what we were looking for. Let's just return the first valid item that works with the given device.
	for (auto &sfi : soundfonts)
	{
		if (allowed & sfi.type)
		{
			DPrintf(DMSG_NOTIFY, "Unable to find %s soundfont. Falling back to %s\n", name, sfi.mNameExt.GetChars());
			return &sfi;
		}
	}
	return nullptr;
}

//==========================================================================
//
//
//
//==========================================================================

FSoundFontReader *FSoundFontManager::OpenSoundFont(const char *const name, int allowed)
{
	if (name == nullptr) return nullptr;
	// First check if the given name is inside the loaded resources.
	// To avoid clashes this will only be done if the name has the '.cfg' extension.
	// Sound fonts cannot be loaded this way.
	const char *p = name + strlen(name) - 4;
	if (p > name && !stricmp(p, ".cfg") && fileSystem.FindFile(name) >= 0)
	{
		return new FLumpPatchSetReader(name);
	}

	// Next check if the file is a .sf file
	if (allowed & SF_SF2)
	{
		FileReader fr;
		if (fr.OpenFile(name))
		{
			char head[16] = { 0};
			fr.Read(head, 16);
			fr.Close();
			if (!memcmp(head, "RIFF", 4) && !memcmp(head+8, "sfbkLIST", 8))
			{
				return new FSF2Reader(name);
			}
		}
	}
	// Next check if the file is a resource file (it should contains gus patches and a timidity.cfg file)
	if (allowed & SF_GUS)
	{
		FileReader fr;
		if (fr.OpenFile(name))
		{
			char head[16] = { 0 };
			fr.Read(head, 2);
			fr.Close();
			if (!memcmp(head, "PK", 2))	// The only reason for this check is to block non-Zips. The actual validation will be done by FZipFile.
			{
				auto r = new FZipPatReader(name);
				if (r->isOk()) return r;
				delete r;
			}
		}

		// Config files are only accepted if they are named '.cfg', because they are impossible to validate.
		const char *p = name + strlen(name) - 4;
		if (p > name && !stricmp(p, ".cfg") && FileExists(name))
		{
			return new FPatchSetReader(name);
		}
	}
	// Lastly check in the sound font collection for a specific item or pick the first valid item available.
	auto sfi = FindSoundFont(name, allowed);
	if (sfi != nullptr)
	{
		if (sfi->type == SF_SF2) return new FSF2Reader(sfi->mFilename.GetChars());
		else return new FZipPatReader(sfi->mFilename.GetChars());
	}
	return nullptr;

}

void I_InitSoundFonts()
{
	sfmanager.CollectSoundfonts();
}


