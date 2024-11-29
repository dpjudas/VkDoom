/*
** file_grp.cpp
**
**---------------------------------------------------------------------------
** Copyright 1998-2009 Randy Heit
** Copyright 2005-2009 Christoph Oelckers
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
**
*/

#include "resourcefile.h"
#include "fs_swap.h"

namespace FileSys {
	using namespace byteswap;

//==========================================================================
//
//
//
//==========================================================================

struct GrpHeader
{
	uint32_t		Magic[3];
	uint32_t		NumLumps;
};

struct GrpLump
{
	union
	{
		struct
		{
			char		Name[12];
			uint32_t		Size;
		};
		char NameWithZero[13];
	};
};


//==========================================================================
//
// Open it
//
//==========================================================================

static bool OpenGrp(FResourceFile* file, FileSystemFilterInfo* filter)
{
	GrpHeader header;

	auto Reader = file->GetContainerReader();
	Reader->Read(&header, sizeof(header));
	uint32_t NumLumps = LittleLong(header.NumLumps);
	auto Entries = file->AllocateEntries(NumLumps);

	GrpLump *fileinfo = new GrpLump[NumLumps];
	Reader->Read (fileinfo, NumLumps * sizeof(GrpLump));

	int Position = sizeof(GrpHeader) + NumLumps * sizeof(GrpLump);

	for(uint32_t i = 0; i < NumLumps; i++)
	{
		Entries[i].Position = Position;
		Entries[i].CompressedSize = Entries[i].Length = LittleLong(fileinfo[i].Size);
		Position += fileinfo[i].Size;
		Entries[i].Flags = 0;
		fileinfo[i].NameWithZero[12] = '\0';	// Be sure filename is null-terminated
		Entries[i].ResourceID = -1;
		Entries[i].Method = METHOD_STORED;
		Entries[i].FileName = file->NormalizeFileName(fileinfo[i].Name);
	}
	file->GenerateHash();
	delete[] fileinfo;
	return true;
}


//==========================================================================
//
// File open
//
//==========================================================================

FResourceFile *CheckGRP(const char *filename, FileReader &file, FileSystemFilterInfo* filter, FileSystemMessageFunc Printf, StringPool* sp)
{
	char head[12];

	if (file.GetLength() >= 12)
	{
		file.Seek(0, FileReader::SeekSet);
		file.Read(&head, 12);
		file.Seek(0, FileReader::SeekSet);
		if (!memcmp(head, "KenSilverman", 12))
		{
			auto rf = new FResourceFile(filename, file, sp, FResourceFile::NO_FOLDERS | FResourceFile::SHORTNAMES);
			if (OpenGrp(rf, filter)) return rf;
			file = rf->Destroy();
		}
	}
	return nullptr;
}

}
