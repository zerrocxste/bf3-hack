#include <Windows.h>
#include <iostream>
#include <vector>

namespace memory_utils
{
	#ifdef _WIN64
		#define DWORD_OF_BITNESS DWORD64
		#define PTRMAXVAL ((PVOID)0x000F000000000000)
	#elif _WIN32
		#define DWORD_OF_BITNESS DWORD
		#define PTRMAXVAL ((PVOID)0xFFF00000)
	#endif

	bool is_valid_ptr(PVOID ptr)
	{
		return (ptr >= (PVOID)0x10000) && (ptr < PTRMAXVAL) && ptr != nullptr && !IsBadReadPtr(ptr, sizeof(ptr));
	}

	HMODULE base;

	HMODULE get_base()
	{
		if (!base)
			base = GetModuleHandle(0);
		return base;
	}

	DWORD_OF_BITNESS get_base_address()
	{
		return (DWORD_OF_BITNESS)get_base();
	}

	template<class T>
	void write(std::vector<DWORD_OF_BITNESS>address, T value)
	{
		size_t lengh_array = address.size() - 1;
		DWORD_OF_BITNESS relative_address;
		relative_address = address[0];
		for (int i = 1; i < lengh_array + 1; i++)
		{
			if (is_valid_ptr((LPVOID)relative_address) == false)
				return;

			if (i < lengh_array)
				relative_address = *(DWORD_OF_BITNESS*)(relative_address + address[i]);
			else
			{
				T* writable_address = (T*)(relative_address + address[lengh_array]);
				*writable_address = value;
			}
		}
	}

	template<class T>
	T read(std::vector<DWORD_OF_BITNESS>address)
	{
		size_t lengh_array = address.size() - 1;
		DWORD_OF_BITNESS relative_address;
		relative_address = address[0];
		for (int i = 1; i < lengh_array + 1; i++)
		{
			if (is_valid_ptr((LPVOID)relative_address) == false)
				return *static_cast<T*>(0);

			if (i < lengh_array)
				relative_address = *(DWORD_OF_BITNESS*)(relative_address + address[i]);
			else
			{
				T readable_address = *(T*)(relative_address + address[lengh_array]);
				return readable_address;
			}
		}
	}

	void write_string(std::vector<DWORD_OF_BITNESS>address, char* value)
	{
		size_t lengh_array = address.size() - 1;
		DWORD_OF_BITNESS relative_address;
		relative_address = address[0];
		for (int i = 1; i < lengh_array + 1; i++)
		{
			if (is_valid_ptr((LPVOID)relative_address) == false)
				return;

			if (i < lengh_array)
				relative_address = *(DWORD_OF_BITNESS*)(relative_address + address[i]);
			else
			{
				char* writable_address = (char*)(relative_address + address[lengh_array]);
				*writable_address = *value;
			}
		}
	}

	char* read_string(std::vector<DWORD_OF_BITNESS>address)
	{
		size_t lengh_array = address.size() - 1;
		DWORD_OF_BITNESS relative_address;
		relative_address = address[0];
		for (int i = 1; i < lengh_array + 1; i++)
		{
			if (is_valid_ptr((LPVOID)relative_address) == false)
				return NULL;

			if (i < lengh_array)
				relative_address = *(DWORD_OF_BITNESS*)(relative_address + address[i]);
			else
			{
				char* readable_address = (char*)(relative_address + address[lengh_array]);
				return readable_address;
			}
		}
	}

	DWORD_OF_BITNESS get_module_size(DWORD_OF_BITNESS address)
	{
		return PIMAGE_NT_HEADERS(address + (DWORD_OF_BITNESS)PIMAGE_DOS_HEADER(address)->e_lfanew)->OptionalHeader.SizeOfImage;
	}

	DWORD_OF_BITNESS find_pattern(HMODULE module, const char* pattern, const char* mask)
	{
		DWORD_OF_BITNESS base = (DWORD_OF_BITNESS)module;
		DWORD_OF_BITNESS size = get_module_size(base);

		DWORD_OF_BITNESS patternLength = (DWORD_OF_BITNESS)strlen(mask);

		for (DWORD_OF_BITNESS i = 0; i < size - patternLength; i++)
		{
			bool found = true;
			for (DWORD_OF_BITNESS j = 0; j < patternLength; j++)
			{
				found &= mask[j] == '?' || pattern[j] == *(char*)(base + i + j);
			}

			if (found)
			{
				return base + i;
			}
		}

		return NULL;
	}

	void patch_instruction(DWORD_OF_BITNESS instruction_address, const char* instruction_bytes, int sizeof_instruction_byte)
	{
		DWORD dwOldProtection;

		VirtualProtect((LPVOID)instruction_address, sizeof_instruction_byte, PAGE_EXECUTE_READWRITE, &dwOldProtection);

		memcpy((LPVOID)instruction_address, instruction_bytes, sizeof_instruction_byte);

		VirtualProtect((LPVOID)instruction_address, sizeof_instruction_byte, dwOldProtection, NULL);

		FlushInstructionCache(GetCurrentProcess(), (LPVOID)instruction_address, sizeof_instruction_byte);
	}
}

namespace console
{
	FILE* out;
	void attach()
	{
		if (AllocConsole())
		{
			freopen_s(&out, "conout$", "w", stdout);
		}
	}
	void free()
	{
		fclose(out);
		FreeConsole();
	}
}

void start_hack(PVOID module)
{
	console::attach();

	std::cout << "attach success\n";

	while (true)
	{
		if (GetAsyncKeyState(VK_DELETE))
			break;

		DWORD entity_list = memory_utils::read<DWORD>( { memory_utils::get_base_address(), 0x1EF25C4, 0x48, 0x4, 0x6C } );

		if (entity_list == NULL)
			continue;

		for (int i = 0; i < 64; i++)
		{
			DWORD entity = memory_utils::read<DWORD>( { entity_list, (DWORD)(i * 0x4) } );

			if (entity == NULL)
				continue;

			char* name = memory_utils::read_string( { entity,  0x28 } );

			if (name == NULL)
				continue;

			int team = memory_utils::read<int>( { entity, 0x31C } );

			DWORD player_entity = memory_utils::read<DWORD>( { entity, 0x3D8 } );

			if (player_entity == NULL)
				continue;

			float health = memory_utils::read<float>( { player_entity, 0x20 } );

			if (health <= 0)
				continue;

			DWORD entity_player_transform = memory_utils::read<DWORD>( { player_entity, 0x24C } );

			if (entity_player_transform == NULL)
				continue;

			float origin[3];
			origin[0] = memory_utils::read<float>( { entity_player_transform, 0x20 } );
			origin[1] = memory_utils::read<float>( { entity_player_transform, 0x24 } );
			origin[2] = memory_utils::read<float>( { entity_player_transform, 0x28 } );

			std::cout << "player id: " << i << " name: " << name << " health: " << health << " team: " << team 
				<< " origin.x: " << origin[0] << " origin.y: " << origin[1] << " origin.z: " << origin[2] << std::endl;
		}

		Sleep(200);
		std::system("cls");
	}

	std::cout << "free library...\n";
	FreeLibraryAndExitThread((HMODULE)module, 0);
}

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)start_hack, hModule, NULL, NULL);
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}