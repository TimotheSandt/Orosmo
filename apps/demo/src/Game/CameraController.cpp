#include "CameraController.h"

#include <orosmo/platform/input_manager.hpp>
#include <orosmo/platform/window.hpp>

namespace platform = orosmo::platform;

CameraController::CameraController()
{
    platform::InputManager::bindActionToInput("Camera::MoveForward", platform::KeyButton::kZ);
    platform::InputManager::bindActionToInput("Camera::MoveBackward", platform::KeyButton::kS);
    platform::InputManager::bindActionToInput("Camera::MoveLeft", platform::KeyButton::kQ);
    platform::InputManager::bindActionToInput("Camera::MoveRight", platform::KeyButton::kD);
    platform::InputManager::bindActionToInput("Camera::MoveUp", platform::KeyButton::kSpace);
    platform::InputManager::bindActionToInput("Camera::MoveDown", platform::KeyButton::kLeftShift);
    platform::InputManager::bindActionToInput("Camera::SpeedDown", platform::KeyButton::kE);
    platform::InputManager::bindActionToInput("Camera::SpeedUp", platform::KeyButton::kA);
    platform::InputManager::bindActionToInput("Camera::Rotate", platform::MouseButton::kRight);
    platform::InputManager::bindActionToInput("Camera::ToggleWireframe", platform::KeyButton::kW);
}

void CameraController::update(platform::Window &window, orosmo::renderer::Camera &camera, float elapsedSeconds, int viewWidth,
                              int viewHeight)
{
    const float distance = speed_ * elapsedSeconds;
    platform::InputManager &input = platform::InputManager::getInstance(window.getWindow());

    if (input.isActionActive("Camera::MoveForward"))
    {
        camera.moveForward(distance);
    }
    if (input.isActionActive("Camera::MoveBackward"))
    {
        camera.moveForward(-distance);
    }
    if (input.isActionActive("Camera::MoveLeft"))
    {
        camera.moveRight(-distance);
    }
    if (input.isActionActive("Camera::MoveRight"))
    {
        camera.moveRight(distance);
    }
    if (input.isActionActive("Camera::MoveUp"))
    {
        camera.moveUp(distance);
    }
    if (input.isActionActive("Camera::MoveDown"))
    {
        camera.moveUp(-distance);
    }

    if (input.isActionActive("Camera::SpeedDown"))
    {
        speed_ = 1.0f;
    }
    else if (input.isActionActive("Camera::SpeedUp"))
    {
        speed_ = 25.0f;
    }
    else
    {
        speed_ = 6.0f;
    }

    if (input.isActionActive("Camera::Rotate") && viewWidth > 0 && viewHeight > 0)
    {
        window.setCursorMode(platform::CursorMode::kDisabled);
        const glm::dvec2 mouse = window.getCursorPosition();
        if (firstClick_)
        {
            lastCursor_ = mouse; // capture du curseur : pas de rotation à la première image
            firstClick_ = false;
        }
        const glm::dvec2 delta = mouse - lastCursor_;
        lastCursor_ = mouse;
        const float rotX = sensitivity_ * static_cast<float>(delta.x) / static_cast<float>(viewWidth);
        const float rotY = sensitivity_ * static_cast<float>(delta.y) / static_cast<float>(viewHeight);
        camera.rotatePitch(-rotY);
        camera.rotateYaw(-rotX);
    }
    else
    {
        window.setCursorMode(platform::CursorMode::kNormal);
        firstClick_ = true;
    }

#if defined(_DEBUG) || defined(DEBUG)
    wireframe_ = input.isActionActive("Camera::ToggleWireframe");
    camera.setWireframe(wireframe_);
#endif
}
