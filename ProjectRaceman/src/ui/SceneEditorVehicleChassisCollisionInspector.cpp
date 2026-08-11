#include "SceneEditorInternal.h"

#include <algorithm>
#include <string>

namespace raceman {
using namespace scene_editor_internal;

namespace {

constexpr const char* kChassisCollisionModeLabels[] = {
    "None (wheels only)",
    "Auto Box (fit to mesh)",
    "Box",
    "Shapes",
    "Convex Mesh",
    "Child Colliders",
};

constexpr const char* kChassisCollisionModeHints[] = {
    "No chassis volume. The car is held up by wheel rays only and passes through walls.",
    "One box auto-fitted to this object's mesh bounds. Rebuilt whenever the mesh changes.",
    "One box you position and size by hand.",
    "A list of primitives. Use this to approximate a real body shell - nose, cabin, tail.",
    "A convex hull cooked from a mesh asset. Concave detail is filled in; the hull is the collision volume.",
    "Collider components on this object and its bound chassis parts.",
};

constexpr const char* kChassisShapeTypeLabels[] = {"Box", "Sphere", "Capsule", "Convex Mesh"};

int ModeToIndex(VehicleChassisCollisionMode mode) {
    switch (mode) {
    case VehicleChassisCollisionMode::None: return 0;
    case VehicleChassisCollisionMode::AutoBox: return 1;
    case VehicleChassisCollisionMode::Box: return 2;
    case VehicleChassisCollisionMode::Shapes: return 3;
    case VehicleChassisCollisionMode::ConvexMesh: return 4;
    case VehicleChassisCollisionMode::ChildColliders: return 5;
    }
    return 1;
}

VehicleChassisCollisionMode IndexToMode(int index) {
    switch (index) {
    case 0: return VehicleChassisCollisionMode::None;
    case 2: return VehicleChassisCollisionMode::Box;
    case 3: return VehicleChassisCollisionMode::Shapes;
    case 4: return VehicleChassisCollisionMode::ConvexMesh;
    case 5: return VehicleChassisCollisionMode::ChildColliders;
    case 1:
    default: return VehicleChassisCollisionMode::AutoBox;
    }
}

} // namespace

void SceneEditor::RenderVehicleChassisCollisionInspector(SceneObject& obj) {
    VehicleChassisCollisionConfig& config = obj.vehicle.chassisCollision;

    // Same continuous-edit coalescing the rest of the inspector uses: one undo entry
    // per drag, pushed before the first value change rather than after it.
    auto beginInspectorContinuousEdit = [&]() {
        if (!inspectorEditActive_) {
            PushUndoState();
            inspectorEditActive_ = true;
        }
    };
    auto endInspectorContinuousEdit = [&]() {
        if (ImGui::IsItemDeactivated()) {
            inspectorEditActive_ = false;
        }
    };

    const bool enabledBefore = config.enabled;
    if (ImGui::Checkbox("Enable Chassis Collision", &config.enabled)) {
        const bool after = config.enabled;
        config.enabled = enabledBefore;
        PushUndoState();
        config.enabled = after;
        if (onDirty_) onDirty_();
    }
    if (!config.enabled) {
        ImGui::TextDisabled("Disabled - the car falls back to a single forward probe against walls.");
        return;
    }

    int modeIndex = ModeToIndex(config.mode);
    if (ImGui::Combo("Mode", &modeIndex, kChassisCollisionModeLabels, IM_ARRAYSIZE(kChassisCollisionModeLabels))) {
        PushUndoState();
        config.mode = IndexToMode(modeIndex);
        if (onDirty_) onDirty_();
    }
    ImGui::TextDisabled("%s", kChassisCollisionModeHints[(std::clamp)(modeIndex, 0, 5)]);

    ImGui::Spacing();

    switch (config.mode) {
    case VehicleChassisCollisionMode::AutoBox: {
        glm::vec3 previewCenter{0.0f, -0.2f, 0.0f};
        glm::vec3 previewSize{1.8f, 0.6f, 4.0f};
        if (obj.hasMeshFilter) {
            const glm::vec3 size = obj.meshFilter.localBoundsMax - obj.meshFilter.localBoundsMin;
            if (size.x > 0.001f && size.y > 0.001f && size.z > 0.001f) {
                previewCenter = (obj.meshFilter.localBoundsMin + obj.meshFilter.localBoundsMax) * 0.5f;
                previewSize = size;
            }
        }
        ImGui::TextDisabled("Fitted center: %.2f, %.2f, %.2f", previewCenter.x, previewCenter.y, previewCenter.z);
        ImGui::TextDisabled("Fitted size:   %.2f, %.2f, %.2f", previewSize.x, previewSize.y, previewSize.z);
        if (ImGui::Button("Copy To Box Mode")) {
            PushUndoState();
            config.boxCenter = previewCenter;
            config.boxSize = previewSize;
            config.mode = VehicleChassisCollisionMode::Box;
            if (onDirty_) onDirty_();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(switch to Box so you can tune it by hand)");
        break;
    }
    case VehicleChassisCollisionMode::Box: {
        glm::vec3 boxCenter = config.boxCenter;
        if (ImGui::DragFloat3("Center##chassisBoxCenter", &boxCenter.x, 0.01f)) {
            beginInspectorContinuousEdit();
            config.boxCenter = boxCenter;
            if (onDirty_) onDirty_();
        }
        endInspectorContinuousEdit();

        glm::vec3 boxSize = config.boxSize;
        if (ImGui::DragFloat3("Size##chassisBoxSize", &boxSize.x, 0.01f, 0.01f, 100.0f)) {
            beginInspectorContinuousEdit();
            config.boxSize = (glm::max)(boxSize, glm::vec3(0.01f));
            if (onDirty_) onDirty_();
        }
        endInspectorContinuousEdit();
        if (ImGui::Button("Fit To Mesh Bounds") && obj.hasMeshFilter) {
            const glm::vec3 size = obj.meshFilter.localBoundsMax - obj.meshFilter.localBoundsMin;
            if (size.x > 0.001f && size.y > 0.001f && size.z > 0.001f) {
                PushUndoState();
                config.boxCenter = (obj.meshFilter.localBoundsMin + obj.meshFilter.localBoundsMax) * 0.5f;
                config.boxSize = size;
                if (onDirty_) onDirty_();
            }
        }
        break;
    }
    case VehicleChassisCollisionMode::ConvexMesh: {
        const bool usingOwnMesh = config.meshAssetPath.empty();
        const std::string display = usingOwnMesh
            ? (obj.hasMeshFilter && !obj.meshFilter.sourcePath.empty()
                   ? ("(vehicle mesh) " + ProjectAssetDisplayFilename(obj.meshFilter.sourcePath))
                   : std::string("(none)"))
            : ProjectAssetDisplayFilename(config.meshAssetPath);
        ImGui::TextDisabled("Source:");
        ImGui::SameLine();
        ImGui::Button(display.c_str(), ImVec2((std::max)(1.0f, ImGui::GetContentRegionAvail().x), 0.0f));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kMeshAssetPayload)) {
                if (const char* path = static_cast<const char*>(payload->Data)) {
                    PushUndoState();
                    config.meshAssetPath = NormalizeSlashes(path);
                    config.meshName.clear();
                    config.meshIndex = 0;
                    if (onDirty_) onDirty_();
                }
            }
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kModelChildAssetPayload)) {
                std::string path;
                int meshIndex = -1;
                if (ParseModelChildAssetPayload(payload->Data, payload->DataSize, path, meshIndex)) {
                    PushUndoState();
                    config.meshAssetPath = path;
                    config.meshIndex = meshIndex;
                    config.meshName.clear();
                    if (onDirty_) onDirty_();
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::TextDisabled("Drop a mesh asset here, or leave empty to hull this object's own mesh.");
        if (!usingOwnMesh && ImGui::Button("Use Vehicle Mesh")) {
            PushUndoState();
            config.meshAssetPath.clear();
            config.meshName.clear();
            config.meshIndex = 0;
            if (onDirty_) onDirty_();
        }
        ImGui::TextDisabled("Cooked as a convex hull - a swept body cannot use a triangle mesh.");
        break;
    }
    case VehicleChassisCollisionMode::Shapes: {
        if (ImGui::Button("Add Box")) {
            PushUndoState();
            VehicleChassisShape shape;
            shape.name = "Box " + std::to_string(config.shapes.size() + 1);
            shape.type = VehicleChassisShapeType::Box;
            config.shapes.push_back(std::move(shape));
            if (onDirty_) onDirty_();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Sphere")) {
            PushUndoState();
            VehicleChassisShape shape;
            shape.name = "Sphere " + std::to_string(config.shapes.size() + 1);
            shape.type = VehicleChassisShapeType::Sphere;
            config.shapes.push_back(std::move(shape));
            if (onDirty_) onDirty_();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Capsule")) {
            PushUndoState();
            VehicleChassisShape shape;
            shape.name = "Capsule " + std::to_string(config.shapes.size() + 1);
            shape.type = VehicleChassisShapeType::Capsule;
            config.shapes.push_back(std::move(shape));
            if (onDirty_) onDirty_();
        }

        if (config.shapes.empty()) {
            ImGui::TextDisabled("No shapes. Add at least one or the car falls back to the forward probe.");
            break;
        }

        int removeIndex = -1;
        int duplicateIndex = -1;
        for (int shapeIndex = 0; shapeIndex < static_cast<int>(config.shapes.size()); ++shapeIndex) {
            VehicleChassisShape& shape = config.shapes[static_cast<std::size_t>(shapeIndex)];
            ImGui::PushID(shapeIndex);

            const std::string header = (shape.name.empty() ? std::string("Shape") : shape.name) +
                                       " (" + VehicleChassisShapeTypeLabel(shape.type) + ")###chassisShape";
            if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                const bool shapeEnabledBefore = shape.enabled;
                if (ImGui::Checkbox("Enabled", &shape.enabled)) {
                    const bool after = shape.enabled;
                    shape.enabled = shapeEnabledBefore;
                    PushUndoState();
                    shape.enabled = after;
                    if (onDirty_) onDirty_();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Duplicate")) duplicateIndex = shapeIndex;
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) removeIndex = shapeIndex;

                char nameBuffer[64];
                std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", shape.name.c_str());
                if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
                    beginInspectorContinuousEdit();
                    shape.name = nameBuffer;
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                int typeIndex = static_cast<int>(shape.type);
                if (ImGui::Combo("Type", &typeIndex, kChassisShapeTypeLabels, IM_ARRAYSIZE(kChassisShapeTypeLabels))) {
                    PushUndoState();
                    shape.type = static_cast<VehicleChassisShapeType>((std::clamp)(typeIndex, 0, 3));
                    if (onDirty_) onDirty_();
                }

                glm::vec3 shapeCenter = shape.center;
                if (ImGui::DragFloat3("Center", &shapeCenter.x, 0.01f)) {
                    beginInspectorContinuousEdit();
                    shape.center = shapeCenter;
                    if (onDirty_) onDirty_();
                }
                endInspectorContinuousEdit();

                if (shape.type != VehicleChassisShapeType::Sphere) {
                    glm::vec3 shapeRotation = shape.rotationEuler;
                    if (ImGui::DragFloat3("Rotation", &shapeRotation.x, 0.5f)) {
                        beginInspectorContinuousEdit();
                        shape.rotationEuler = shapeRotation;
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();
                }

                switch (shape.type) {
                case VehicleChassisShapeType::Box: {
                    glm::vec3 shapeSize = shape.size;
                    if (ImGui::DragFloat3("Size", &shapeSize.x, 0.01f, 0.01f, 100.0f)) {
                        beginInspectorContinuousEdit();
                        shape.size = (glm::max)(shapeSize, glm::vec3(0.01f));
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();
                    break;
                }
                case VehicleChassisShapeType::Sphere: {
                    float shapeRadius = shape.radius;
                    if (ImGui::DragFloat("Radius", &shapeRadius, 0.01f, 0.01f, 50.0f)) {
                        beginInspectorContinuousEdit();
                        shape.radius = shapeRadius;
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();
                    break;
                }
                case VehicleChassisShapeType::Capsule: {
                    float capsuleRadius = shape.radius;
                    if (ImGui::DragFloat("Radius", &capsuleRadius, 0.01f, 0.01f, 50.0f)) {
                        beginInspectorContinuousEdit();
                        shape.radius = capsuleRadius;
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();

                    float capsuleHeight = shape.height;
                    if (ImGui::DragFloat("Height", &capsuleHeight, 0.01f, 0.01f, 50.0f)) {
                        beginInspectorContinuousEdit();
                        shape.height = (std::max)(capsuleHeight, shape.radius * 2.0f);
                        if (onDirty_) onDirty_();
                    }
                    endInspectorContinuousEdit();
                    break;
                }
                case VehicleChassisShapeType::ConvexMesh: {
                    const std::string display = shape.meshAssetPath.empty()
                        ? std::string("(none)")
                        : ProjectAssetDisplayFilename(shape.meshAssetPath);
                    ImGui::Button(display.c_str(), ImVec2((std::max)(1.0f, ImGui::GetContentRegionAvail().x), 0.0f));
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kMeshAssetPayload)) {
                            if (const char* path = static_cast<const char*>(payload->Data)) {
                                PushUndoState();
                                shape.meshAssetPath = NormalizeSlashes(path);
                                shape.meshIndex = 0;
                                shape.meshName.clear();
                                if (onDirty_) onDirty_();
                            }
                        }
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kModelChildAssetPayload)) {
                            std::string path;
                            int meshIndex = -1;
                            if (ParseModelChildAssetPayload(payload->Data, payload->DataSize, path, meshIndex)) {
                                PushUndoState();
                                shape.meshAssetPath = path;
                                shape.meshIndex = meshIndex;
                                shape.meshName.clear();
                                if (onDirty_) onDirty_();
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    ImGui::TextDisabled("Drop a mesh asset to hull.");
                    break;
                }
                }
            }
            ImGui::PopID();
        }

        if (duplicateIndex >= 0) {
            PushUndoState();
            VehicleChassisShape copy = config.shapes[static_cast<std::size_t>(duplicateIndex)];
            copy.name += " Copy";
            config.shapes.insert(config.shapes.begin() + duplicateIndex + 1, std::move(copy));
            if (onDirty_) onDirty_();
        } else if (removeIndex >= 0) {
            PushUndoState();
            config.shapes.erase(config.shapes.begin() + removeIndex);
            if (onDirty_) onDirty_();
        }
        break;
    }
    case VehicleChassisCollisionMode::ChildColliders: {
        ImGui::TextDisabled("Uses collider components on this object and the Chassis parts bound above.");
        ImGui::TextDisabled("Mesh colliders are cooked as convex hulls regardless of their own mode.");
        break;
    }
    case VehicleChassisCollisionMode::None:
        break;
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Sweep");
    ImGui::TextDisabled("Impact response (restitution, friction, spin) is physics -");
    ImGui::TextDisabled("edit it in the vehicle profile's Collision section.");

    float skinWidth = config.skinWidth;
    if (ImGui::SliderFloat("Skin Width", &skinWidth, 0.0f, 0.25f, "%.3f m")) {
        beginInspectorContinuousEdit();
        config.skinWidth = skinWidth;
        if (onDirty_) onDirty_();
    }
    endInspectorContinuousEdit();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Gap left between the chassis and a wall after a hit. Too small causes jitter.");
    }

    int maxSlideIterations = config.maxSlideIterations;
    if (ImGui::SliderInt("Slide Passes", &maxSlideIterations, 1, 8)) {
        beginInspectorContinuousEdit();
        config.maxSlideIterations = maxSlideIterations;
        if (onDirty_) onDirty_();
    }
    endInspectorContinuousEdit();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("How many times the leftover motion is redirected along a wall. 1 = stop dead, 3 = smooth scrape.");
    }

    const bool depenBefore = config.enableDepenetration;
    if (ImGui::Checkbox("Depenetration", &config.enableDepenetration)) {
        const bool after = config.enableDepenetration;
        config.enableDepenetration = depenBefore;
        PushUndoState();
        config.enableDepenetration = after;
        if (onDirty_) onDirty_();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Pushes the chassis back out if it ends a step already inside geometry.");
    }
    if (config.enableDepenetration) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        float maxDepenetration = config.maxDepenetrationPerStep;
        if (ImGui::SliderFloat("Max Push", &maxDepenetration, 0.0f, 1.0f, "%.2f m")) {
            beginInspectorContinuousEdit();
            config.maxDepenetrationPerStep = maxDepenetration;
            if (onDirty_) onDirty_();
        }
        endInspectorContinuousEdit();
    }

    const bool debugBefore = config.debugDraw;
    if (ImGui::Checkbox("Debug Draw", &config.debugDraw)) {
        const bool after = config.debugDraw;
        config.debugDraw = debugBefore;
        PushUndoState();
        config.debugDraw = after;
        if (onDirty_) onDirty_();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Draws the chassis collision volume and the latest impact normal in the viewport.");
    }

    if (scriptsRunning_) {
        const auto runtimeIt = std::find_if(runtimeVehicles_.begin(), runtimeVehicles_.end(),
            [&](const RuntimeVehicleInstance& runtimeVehicle) { return runtimeVehicle.objectId == obj.id; });
        if (runtimeIt != runtimeVehicles_.end()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Runtime");
            ImGui::TextDisabled("Cooked shapes: %d", static_cast<int>(runtimeIt->chassisColliderDescs.size()));
            if (runtimeIt->chassisQueryShape == nullptr) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.25f, 1.0f), "No query shape - using the forward probe fallback.");
            } else if (runtimeIt->chassisCollided) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                                   "Contact  closing %.2f m/s",
                                   runtimeIt->chassisImpactSpeed);
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                                   "Impulse %.0f N*s   spin %+.1f deg/s",
                                   runtimeIt->chassisNormalImpulse,
                                   runtimeIt->chassisYawImpulseDegrees);
                ImGui::TextDisabled("Normal %.2f, %.2f, %.2f",
                                    runtimeIt->chassisImpactNormal.x,
                                    runtimeIt->chassisImpactNormal.y,
                                    runtimeIt->chassisImpactNormal.z);
            } else {
                ImGui::TextDisabled("No contact.");
            }
            ImGui::TextDisabled("Changes apply on the next Play.");
        }
    }
}

} // namespace raceman
