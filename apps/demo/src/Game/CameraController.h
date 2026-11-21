// Pilotage de la caméra au clavier et à la souris (ancien `Camera::Inputs`). La caméra du moteur (`orosmo::renderer::Camera`) ne lit
// aucune entrée : c'est l'application qui la déplace. À terme, ceci passe par `platform::Input` (M2 lot C).
#pragma once

#include "orosmo/renderer/camera.hpp"

namespace orosmo::platform
{
class Window;
}

class CameraController
{
  public:
    CameraController();

    // Déplace et oriente `camera` selon les actions actives de l'InputManager (clavier, clic droit pour tourner). `viewWidth` et
    // `viewHeight` : taille de la vue en pixels (centre de rotation de la souris).
    void update(orosmo::platform::Window &window, orosmo::renderer::Camera &camera, float elapsedSeconds, int viewWidth,
                int viewHeight);
    // Fil de fer (touche W, en debug seulement) : l'état voulu après `update`.
    bool wireframeRequested() const noexcept { return wireframe_; }

  private:
    float speed_ = 6.0f;
    float sensitivity_ = 100.0f;
    bool firstClick_ = true;
    bool wireframe_ = false;
};
