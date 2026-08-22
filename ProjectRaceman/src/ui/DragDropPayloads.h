#pragma once

// ImGui drag-and-drop payload ids, shared by every panel that produces or
// accepts one. They live here rather than in SceneEditorInternal.h so a window
// outside the scene editor - the settings menus, for one - can accept an asset
// dragged out of the Project panel without pulling in the editor's internals.
//
// kProjectFilePayload carries a project-relative file path as a NUL-terminated
// string; the Project panel attaches it to every file tile it draws.

namespace raceman {

inline constexpr const char* kObjAssetPayload = "RACEMAN_PROJECT_OBJ";
inline constexpr const char* kMeshAssetPayload = "RACEMAN_PROJECT_MESH";
inline constexpr const char* kModelChildAssetPayload = "RACEMAN_PROJECT_MODEL_CHILD";
inline constexpr const char* kMaterialAssetPayload = "RACEMAN_PROJECT_MATERIAL";
inline constexpr const char* kProjectFilePayload = "RACEMAN_PROJECT_FILE";
inline constexpr const char* kHierarchyObjectPayload = "SCENE_HIERARCHY_OBJECT_INDEX";
inline constexpr const char* kHierarchyMultiObjectPayload = "SCENE_HIERARCHY_OBJECT_IDS";

} // namespace raceman
