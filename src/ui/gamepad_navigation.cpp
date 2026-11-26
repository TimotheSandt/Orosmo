#include "orosmo/ui/gamepad_navigation.hpp"

#include <algorithm>

namespace orosmo::ui
{

void GamepadNavigator::apply(const platform::GamepadState *pad, InputFrame &frame)
{
    using platform::GamepadAxis;
    using platform::GamepadButton;
    std::array<bool, kCommandCount> down{};
    if (pad != nullptr && pad->connected)
    {
        const float dz = settings.deadZone;
        const float x = pad->axis(GamepadAxis::kLeftX);
        const float y = pad->axis(GamepadAxis::kLeftY); // positif vers le bas
        down[kUp] = pad->pressed(GamepadButton::kDpadUp) || y < -dz;
        down[kDown] = pad->pressed(GamepadButton::kDpadDown) || y > dz;
        down[kLeft] = pad->pressed(GamepadButton::kDpadLeft) || x < -dz;
        down[kRight] = pad->pressed(GamepadButton::kDpadRight) || x > dz;
        down[kAccept] = pad->pressed(GamepadButton::kA);
        down[kCancel] = pad->pressed(GamepadButton::kB);
        down[kNext] = pad->pressed(GamepadButton::kRightBumper);
        down[kPrevious] = pad->pressed(GamepadButton::kLeftBumper);
    }
    static constexpr std::array<int, kCommandCount> kKeys = {kKeyUp, kKeyDown, kKeyLeft, kKeyRight, kKeyEnter, kKeyEscape, kKeyTab, kKeyTab};
    for (std::size_t i = 0; i < kCommandCount; ++i)
    {
        if (!down[i])
        {
            held_[i] = false;
            continue;
        }
        bool fire = false;
        if (!held_[i])
        {
            fire = true; // front montant
            held_[i] = true;
            heldTime_[i] = 0.0f;
            nextRepeat_[i] = settings.repeatDelay;
        }
        else if (i <= kRight) // seules les directions se répètent (A tenu n'active pas deux fois)
        {
            heldTime_[i] += frame.deltaTime;
            if (heldTime_[i] >= nextRepeat_[i])
            {
                fire = true;
                nextRepeat_[i] += std::max(settings.repeatInterval, 1.0e-3f);
                if (nextRepeat_[i] <= heldTime_[i]) // image très longue : une seule répétition, pas une rafale
                    nextRepeat_[i] = heldTime_[i] + settings.repeatInterval;
            }
        }
        if (fire)
            frame.navigation.push_back({kKeys[i], i == kPrevious, {}});
    }
}

} // namespace orosmo::ui
