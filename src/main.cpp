#include "main.hpp"
#include "ModConfig.hpp"
#include "logging.hpp"
#include "GlobalNamespace/LightManager.hpp"
#include "UnityEngine/Rendering/ScriptableRenderContext.hpp"
#include "GlobalNamespace/VRController.hpp"
#include "GlobalNamespace/Saber.hpp"
#include "GlobalNamespace/OVRInput.hpp"
#include "GlobalNamespace/AudioTimeSyncController.hpp"

#include "beatsaber-hook/shared/hooking.hpp"
#include "beatsaber-hook/shared/types.hpp"

#include "UnityEngine/Transform.hpp"
#include "UnityEngine/EntityId.hpp"
#include "UnityEngine/FindObjectsSortMode.hpp"
#include "UnityEngine/Time.hpp"
#include "UnityEngine/Camera.hpp"
#include "UnityEngine/GameObject.hpp"
#include "UnityEngine/Quaternion.hpp"
#include "UnityEngine/Matrix4x4.hpp"
#include "UnityEngine/SceneManagement/Scene.hpp"
#include "UnityEngine/SceneManagement/SceneManager.hpp"

#include "Play3rdPerViewController.hpp"

#include "bsml/shared/BSML.hpp"

#include <math.h>

using namespace GlobalNamespace;

static modloader::ModInfo modInfo{MOD_ID, VERSION, 0};

UnityEngine::Matrix4x4 TranslateMatrix(UnityEngine::Vector3 vector)
        {
            UnityEngine::Matrix4x4 result;
            result.m00 = 1;
            result.m01 = 0;
            result.m02 = 0;
            result.m03 = vector.x;
            result.m10 = 0;
            result.m11 = 1;
            result.m12 = 0;
            result.m13 = vector.y;
            result.m20 = 0;
            result.m21 = 0;
            result.m22 = 1;
            result.m23 = vector.z;
            result.m30 = 0;
            result.m31 = 0;
            result.m32 = 0;
            result.m33 = 1;
            return result;
        }

namespace {
struct ControllerLookup {
    UnityEngine::EntityId id{0};
    int retryFrame = 0;
};

ControllerLookup leftController, rightController;
UnityEngine::EntityId cullingCameraId{0};
int previousMovementSource = 0;
UnityEngine::Vector3 prevPos;

UnityW<VRController> FindController(bool right) {
    auto& lookup = right ? rightController : leftController;
    if (lookup.id.m_Data != 0) {
        auto controller = UnityEngine::Object::FindObjectFromInstanceID(lookup.id).try_cast<VRController>();
        if (controller && controller->get_isActiveAndEnabled()) return controller;
        lookup = {};
    }

    const int currentFrame = UnityEngine::Time::get_frameCount();
    if (currentFrame < lookup.retryFrame) return nullptr;
    lookup.retryFrame = currentFrame + 60;
    auto node = right ? UnityEngine::XR::XRNode::RightHand : UnityEngine::XR::XRNode::LeftHand;
    auto controllers = UnityEngine::Object::FindObjectsByType(
        i2c::cs_type_of<VRController*>(), UnityEngine::FindObjectsSortMode::None);
    for (auto object : controllers) {
        auto controller = object.try_cast<VRController>();
        if (controller && controller->get_isActiveAndEnabled() && controller->get_node() == node) {
            lookup.id = controller->GetEntityId();
            return controller;
        }
    }
    return nullptr;
}

void UpdateMovementOffset(UnityEngine::Camera* camera, UnityEngine::Vector3 position) {
    auto source = getModConfig().MoveController.GetValue();
    int sourceId = camera->GetInstanceID();
    if (source != 0) {
        auto controller = FindController(source != 1);
        if (!controller) {
            previousMovementSource = 0;
            return;
        }
        position = controller->get_position();
        sourceId = controller->GetInstanceID();
    }
    OVRInput::Update();
    if (sourceId == previousMovementSource && OVRInput::Get(OVRInput::Button::Four, OVRInput::Controller::Touch)) {
        auto delta = UnityEngine::Vector3::op_Subtraction(position, prevPos);
        auto multiplier = getModConfig().MoveMultiplier.GetValue();
        getModConfig().XOffset.SetValue(getModConfig().XOffset.GetValue() + delta.x * multiplier);
        getModConfig().YOffset.SetValue(getModConfig().YOffset.GetValue() + delta.y * multiplier);
        getModConfig().ZOffset.SetValue(getModConfig().ZOffset.GetValue() + delta.z * multiplier);
    }
    previousMovementSource = sourceId;
    prevPos = position;
}

void ResetCullingOverride() {
    if (cullingCameraId.m_Data == 0) return;
    auto camera = UnityEngine::Object::FindObjectFromInstanceID(cullingCameraId).try_cast<UnityEngine::Camera>();
    if (camera) camera->ResetCullingMatrix();
    cullingCameraId = UnityEngine::EntityId{0};
}
}

bool replay = false;
float rotated = 0.0f;
int framesPressed = 0;
int frame = 0;

MAKE_HOOK_MATCH(LightManager_OnWillRenderObject, &LightManager::OnCameraPreRender, void, LightManager* self, UnityEngine::Rendering::ScriptableRenderContext context, UnityEngine::Camera* camera) {
    LightManager_OnWillRenderObject(self, context, camera);
    
    if (!getModConfig().Active.GetValue()) {
        ResetCullingOverride();
        previousMovementSource = 0;
        return;
    }

    auto mainCamera = UnityEngine::Camera::get_main();
    if (!mainCamera || camera != mainCamera.unsafe_ptr()) return;

    frame++;
    if(frame < 20) return;
    frame = 20;

  UnityEngine::Vector3 rot = mainCamera->get_transform()->get_eulerAngles();
  UnityEngine::Vector3 pos = mainCamera->get_transform()->get_position();

    if(GlobalNamespace::OVRInput::Get(GlobalNamespace::OVRInput::Button::One, OVRInput::Controller::Touch) && GlobalNamespace::OVRInput::Get(GlobalNamespace::OVRInput::Button::Two, OVRInput::Controller::Touch) && GlobalNamespace::OVRInput::Get(GlobalNamespace::OVRInput::Button::Three, OVRInput::Controller::Touch) && GlobalNamespace::OVRInput::Get(GlobalNamespace::OVRInput::Button::Four, OVRInput::Controller::Touch) && GlobalNamespace::OVRInput::Get(GlobalNamespace::OVRInput::Button::PrimaryHandTrigger, OVRInput::Controller::Touch) && GlobalNamespace::OVRInput::Get(GlobalNamespace::OVRInput::Button::PrimaryIndexTrigger, OVRInput::Controller::Touch) && GlobalNamespace::OVRInput::Get(GlobalNamespace::OVRInput::Button::SecondaryHandTrigger, OVRInput::Controller::Touch) && GlobalNamespace::OVRInput::Get(GlobalNamespace::OVRInput::Button::SecondaryIndexTrigger, OVRInput::Controller::Touch)) {
        framesPressed++;
        if(framesPressed >= 1000) {
            getModConfig().XOffset.SetValue(0.0f);
            getModConfig().YOffset.SetValue(0.0f);
            getModConfig().ZOffset.SetValue(0.0f);
            getModConfig().XRot.SetValue(0.0f);
            getModConfig().YRot.SetValue(0.0f);
            getModConfig().ZRot.SetValue(0.0f);
            getModConfig().WashingMachine.SetValue(false);
            getModConfig().Active.SetValue(false);
            framesPressed = 0;
            previousMovementSource = 0;
            ResetCullingOverride();
            return;
        }
    }
    else if(getModConfig().MoveWhilePlaying.GetValue()) {
        UpdateMovementOffset(camera, pos);
    }
    else {
        framesPressed = 0;
        previousMovementSource = 0;
    }

    UnityEngine::Vector3 saberPos{}, saberRot{};
    if (getModConfig().LeftSaber.GetValue()) {
        auto controller = FindController(getModConfig().SwapSaber.GetValue());
        if (!controller) {
            ResetCullingOverride();
            return;
        }
        saberPos = controller->get_position();
        saberRot = controller->get_rotation().get_eulerAngles();
    }

    UnityEngine::Vector3 offsetRot = UnityEngine::Vector3(getModConfig().XRot.GetValue(), getModConfig().YRot.GetValue(), getModConfig().ZRot.GetValue());
    UnityEngine::Vector3 offsetPos = UnityEngine::Vector3(getModConfig().XOffset.GetValue(), getModConfig().YOffset.GetValue(), getModConfig().ZOffset.GetValue());
    
    auto cameraId = mainCamera->GetEntityId();
    if (cullingCameraId.m_Data != cameraId.m_Data) {
        ResetCullingOverride();
        cullingCameraId = cameraId;
    }
    auto vector = UnityEngine::Vector3::op_Multiply(UnityEngine::Vector3::get_forward(), -99999 / 2);
    auto matrix = UnityEngine::Matrix4x4::op_Multiply(UnityEngine::Matrix4x4::Ortho(-99999, 99999, -99999, 99999, 0.001f, 99999), TranslateMatrix(vector));
    // The public setter marshals the managed Camera to its native Unity object.
    mainCamera->set_cullingMatrix(UnityEngine::Matrix4x4::op_Multiply(matrix, mainCamera->get_worldToCameraMatrix()));

  if(getModConfig().Fixed.GetValue()) {
    if(getModConfig().LeftSaber.GetValue()) { 
        pos.x = saberPos.x + (getModConfig().ApplyOffsets.GetValue() ? offsetPos.x : 0.0f);
        pos.y = saberPos.y + (getModConfig().ApplyOffsets.GetValue() ? offsetPos.y : 0.0f);
        pos.z = saberPos.z + (getModConfig().ApplyOffsets.GetValue() ? offsetPos.z : 0.0f);

        if(!getModConfig().OnlyPos.GetValue()) {
            rot.x = saberRot.x + (getModConfig().ApplyOffsets.GetValue() ? offsetRot.x : 0.0f);
            rot.y = saberRot.y + (getModConfig().ApplyOffsets.GetValue() ? offsetRot.y : 0.0f);
            rot.z = saberRot.z + (getModConfig().ApplyOffsets.GetValue() ? offsetRot.z : 0.0f);
        }
    } else {
        pos.x = offsetPos.x;
        pos.y = offsetPos.y;
        pos.z = offsetPos.z;

        if(!getModConfig().OnlyPos.GetValue()) {
            rot.x = offsetRot.z;
            rot.y = offsetRot.y;
            rot.z = offsetRot.z;
        }
    }
  } else {
      if(getModConfig().LeftSaber.GetValue()) {
        pos.x += saberPos.x + (getModConfig().ApplyOffsets.GetValue() ? offsetPos.x : 0.0f);
        pos.y += saberPos.y + (getModConfig().ApplyOffsets.GetValue() ? offsetPos.y : 0.0f);
        pos.z += saberPos.z + (getModConfig().ApplyOffsets.GetValue() ? offsetPos.z : 0.0f);

        if(!getModConfig().OnlyPos.GetValue()) {
            rot.x += saberRot.x + (getModConfig().ApplyOffsets.GetValue() ? offsetRot.x : 0.0f);
            rot.y += saberRot.y + (getModConfig().ApplyOffsets.GetValue() ? offsetRot.y : 0.0f);
            rot.z += saberRot.z + (getModConfig().ApplyOffsets.GetValue() ? offsetRot.z : 0.0f);
        }
      } else {
        pos.x += offsetPos.x;
        pos.y += offsetPos.y;
        pos.z += offsetPos.z;

        if(!getModConfig().OnlyPos.GetValue()) {
            rot.x += offsetRot.z;
            rot.y += offsetRot.y;
            rot.z += offsetRot.z;
        }
      }
  } 

  if(getModConfig().WashingMachine.GetValue()) {
      rotated = remainderf(getModConfig().WashingMachineSpeed.GetValue() + rotated, 360.0f);
      rot.z += rotated;
  }

  mainCamera->get_transform()->set_position(pos);
  mainCamera->get_transform()->set_eulerAngles(rot);
}

MAKE_HOOK_MATCH(AudioTimeSyncController_Start, &AudioTimeSyncController::Start, void, AudioTimeSyncController* self) {
    AudioTimeSyncController_Start(self);
    if(getModConfig().DisableWhileReplay.GetValue() && (strcmp(getenv("ViewingReplay"), "true") == 0)) replay = true;
}

MAKE_HOOK_MATCH(SceneManager_ActiveSceneChanged, &UnityEngine::SceneManagement::SceneManager::Internal_ActiveSceneChanged, void, UnityEngine::SceneManagement::Scene previousActiveScene, UnityEngine::SceneManagement::Scene nextActiveScene) {
    ResetCullingOverride();
    leftController = {};
    rightController = {};
    previousMovementSource = 0;
    framesPressed = 0;
    frame = 0;
    SceneManager_ActiveSceneChanged(previousActiveScene, nextActiveScene);
}

PLAY_3RD_PERSON_EXPORT void setup(CModInfo* info) {
    *info = modInfo.to_c();

    Paper::Logger::RegisterFileContextId(Logger.tag);
	
    getModConfig().Init(modInfo);
    Logger.info("Completed setup!");
}

PLAY_3RD_PERSON_EXPORT void late_load() {
    i2c::functions::initialize();

    Logger.info("Installing hooks...");
    
    BSML::Init();
    BSML::Register::RegisterMainMenu("Play3rdPerson", "Play3rdPerson", "Play3rdPerson mod settings", DidActivate);
    BSML::Register::RegisterSettingsMenu("Play3rdPerson", DidActivate, false);
    // Install our hooks
    INSTALL_HOOK(Logger, LightManager_OnWillRenderObject);
    INSTALL_HOOK(Logger, SceneManager_ActiveSceneChanged);

    Logger.info("Installed all hooks!");
}
