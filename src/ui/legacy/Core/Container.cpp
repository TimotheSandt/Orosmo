#include "Core/Container.h"
#include <engine/core/log.hpp>
#include <engine/core/paths.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace UI
{



ContainerBase::ContainerBase(Bounds bounds, bool useRenderTarget) : ComponentBase(bounds)
{
    contentSize = localBounds.scale;
    renderToTexture.ForceSet(useRenderTarget);
    UpdateTheme();
}

void ContainerBase::AddChild(const std::shared_ptr<ComponentBase> &child)
{
    children.push_back(child);
    child->SetParent(std::static_pointer_cast<ContainerBase>(shared_from_this()));
    if (initialized)
    {
        child->Initialize();
    }
}

void ContainerBase::Initialize()
{
    ComponentBase::Initialize();

    for (auto &child : children)
    {
        child->Initialize();
    }
    RecalculateChildBounds();
}

void ContainerBase::Update()
{
    ComponentBase::Update();
    if (WasThrottled())
        return;

    // Apply deferred layout properties
    bool layoutChanged = false;
    if (padding.Apply())
    {
        layoutChanged = true;
    }
    if (spacing.Apply())
    {
        layoutChanged = true;
    }
    bool wasWrap = overflowMode.Get() == OverflowMode::WRAP;
    if (overflowMode.Apply())
    {
        if (wasWrap || (overflowMode.Get() == OverflowMode::WRAP))
        {
            layoutChanged = true;
        }
    }
    bool renderModeChanged = false;
    if (renderToTexture.Apply())
    {
        renderModeChanged = true;
        collectedLayerSize = {0, 0}; // la couche est recréée (ou abandonnée) au prochain Collect
        dirtySelfLayout = true;
        dirtyChildLayout = true;
        dirtyAppearance = true;
        NotifyParentChildAppearanceDirty();
    }

    if (layoutChanged)
    {
        MarkFullDirty();
    }

    for (auto &child : children)
    {
        child->Update();
    }
    if (dirtySelfLayout || dirtyChildLayout || renderModeChanged)
    {
        RecalculateChildBounds();
    }
}

void ContainerBase::Collect(engine::renderer::DrawList &list, glm::vec2 containerSize, glm::vec2 offset)
{
    if (!visible.Get())
    {
        collectedLayerSize = {0, 0}; // la couche sera recréée quand le conteneur redeviendra visible
        return;
    }

    if (dirtySelfLayout || dirtyChildLayout)
    {
        RecalculateChildBounds();
    }

    if (UsesOwnRenderTarget())
    {
        if (contentSize.x <= 0 || contentSize.y <= 0)
        {
            ClearDirty();
            return;
        }

        // Taille changée : la couche est recréée vide, tout est à redessiner (comme l'ancienne InitializeRenderTarget).
        if (collectedLayerSize != contentSize)
        {
            collectedLayerSize = contentSize;
            MarkFullDirty();
        }

        const std::uint64_t layerId = reinterpret_cast<std::uintptr_t>(this);
        bool hasLayoutDirty = IsSelfLayoutDirty();
        bool hasAppearanceDirty = false;
        for (auto &child : children)
        {
            if (child->IsSelfLayoutDirty() || child->IsChildLayoutDirty())
            {
                hasLayoutDirty = true;
                break;
            }
            if (child->IsAppearanceDirty())
            {
                hasAppearanceDirty = true;
            }
        }

        if (hasLayoutDirty)
        {
            list.beginLayer(layerId, contentSize, true);
            for (auto &child : children)
            {
                const glm::vec4 childBounds = child->GetCachedBoundsInParent();
                child->Collect(list, contentSize, {childBounds.x, childBounds.y});
                child->ClearDirty();
            }
            list.endLayer();
        }
        else if (hasAppearanceDirty)
        {
            // Redessin partiel : chaque zone d'un enfant modifié est effacée puis TOUS les enfants y sont redessinés, dans l'ordre de
            // la liste et sous un découpage limité à la zone (le découpage écarte ce qui tombe dehors). On ne filtre pas sur les
            // bornes des enfants : un descendant peut déborder de son conteneur. Un enfant qui recouvre le modifié reste devant.
            std::vector<glm::vec4> zones;
            for (auto &child : children)
            {
                if (child->IsAppearanceDirty())
                {
                    zones.push_back(child->GetCachedBoundsInParent());
                }
            }
            list.beginLayer(layerId, contentSize, false);
            for (const glm::vec4 &zone : zones)
            {
                list.pushClip({zone.x, zone.y}, {zone.z, zone.w});
                list.clearRect({zone.x, zone.y}, {zone.z, zone.w});
                for (auto &child : children)
                {
                    const glm::vec4 childBounds = child->GetCachedBoundsInParent();
                    child->Collect(list, contentSize, {childBounds.x, childBounds.y});
                }
                list.popClip();
            }
            for (auto &child : children)
            {
                child->ClearDirty();
            }
            list.endLayer();
        }
        // Sinon la couche garde son contenu : seule son image est redessinée.

        engine::renderer::DrawLayerImage image;
        image.layerId = layerId;
        image.position = offset;
        image.size = localBounds.scale;
        image.scrollOffset = scrollOffset;
        image.contentSize = contentSize;
        image.color = this->color.Get();
        list.layerImage(image);

        ClearDirty();
        return;
    }

    if (GetColor().a > 0.0f)
    {
        list.rect(offset, localBounds.scale, this->color.Get());
    }

    const bool shouldClipChildren = overflowMode.Get() == OverflowMode::HIDDEN || overflowMode.Get() == OverflowMode::SCROLL;
    if (shouldClipChildren)
    {
        list.pushClip(offset, localBounds.scale);
    }

    const glm::vec2 childBaseOffset = offset - scrollOffset;
    for (auto &child : children)
    {
        const glm::vec4 childBounds = child->GetCachedBoundsInParent();
        child->Collect(list, containerSize, {childBaseOffset.x + childBounds.x, childBaseOffset.y + childBounds.y});
    }

    if (shouldClipChildren)
    {
        list.popClip();
    }

    ClearDirty();
}

void ContainerBase::MarkFullDirty()
{
    MarkSelfLayoutDirty();
    for (auto &child : children)
    {
        child->MarkFullDirty();
    }
}

void ContainerBase::RecalculateChildBounds()
{
    CalculatePixelSize();
    float p = GetPadding();
    // Default implementation: stack children at origin + padding, with anchor offset
    for (auto &child : children)
    {
        glm::vec2 childSize = child->CalculatePixelSize();
        // Calculate anchor offset based on contentSize
        glm::vec2 anchorOffset = child->GetAnchorOffset(contentSize);
        float x = p + anchorOffset.x;
        float y = p + anchorOffset.y;
        child->SetCachedBoundsInParent({x, y, childSize.x, childSize.y});
    }

    contentSize.x = localBounds.scale.x;
    contentSize.y = localBounds.scale.y;
}

void ContainerBase::UpdateTheme()
{
    ComponentBase::UpdateTheme();
    if (auto t = theme.lock())
    {
        padding.ForceSet(t->GetPadding());
        spacing.ForceSet(t->GetSpacing());
    }
}

void ContainerBase::DoSetPadding(float p) { padding.Set(p); }

void ContainerBase::DoSetSpacing(float s) { spacing.Set(s); }

void ContainerBase::DoSetOverflowMode(OverflowMode mode) { overflowMode.Set(mode); }

void ContainerBase::DoSetRenderToTexture(bool enabled) { renderToTexture.Set(enabled); }

void ContainerBase::DoSetChildrenAllowDeform(bool deform)
{
    for (auto &child : children)
    {
        child->DoSetAllowDeform(deform);
    }
}

void ContainerBase::OnChildAppearanceDirty()
{
    dirtyAppearance = true;
    NotifyParentChildAppearanceDirty();
}

void ContainerBase::OnChildLayoutDirty()
{
    dirtyChildLayout = true;
    dirtyAppearance = true;
    NotifyParentChildLayoutDirty();
}

} // namespace UI
