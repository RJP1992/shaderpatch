#pragma once

namespace sp::game_support {

struct Game_memory {
   /// @brief Pointer to float - Projection Z component (far plane)
   float* projection_vector_z = nullptr;

   /// @brief Pointer to float - Projection Z component negative
   float* projection_vector_z_neg = nullptr;

   bool is_debug_executable = false;
};

auto get_game_memory() noexcept -> const Game_memory&;

}
