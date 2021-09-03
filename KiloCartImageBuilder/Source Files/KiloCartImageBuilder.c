/*****************************************************************************/
/* KiloCartImageBuilder - Videoton TV Computer 64k Cart Image Builder        */
/* CAS File format declarations                                              */
/*                                                                           */
/* Copyright (C) 2021 Laszlo Arvai                                           */
/* All rights reserved.                                                      */
/*                                                                           */
/* This software may be modified and distributed under the terms             */
/* of the BSD license.  See the LICENSE file for details.                    */
/*****************************************************************************/

///////////////////////////////////////////////////////////////////////////////
// Includes
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <Windows.h>
#include <CASFile.h>
#include <FileUtils.h>
#include "ZX7Compress.h"

///////////////////////////////////////////////////////////////////////////////
// Constants
#define CART_ROM_SIZE 65536
#define CART_PAGE_SIZE 16384
#define FILE_BUFFER_SIZE 1024*1024
#define MAX_FILE_NUMBER 64
#define ROM_PAGE_CHANGE_ADDRESS 0x3ffc

#define PRINT_ERROR(...) fwprintf (stderr, __VA_ARGS__)
#define PRINT_INFO(...) fwprintf (stdout, __VA_ARGS__)

///////////////////////////////////////////////////////////////////////////////
// Types
typedef struct 
{
	wchar_t Filename[MAX_PATH_LENGTH];
	int BufferPos;
	int ROMAddress;
	int Length;
	bool Version2xFile;
} ProgramFileInfo;

#pragma pack(push, 1)

typedef struct
{
	char Filename[MAX_TVC_FILE_NAME_LENGTH];
	uint16_t Address;
	uint16_t Length;
} ROMFileInfo;

typedef struct
{
	uint8_t Files1xCount;	// Number of files in the image for 1.x TVC ROM version
	uint8_t Files2xCount;	// Number of files in the image for 2.x TVC ROM version
	uint16_t Directory1xAddress;	// Address of the directory for 1.x TVC ROM version
	uint16_t Directory2xAddress;	// Address of the directory for 1.x TVC ROM version
	uint16_t FilesAddress;				// Address of the file data
} ROMFileSystemInfo;

#pragma pack(pop)

///////////////////////////////////////////////////////////////////////////////
// Function prototypes
bool LoadFiles(void);
bool LoadProgramFile(ProgramFileInfo* inout_cas_file);
bool CreateROMImage(void);
bool CreateROMLoader();
bool CreateROMDirectory();
bool CreateROMFileSystem();


///////////////////////////////////////////////////////////////////////////////
// Global variables

uint8_t g_page_start_bytes[] = { 'M', 'O', 'P', 'S', 0x3A, 0xFC, 0xFF };

byte g_file_buffer[FILE_BUFFER_SIZE];
int g_file_buffer_length;

byte g_rom_image[FILE_BUFFER_SIZE];
int g_rom_image_address;

ProgramFileInfo g_file_info[MAX_FILE_NUMBER];
int g_file_info_count;

bool g_compressed_mode = false;

int g_rom_file_system_info_address;
int g_rom_files_address;


///////////////////////////////////////////////////////////////////////////////
// Main function
int wmain(int argc, wchar_t** argv)
{
	int i;
	bool success = true;
	int file_index = 0;
	bool version_2x_enabled = false;
	wchar_t output_file_name[MAX_PATH_LENGTH];
	FILE* output_file = NULL;

	// intro
	PRINT_INFO(L"\nROM Image Builder for 64k TV Computer Cartridge v0.1");
	PRINT_INFO(L"\n(c) 2021 Laszlo Arvai");

	// default output file name
	wcscpy_s(output_file_name, MAX_PATH_LENGTH, L"KiloCart.bin");

	i = 1;
	while (i < argc && success)
	{
		// switch found
		if (argv[i][0] == '-')
		{
			switch (tolower(argv[i][1]))
			{
			case '2':
				version_2x_enabled = true;
				break;

				// output file name
			case 'o':

				break;

			// force compressed mode
			case 'c':
				g_compressed_mode = true;
				break;

			case 'h':
			case'?':
				break;
			}
		}
		else
		{
			// filename found
			wcsncpy_s(g_file_info[g_file_info_count].Filename, MAX_PATH_LENGTH, argv[i], MAX_PATH_LENGTH);
			g_file_info[g_file_info_count].Version2xFile = version_2x_enabled;
			g_file_info_count++;
		}

		i++;
	}

	// Loads CAS files
	if (success)
	{
		g_file_buffer_length = 0;
		success = LoadFiles();
	}

	// Creates ROM image
	if (success)
	{
		success = CreateROMImage();
	}

	// saves ROM image
	if (success)
	{
		if (_wfopen_s(&output_file, output_file_name, L"wb") == 0 && output_file != NULL)
		{
			fwrite(g_rom_image, CART_ROM_SIZE, 1, output_file);
			fclose(output_file);
		}
	}

	return (success) ? 0 : -1;
}

///////////////////////////////////////////////////////////////////////////////
// Loads all CAS files
bool LoadFiles()
{
	int i;
	bool success = true;

	for (i = 0; i < g_file_info_count && success; i++)
	{
		success = LoadProgramFile(&g_file_info[i]);
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////
// Load program file
bool LoadProgramFile(ProgramFileInfo* inout_program_file)
{
	FILE* program_file = NULL;
	bool success = true;
	CASUPMHeaderType upm_header;
	CASProgramFileHeaderType program_header;
	wchar_t display_filename[MAX_PATH_LENGTH];
	wchar_t file_extension[MAX_PATH_LENGTH];
	bool cas_file_type = true;

	// convert and copy file name
	GetFileNameAndExtension(display_filename, MAX_PATH_LENGTH, inout_program_file->Filename);
	PRINT_INFO(L"\nLoading: %s", display_filename);

	// determine file type by extension
	GetExtension(file_extension, display_filename);
	if (_wcsicmp(file_extension, L"CAS") != 0)
		cas_file_type = false;

	// open program file
	if (_wfopen_s(&program_file, inout_program_file->Filename, L"rb") != 0 || program_file == NULL)
	{
		PRINT_ERROR(L"\nCan't open file!");
		return false;
	}

	if (cas_file_type)
	{
		// load UPM header
		ReadBlock(program_file, &upm_header, sizeof(upm_header), &success);

		// load program header
		ReadBlock(program_file, &program_header, sizeof(program_header), &success);

		// Check validity
		if (!CASCheckHeaderValidity(&program_header))
		{
			PRINT_ERROR(L"\nInvalid file!");
			success = false;
		}

		if (!CASCheckUPMHeaderValidity(&upm_header))
		{
			PRINT_ERROR(L"\nInvalid file!");
			success = false;
		}
	}
	else
	{
		// detrmine length
		fseek(program_file, 0, SEEK_END);

		program_header.FileLength = (uint16_t)ftell(program_file);
		fseek(program_file, 0, SEEK_SET);
	}

	// check size
	if (success)
	{
		if (g_file_buffer_length + program_header.FileLength >= FILE_BUFFER_SIZE)
		{
			PRINT_ERROR(L"\nToo many file specified!");
			success = false;
		}
	}

	// load program data
	if (success)
	{
		ReadBlock(program_file, g_file_buffer + g_file_buffer_length, program_header.FileLength, &success);

		if (success)
		{
			inout_program_file->Length = program_header.FileLength;
			inout_program_file->BufferPos = g_file_buffer_length;
			g_file_buffer_length += program_header.FileLength;
			inout_program_file->ROMAddress = 0;
		}
		else
		{
			PRINT_ERROR(L"\nFile load error!");
			success = false;
		}
	}
	
	// close file
	if (program_file != NULL)
		fclose(program_file);

	return success;
}

///////////////////////////////////////////////////////////////////////////////
// Creates ROM image
bool CreateROMImage(void)
{
	bool success = true;

	do
	{
		g_rom_image_address = 0;

		// load loader code
		if (success)
			success = CreateROMLoader();

		if (success)
		{
			// update addresses
			g_rom_file_system_info_address = g_rom_image_address - sizeof(ROMFileSystemInfo);
			g_rom_files_address = g_rom_file_system_info_address + sizeof(ROMFileSystemInfo) + sizeof(ROMFileInfo) * g_file_info_count;
			g_rom_image_address = g_rom_files_address;

			success = CreateROMFileSystem();
		}

		// check if image is fit into the ROM
		if (success && g_rom_image_address >= CART_ROM_SIZE)
		{
			if (g_compressed_mode)
			{
				PRINT_ERROR(L"\nCartridge memory is too low!");
				success = false;
			}
			else
			{
				// try compressed mode
				g_compressed_mode = true;
			}
		}
		else
		{
			// add directory to the image 
			if (success)
				success = CreateROMDirectory();
		}

	} while (success && g_rom_image_address >= CART_ROM_SIZE);

	// display statistics
	if (g_compressed_mode)
		PRINT_INFO(L"\nCompressed mode statistic:");
	else
		PRINT_INFO(L"\nStorege statistics:");

	PRINT_INFO(L" %d bytes used, %d bytes free (%d total bytes)", g_rom_image_address, CART_ROM_SIZE - g_rom_image_address, CART_ROM_SIZE);

	// fill remaining bytes with FFH
	if (success)
	{
		while (g_rom_image_address < CART_ROM_SIZE)
		{
			g_rom_image[g_rom_image_address++] = 0xff;
		}
	}

	return success;
}

///////////////////////////////////////////////////////////////////////////////
// Creates loader code
bool CreateROMLoader()
{
	FILE* loader_file = NULL;
	wchar_t* loader_name;

	if (g_compressed_mode)
		loader_name = L"kilocartloader_compressed.bin";
	else
		loader_name = L"kilocartloader.bin";

	if (_wfopen_s(&loader_file, loader_name, L"rb") != 0)
	{
		PRINT_ERROR(L"\nCan't open file!");
		return false;
	}

	// get file size
	if (loader_file != NULL)
	{
		fseek(loader_file, 0, SEEK_END);

		int size = ftell(loader_file);
		if (size >= CART_ROM_SIZE)
		{
			PRINT_ERROR(L"\nInvalid loader!");
			fclose(loader_file);
			return false;
		}

		fseek(loader_file, 0, SEEK_SET);

		fread(g_rom_image, size, 1, loader_file);

		fclose(loader_file);

		g_rom_image_address = size;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////
// Creates directory on the ROM image
bool CreateROMDirectory()
{
	ROMFileInfo* file_info;
	int file_info_address;
	int file_count = 0;
	bool file_system_version2x = false;
	wchar_t buffer[MAX_PATH_LENGTH];

	ROMFileSystemInfo* file_system_info = (ROMFileSystemInfo*)(g_rom_image + g_rom_file_system_info_address);
	file_system_info->FilesAddress = g_rom_files_address;
	file_system_info->Directory1xAddress = g_rom_file_system_info_address + sizeof(ROMFileSystemInfo);

	// create directory entries
	for (int i = 0; i < g_file_info_count; i++)
	{
		file_info_address = g_rom_file_system_info_address + sizeof(ROMFileSystemInfo) + i * sizeof(ROMFileInfo);
		file_info = (ROMFileInfo*)(g_rom_image + file_info_address);

		// change to 2x ROM version if required
		if (g_file_info[i].Version2xFile && !file_system_version2x)
		{
			file_system_version2x = true;
			file_system_info->Files1xCount = (uint8_t)file_count;
			file_count = 0;
			file_system_info->Directory2xAddress = (uint16_t)file_info_address;
		}

		// convert and copy file name
		GetFileNameAndExtension(buffer, MAX_PATH_LENGTH, g_file_info[i].Filename);
		_wcsupr_s(buffer, MAX_PATH_LENGTH);
		PCToTVCFilenameAndExtension(file_info->Filename, buffer);

		file_info->Address = g_file_info[i].ROMAddress;
		file_info->Length = (uint16_t)g_file_info[i].Length;

		file_count++;
	}

	// update file system info
	if (file_system_version2x)
	{
		file_system_info->Files2xCount = file_count;
	}
	else
	{
		file_system_info->Files1xCount = file_count;
		file_system_info->Directory2xAddress = file_system_info->Directory1xAddress;
		file_system_info->Files2xCount = file_system_info->Files1xCount;
	}

	return true;
}

///////////////////////////////////////////////////////////////////////////////
// Creates files on the ROM image
bool CreateROMFileSystem()
{
	int j;
	int byte_count;
	uint8_t* compressed_data = NULL;
	size_t compressed_size = 0;
	int length;
	uint8_t* source;

	// generate files in the ROM
	for (int i = 0; i < g_file_info_count; i++)
	{
		// check if file is already in the ROM image
		j = 0;
		while (j < i)
		{
			if (CompareFilenames(g_file_info[i].Filename, g_file_info[j].Filename) == 0)
				break;

			j++;
		}

		if (j < i)
		{
			// file already included in the image, copy only the address
			g_file_info[i].ROMAddress = g_file_info[j].ROMAddress;
		}
		else
		{
			// update ROM address
			g_file_info[i].ROMAddress = g_rom_image_address;

			if (g_compressed_mode)
			{
				compressed_data = ZX7Compress(ZX7Optimize(g_file_buffer + g_file_info[i].BufferPos, g_file_info[i].Length), g_file_buffer + g_file_info[i].BufferPos, g_file_info[i].Length, &compressed_size);
				length = (int)compressed_size;
				source = compressed_data;
			}
			else
			{
				source = (uint8_t*)(g_file_buffer + g_file_info[i].BufferPos);
				length = g_file_info[i].Length;
			}

			// copy file to the ROM image
			for (byte_count = 0; byte_count < length; byte_count++)
			{
				if ((g_rom_image_address % CART_PAGE_SIZE) >= ROM_PAGE_CHANGE_ADDRESS)
				{
					// cover page change addresses with some less useful data
					g_rom_image[g_rom_image_address++] = 0;
					g_rom_image[g_rom_image_address++] = 1;
					g_rom_image[g_rom_image_address++] = 2;
					g_rom_image[g_rom_image_address++] = 3;

					// copy page start bytes
					memcpy(g_rom_image + g_rom_image_address, g_page_start_bytes, sizeof(g_page_start_bytes));
					g_rom_image_address += sizeof(g_page_start_bytes);
				}

				g_rom_image[g_rom_image_address++] = *source;
				source++;
			}

			if(g_compressed_mode)
			{
				free(compressed_data);
			}
		}
	}

	return true;
}