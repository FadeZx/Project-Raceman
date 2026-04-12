#include "SceneEditorInternal.h"
#include "../physics/SimpleJson.h"

namespace fs = std::filesystem;

namespace raceman {
using namespace scene_editor_internal;

void SceneEditor::CreateDefaultSceneObjects() {
    try {
        SceneObject planeObject;
        planeObject.id = MakeId("mesh");
        planeObject.name = "Plane";
        planeObject.type = "GameObject";
        planeObject.transform.scale = {10.0f, 1.0f, 10.0f};
        planeObject.meshRenderer.materialId = "pbr_default";
        if (ConfigureBuiltInPrimitive(planeObject, "Plane", builtInPrimitiveMeshes_)) {
            AddDefaultPlaneColliderToPlane(planeObject);
            objects_.push_back(std::move(planeObject));
        }
    } catch (...) {
        if (console_) {
            console_->AddWarning("Failed to add default Plane object.");
        }
    }

    SceneObject cameraObject;
    cameraObject.id = MakeId("camera");
    cameraObject.name = "Camera";
    cameraObject.type = "GameObject";
    cameraObject.transform.position = {0.0f, 2.0f, 5.0f};
    cameraObject.transform.rotationEuler = {-15.0f, 0.0f, 0.0f};
    cameraObject.hasMeshFilter = false;
    cameraObject.hasMeshRenderer = false;
    cameraObject.hasScriptComponent = false;
    cameraObject.hasCamera = true;
    cameraObject.camera = CameraComponent{};
    cameraObject.camera.isMain = true;
    objects_.push_back(std::move(cameraObject));

    SceneObject lightObject;
    lightObject.id = MakeId("light");
    lightObject.name = "Directional Light";
    lightObject.type = "GameObject";
    lightObject.transform.rotationEuler = {-45.0f, 35.0f, 0.0f};
    lightObject.hasMeshFilter = false;
    lightObject.hasMeshRenderer = false;
    lightObject.hasScriptComponent = false;
    lightObject.hasLight = true;
    lightObject.light = LightComponent{};
    lightObject.light.type = LightType::Directional;
    lightObject.light.intensity = 1.5f;
    lightObject.light.range = 100.0f;
    objects_.push_back(std::move(lightObject));

    selectedIndex_ = -1;
    selectedIndices_.clear();
}

void SceneEditor::NewScene() {
    NewScene("Untitled");
}

void SceneEditor::NewScene(const std::string& sceneName) {
    if (scriptsRunning_) {
        SetScriptsRunning(false);
    }
    ClearScriptRuntime();
    objects_.clear();
    hierarchyOpenStates_.clear();
    selectedIndex_ = -1;
    selectedIndices_.clear();
    undoStack_.clear();
    redoStack_.clear();
    playModeSnapshot_ = {};
    hasPlayModeSnapshot_ = false;
    renamingObjectIndex_ = -1;
    activeGizmoAxis_ = -1;
    hoveredGizmoAxis_ = -1;
    inspectMaterial_ = false;
    CreateDefaultSceneObjects();
    savePath_ = MakeUniqueSceneAssetPath(sceneName.empty() ? "Untitled" : sceneName);
    lastScenePath_ = savePath_;
    activeViewport_ = SceneEditorActiveViewport::Scene;
    SaveProject();
    RefreshProjectFiles();
    if (console_) {
        console_->AddLog("New scene: " + savePath_);
    }
    if (onDirty_) onDirty_();
}

void SceneEditor::SaveCurrentScene() {
    if (!IsSceneAssetPath(savePath_)) {
        savePath_ = MakeUniqueSceneAssetPath("Untitled");
    }
    Save(savePath_);
    lastScenePath_ = NormalizeSlashes(savePath_);
    if (defaultScenePath_.empty()) {
        defaultScenePath_ = lastScenePath_;
    }
    SaveProject();
    RefreshProjectFiles();
    if (console_) {
        console_->AddLog("Scene saved: " + lastScenePath_);
    }
}

void SceneEditor::SaveActiveAsset() {
    if (inspectMaterial_ && !inspectedMaterialId_.empty()) {
        Material* material = materialManager_.Get(inspectedMaterialId_);
        if (material == nullptr) {
            materialManager_.LoadAll();
            material = materialManager_.Get(inspectedMaterialId_);
        }

        if (material != nullptr) {
            if (materialManager_.Save(inspectedMaterialId_, *material)) {
                materialManager_.LoadAll();
                if (console_) {
                    console_->AddLog("Saved material: " + inspectedMaterialId_);
                }
            } else if (console_) {
                console_->AddError("Failed to save material: " + inspectedMaterialId_);
            }
            return;
        }

        if (console_) {
            console_->AddError("Material not found: " + inspectedMaterialId_);
        }
        return;
    }

    if (showVehicleConfigEditor_ && !inspectedVehicleConfigPath_.empty()) {
        std::string error;
        const fs::path configPath = ProjectAssetPathToAbsolute(inspectedVehicleConfigPath_);
        if (physics::VehicleConfigLoader::saveToFile(configPath.string(), inspectedVehicleConfig_, &error)) {
            inspectedVehicleConfigLoaded_ = false;
            if (console_) {
                console_->AddLog("Saved vehicle config: " + inspectedVehicleConfigPath_);
            }
        } else if (console_) {
            console_->AddError(error.empty() ? ("Failed to save vehicle config: " + inspectedVehicleConfigPath_) : error);
        }
        return;
    }

    SaveCurrentScene();
}

void SceneEditor::SaveCurrentSceneAs() {
    std::string baseName = fs::path(savePath_).filename().string();
    const std::string suffix = ".scene.json";
    if (EndsWith(ToLowerCopy(baseName), suffix)) {
        baseName = baseName.substr(0, baseName.size() - suffix.size());
    } else {
        baseName = fs::path(baseName).stem().string();
    }
    if (baseName.empty()) {
        baseName = "Scene";
    }
    savePath_ = MakeUniqueSceneAssetPath(baseName);
    SaveCurrentScene();
}

void SceneEditor::Save(const std::string& path) {
    const fs::path targetPath = ResolveEditorPath(path);
    try {
        fs::create_directories(targetPath.parent_path());
    } catch (...) {}

    std::ofstream out(targetPath, std::ios::trunc);
    if (!out.good()) return;

    // Minimal JSON (manual)
    out << "{\n  \"objects\": [\n";
    for (size_t i = 0; i < objects_.size(); ++i) {
        const auto& o = objects_[i];
        const std::string meshType = o.meshFilter.meshType.empty() ? o.type : o.meshFilter.meshType;
        out << "    {\n";
        out << "      \"id\": \"" << JsonEscape(o.id) << "\",\n";
        out << "      \"parentId\": \"" << JsonEscape(o.parentId) << "\",\n";
        out << "      \"name\": \"" << JsonEscape(o.name) << "\",\n";
        out << "      \"type\": \"" << JsonEscape(o.type) << "\",\n";
        out << "      \"physicsLayer\": " << ClampPhysicsLayerIndex(o.physicsLayer) << ",\n";
        out << "      \"enabled\": " << (o.enabled ? "true" : "false") << ",\n";
        SceneObject orderedObject = o;
        SyncInspectorComponentOrder(orderedObject);
        out << "      \"componentOrder\": [";
        for (std::size_t orderIndex = 0; orderIndex < orderedObject.inspectorComponentOrder.size(); ++orderIndex) {
            out << "\"" << InspectorComponentTypeToString(orderedObject.inspectorComponentOrder[orderIndex]) << "\"";
            if (orderIndex + 1 < orderedObject.inspectorComponentOrder.size()) {
                out << ", ";
            }
        }
        out << "],\n";
        out << "      \"components\": [\n";
        out << "        {\n";
        out << "          \"type\": \"Transform\",\n";
        out << "          \"position\": [" << o.transform.position.x << ", " << o.transform.position.y << ", " << o.transform.position.z << "],\n";
        out << "          \"rotationEuler\": [" << o.transform.rotationEuler.x << ", " << o.transform.rotationEuler.y << ", " << o.transform.rotationEuler.z << "],\n";
        out << "          \"scale\": [" << o.transform.scale.x << ", " << o.transform.scale.y << ", " << o.transform.scale.z << "]\n";
        out << "        }";
        if (o.hasMeshFilter) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"MeshFilter\",\n";
            out << "          \"enabled\": " << (o.meshFilter.enabled ? "true" : "false") << ",\n";
            out << "          \"meshType\": \"" << JsonEscape(meshType) << "\",\n";
            out << "          \"sourcePath\": \"" << JsonEscape(NormalizeSlashes(o.meshFilter.sourcePath)) << "\",\n";
            out << "          \"meshIndex\": " << o.meshFilter.meshIndex << ",\n";
            out << "          \"importedMaterialName\": \"" << JsonEscape(o.meshFilter.importedMaterialName) << "\",\n";
            out << "          \"diffuseTexturePath\": \"" << JsonEscape(NormalizeSlashes(o.meshFilter.diffuseTexturePath)) << "\"\n";
            out << "        }";
        }
        if (o.hasMeshRenderer) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"MeshRenderer\",\n";
            out << "          \"enabled\": " << (o.meshRenderer.enabled ? "true" : "false") << ",\n";
            out << "          \"materialId\": \"" << JsonEscape(o.meshRenderer.materialId.empty() ? "pbr_default" : o.meshRenderer.materialId) << "\",\n";
            out << "          \"color\": [" << o.meshRenderer.color.r << ", " << o.meshRenderer.color.g << ", " << o.meshRenderer.color.b << ", " << o.meshRenderer.color.a << "]\n";
            out << "        }";
        }
        if (o.hasScriptComponent) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"Script\",\n";
            out << "          \"enabled\": " << (o.scriptComponent.enabled ? "true" : "false") << ",\n";
            out << "          \"attachments\": [\n";
            for (size_t scriptIndex = 0; scriptIndex < o.scriptComponent.attachments.size(); ++scriptIndex) {
                const ObjectScriptAttachment& script = o.scriptComponent.attachments[scriptIndex];
                out << "            {\n";
                out << "              \"enabled\": " << (script.enabled ? "true" : "false") << ",\n";
                out << "              \"scriptName\": \"" << JsonEscape(script.scriptName) << "\",\n";
                out << "              \"scriptPath\": \"" << JsonEscape(NormalizeSlashes(script.scriptPath)) << "\"";
                if (!script.fields.empty()) {
                    out << ",\n";
                    out << "              \"fields\": [\n";
                    for (std::size_t fieldIndex = 0; fieldIndex < script.fields.size(); ++fieldIndex) {
                        const ScriptFieldEntry& field = script.fields[fieldIndex];
                        out << "                {\n";
                        out << "                  \"name\": \"" << JsonEscape(field.name) << "\",\n";
                        out << "                  \"type\": \"" << ScriptFieldTypeToString(field.type) << "\",\n";
                        WriteScriptFieldValue(out, field);
                        out << "                }" << (fieldIndex + 1 < script.fields.size() ? ",\n" : "\n");
                    }
                    out << "              ]\n";
                    out << "            }";
                } else {
                    out << "\n";
                    out << "            }";
                }
                out << (scriptIndex + 1 < o.scriptComponent.attachments.size() ? ",\n" : "\n");
            }
            out << "          ]\n";
            out << "        }";
        }
        if (o.hasRigidbody) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"Rigidbody\",\n";
            out << "          \"enabled\": " << (o.rigidbody.enabled ? "true" : "false") << ",\n";
            out << "          \"bodyType\": \"" << RigidbodyBodyTypeToString(o.rigidbody.bodyType) << "\",\n";
            out << "          \"mass\": " << o.rigidbody.mass << ",\n";
            out << "          \"useGravity\": " << (o.rigidbody.useGravity ? "true" : "false") << ",\n";
            out << "          \"linearDamping\": " << o.rigidbody.linearDamping << ",\n";
            out << "          \"angularDamping\": " << o.rigidbody.angularDamping << ",\n";
            out << "          \"friction\": " << o.rigidbody.friction << ",\n";
            out << "          \"restitution\": " << o.rigidbody.restitution << ",\n";
            out << "          \"velocity\": [" << o.rigidbody.velocity.x << ", " << o.rigidbody.velocity.y << ", " << o.rigidbody.velocity.z << "],\n";
            out << "          \"angularVelocity\": [" << o.rigidbody.angularVelocity.x << ", " << o.rigidbody.angularVelocity.y << ", " << o.rigidbody.angularVelocity.z << "],\n";
            out << "          \"freezePosition\": [" << (o.rigidbody.freezePositionX ? "true" : "false") << ", " << (o.rigidbody.freezePositionY ? "true" : "false") << ", " << (o.rigidbody.freezePositionZ ? "true" : "false") << "],\n";
            out << "          \"freezeRotation\": [" << (o.rigidbody.freezeRotationX ? "true" : "false") << ", " << (o.rigidbody.freezeRotationY ? "true" : "false") << ", " << (o.rigidbody.freezeRotationZ ? "true" : "false") << "]\n";
            out << "        }";
        }
        if (o.hasVehicle) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"Vehicle\",\n";
            out << "          \"enabled\": " << (o.vehicle.enabled ? "true" : "false") << ",\n";
            out << "          \"configPath\": \"" << JsonEscape(NormalizeSlashes(o.vehicle.configPath)) << "\",\n";
            out << "          \"chassisObjectIds\": [";
            for (std::size_t chassisIndex = 0; chassisIndex < o.vehicle.chassisObjectIds.size(); ++chassisIndex) {
                out << (chassisIndex == 0 ? "" : ", ") << "\"" << JsonEscape(o.vehicle.chassisObjectIds[chassisIndex]) << "\"";
            }
            out << "],\n";
            out << "          \"wheelBindings\": [\n";
            for (std::size_t wheelIndex = 0; wheelIndex < o.vehicle.wheelBindings.size(); ++wheelIndex) {
                const VehicleWheelBinding& binding = o.vehicle.wheelBindings[wheelIndex];
                out << "            {\n";
                out << "              \"wheelName\": \"" << JsonEscape(binding.wheelName) << "\",\n";
                out << "              \"objectId\": \"" << JsonEscape(binding.objectId) << "\",\n";
                out << "              \"visualRotationEuler\": [" << binding.visualRotationEuler.x << ", " << binding.visualRotationEuler.y << ", " << binding.visualRotationEuler.z << "]\n";
                out << "            }" << (wheelIndex + 1 < o.vehicle.wheelBindings.size() ? ",\n" : "\n");
            }
            out << "          ]\n";
            out << "        }";
        }
        if (o.hasCharacterController) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"CharacterController\",\n";
            out << "          \"enabled\": " << (o.characterController.enabled ? "true" : "false") << ",\n";
            out << "          \"height\": " << o.characterController.height << ",\n";
            out << "          \"radius\": " << o.characterController.radius << ",\n";
            out << "          \"center\": [" << o.characterController.center.x << ", " << o.characterController.center.y << ", " << o.characterController.center.z << "],\n";
            out << "          \"stepHeight\": " << o.characterController.stepHeight << ",\n";
            out << "          \"slopeLimitDegrees\": " << o.characterController.slopeLimitDegrees << ",\n";
            out << "          \"maxStrength\": " << o.characterController.maxStrength << ",\n";
            out << "          \"mass\": " << o.characterController.mass << "\n";
            out << "        }";
        }
        const SceneColliderType colliderType = GetActiveColliderType(o);
        if (colliderType != SceneColliderType::None) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"Collider\",\n";
            out << "          \"colliderType\": \"" << SceneColliderTypeLabel(colliderType) << "\"";
            if (colliderType == SceneColliderType::Box) {
                out << ",\n";
                out << "          \"enabled\": " << (o.boxCollider.enabled ? "true" : "false") << ",\n";
                out << "          \"isTrigger\": " << (o.boxCollider.isTrigger ? "true" : "false") << ",\n";
                out << "          \"center\": [" << o.boxCollider.center.x << ", " << o.boxCollider.center.y << ", " << o.boxCollider.center.z << "],\n";
                out << "          \"size\": [" << o.boxCollider.size.x << ", " << o.boxCollider.size.y << ", " << o.boxCollider.size.z << "]\n";
            } else if (colliderType == SceneColliderType::Sphere) {
                out << ",\n";
                out << "          \"enabled\": " << (o.sphereCollider.enabled ? "true" : "false") << ",\n";
                out << "          \"isTrigger\": " << (o.sphereCollider.isTrigger ? "true" : "false") << ",\n";
                out << "          \"center\": [" << o.sphereCollider.center.x << ", " << o.sphereCollider.center.y << ", " << o.sphereCollider.center.z << "],\n";
                out << "          \"radius\": " << o.sphereCollider.radius << "\n";
            } else if (colliderType == SceneColliderType::Capsule) {
                out << ",\n";
                out << "          \"enabled\": " << (o.capsuleCollider.enabled ? "true" : "false") << ",\n";
                out << "          \"isTrigger\": " << (o.capsuleCollider.isTrigger ? "true" : "false") << ",\n";
                out << "          \"center\": [" << o.capsuleCollider.center.x << ", " << o.capsuleCollider.center.y << ", " << o.capsuleCollider.center.z << "],\n";
                out << "          \"radius\": " << o.capsuleCollider.radius << ",\n";
                out << "          \"height\": " << o.capsuleCollider.height << "\n";
            } else if (colliderType == SceneColliderType::Plane) {
                out << ",\n";
                out << "          \"enabled\": " << (o.planeCollider.enabled ? "true" : "false") << ",\n";
                out << "          \"isTrigger\": " << (o.planeCollider.isTrigger ? "true" : "false") << ",\n";
                out << "          \"normal\": [" << o.planeCollider.normal.x << ", " << o.planeCollider.normal.y << ", " << o.planeCollider.normal.z << "],\n";
                out << "          \"offset\": " << o.planeCollider.offset << ",\n";
                out << "          \"infinite\": " << (o.planeCollider.infinite ? "true" : "false") << ",\n";
                out << "          \"halfExtent\": " << o.planeCollider.halfExtent << "\n";
            } else if (colliderType == SceneColliderType::Mesh) {
                out << ",\n";
                out << "          \"enabled\": " << (o.meshCollider.enabled ? "true" : "false") << ",\n";
                out << "          \"isTrigger\": " << (o.meshCollider.isTrigger ? "true" : "false") << ",\n";
                out << "          \"buildQuality\": \"" << MeshColliderBuildQualityToString(o.meshCollider.buildQuality) << "\"\n";
            }
            out << "        }";
        }
        if (o.hasCamera) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"Camera\",\n";
            out << "          \"enabled\": " << (o.camera.enabled ? "true" : "false") << ",\n";
            out << "          \"isMain\": " << (o.camera.isMain ? "true" : "false") << ",\n";
            out << "          \"fieldOfViewDegrees\": " << o.camera.fieldOfViewDegrees << ",\n";
            out << "          \"nearClip\": " << o.camera.nearClip << ",\n";
            out << "          \"farClip\": " << o.camera.farClip << ",\n";
            out << "          \"clearColor\": [" << o.camera.clearColor.r << ", " << o.camera.clearColor.g << ", " << o.camera.clearColor.b << ", " << o.camera.clearColor.a << "]\n";
            out << "        }";
        }
        if (o.hasLight) {
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"Light\",\n";
            out << "          \"enabled\": " << (o.light.enabled ? "true" : "false") << ",\n";
            out << "          \"lightType\": \"" << LightTypeToString(o.light.type) << "\",\n";
            out << "          \"color\": [" << o.light.color.r << ", " << o.light.color.g << ", " << o.light.color.b << "],\n";
            out << "          \"intensity\": " << o.light.intensity << ",\n";
            out << "          \"range\": " << o.light.range << ",\n";
            out << "          \"spotAngleDegrees\": " << o.light.spotAngleDegrees << "\n";
            out << "        }";
        }
        out << "\n";
        out << "      ]\n";
        out << "    }" << (i + 1 < objects_.size() ? ",\n" : "\n");
    }
    out << "  ],\n";
    out << "  \"hierarchyClosed\": [";
    bool wroteClosed = false;
    for (const auto& entry : hierarchyOpenStates_) {
        if (entry.second) {
            continue;
        }
        if (wroteClosed) {
            out << ", ";
        }
        out << "\"" << JsonEscape(entry.first) << "\"";
        wroteClosed = true;
    }
    out << "]\n}\n";
}

} // namespace raceman
