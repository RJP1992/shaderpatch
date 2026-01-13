#include "game_memory.hpp"

#include "../logger.hpp"

#include <Windows.h>

namespace sp::game_support {

namespace {

const char signature_string[] = "Application";

struct Executable_info {
   const char* version = "";
   std::uintptr_t base_address = 0;
   std::uintptr_t signature_ptr = 0;

   std::uintptr_t projection_vector_z_ptr = 0;
   std::uintptr_t projection_vector_z_neg_ptr = 0;

   bool is_debug_executable = false;
};

const Executable_info known_executables[] = {
   {
      .version = "GoG",

      .base_address = 0x00400000,

      .signature_ptr = 0x007a0698,

      .projection_vector_z_ptr = 0,      // TODO: Find address
      .projection_vector_z_neg_ptr = 0,  // TODO: Find address
   },

   {
      .version = "Steam",

      .base_address = 0x00400000,

      .signature_ptr = 0x0079f834,

      .projection_vector_z_ptr = 0,      // TODO: Find address
      .projection_vector_z_neg_ptr = 0,  // TODO: Find address
   },

   {
      .version = "DVD",

      .base_address = 0x00400000,

      .signature_ptr = 0x007bf12c,

      .projection_vector_z_ptr = 0,      // TODO: Find address
      .projection_vector_z_neg_ptr = 0,  // TODO: Find address
   },

   {
      .version = "Modtools",

      .base_address = 0x00400000,

      .signature_ptr = 0x00a2b59c,

      .projection_vector_z_ptr = 0x00a74cb0,
      .projection_vector_z_neg_ptr = 0x00a74794,

      .is_debug_executable = true,
   },
};

template<typename T = void>
auto adjust_ptr(const std::uint32_t pointer, const std::uintptr_t base_address,
                const std::uintptr_t executable_base) -> T*
{
   if (pointer == 0) return nullptr;

   return reinterpret_cast<T*>(pointer - base_address + executable_base);
}

int exception_filter(unsigned int code, [[maybe_unused]] EXCEPTION_POINTERS* ep)
{
   return code == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                             : EXCEPTION_CONTINUE_SEARCH;
}

auto init_game_memory() noexcept -> Game_memory
{
    const std::uintptr_t executable_base =
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    for (const Executable_info& info : known_executables) {
        __try {
            if (std::memcmp(signature_string,
                adjust_ptr(info.signature_ptr, info.base_address, executable_base),
                sizeof(signature_string)) == 0) {
                log_fmt(Log_level::info, "Identified game version as: {}", info.version);
                Game_memory mem = {
                   .projection_vector_z = adjust_ptr<float>(info.projection_vector_z_ptr,
                                                            info.base_address, executable_base),
                   .projection_vector_z_neg = adjust_ptr<float>(info.projection_vector_z_neg_ptr,
                                                                info.base_address, executable_base),
                   .is_debug_executable = info.is_debug_executable,
                };
                // Write once at startup - unprotect memory first
                if (mem.projection_vector_z && mem.projection_vector_z_neg) {
                    DWORD old_protect1, old_protect2;

                    VirtualProtect(mem.projection_vector_z, sizeof(float), PAGE_EXECUTE_READWRITE, &old_protect1);
                    VirtualProtect(mem.projection_vector_z_neg, sizeof(float), PAGE_EXECUTE_READWRITE, &old_protect2);

                    //*mem.projection_vector_z = 10000.0f;
                    //*mem.projection_vector_z_neg = -10000.0f;

                    VirtualProtect(mem.projection_vector_z, sizeof(float), old_protect1, &old_protect1);
                    VirtualProtect(mem.projection_vector_z_neg, sizeof(float), old_protect2, &old_protect2);

                    log(Log_level::info, "Projection range memory unlocked and available to adjust");
                }
                return mem;
            }
        }
        __except (exception_filter(GetExceptionCode(), GetExceptionInformation())) {
            break;
        }
    }
    log(Log_level::warning,
        "Couldn't identify game. Some features that depend "
        "on reading/writing the game's memory will not work.");
    return {};
}

}

auto get_game_memory() noexcept -> const Game_memory&
{
   static Game_memory memory = init_game_memory();

   return memory;
}

}
