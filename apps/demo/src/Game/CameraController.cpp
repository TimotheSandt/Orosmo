#include "CameraController.h"

#include "InputManager.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

CameraController::CameraController()
{
    InputManager::BindActionToInput("Camera::MoveForward", KeyButton::Z);
    InputManager::BindActionToInput("Camera::MoveBackward", KeyButton::S);
    InputManager::BindActionToInput("Camera::MoveLeft", KeyButton::Q);
    InputManager::BindActionToInput("Camera::MoveRight", KeyButton::D);
    InputManager::BindActionToInput("Camera::MoveUp", KeyButton::SPACE);
    InputManager::BindActionToInput("Camera::MoveDown", KeyButton::LEFT_SHIFT);
    InputManager::BindActionToInput("Camera::SpeedDown", KeyButton::E);
    InputManager::BindActionToInput("Camera::SpeedUp", KeyButton::A);
    InputManager::BindActionToInput("Camera::Rotate", MouseButton::RIGHT);
    InputManager::BindActionToInput("Camera::ToggleWireframe", KeyButton::W);
}

void CameraController::update(GLFWwindow *window, engine::renderer::Camera &camera, float elapsedSeconds, int viewWidth, int viewHeight)
{
    const float distance = speed_ * elapsedSeconds;
    InputManager &input = InputManager::GetInstance(window);

    if (input.IsActionActive("Camera::MoveForward"))
    {
        camera.moveForward(distance);
    }
    if (input.IsActionActive("Camera::MoveBackward"))
    {
        camera.moveForward(-distance);
    }
    if (input.IsActionActive("Camera::MoveLeft"))
    {
        camera.moveRight(-distance);
    }
    if (input.IsActionActive("Camera::MoveRight"))
    {
        camera.moveRight(distance);
    }
    if (input.IsActionActive("Camera::MoveUp"))
    {
        camera.moveUp(distance);
    }
    if (input.IsActionActive("Camera::MoveDown"))
    {
        camera.moveUp(-distance);
    }

    if (input.IsActionActive("Camera::SpeedDown"))
    {
        speed_ = 1.0f;
    }
    else if (input.IsActionActive("Camera::SpeedUp"))
    {
        speed_ = 25.0f;
    }
    else
    {
        speed_ = 6.0f;
    }

    if (input.IsActionActive("Camera::Rotate") && viewWidth > 0 && viewHeight > 0)
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

        const double centerX = static_cast<double>(viewWidth) / 2.0;
        const double centerY = static_cast<double>(viewHeight) / 2.0;
        if (firstClick_)
        {
            glfwSetCursorPos(window, centerX, centerY);
            firstClick_ = false;
        }

        double mouseX = 0.0;
        double mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        const float rotX = sensitivity_ * static_cast<float>(mouseX - centerX) / static_cast<float>(viewWidth);
        const float rotY = sensitivity_ * static_cast<float>(mouseY - centerY) / static_cast<float>(viewHeight);
        camera.rotatePitch(-rotY);
        camera.rotateYaw(-rotX);
        glfwSetCursorPos(window, centerX, centerY);
    }
    else
    {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        firstClick_ = true;
    }

#if defined(_DEBUG) || defined(DEBUG)
    wireframe_ = input.IsActionActive("Camera::ToggleWireframe");
    camera.setWireframe(wireframe_);
#endif
}
