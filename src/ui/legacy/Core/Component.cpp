#include "Core/Component.h"

#include <utility>
#include "Core/Container.h"

#include <engine/core/paths.hpp>

namespace UI
{

ComponentBase::ComponentBase(Bounds bounds) : localBounds(bounds)
{

    // Direct initialization with ForceSet to avoid deferred behavior
    this->theme = Theme::GetTheme("default");
    this->kind.ForceSet(IdentifierKind::PRIMARY);
    UpdateTheme();
}

void ComponentBase::Initialize()
{
    initialized = true;
    MarkSelfLayoutDirty();
    CalculatePixelSize();
}

void ComponentBase::Update()
{
    // Throttle gate: skip update if period hasn't elapsed
    if (throttlePeriod.count() > 0)
    {
        auto now = std::chrono::high_resolution_clock::now();
        if (now - lastThrottleUpdate < throttlePeriod)
        {
            throttledThisFrame = true;
            return;
        }
        lastThrottleUpdate = now;
    }
    throttledThisFrame = false;

    // Apply deferred values and mark dirty if they changed
    if (kind.Apply())
    {
        UpdateTheme();
        MarkAppearanceDirty();
    }
    if (color.Apply())
    {
        MarkAppearanceDirty();
    }
    if (visible.Apply())
    {
        MarkAppearanceDirty();
    }
    if (allowDeform.Apply())
    {
        MarkSelfLayoutDirty();
    }

    if (dirtyChildLayout || dirtySelfLayout)
    {
        CalculatePixelSize();
    }
}

glm::vec2 ComponentBase::CalculatePixelSize()
{
    if (auto p = parent.lock())
    {
        this->localBounds.scale = localBounds.getPixelSize(p->GetAvailableSize());
    }
    else
    {
        this->localBounds.scale = localBounds.getPixelSize();
    }

    return this->localBounds.scale;
}

void ComponentBase::Collect(engine::renderer::DrawList &list, glm::vec2 containerSize, glm::vec2 offset)
{
    (void)containerSize;
    if (!visible.Get())
    {
        return;
    }

    // offset already includes anchor offset from cachedBoundsInParent
    const glm::vec4 c = this->color.Get();
    if (c.a > 0.0f)
    {
        list.rect(offset, this->localBounds.scale, c);
    }

    ClearDirty();
}

bool ComponentBase::IsMouseOver(glm::vec2 mousePos, glm::vec2 offset) const { return localBounds.isHover(mousePos - offset); }

void ComponentBase::DoSetColor(glm::vec4 c)
{
    color.Set(c);
    MarkAppearanceDirty();
}

void ComponentBase::DoSetTheme(std::weak_ptr<Theme> t)
{
    theme = std::move(t);
    UpdateTheme();
    MarkAppearanceDirty();
}

void ComponentBase::DoSetIdentifierKind(IdentifierKind k)
{
    kind.Set(k);
    UpdateTheme();
    MarkAppearanceDirty();
}

void ComponentBase::DoSetThrottlePeriod(std::chrono::duration<double> period)
{
    throttlePeriod = period;
    lastThrottleUpdate = {};
}

void ComponentBase::DoSetAllowDeform(bool allow)
{
    allowDeform.Set(allow);
    if (!isDeformed && allowDeform.Get())
    {
        allowDeform.Apply();
    }
}

// Three-tier dirty system implementation
void ComponentBase::MarkAppearanceDirty()
{
    dirtyAppearance = true;
    NotifyParentChildAppearanceDirty();
}

void ComponentBase::MarkChildLayoutDirty()
{
    dirtyChildLayout = true;
    dirtyAppearance = true; // Layout implies appearance
    NotifyParentChildLayoutDirty();
}

void ComponentBase::MarkSelfLayoutDirty()
{
    dirtySelfLayout = true;
    dirtyChildLayout = true;
    dirtyAppearance = true; // Self layout implies all dirty
    NotifyParentChildLayoutDirty();
}

void ComponentBase::MarkFullDirty() { MarkSelfLayoutDirty(); }

void ComponentBase::NotifyParentChildLayoutDirty()
{
    if (auto p = parent.lock())
    {
        p->OnChildLayoutDirty();
    }
}

void ComponentBase::NotifyParentChildAppearanceDirty()
{
    if (auto p = parent.lock())
    {
        p->OnChildAppearanceDirty();
    }
}

void ComponentBase::NotifyParentFullDirty()
{
    if (auto p = parent.lock())
    {
        p->MarkFullDirty();
    }
}

void ComponentBase::UpdateTheme()
{
    if (auto t = theme.lock())
    {
        color.ForceSet(t->GetColor(kind.Get()));
    }
}

} // namespace UI
