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
    // Never write runtime transforms to disk.
    // The destructor sets scriptsRunning_ = false before calling here,
    // so snapshot restoration still saves correctly on app close.
    if (scriptsRunning_) {
        return;
    }
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
            out << "          \"diffuseTexturePath\": \"" << JsonEscape(NormalizeSlashes(o.meshFilter.diffuseTexturePath)) << "\"";
            {
                const glm::vec3& po = o.meshFilter.pivotOffset;
                if (po.x != 0.0f || po.y != 0.0f || po.z != 0.0f) {
                    out << ",\n          \"pivotOffset\": [" << po.x << ", " << po.y << ", " << po.z << "]";
                }
            }
            out << "\n";
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
            out << "          \"canTilt\": " << (o.vehicle.canTilt ? "true" : "false") << ",\n";
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
                out << "          \"buildQuality\": \"" << MeshColliderBuildQualityToString(o.meshCollider.buildQuality) << "\",\n";
                out << "          \"mode\": \"" << MeshColliderModeToString(o.meshCollider.mode) << "\"\n";
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
        if (o.hasCinemachine) {
            const auto cinemachineTypeStr = [](CinemachineCameraType t) -> const char* {
                switch (t) {
                    case CinemachineCameraType::Follow:          return "Follow";
                    case CinemachineCameraType::LookAt:          return "LookAt";
                    case CinemachineCameraType::FollowAndLookAt: return "FollowAndLookAt";
                }
                return "FollowAndLookAt";
            };
            out << ",\n";
            out << "        {\n";
            out << "          \"type\": \"Cinemachine\",\n";
            out << "          \"enabled\": " << (o.cinemachine.enabled ? "true" : "false") << ",\n";
            out << "          \"cameraType\": \"" << cinemachineTypeStr(o.cinemachine.type) << "\",\n";
            out << "          \"followTargetId\": \"" << JsonEscape(o.cinemachine.followTargetId) << "\",\n";
            out << "          \"lookAtTargetId\": \"" << JsonEscape(o.cinemachine.lookAtTargetId) << "\",\n";
            out << "          \"followOffset\": [" << o.cinemachine.followOffset.x << ", " << o.cinemachine.followOffset.y << ", " << o.cinemachine.followOffset.z << "],\n";
            out << "          \"pitchOffset\": " << o.cinemachine.pitchOffset << ",\n";
            out << "          \"yawOffset\": " << o.cinemachine.yawOffset << ",\n";
            out << "          \"positionDamping\": " << o.cinemachine.positionDamping << ",\n";
            out << "          \"rotationDamping\": " << o.cinemachine.rotationDamping << "\n";
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

void SceneEditor::Load(const std::string& path) {
    using namespace raceman::physics::json;
    using raceman::physics::json::Value;
    using raceman::physics::json::parse;
    using scene_editor_internal::ReadBool;
    using scene_editor_internal::ReadBoolArray;
    using scene_editor_internal::ReadString;
    using scene_editor_internal::ReadVec2;
    using scene_editor_internal::ReadVec3;
    using scene_editor_internal::ReadVec4;
    objects_.clear();
    hierarchyOpenStates_.clear();
    selectedIndex_ = -1;
    selectedIndices_.clear();
    undoStack_.clear();
    redoStack_.clear();

    const fs::path sourcePath = ResolveEditorPath(path);
    if (!fs::exists(sourcePath)) return;

    std::ifstream in(sourcePath);
    if (!in.good()) return;
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string src = buffer.str();

    struct CachedLoadedMeshAsset {
        bool attempted{false};
        bool loaded{false};
        std::string resolvedPath;
        std::shared_ptr<::Model> model;
        std::vector<ImportedMeshInfo> infos;
    };
    std::unordered_map<std::string, CachedLoadedMeshAsset> meshAssetCache;

    try {
        Value root = parse(src);
        if (!root.is_object()) return;
        const auto& obj = root.as_object();
        auto it = obj.find("objects");
        if (it == obj.end() || !it->second.is_array()) return;

        const auto& arr = it->second.as_array();
        for (const auto& v : arr) {
            if (!v.is_object()) continue;
            const auto& o = v.as_object();

            SceneObject so;
            so.inspectorComponentOrder = DefaultInspectorComponentOrder();

            // id
            auto idIt = o.find("id");
            if (idIt != o.end() && idIt->second.is_string()) {
                so.id = idIt->second.as_string();
            }
            else {
                so.id = MakeId("obj");
            }

            ReadString(o, "parentId", so.parentId);

            // name
            auto nameIt = o.find("name");
            if (nameIt != o.end() && nameIt->second.is_string()) {
                so.name = nameIt->second.as_string();
            }
            else {
                so.name = "Object";
            }

            // type
            std::string legacyType = "GameObject";
            auto typeIt = o.find("type");
            if (typeIt != o.end() && typeIt->second.is_string()) {
                legacyType = typeIt->second.as_string();
            }
            so.type = "GameObject";
            so.physicsLayer = 0;

            // transform
            auto trIt = o.find("transform");
            if (trIt != o.end() && trIt->second.is_object()) {
                const auto& tr = trIt->second.as_object();

                auto posIt = tr.find("position");
                if (posIt != tr.end() && posIt->second.is_array()) {
                    const auto& a = posIt->second.as_array();
                    if (a.size() == 3 && a[0].is_number() && a[1].is_number() && a[2].is_number()) {
                        so.transform.position = {
                            static_cast<float>(a[0].as_number()),
                            static_cast<float>(a[1].as_number()),
                            static_cast<float>(a[2].as_number())
                        };
                    }
                }

                auto rotIt = tr.find("rotationEuler");
                if (rotIt != tr.end() && rotIt->second.is_array()) {
                    const auto& a = rotIt->second.as_array();
                    if (a.size() == 3 && a[0].is_number() && a[1].is_number() && a[2].is_number()) {
                        so.transform.rotationEuler = {
                            static_cast<float>(a[0].as_number()),
                            static_cast<float>(a[1].as_number()),
                            static_cast<float>(a[2].as_number())
                        };
                    }
                }

                auto scIt = tr.find("scale");
                if (scIt != tr.end() && scIt->second.is_array()) {
                    const auto& a = scIt->second.as_array();
                    if (a.size() == 3 && a[0].is_number() && a[1].is_number() && a[2].is_number()) {
                        so.transform.scale = {
                            static_cast<float>(a[0].as_number()),
                            static_cast<float>(a[1].as_number()),
                            static_cast<float>(a[2].as_number())
                        };
                    }
                }
            }

            ReadBool(o, "enabled", so.enabled);
            if (auto physicsLayerIt = o.find("physicsLayer"); physicsLayerIt != o.end() && physicsLayerIt->second.is_number()) {
                so.physicsLayer = ClampPhysicsLayerIndex(static_cast<int>(physicsLayerIt->second.as_number()));
            }
            if (auto componentOrderIt = o.find("componentOrder"); componentOrderIt != o.end() && componentOrderIt->second.is_array()) {
                so.inspectorComponentOrder.clear();
                for (const auto& orderValue : componentOrderIt->second.as_array()) {
                    if (!orderValue.is_string()) {
                        continue;
                    }
                    SceneInspectorComponentType componentType;
                    if (InspectorComponentTypeFromString(orderValue.as_string(), componentType)) {
                        so.inspectorComponentOrder.push_back(componentType);
                    }
                }
            }

            // color (optional)
            auto colIt = o.find("color");
            if (colIt != o.end() && colIt->second.is_array()) {
                const auto& a = colIt->second.as_array();
                if (a.size() == 4 && a[0].is_number() && a[1].is_number() && a[2].is_number() && a[3].is_number()) {
                    so.meshRenderer.color = {
                        static_cast<float>(a[0].as_number()),
                        static_cast<float>(a[1].as_number()),
                        static_cast<float>(a[2].as_number()),
                        static_cast<float>(a[3].as_number())
                    };
                }
            }

            // materialId (optional)
            auto matIt = o.find("materialId");
            if (matIt != o.end() && matIt->second.is_string()) {
                so.meshRenderer.materialId = matIt->second.as_string();
            }

            auto sourceIt = o.find("sourcePath");
            if (sourceIt != o.end() && sourceIt->second.is_string()) {
                so.meshFilter.sourcePath = NormalizeSlashes(sourceIt->second.as_string());
            }

            auto meshIndexIt = o.find("meshIndex");
            if (meshIndexIt != o.end() && meshIndexIt->second.is_number()) {
                so.meshFilter.meshIndex = static_cast<int>(meshIndexIt->second.as_number());
            }

            auto importedMaterialIt = o.find("importedMaterialName");
            if (importedMaterialIt != o.end() && importedMaterialIt->second.is_string()) {
                so.meshFilter.importedMaterialName = importedMaterialIt->second.as_string();
            }

            auto diffuseTextureIt = o.find("diffuseTexturePath");
            if (diffuseTextureIt != o.end() && diffuseTextureIt->second.is_string()) {
                so.meshFilter.diffuseTexturePath = NormalizeSlashes(diffuseTextureIt->second.as_string());
            }

            auto scriptAttachmentsIt = o.find("scriptAttachments");
            if (scriptAttachmentsIt != o.end() && scriptAttachmentsIt->second.is_array()) {
                const auto& scripts = scriptAttachmentsIt->second.as_array();
                for (const auto& scriptValue : scripts) {
                    if (!scriptValue.is_object()) {
                        continue;
                    }

                    const auto& scriptObject = scriptValue.as_object();
                    ObjectScriptAttachment script;
                    ReadBool(scriptObject, "enabled", script.enabled);
                    ReadString(scriptObject, "scriptName", script.scriptName);
                    ReadString(scriptObject, "scriptPath", script.scriptPath);
                    script.scriptPath = NormalizeSlashes(script.scriptPath);
                    auto fieldsIt = scriptObject.find("fields");
                    if (fieldsIt != scriptObject.end() && fieldsIt->second.is_array()) {
                        for (const auto& fieldValue : fieldsIt->second.as_array()) {
                            if (!fieldValue.is_object()) {
                                continue;
                            }
                            const auto& fieldObject = fieldValue.as_object();
                            ScriptFieldEntry field;
                            std::string typeName;
                            if (!ReadString(fieldObject, "name", field.name) || !ReadString(fieldObject, "type", typeName)) {
                                continue;
                            }
                            field.type = ScriptFieldTypeFromString(typeName);
                            if (!TryReadScriptFieldValue(fieldObject, field.type, field.value)) {
                                continue;
                            }
                            script.fields.push_back(std::move(field));
                        }
                    }
                    if (!script.scriptName.empty()) {
                        SyncAttachmentScriptFields(script);
                        so.scriptComponent.attachments.push_back(script);
                    }
                }
            }

            auto componentsIt = o.find("components");
            if (componentsIt != o.end() && componentsIt->second.is_array()) {
                so.hasMeshFilter = false;
                so.hasMeshRenderer = false;
                so.hasScriptComponent = false;
                so.hasRigidbody = false;
                so.hasVehicle = false;
                so.hasCharacterController = false;
                so.hasBoxCollider = false;
                so.hasSphereCollider = false;
                so.hasCapsuleCollider = false;
                so.hasPlaneCollider = false;
                so.hasMeshCollider = false;
                so.hasCamera = false;
                so.hasCinemachine = false;
                so.hasLight = false;

                const auto& components = componentsIt->second.as_array();
                for (const auto& componentValue : components) {
                    if (!componentValue.is_object()) {
                        continue;
                    }

                    const auto& component = componentValue.as_object();
                    std::string componentType;
                    ReadString(component, "type", componentType);

                    if (componentType == "Transform") {
                        ReadVec3(component, "position", so.transform.position);
                        ReadVec3(component, "rotationEuler", so.transform.rotationEuler);
                        ReadVec3(component, "scale", so.transform.scale);
                    } else if (componentType == "MeshFilter") {
                        so.hasMeshFilter = true;
                        ReadBool(component, "enabled", so.meshFilter.enabled);
                        ReadString(component, "meshType", so.meshFilter.meshType);
                        ReadString(component, "sourcePath", so.meshFilter.sourcePath);
                        so.meshFilter.sourcePath = NormalizeSlashes(so.meshFilter.sourcePath);

                        auto componentMeshIndexIt = component.find("meshIndex");
                        if (componentMeshIndexIt != component.end() && componentMeshIndexIt->second.is_number()) {
                            so.meshFilter.meshIndex = static_cast<int>(componentMeshIndexIt->second.as_number());
                        }

                        ReadString(component, "importedMaterialName", so.meshFilter.importedMaterialName);
                        ReadString(component, "diffuseTexturePath", so.meshFilter.diffuseTexturePath);
                        so.meshFilter.diffuseTexturePath = NormalizeSlashes(so.meshFilter.diffuseTexturePath);
                        ReadVec3(component, "pivotOffset", so.meshFilter.pivotOffset);
                    } else if (componentType == "MeshRenderer") {
                        so.hasMeshRenderer = true;
                        ReadBool(component, "enabled", so.meshRenderer.enabled);
                        ReadString(component, "materialId", so.meshRenderer.materialId);
                        ReadVec4(component, "color", so.meshRenderer.color);
                    } else if (componentType == "Script") {
                        so.hasScriptComponent = true;
                        ReadBool(component, "enabled", so.scriptComponent.enabled);
                        so.scriptComponent.attachments.clear();

                        auto attachmentsIt = component.find("attachments");
                        if (attachmentsIt != component.end() && attachmentsIt->second.is_array()) {
                            const auto& scripts = attachmentsIt->second.as_array();
                            for (const auto& scriptValue : scripts) {
                                if (!scriptValue.is_object()) {
                                    continue;
                                }

                                const auto& scriptObject = scriptValue.as_object();
                                ObjectScriptAttachment script;
                                ReadBool(scriptObject, "enabled", script.enabled);
                                ReadString(scriptObject, "scriptName", script.scriptName);
                                ReadString(scriptObject, "scriptPath", script.scriptPath);
                                script.scriptPath = NormalizeSlashes(script.scriptPath);
                                auto fieldsIt = scriptObject.find("fields");
                                if (fieldsIt != scriptObject.end() && fieldsIt->second.is_array()) {
                                    for (const auto& fieldValue : fieldsIt->second.as_array()) {
                                        if (!fieldValue.is_object()) {
                                            continue;
                                        }
                                        const auto& fieldObject = fieldValue.as_object();
                                        ScriptFieldEntry field;
                                        std::string typeName;
                                        if (!ReadString(fieldObject, "name", field.name) || !ReadString(fieldObject, "type", typeName)) {
                                            continue;
                                        }
                                        field.type = ScriptFieldTypeFromString(typeName);
                                        if (!TryReadScriptFieldValue(fieldObject, field.type, field.value)) {
                                            continue;
                                        }
                                        script.fields.push_back(std::move(field));
                                    }
                                }
                                if (!script.scriptName.empty()) {
                                    SyncAttachmentScriptFields(script);
                                    so.scriptComponent.attachments.push_back(script);
                                }
                            }
                        }
                    } else if (componentType == "Rigidbody") {
                        so.hasRigidbody = true;
                        ReadBool(component, "enabled", so.rigidbody.enabled);

                        std::string bodyType;
                        if (ReadString(component, "bodyType", bodyType)) {
                            so.rigidbody.bodyType = RigidbodyBodyTypeFromString(bodyType);
                        }

                        auto massIt = component.find("mass");
                        if (massIt != component.end() && massIt->second.is_number()) {
                            so.rigidbody.mass = (std::max)(0.0001f, static_cast<float>(massIt->second.as_number()));
                        }

                        ReadBool(component, "useGravity", so.rigidbody.useGravity);
                        if (auto linearDampingIt = component.find("linearDamping"); linearDampingIt != component.end() && linearDampingIt->second.is_number()) {
                            so.rigidbody.linearDamping = (std::max)(0.0f, static_cast<float>(linearDampingIt->second.as_number()));
                        }
                        if (auto angularDampingIt = component.find("angularDamping"); angularDampingIt != component.end() && angularDampingIt->second.is_number()) {
                            so.rigidbody.angularDamping = (std::max)(0.0f, static_cast<float>(angularDampingIt->second.as_number()));
                        }
                        if (auto frictionIt = component.find("friction"); frictionIt != component.end() && frictionIt->second.is_number()) {
                            so.rigidbody.friction = (std::max)(0.0f, static_cast<float>(frictionIt->second.as_number()));
                        }
                        if (auto restitutionIt = component.find("restitution"); restitutionIt != component.end() && restitutionIt->second.is_number()) {
                            so.rigidbody.restitution = (std::max)(0.0f, static_cast<float>(restitutionIt->second.as_number()));
                        }
                        ReadVec3(component, "velocity", so.rigidbody.velocity);
                        ReadVec3(component, "angularVelocity", so.rigidbody.angularVelocity);
                        if (auto freezePositionIt = component.find("freezePosition"); freezePositionIt != component.end() && freezePositionIt->second.is_array()) {
                            const auto& value = freezePositionIt->second.as_array();
                            if (value.size() == 3 && value[0].is_bool() && value[1].is_bool() && value[2].is_bool()) {
                                so.rigidbody.freezePositionX = value[0].as_bool();
                                so.rigidbody.freezePositionY = value[1].as_bool();
                                so.rigidbody.freezePositionZ = value[2].as_bool();
                            }
                        }
                        if (auto freezeRotationIt = component.find("freezeRotation"); freezeRotationIt != component.end() && freezeRotationIt->second.is_array()) {
                            const auto& value = freezeRotationIt->second.as_array();
                            if (value.size() == 3 && value[0].is_bool() && value[1].is_bool() && value[2].is_bool()) {
                                so.rigidbody.freezeRotationX = value[0].as_bool();
                                so.rigidbody.freezeRotationY = value[1].as_bool();
                                so.rigidbody.freezeRotationZ = value[2].as_bool();
                            }
                        }
                    } else if (componentType == "Vehicle") {
                        so.hasVehicle = true;
                        ReadBool(component, "enabled", so.vehicle.enabled);
                        so.vehicle.canTilt = true;  // default — override if saved
                        ReadBool(component, "canTilt", so.vehicle.canTilt);
                        ReadString(component, "configPath", so.vehicle.configPath);
                        so.vehicle.configPath = NormalizeSlashes(so.vehicle.configPath);
                        so.vehicle.chassisObjectIds.clear();
                        if (auto chassisObjectIdsIt = component.find("chassisObjectIds"); chassisObjectIdsIt != component.end() && chassisObjectIdsIt->second.is_array()) {
                            for (const auto& chassisValue : chassisObjectIdsIt->second.as_array()) {
                                if (chassisValue.is_string()) {
                                    const std::string chassisObjectId = chassisValue.as_string();
                                    if (!chassisObjectId.empty()) {
                                        so.vehicle.chassisObjectIds.push_back(chassisObjectId);
                                    }
                                }
                            }
                        }
                        so.vehicle.wheelBindings.clear();
                        if (auto wheelBindingsIt = component.find("wheelBindings"); wheelBindingsIt != component.end() && wheelBindingsIt->second.is_array()) {
                            for (const auto& bindingValue : wheelBindingsIt->second.as_array()) {
                                if (!bindingValue.is_object()) {
                                    continue;
                                }
                                const auto& bindingObject = bindingValue.as_object();
                                VehicleWheelBinding binding;
                                ReadString(bindingObject, "wheelName", binding.wheelName);
                                ReadString(bindingObject, "objectId", binding.objectId);
                                ReadVec3(bindingObject, "visualRotationEuler", binding.visualRotationEuler);
                                if (!binding.wheelName.empty()) {
                                    so.vehicle.wheelBindings.push_back(std::move(binding));
                                }
                            }
                        }
                    } else if (componentType == "CharacterController") {
                        so.hasCharacterController = true;
                        ReadBool(component, "enabled", so.characterController.enabled);

                        auto heightIt = component.find("height");
                        if (heightIt != component.end() && heightIt->second.is_number()) {
                            so.characterController.height = (std::max)(0.001f, static_cast<float>(heightIt->second.as_number()));
                        }
                        auto radiusIt = component.find("radius");
                        if (radiusIt != component.end() && radiusIt->second.is_number()) {
                            so.characterController.radius = (std::max)(0.001f, static_cast<float>(radiusIt->second.as_number()));
                        }
                        so.characterController.height = (std::max)(so.characterController.radius * 2.0f, so.characterController.height);
                        ReadVec3(component, "center", so.characterController.center);
                        auto stepHeightIt = component.find("stepHeight");
                        if (stepHeightIt != component.end() && stepHeightIt->second.is_number()) {
                            so.characterController.stepHeight = (std::max)(0.0f, static_cast<float>(stepHeightIt->second.as_number()));
                        }
                        auto slopeLimitIt = component.find("slopeLimitDegrees");
                        if (slopeLimitIt != component.end() && slopeLimitIt->second.is_number()) {
                            so.characterController.slopeLimitDegrees = (std::max)(1.0f, (std::min)(89.0f, static_cast<float>(slopeLimitIt->second.as_number())));
                        }
                        auto maxStrengthIt = component.find("maxStrength");
                        if (maxStrengthIt != component.end() && maxStrengthIt->second.is_number()) {
                            so.characterController.maxStrength = (std::max)(0.0f, static_cast<float>(maxStrengthIt->second.as_number()));
                        }
                        auto massIt = component.find("mass");
                        if (massIt != component.end() && massIt->second.is_number()) {
                            so.characterController.mass = (std::max)(0.001f, static_cast<float>(massIt->second.as_number()));
                        }
                    } else if (componentType == "Collider") {
                        std::string colliderTypeName;
                        if (ReadString(component, "colliderType", colliderTypeName)) {
                            const std::string lowerType = ToLowerCopy(colliderTypeName);
                            if (lowerType == "box") {
                                SetActiveColliderType(so, SceneColliderType::Box);
                                ReadBool(component, "enabled", so.boxCollider.enabled);
                                ReadBool(component, "isTrigger", so.boxCollider.isTrigger);
                                ReadVec3(component, "center", so.boxCollider.center);
                                ReadVec3(component, "size", so.boxCollider.size);
                                so.boxCollider.size = {
                                    (std::max)(0.001f, so.boxCollider.size.x),
                                    (std::max)(0.001f, so.boxCollider.size.y),
                                    (std::max)(0.001f, so.boxCollider.size.z)
                                };
                            } else if (lowerType == "sphere") {
                                SetActiveColliderType(so, SceneColliderType::Sphere);
                                ReadBool(component, "enabled", so.sphereCollider.enabled);
                                ReadBool(component, "isTrigger", so.sphereCollider.isTrigger);
                                ReadVec3(component, "center", so.sphereCollider.center);
                                if (auto radiusIt = component.find("radius"); radiusIt != component.end() && radiusIt->second.is_number()) {
                                    so.sphereCollider.radius = (std::max)(0.001f, static_cast<float>(radiusIt->second.as_number()));
                                }
                            } else if (lowerType == "capsule") {
                                SetActiveColliderType(so, SceneColliderType::Capsule);
                                ReadBool(component, "enabled", so.capsuleCollider.enabled);
                                ReadBool(component, "isTrigger", so.capsuleCollider.isTrigger);
                                ReadVec3(component, "center", so.capsuleCollider.center);
                                if (auto radiusIt = component.find("radius"); radiusIt != component.end() && radiusIt->second.is_number()) {
                                    so.capsuleCollider.radius = (std::max)(0.001f, static_cast<float>(radiusIt->second.as_number()));
                                }
                                if (auto heightIt = component.find("height"); heightIt != component.end() && heightIt->second.is_number()) {
                                    so.capsuleCollider.height = (std::max)(so.capsuleCollider.radius * 2.0f, static_cast<float>(heightIt->second.as_number()));
                                }
                            } else if (lowerType == "plane") {
                                SetActiveColliderType(so, SceneColliderType::Plane);
                                ReadBool(component, "enabled", so.planeCollider.enabled);
                                ReadBool(component, "isTrigger", so.planeCollider.isTrigger);
                                ReadVec3(component, "normal", so.planeCollider.normal);
                                if (glm::length2(so.planeCollider.normal) <= 0.000001f) {
                                    so.planeCollider.normal = {0.0f, 1.0f, 0.0f};
                                } else {
                                    so.planeCollider.normal = glm::normalize(so.planeCollider.normal);
                                }
                                if (auto offsetIt = component.find("offset"); offsetIt != component.end() && offsetIt->second.is_number()) {
                                    so.planeCollider.offset = static_cast<float>(offsetIt->second.as_number());
                                }
                                ReadBool(component, "infinite", so.planeCollider.infinite);
                                if (auto halfExtentIt = component.find("halfExtent"); halfExtentIt != component.end() && halfExtentIt->second.is_number()) {
                                    so.planeCollider.halfExtent = (std::max)(0.001f, static_cast<float>(halfExtentIt->second.as_number()));
                                }
                            } else if (lowerType == "mesh") {
                                SetActiveColliderType(so, SceneColliderType::Mesh);
                                ReadBool(component, "enabled", so.meshCollider.enabled);
                                ReadBool(component, "isTrigger", so.meshCollider.isTrigger);
                                std::string buildQuality;
                                if (ReadString(component, "buildQuality", buildQuality)) {
                                    so.meshCollider.buildQuality = MeshColliderBuildQualityFromString(buildQuality);
                                }
                                std::string meshMode;
                                if (ReadString(component, "mode", meshMode)) {
                                    so.meshCollider.mode = MeshColliderModeFromString(meshMode);
                                }
                            }
                        }
                    } else if (componentType == "BoxCollider") {
                        SetActiveColliderType(so, SceneColliderType::Box);
                        ReadBool(component, "enabled", so.boxCollider.enabled);
                        ReadBool(component, "isTrigger", so.boxCollider.isTrigger);
                        ReadVec3(component, "center", so.boxCollider.center);
                        ReadVec3(component, "size", so.boxCollider.size);
                        so.boxCollider.size = {
                            (std::max)(0.001f, so.boxCollider.size.x),
                            (std::max)(0.001f, so.boxCollider.size.y),
                            (std::max)(0.001f, so.boxCollider.size.z)
                        };
                    } else if (componentType == "SphereCollider") {
                        SetActiveColliderType(so, SceneColliderType::Sphere);
                        ReadBool(component, "enabled", so.sphereCollider.enabled);
                        ReadBool(component, "isTrigger", so.sphereCollider.isTrigger);
                        ReadVec3(component, "center", so.sphereCollider.center);

                        auto radiusIt = component.find("radius");
                        if (radiusIt != component.end() && radiusIt->second.is_number()) {
                            so.sphereCollider.radius = (std::max)(0.001f, static_cast<float>(radiusIt->second.as_number()));
                        }
                    } else if (componentType == "CapsuleCollider") {
                        SetActiveColliderType(so, SceneColliderType::Capsule);
                        ReadBool(component, "enabled", so.capsuleCollider.enabled);
                        ReadBool(component, "isTrigger", so.capsuleCollider.isTrigger);
                        ReadVec3(component, "center", so.capsuleCollider.center);

                        auto radiusIt = component.find("radius");
                        if (radiusIt != component.end() && radiusIt->second.is_number()) {
                            so.capsuleCollider.radius = (std::max)(0.001f, static_cast<float>(radiusIt->second.as_number()));
                        }
                        auto heightIt = component.find("height");
                        if (heightIt != component.end() && heightIt->second.is_number()) {
                            so.capsuleCollider.height = (std::max)(so.capsuleCollider.radius * 2.0f, static_cast<float>(heightIt->second.as_number()));
                        }
                    } else if (componentType == "PlaneCollider") {
                        SetActiveColliderType(so, SceneColliderType::Plane);
                        ReadBool(component, "enabled", so.planeCollider.enabled);
                        ReadBool(component, "isTrigger", so.planeCollider.isTrigger);
                        ReadVec3(component, "normal", so.planeCollider.normal);
                        if (glm::length2(so.planeCollider.normal) <= 0.000001f) {
                            so.planeCollider.normal = {0.0f, 1.0f, 0.0f};
                        } else {
                            so.planeCollider.normal = glm::normalize(so.planeCollider.normal);
                        }
                        auto offsetIt = component.find("offset");
                        if (offsetIt != component.end() && offsetIt->second.is_number()) {
                            so.planeCollider.offset = static_cast<float>(offsetIt->second.as_number());
                        }
                        ReadBool(component, "infinite", so.planeCollider.infinite);
                        auto halfExtentIt = component.find("halfExtent");
                        if (halfExtentIt != component.end() && halfExtentIt->second.is_number()) {
                            so.planeCollider.halfExtent = (std::max)(0.001f, static_cast<float>(halfExtentIt->second.as_number()));
                        }
                    } else if (componentType == "MeshCollider") {
                        SetActiveColliderType(so, SceneColliderType::Mesh);
                        ReadBool(component, "enabled", so.meshCollider.enabled);
                        ReadBool(component, "isTrigger", so.meshCollider.isTrigger);
                        std::string buildQuality;
                        if (ReadString(component, "buildQuality", buildQuality)) {
                            so.meshCollider.buildQuality = MeshColliderBuildQualityFromString(buildQuality);
                        }
                        std::string meshMode;
                        if (ReadString(component, "mode", meshMode)) {
                            so.meshCollider.mode = MeshColliderModeFromString(meshMode);
                        }
                    } else if (componentType == "Camera") {
                        so.hasCamera = true;
                        ReadBool(component, "enabled", so.camera.enabled);
                        ReadBool(component, "isMain", so.camera.isMain);
                        ReadVec4(component, "clearColor", so.camera.clearColor);

                        auto fovIt = component.find("fieldOfViewDegrees");
                        if (fovIt != component.end() && fovIt->second.is_number()) {
                            so.camera.fieldOfViewDegrees = (std::max)(1.0f, (std::min)(179.0f, static_cast<float>(fovIt->second.as_number())));
                        }
                        auto nearIt = component.find("nearClip");
                        if (nearIt != component.end() && nearIt->second.is_number()) {
                            so.camera.nearClip = (std::max)(0.001f, static_cast<float>(nearIt->second.as_number()));
                        }
                        auto farIt = component.find("farClip");
                        if (farIt != component.end() && farIt->second.is_number()) {
                            so.camera.farClip = (std::max)(so.camera.nearClip + 0.001f, static_cast<float>(farIt->second.as_number()));
                        }
                    } else if (componentType == "Cinemachine") {
                        so.hasCinemachine = true;
                        ReadBool(component, "enabled", so.cinemachine.enabled);
                        std::string cameraTypeStr;
                        if (ReadString(component, "cameraType", cameraTypeStr)) {
                            if (cameraTypeStr == "Follow") {
                                so.cinemachine.type = CinemachineCameraType::Follow;
                            } else if (cameraTypeStr == "LookAt") {
                                so.cinemachine.type = CinemachineCameraType::LookAt;
                            } else {
                                so.cinemachine.type = CinemachineCameraType::FollowAndLookAt;
                            }
                        }
                        ReadString(component, "followTargetId", so.cinemachine.followTargetId);
                        ReadString(component, "lookAtTargetId", so.cinemachine.lookAtTargetId);
                        ReadVec3(component, "followOffset", so.cinemachine.followOffset);
                        auto pitchIt = component.find("pitchOffset");
                        if (pitchIt != component.end() && pitchIt->second.is_number()) {
                            so.cinemachine.pitchOffset = static_cast<float>(pitchIt->second.as_number());
                        }
                        auto yawIt = component.find("yawOffset");
                        if (yawIt != component.end() && yawIt->second.is_number()) {
                            so.cinemachine.yawOffset = static_cast<float>(yawIt->second.as_number());
                        }
                        auto posDampingIt = component.find("positionDamping");
                        if (posDampingIt != component.end() && posDampingIt->second.is_number()) {
                            so.cinemachine.positionDamping = (std::max)(0.0f, static_cast<float>(posDampingIt->second.as_number()));
                        }
                        auto rotDampingIt = component.find("rotationDamping");
                        if (rotDampingIt != component.end() && rotDampingIt->second.is_number()) {
                            so.cinemachine.rotationDamping = (std::max)(0.0f, static_cast<float>(rotDampingIt->second.as_number()));
                        }
                    } else if (componentType == "Light") {
                        so.hasLight = true;
                        ReadBool(component, "enabled", so.light.enabled);
                        ReadVec3(component, "color", so.light.color);

                        std::string lightType;
                        if (ReadString(component, "lightType", lightType)) {
                            so.light.type = LightTypeFromString(lightType);
                        }

                        auto intensityIt = component.find("intensity");
                        if (intensityIt != component.end() && intensityIt->second.is_number()) {
                            so.light.intensity = (std::max)(0.0f, static_cast<float>(intensityIt->second.as_number()));
                        }
                        auto rangeIt = component.find("range");
                        if (rangeIt != component.end() && rangeIt->second.is_number()) {
                            so.light.range = (std::max)(0.001f, static_cast<float>(rangeIt->second.as_number()));
                        }
                        auto spotAngleIt = component.find("spotAngleDegrees");
                        if (spotAngleIt != component.end() && spotAngleIt->second.is_number()) {
                            so.light.spotAngleDegrees = (std::max)(1.0f, (std::min)(179.0f, static_cast<float>(spotAngleIt->second.as_number())));
                        }
                    }
                }
            }

            if (so.meshFilter.meshType.empty()) {
                if (legacyType == "Plane" || legacyType == "Mesh") {
                    so.meshFilter.meshType = legacyType;
                } else if (so.hasMeshFilter) {
                    so.meshFilter.meshType = !so.meshFilter.sourcePath.empty() ? std::string("Mesh") : std::string("Plane");
                }
            }

            if (so.hasCharacterController && so.hasRigidbody) {
                so.hasRigidbody = false;
                so.rigidbody = RigidbodyComponent{};
            }

            SyncInspectorComponentOrder(so);

            if (legacyType == "Plane" || legacyType == "Mesh") {
                so.hasMeshFilter = true;
                so.hasMeshRenderer = true;
            } else if (legacyType == "MeshFilter") {
                so.hasMeshFilter = true;
            } else if (legacyType == "MeshRenderer") {
                so.hasMeshRenderer = true;
            } else if (legacyType == "BoxCollider") {
                SetActiveColliderType(so, SceneColliderType::Box);
            } else if (legacyType == "SphereCollider") {
                SetActiveColliderType(so, SceneColliderType::Sphere);
            } else if (legacyType == "CapsuleCollider") {
                SetActiveColliderType(so, SceneColliderType::Capsule);
            } else if (legacyType == "PlaneCollider") {
                SetActiveColliderType(so, SceneColliderType::Plane);
            } else if (legacyType == "MeshCollider") {
                SetActiveColliderType(so, SceneColliderType::Mesh);
            } else if (legacyType == "Camera") {
                so.hasCamera = true;
            } else if (legacyType == "Light") {
                so.hasLight = true;
            }

            if (!so.hasMeshFilter && !so.meshFilter.sourcePath.empty()) {
                so.hasMeshFilter = true;
            }

            if (!so.hasMeshRenderer && (!so.meshRenderer.materialId.empty() || so.meshRenderer.color != glm::vec4(1.0f))) {
                so.hasMeshRenderer = true;
            }

            if (!so.hasMeshFilter && !so.hasMeshRenderer && !so.hasScriptComponent && !so.hasRigidbody &&
                !so.hasVehicle && !so.hasCharacterController && !so.hasBoxCollider && !so.hasSphereCollider &&
                !so.hasCapsuleCollider && !so.hasPlaneCollider && !so.hasMeshCollider && !so.hasCamera && !so.hasCinemachine && !so.hasLight) {
                so.hasMeshFilter = true;
                so.hasMeshRenderer = true;
                so.meshFilter.meshType = "Plane";
            }

            if (!so.hasMeshFilter) {
                so.meshFilter.meshType.clear();
            }

            if (so.meshRenderer.materialId.empty()) {
                so.meshRenderer.materialId = "pbr_default";
            }

            if (so.meshFilter.meshType == "Mesh" && so.meshFilter.meshIndex < 0) {
                so.meshFilter.meshIndex = 0;
            }

            const std::string meshType = so.meshFilter.meshType.empty() ? std::string("Mesh") : so.meshFilter.meshType;
            if (so.hasMeshFilter && IsBuiltInPrimitiveMeshType(meshType)) {
                ConfigureBuiltInPrimitive(so, meshType, builtInPrimitiveMeshes_);
            } else if (so.hasMeshFilter && meshType == "Mesh" && !so.meshFilter.sourcePath.empty()) {
                try {
                    if (ShouldFallbackToBuiltInPlane(so)) {
                        ConfigureBuiltInPrimitive(so, "Plane", builtInPrimitiveMeshes_);
                    } else {
                        const std::string cacheKey = NormalizeSlashes(so.meshFilter.sourcePath);
                        CachedLoadedMeshAsset& cacheEntry = meshAssetCache[cacheKey];
                        if (!cacheEntry.attempted) {
                            cacheEntry.attempted = true;
                            cacheEntry.loaded = TryLoadMeshAsset(
                                so.meshFilter.sourcePath,
                                cacheEntry.resolvedPath,
                                cacheEntry.model,
                                cacheEntry.infos);
                        }

                        if (cacheEntry.loaded &&
                            so.meshFilter.meshIndex >= 0 &&
                            so.meshFilter.meshIndex < static_cast<int>(cacheEntry.infos.size())) {
                            so.meshFilter.meshType = "Mesh";
                            so.meshFilter.sourcePath = cacheEntry.resolvedPath;
                            ApplyMeshInfoToSceneObject(
                                so,
                                cacheEntry.infos[static_cast<std::size_t>(so.meshFilter.meshIndex)],
                                cacheEntry.model);
                        }
                    }
                } catch (...) {
                    if (ShouldFallbackToBuiltInPlane(so) && ConfigureBuiltInPrimitive(so, "Plane", builtInPrimitiveMeshes_)) {
                        AddDefaultPlaneColliderToPlane(so);
                    } else if (console_) {
                        console_->AddLog("Failed to reload mesh source: " + so.meshFilter.sourcePath);
                    }
                }
            }

            objects_.push_back(so);
        }

        std::vector<std::string> seenIds;
        std::vector<std::pair<std::string, std::string>> remappedIds;
        for (SceneObject& object : objects_) {
            const bool missingId = object.id.empty();
            const bool duplicateId = !missingId && std::find(seenIds.begin(), seenIds.end(), object.id) != seenIds.end();
            if (!missingId && !duplicateId) {
                seenIds.push_back(object.id);
                continue;
            }

            const std::string oldId = object.id;
            object.id = MakeId("gameobject");
            seenIds.push_back(object.id);
            if (!oldId.empty()) {
                remappedIds.emplace_back(oldId, object.id);
            }
        }

        for (SceneObject& object : objects_) {
            for (const auto& remap : remappedIds) {
                if (object.parentId == remap.first) {
                    object.parentId = remap.second;
                }
            }
        }

        auto closedIt = obj.find("hierarchyClosed");
        if (closedIt != obj.end() && closedIt->second.is_array()) {
            for (const auto& idValue : closedIt->second.as_array()) {
                if (!idValue.is_string()) {
                    continue;
                }
                hierarchyOpenStates_[idValue.as_string()] = false;
            }
        } else {
            auto openIt = obj.find("hierarchyOpen");
            if (openIt != obj.end() && openIt->second.is_array()) {
                for (const auto& idValue : openIt->second.as_array()) {
                    if (!idValue.is_string()) {
                        continue;
                    }
                    hierarchyOpenStates_[idValue.as_string()] = true;
                }
            }
        }

        for (auto& [id, isOpen] : hierarchyOpenStates_) {
            (void)id;
            (void)isOpen;
        }

        for (auto& object : objects_) {
            if (object.hasScriptComponent) {
                for (auto& attachment : object.scriptComponent.attachments) {
                    SyncAttachmentScriptFields(attachment);
                }
            }
        }

        RemapVehicleObjectReferences(objects_);

        for (SceneObject& object : objects_) {
            if (!object.parentId.empty() && FindObjectIndexById(object.parentId) < 0) {
                object.parentId.clear();
            }
        }
        if (!objects_.empty()) {
            Select(0);
        }
    }
    catch (...) {
        objects_.clear();
        selectedIndex_ = -1;
        selectedIndices_.clear();
        undoStack_.clear();
        redoStack_.clear();
        if (console_) {
            console_->AddError("Failed to load scene: " + path);
        }
        return;
    }

    NormalizeSelection();
}

void SceneEditor::LoadProject() {
    using namespace raceman::physics::json;
    using raceman::physics::json::Value;
    using raceman::physics::json::parse;
    using scene_editor_internal::ReadBoolArray;
    using scene_editor_internal::ReadString;
    MigrateLegacyProjectLayout();

    projectName_ = "Project Raceman";
    assetsRootSetting_ = "assets";
    defaultScenePath_ = "assets/scenes/EditorScene.scene.json";
    lastScenePath_ = defaultScenePath_;
    selectedProjectDirectory_ = "assets";
    activeViewport_ = SceneEditorActiveViewport::Scene;
    ResetPhysicsLayerSettings();

    bool shouldSaveProject = false;

    const fs::path assetsRoot = FindAssetsRoot();
    const fs::path projectFile = ProjectRootPath() / projectPath_;
    const fs::path scenesRoot = assetsRoot / "scenes";

    if (!fs::exists(projectFile)) {
        if (console_) {
            console_->AddLog("Project file not found, creating default: " + NormalizeSlashes(projectFile.string()));
        }
        shouldSaveProject = true;
    }

    try {
        fs::create_directories(scenesRoot);
    } catch (...) {}

    std::string projectSource;
    if (ReadTextFile(projectFile, projectSource)) {
        try {
            Value root = parse(projectSource);
            if (root.is_object()) {
                const auto& object = root.as_object();
                ReadString(object, "projectName", projectName_);
                ReadString(object, "assetsRoot", assetsRootSetting_);
                ReadString(object, "defaultScene", defaultScenePath_);
                ReadString(object, "lastScene", lastScenePath_);

                auto editorIt = object.find("editorState");
                if (editorIt != object.end() && editorIt->second.is_object()) {
                    const auto& editorState = editorIt->second.as_object();
                    ReadString(editorState, "selectedProjectDirectory", selectedProjectDirectory_);
                }

                auto physicsIt = object.find("physics");
                if (physicsIt != object.end() && physicsIt->second.is_object()) {
                    const auto& physicsSettings = physicsIt->second.as_object();

                    auto layersIt = physicsSettings.find("layers");
                    if (layersIt != physicsSettings.end() && layersIt->second.is_array()) {
                        const auto& layers = layersIt->second.as_array();
                        for (int layerIndex = 0; layerIndex < kPhysicsLayerCount && layerIndex < static_cast<int>(layers.size()); ++layerIndex) {
                            if (layers[static_cast<std::size_t>(layerIndex)].is_string()) {
                                physicsLayerNames_[static_cast<std::size_t>(layerIndex)] = MakePhysicsLayerStorageName(
                                    layers[static_cast<std::size_t>(layerIndex)].as_string(),
                                    layerIndex);
                            }
                        }
                    }

                    auto matrixIt = physicsSettings.find("collisionMatrix");
                    if (matrixIt != physicsSettings.end() && matrixIt->second.is_array()) {
                        const auto& rows = matrixIt->second.as_array();
                        for (int row = 0; row < kPhysicsLayerCount && row < static_cast<int>(rows.size()); ++row) {
                            if (!rows[static_cast<std::size_t>(row)].is_object()) {
                                continue;
                            }
                            std::array<bool, kPhysicsLayerCount> rowValues = physicsLayerCollisionMatrix_[static_cast<std::size_t>(row)];
                            if (ReadBoolArray(rows[static_cast<std::size_t>(row)].as_object(), "values", rowValues)) {
                                physicsLayerCollisionMatrix_[static_cast<std::size_t>(row)] = rowValues;
                            }
                        }
                    }
                }
            }
        } catch (...) {
            shouldSaveProject = true;
        }
    } else {
        shouldSaveProject = true;
    }

    defaultScenePath_ = NormalizeSlashes(defaultScenePath_.empty() ? "assets/scenes/EditorScene.scene.json" : defaultScenePath_);
    lastScenePath_ = NormalizeSlashes(lastScenePath_.empty() ? defaultScenePath_ : lastScenePath_);

    std::string sceneToLoad = lastScenePath_;
    if (!IsSceneAssetPath(sceneToLoad)) {
        sceneToLoad = defaultScenePath_;
        shouldSaveProject = true;
    }

    if (!fs::exists(ResolveEditorPath(sceneToLoad))) {
        const fs::path defaultSceneAbsolute = ResolveEditorPath(defaultScenePath_);
        const fs::path legacyEditorScene = EngineRootPath() / "config" / "scenes" / "EditorScene.json";
        if (!fs::exists(defaultSceneAbsolute) && fs::exists(legacyEditorScene)) {
            try {
                fs::create_directories(defaultSceneAbsolute.parent_path());
                fs::copy_file(legacyEditorScene, defaultSceneAbsolute, fs::copy_options::overwrite_existing);
                shouldSaveProject = true;
            } catch (...) {}
        }
        sceneToLoad = defaultScenePath_;
    }

    savePath_ = NormalizeSlashes(sceneToLoad);
    if (fs::exists(ResolveEditorPath(savePath_))) {
        Load(savePath_);
    } else {
        objects_.clear();
        selectedIndex_ = -1;
        selectedIndices_.clear();
        undoStack_.clear();
        redoStack_.clear();
        CreateDefaultSceneObjects();
        Save(savePath_);
        shouldSaveProject = true;
    }

    lastScenePath_ = savePath_;
    RefreshProjectFiles();
    if (shouldSaveProject) {
        SaveProject();
    }
}

void SceneEditor::SaveProject() {
    try {
        const fs::path projectFile = ProjectRootPath() / projectPath_;
        fs::create_directories(projectFile.parent_path());
        std::ofstream out(projectFile, std::ios::trunc);
        if (!out.good()) {
            return;
        }

        out << "{\n";
        out << "  \"version\": 1,\n";
        out << "  \"projectName\": \"" << JsonEscape(projectName_) << "\",\n";
        out << "  \"assetsRoot\": \"" << JsonEscape(assetsRootSetting_) << "\",\n";
        out << "  \"defaultScene\": \"" << JsonEscape(NormalizeSlashes(defaultScenePath_)) << "\",\n";
        out << "  \"lastScene\": \"" << JsonEscape(NormalizeSlashes(lastScenePath_)) << "\",\n";
        out << "  \"physics\": {\n";
        out << "    \"layers\": [\n";
        for (int layerIndex = 0; layerIndex < kPhysicsLayerCount; ++layerIndex) {
            const std::string layerName = MakePhysicsLayerStorageName(physicsLayerNames_[static_cast<std::size_t>(layerIndex)], layerIndex);
            out << "      \"" << JsonEscape(layerName) << "\"" << (layerIndex + 1 < kPhysicsLayerCount ? ",\n" : "\n");
        }
        out << "    ],\n";
        out << "    \"collisionMatrix\": [\n";
        for (int row = 0; row < kPhysicsLayerCount; ++row) {
            out << "      {\n";
            out << "        \"values\": [";
            for (int column = 0; column < kPhysicsLayerCount; ++column) {
                out << (physicsLayerCollisionMatrix_[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] ? "true" : "false");
                if (column + 1 < kPhysicsLayerCount) {
                    out << ", ";
                }
            }
            out << "]\n";
            out << "      }" << (row + 1 < kPhysicsLayerCount ? ",\n" : "\n");
        }
        out << "    ]\n";
        out << "  },\n";
        out << "  \"editorState\": {\n";
        out << "    \"selectedProjectDirectory\": \"" << JsonEscape(NormalizeSlashes(selectedProjectDirectory_)) << "\"\n";
        out << "  }\n";
        out << "}\n";
        if (console_) {
            console_->AddLog("Project saved: " + NormalizeSlashes(projectFile.string()));
        }
    } catch (...) {
        if (console_) {
            console_->AddError("Failed to save project file.");
        }
    }
}

} // namespace raceman
