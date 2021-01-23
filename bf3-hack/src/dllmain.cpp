#include <Windows.h>
#include <iostream>
#include <vector>

#include <d3d11.h>
#pragma comment(lib, "d3d11")

#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "../imgui/imgui_impl_dx11.h"
#include "../imgui/imgui_impl_win32.h"

#include "../minhook/minhook.h"
#pragma comment (lib, "minhook/minhook.lib")

LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

using fPresent = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
fPresent pPresent = NULL;

using fResizeBuffers = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
fResizeBuffers pResizeBuffers = NULL;

WNDPROC pWndProc = NULL;

HWND hwndGame = NULL;

LPVOID pPresentAddress = NULL;
LPVOID pResizeBuffersAddress = NULL;

ID3D11Device* device = nullptr;
ID3D11DeviceContext* context = nullptr;
ID3D11RenderTargetView* render_view = nullptr;

static bool renderview_lost = true;

enum IDXGISwapChainvTable //for dx10 / dx11
{
	QUERY_INTERFACE,
	ADD_REF, 
	RELEASE,
	SET_PRIVATE_DATA,
	SET_PRIVATE_DATA_INTERFACE,
	GET_PRIVATE_DATA,
	GET_PARENT,
	GET_DEVICE,
	PRESENT,
	GET_BUFFER,
	SET_FULLSCREEN_STATE,
	GET_FULLSCREEN_STATE,
	GET_DESC,
	RESIZE_BUFFERS,
	RESIZE_TARGET,
	GET_CONTAINING_OUTPUT,
	GET_FRAME_STATISTICS,
	GET_LAST_PRESENT_COUNT
};

namespace vars
{
	bool unload_library;
	bool menu_open;
	namespace visuals
	{
		bool enable;
		bool teammates;
		bool box;
		bool name;
		bool health;
		float enemy_color[3];
		float teammate_color[3];
	}
	namespace misc
	{
		bool rapid_fire;
		bool no_recoil;
	}
	void load_default_settings()
	{
		visuals::enemy_color[0] = 1.f;
		visuals::teammate_color[2] = 1.f;
	}
}

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
				return T();

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

namespace game_utils
{
	class Matrix4x4
	{
	public:
		union
		{
			struct
			{
				float        _11, _12, _13, _14;
				float        _21, _22, _23, _24;
				float        _31, _32, _33, _34;
				float        _41, _42, _43, _44;
			};
			float m[4][4];
		};
	};

	Matrix4x4 get_matrix()
	{
		return memory_utils::read<Matrix4x4>({ memory_utils::get_base_address(), 0x1F4F564, 0xB8, 0x4, 0xB8, 0x24, 0x18, 0xC0 });
	}

	bool WorldToScreen(const float vIn[3], float* flOut)
	{
		Matrix4x4 view_projection = get_matrix();

		float w = view_projection.m[0][3] * vIn[0] + view_projection.m[1][3] * vIn[1] + view_projection.m[2][3] * vIn[2] + view_projection.m[3][3];

		if (w < 0.01)
			return false;

		flOut[0] = view_projection.m[0][0] * vIn[0] + view_projection.m[1][0] * vIn[1] + view_projection.m[2][0] * vIn[2] + view_projection.m[3][0];
		flOut[1] = view_projection.m[0][1] * vIn[0] + view_projection.m[1][1] * vIn[1] + view_projection.m[2][1] * vIn[2] + view_projection.m[3][1];

		float invw = 1.0f / w;

		flOut[0] *= invw;
		flOut[1] *= invw;

		int width, height;

		auto io = ImGui::GetIO();
		width = io.DisplaySize.x;
		height = io.DisplaySize.y;

		float x = (float)width / 2;
		float y = (float)height / 2;

		x += 0.5 * flOut[0] * (float)width + 0.5;
		y -= 0.5 * flOut[1] * (float)height + 0.5;

		flOut[0] = x;
		flOut[1] = y;

		return true;
	}
}

namespace drawing
{
	void AddCircle(const ImVec2& position, float radius, const ImColor& color, int segments)
	{
		auto window = ImGui::GetCurrentWindow();

		window->DrawList->AddCircle(position, radius, ImGui::ColorConvertFloat4ToU32(color), segments);
	}

	void AddRect(const ImVec2& position, const ImVec2& size, const ImColor& color, float rounding = 0.f)
	{
		auto window = ImGui::GetCurrentWindow();

		window->DrawList->AddRect(position, size, ImGui::ColorConvertFloat4ToU32(color), rounding);
	}

	void AddRectFilled(const ImVec2& position, const ImVec2& size, const ImColor& color, float rounding)
	{
		auto window = ImGui::GetCurrentWindow();

		window->DrawList->AddRectFilled(position, size, ImGui::ColorConvertFloat4ToU32(color), rounding);
	}

	void DrawBox(float x, float y, float w, float h, const ImColor& color)
	{
		AddRect(ImVec2(x, y), ImVec2(x + w, y + h), color);
	}

	void DrawFillArea(float x, float y, float w, float h, const ImColor& color, float rounding = 0.f)
	{
		AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), color, rounding);
	}

	void DrawEspBox(float x, float y, float w, float h, const ImColor& color)
	{
		if (vars::visuals::box == false)
			return;

		DrawBox(x, y, w, h, color);
	}

	enum
	{
		FL_NONE = 1 << 0,
		FL_SHADOW = 1 << 1,
		FL_OUTLINE = 1 << 2,
		FL_CENTER_X = 1 << 3,
		FL_CENTER_Y = 1 << 4
	};

	void AddText(float x, float y, const ImColor& color, int flags, const char* format, ...)
	{
		int style = 0;

		if (!format)
			return;

		auto& io = ImGui::GetIO();
		auto DrawList = ImGui::GetWindowDrawList();
		auto Font = io.Fonts->Fonts[0];

		char szBuff[256] = { '\0' };
		va_list vlist = nullptr;
		va_start(vlist, format);
		vsprintf_s(szBuff, format, vlist);
		va_end(vlist);

		DrawList->PushTextureID(io.Fonts->TexID);

		float size = Font->FontSize;
		ImVec2 text_size = Font->CalcTextSizeA(size, FLT_MAX, 0.f, szBuff);

		ImColor Color = ImColor(0.f, 0.f, 0.f, color.Value.w);

		if (flags & FL_CENTER_X)
			x -= text_size.x / 2.f;

		if (flags & FL_CENTER_Y)
			y -= text_size.x / 2.f;

		if (style == 1)
			DrawList->AddText(Font, size, ImVec2(x + 1.f, y + 1.f), ImGui::ColorConvertFloat4ToU32(Color), szBuff);

		if (style == 2)
		{
			DrawList->AddText(Font, size, ImVec2(x, y - 1.f), ImGui::ColorConvertFloat4ToU32(Color), szBuff);
			DrawList->AddText(Font, size, ImVec2(x, y + 1.f), ImGui::ColorConvertFloat4ToU32(Color), szBuff);
			DrawList->AddText(Font, size, ImVec2(x + 1.f, y), ImGui::ColorConvertFloat4ToU32(Color), szBuff);
			DrawList->AddText(Font, size, ImVec2(x - 1.f, y), ImGui::ColorConvertFloat4ToU32(Color), szBuff);

			DrawList->AddText(Font, size, ImVec2(x - 1.f, y - 1.f), ImGui::ColorConvertFloat4ToU32(Color), szBuff);
			DrawList->AddText(Font, size, ImVec2(x + 1.f, y - 1.f), ImGui::ColorConvertFloat4ToU32(Color), szBuff);
			DrawList->AddText(Font, size, ImVec2(x - 1.f, y + 1.f), ImGui::ColorConvertFloat4ToU32(Color), szBuff);
			DrawList->AddText(Font, size, ImVec2(x + 1.f, y + 1.f), ImGui::ColorConvertFloat4ToU32(Color), szBuff);
		}

		DrawList->AddText(Font, size, ImVec2(x, y), ImGui::ColorConvertFloat4ToU32(color), szBuff);
		DrawList->PopTextureID();
	}

	void DrawName(const char* pcszPlayerName, float x, float y, float w, ImColor col)
	{
		if (vars::visuals::name == false)
			return;

		if (pcszPlayerName == NULL)
			return;

		ImFont* Font = ImGui::GetIO().Fonts->Fonts[0];
		ImVec2 text_size = Font->CalcTextSizeA(Font->FontSize, FLT_MAX, 0, "");

		AddText(x + w / 2.f, y - text_size.y - 2.f, ImColor(col.Value.x, col.Value.y, col.Value.z, col.Value.w), FL_CENTER_X, u8"%s", pcszPlayerName);
	}

	void DrawHealth(float x, float y, float h, float health, float max_health, ImColor col)
	{
		if (vars::visuals::health == false)
			return;

		health = ImClamp(health, 0.f, max_health);

		const auto size = h / max_health * health;
		const auto thickness = 2.f;

		DrawFillArea(x - thickness - 1.9f, y + h, thickness, -size, ImColor(col.Value.x, col.Value.y, col.Value.z, col.Value.w));

		//DrawBox(x - thickness - 2.9f, y - 1.f, thickness + 2.f, h + 2.f, ImColor(0.f, 0.f, 0.f, col.Value.w));
	}
}

namespace imgui_fonts
{
	ImFont* font_Main;
	ImFont* font_Credits;
}

HRESULT WINAPI wndproc_hooked(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	static auto once = []()
	{
		std::cout << __FUNCTION__ << " > first called!" << std::endl;
		return true;
	}();

	if (Msg == WM_KEYDOWN && wParam == VK_INSERT)
		vars::menu_open = !vars::menu_open;

	if (vars::menu_open && ImGui_ImplWin32_WndProcHandler(hWnd, Msg, wParam, lParam))
		return TRUE;

	return CallWindowProc(pWndProc, hWnd, Msg, wParam, lParam);
}

void initialize_imgui()
{
	ImGui::CreateContext();
	ImGui::StyleColorsClassic();

	auto& style = ImGui::GetStyle();

	style.FrameRounding = 3.f;
	style.ChildRounding = 3.f;
	style.ChildBorderSize = 1.f;
	style.ScrollbarSize = 0.6f;
	style.ScrollbarRounding = 3.f;
	style.GrabRounding = 3.f;
	style.WindowRounding = 0.f;

	style.Colors[ImGuiCol_FrameBg] = ImColor(200, 200, 200);
	style.Colors[ImGuiCol_FrameBgHovered] = ImColor(220, 220, 220);
	style.Colors[ImGuiCol_FrameBgActive] = ImColor(230, 230, 230);
	style.Colors[ImGuiCol_Separator] = ImColor(180, 180, 180);
	style.Colors[ImGuiCol_CheckMark] = ImColor(255, 172, 19);
	style.Colors[ImGuiCol_SliderGrab] = ImColor(255, 172, 19);
	style.Colors[ImGuiCol_SliderGrabActive] = ImColor(255, 172, 19);
	style.Colors[ImGuiCol_ScrollbarBg] = ImColor(120, 120, 120);
	style.Colors[ImGuiCol_ScrollbarGrab] = ImColor(255, 172, 19);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrab);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrab);
	style.Colors[ImGuiCol_Header] = ImColor(160, 160, 160);
	style.Colors[ImGuiCol_HeaderHovered] = ImColor(200, 200, 200);
	style.Colors[ImGuiCol_Button] = ImColor(180, 180, 180);
	style.Colors[ImGuiCol_ButtonHovered] = ImColor(200, 200, 200);
	style.Colors[ImGuiCol_ButtonActive] = ImColor(230, 230, 230);
	style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.78f, 0.78f, 0.78f, 1.f);
	style.Colors[ImGuiCol_WindowBg] = ImColor(220, 220, 220, 0.7 * 255);
	style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.75f, 0.75f, 0.75f, 0.53f);
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.40f, 0.40f, 0.55f, 0.80f);
	style.Colors[ImGuiCol_Border] = ImVec4(0.72f, 0.72f, 0.72f, 0.70f);
	style.Colors[ImGuiCol_TitleBg] = ImVec4(0.77f, 0.77f, 0.77f, 0.83f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.75f, 0.75f, 0.75f, 0.87f);
	style.Colors[ImGuiCol_Text] = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
	style.Colors[ImGuiCol_ChildBg] = ImVec4(0.72f, 0.72f, 0.72f, 0.76f);
	style.Colors[ImGuiCol_PopupBg] = ImVec4(0.76f, 0.76f, 0.76f, 1.00f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.81f, 0.81f, 0.81f, 1.00f);
	style.Colors[ImGuiCol_Tab] = ImVec4(0.61f, 0.61f, 0.61f, 0.79f);
	style.Colors[ImGuiCol_TabHovered] = ImVec4(0.71f, 0.71f, 0.71f, 0.80f);
	style.Colors[ImGuiCol_TabActive] = ImVec4(0.77f, 0.77f, 0.77f, 0.84f);
	style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.73f, 0.73f, 0.73f, 0.82f);
	style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.58f, 0.58f, 0.58f, 0.84f);

	auto& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 15.0f, NULL, io.Fonts->GetGlyphRangesCyrillic());
	imgui_fonts::font_Main = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 21);
	imgui_fonts::font_Credits = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\Verdana.ttf", 15);
	ImGui_ImplWin32_Init(hwndGame);
	ImGui_ImplDX11_Init(device, context);
	ImGui_ImplDX11_CreateDeviceObjects();

	ImGuiWindowFlags flags_color_edit = ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoInputs;
	ImGui::SetColorEditOptions(flags_color_edit);
}

namespace functions
{
	namespace visuals
	{
		void esp()
		{
			if (vars::visuals::enable == false)
				return;

			DWORD local_player = memory_utils::read<DWORD>({ memory_utils::get_base_address(), 0x1EF25C4, 0x48, 0x4, 0x68 });

			if (local_player == NULL)
				return;

			char* my_name = memory_utils::read_string({ local_player, 0x28 });

			int my_team = memory_utils::read<int>({ local_player, 0x31C });

			DWORD entity_list = memory_utils::read<DWORD>({ memory_utils::get_base_address(), 0x1EF25C4, 0x48, 0x4, 0x6C });

			if (entity_list == NULL)
				return;

			for (int i = 0; i < 64; i++)
			{
				DWORD entity = memory_utils::read<DWORD>({ entity_list, (DWORD)(i * 0x4) });

				if (entity == NULL)
					continue;

				char* name = memory_utils::read_string({ entity, 0x28 });

				if (name == NULL || strcmp(name, my_name) == 0)
					continue;

				int team = memory_utils::read<int>({ entity, 0x31C });

				auto col = team == my_team ? 
					ImColor(vars::visuals::teammate_color[0], vars::visuals::teammate_color[1], vars::visuals::teammate_color[2])
						: ImColor(vars::visuals::enemy_color[0], vars::visuals::enemy_color[1], vars::visuals::enemy_color[2]);

				if (vars::visuals::teammates == false && team == my_team)
					continue;

				DWORD player_entity = memory_utils::read<DWORD>({ entity, 0x3D8 });

				if (player_entity == NULL)
					continue;

				float health = memory_utils::read<float>({ player_entity, 0x20 });

				if (health <= 0)
					continue;

				int position_type = memory_utils::read<int>({ player_entity, 0x304 });

				float origin_bottom[3];
				origin_bottom[0] = memory_utils::read<float>({ player_entity, 0x2F0 });
				origin_bottom[1] = memory_utils::read<float>({ player_entity, 0x2F4 });
				origin_bottom[2] = memory_utils::read<float>({ player_entity, 0x2F8 });

				float origin_top[3] = { origin_bottom[0], origin_bottom[1], origin_bottom[2] };

				if (position_type == 2)
					origin_top[1] += 0.4f;
				else if (position_type == 1)
					origin_top[1] += 1.2f;
				else
					origin_top[1] += 1.5f;
				
				float out_bot[2], out_top[2];
				if (game_utils::WorldToScreen(origin_bottom, out_bot) && game_utils::WorldToScreen(origin_top, out_top))
				{
					float h = out_bot[1] - out_top[1];
					float w = h / 2;
					float x = out_bot[0] - w / 2;
					float y = out_top[1];

					drawing::DrawEspBox(x, y, w, h, col);

					drawing::DrawName(name, x, y, w, ImColor(1.f, 1.f, 1.f));

					drawing::DrawHealth(x, y, h, health, 100.f, ImColor(0.f, 1.f, 0.f));
				}
			}
		}
	}
	namespace misc
	{
		void rapid_fire(const bool is_enable)
		{
			static DWORD fire_count_instruction_address = NULL;

			if (fire_count_instruction_address == NULL)
			{
				//D9 9E B8 00 00 00 8B 86
				fire_count_instruction_address = memory_utils::find_pattern(memory_utils::get_base(), "\xD9\x9E\xB8\x00\x00\x00\x8B\x86", "xxxxxxxx");
			}

			if (is_enable)
			{
				//90 90 90 90 90 90
				memory_utils::patch_instruction(fire_count_instruction_address, "\x90\x90\x90\x90\x90\x90", 6);
			}
			else
			{
				//D9 9E B8 00 00 00
				memory_utils::patch_instruction(fire_count_instruction_address, "\xD9\x9E\xB8\x00\x00\x00", 6);
			}
		}

		void no_recoil(const bool is_enable)
		{
			static DWORD recoil_pitch_instruction_address = NULL;
			static DWORD recoil_yaw_instruction_address = NULL;

			if (recoil_pitch_instruction_address == NULL || recoil_yaw_instruction_address == NULL)
			{
				recoil_pitch_instruction_address = memory_utils::find_pattern(memory_utils::get_base(), "\xD9\x9E\xAC\x00\x00\x00\xF3", "xxxxxxx");
				recoil_yaw_instruction_address = memory_utils::find_pattern(memory_utils::get_base(), "\xD8\x86\xB0\x00\x00\x00\xD9", "xxxxxxx");
			}

			if (is_enable)
			{
				memory_utils::patch_instruction(recoil_pitch_instruction_address, "\x90\x90\x90\x90\x90\x90", 6);
				memory_utils::patch_instruction(recoil_yaw_instruction_address, "\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90", 12);
			}
			else
			{
				memory_utils::patch_instruction(recoil_pitch_instruction_address, "\xD9\x9E\xAC\x00\x00\x00", 6);
				memory_utils::patch_instruction(recoil_yaw_instruction_address, "\xD8\x86\xB0\x00\x00\x00\xD9\x9E\xB0\x00\x00\x00", 12);
			}
		}
	}
	void run()
	{
		visuals::esp();
	}
}

void begin_scene()
{
	if (vars::unload_library)
		return;

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();

	if (vars::menu_open)
	{
		ImGui::GetIO().MouseDrawCursor = true;
		ImGui::Begin("test", &vars::menu_open);
		ImGui::BeginChild("functions", ImVec2(), true);
		ImGui::Text("Visuals:");
		ImGui::Checkbox("Enable", &vars::visuals::enable);
		ImGui::Checkbox("Teammates", &vars::visuals::teammates);
		ImGui::Checkbox("Box", &vars::visuals::box);
		ImGui::Checkbox("Name", &vars::visuals::name);
		ImGui::Checkbox("Health", &vars::visuals::health);
		ImGui::ColorEdit3("Enemy color", vars::visuals::enemy_color);
		ImGui::ColorEdit3("Teammate color", vars::visuals::teammate_color);
		ImGui::Text("Misc:");

		std::string no_recoil_status = vars::misc::no_recoil ? "Disable no recoil" : "Enable no recoil";
		if (ImGui::Button(no_recoil_status.c_str()))
		{
			vars::misc::no_recoil = !vars::misc::no_recoil;
			functions::misc::no_recoil(vars::misc::no_recoil);
		}

		std::string rapid_fire_status = vars::misc::rapid_fire ? "Disable rapid fire" : "Enable rapid fire";
		if (ImGui::Button(rapid_fire_status.c_str()))
		{
			vars::misc::rapid_fire = !vars::misc::rapid_fire;
			functions::misc::rapid_fire(vars::misc::rapid_fire);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Works like instant kill");

		ImGui::EndChild();
		ImGui::End();
	}
	else
	{
		ImGui::GetIO().MouseDrawCursor = false;
	}

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4());
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::Begin("##BackBuffer", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
	ImGui::SetWindowPos(ImVec2(), ImGuiCond_Always);
	ImGui::SetWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y), ImGuiCond_Always);

	functions::run();

	ImGui::GetCurrentWindow()->DrawList->PushClipRectFullScreen();
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar();

	ImGui::EndFrame();

	ImGui::Render();

	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

HRESULT WINAPI present_hooked(IDXGISwapChain* pChain, UINT SyncInterval, UINT Flags)
{
	if (renderview_lost)
	{
		if (SUCCEEDED(pChain->GetDevice(__uuidof(ID3D11Device), (void**)&device)))
		{
			device->GetImmediateContext(&context);

			ID3D11Texture2D* pBackBuffer;
			pChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
			device->CreateRenderTargetView(pBackBuffer, NULL, &render_view);
			pBackBuffer->Release();

			std::cout << __FUNCTION__ << " > renderview successfully received!" << std::endl;
			renderview_lost = false;
		}
	}

	static auto once = [pChain, SyncInterval, Flags]()
	{
		initialize_imgui();
		std::cout << __FUNCTION__ << " > first called!" << std::endl;
		return true;
	}();

	context->OMSetRenderTargets(1, &render_view, NULL);

	begin_scene();

	return pPresent(pChain, SyncInterval, Flags);
}

HRESULT WINAPI resizebuffers_hooked(IDXGISwapChain* pChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT Flags)
{
	static auto once = []()
	{
		std::cout << __FUNCTION__ << " > first called!" << std::endl;
		return true;
	}();

	render_view->Release(); 
	render_view = nullptr;
	renderview_lost = true;

	ImGui_ImplDX11_CreateDeviceObjects();

	ImGui_ImplDX11_InvalidateDeviceObjects();

	return pResizeBuffers(pChain, BufferCount, Width, Height, NewFormat, Flags);
}

void hook_dx11()
{
	D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
	DXGI_SWAP_CHAIN_DESC scd{};
	ZeroMemory(&scd, sizeof(scd));
	scd.BufferCount = 1;
	scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	scd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	scd.OutputWindow = hwndGame;
	scd.SampleDesc.Count = 1;
	scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	bool isWindowed = false; // kekw

	auto style = GetWindowLong(hwndGame, GWL_STYLE);

	bool isBORDERLESS = style & WS_POPUP;
	bool isFULLSCREEN_OR_WINDOWED = style & WS_CAPTION;
	bool isFULLSCREEN = style & WS_MINIMIZE;

	if (isBORDERLESS) {

		std::cout << __FUNCTION__ << " > this shit is borderless\n";
		isWindowed = true;
	}
	else if (isFULLSCREEN_OR_WINDOWED) {
		if (isFULLSCREEN)
			std::cout << __FUNCTION__ << " > this shit is fullscreen\n";
		else {
			std::cout << __FUNCTION__ << " > this shit is windowed\n";
			isWindowed = true;
		}
	}

	scd.Windowed = isWindowed;
	scd.BufferDesc.RefreshRate.Numerator = 60;
	scd.BufferDesc.RefreshRate.Denominator = 1;

	IDXGISwapChain* swapchain = nullptr;

	if (FAILED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, NULL, &feature_level, 1, D3D11_SDK_VERSION, &scd, &swapchain, &device, NULL, &context)))
	{
		std::cout << "failed to create device\n";
		return;
	}

	void** vtable_swapchain = *(void***)swapchain;

	swapchain->Release();
	swapchain = nullptr;

	pPresentAddress = vtable_swapchain[IDXGISwapChainvTable::PRESENT];

	if (MH_CreateHook(pPresentAddress, &present_hooked, (LPVOID*)&pPresent) != MH_OK)
	{
		std::cout << __FUNCTION__ << " > failed create hook present\n";
		return;
	}

	if (MH_EnableHook(pPresentAddress) != MH_OK)
	{
		std::cout << __FUNCTION__ << " > failed enable hook present\n";
		return;
	}

	pResizeBuffersAddress = vtable_swapchain[IDXGISwapChainvTable::RESIZE_BUFFERS];

	if (MH_CreateHook(pResizeBuffersAddress, &resizebuffers_hooked, (LPVOID*)&pResizeBuffers) != MH_OK)
	{
		std::cout << __FUNCTION__ << " > failed create hook resizebuffers\n";
		return;
	}

	if (MH_EnableHook(pResizeBuffersAddress) != MH_OK)
	{
		std::cout << __FUNCTION__ << " > failed enable hook resizebuffers\n";
	}
}

void hook_wndproc()
{
	pWndProc = (WNDPROC)SetWindowLong(hwndGame, GWL_WNDPROC, (LONG_PTR)&wndproc_hooked);
}

void unhook(LPVOID address)
{
	MH_DisableHook(address);
	MH_RemoveHook(address);
	Sleep(100);
}

void unhook_wndproc()
{
	SetWindowLong(hwndGame, GWL_WNDPROC, (LONG)pWndProc);
	Sleep(100);
}

void shutdown_hacks()
{
	if (vars::misc::rapid_fire)
		functions::misc::rapid_fire(false);

	if (vars::misc::no_recoil)
		functions::misc::no_recoil(false);
}

void hack_thread(HMODULE module)
{
	console::attach();
	
	std::cout << __FUNCTION__ << " > attach success\n";

	hwndGame = FindWindow(NULL, "Battlefield 3™");

	if (hwndGame == NULL)
	{
		std::cout << __FUNCTION__ << " > game window not found\n";
		FreeLibraryAndExitThread(module, 1);
	}

	MH_Initialize();

	hook_dx11();

	if (pPresentAddress == NULL)
	{
		std::cout << __FUNCTION__ << " > failed hook dx11 present\n";
		FreeLibraryAndExitThread(module, 1);
	}

	if (pResizeBuffers == NULL)
	{
		std::cout << __FUNCTION__ << " > failed hook dx11 resizebuffers\n";
		FreeLibraryAndExitThread(module, 1);
	}

	hook_wndproc();

	while (true)
	{
		if (GetAsyncKeyState(VK_DELETE))
		{
			vars::unload_library = true;
			break;
		}
			
		Sleep(100);
	}

	unhook(pPresentAddress);

	unhook(pResizeBuffersAddress);

	render_view->Release();
	render_view = nullptr;

	unhook_wndproc();

	MH_Uninitialize();

	shutdown_hacks();

	std::cout << __FUNCTION__ << " > free library...\n";

	FreeLibraryAndExitThread(module, 0);
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		vars::load_default_settings();
		CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)hack_thread, hModule, NULL, NULL);
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}