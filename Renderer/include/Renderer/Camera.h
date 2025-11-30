#ifndef REASSETEXPLORER_CAMERA_H
#define REASSETEXPLORER_CAMERA_H

// Mouse deltas accumulated since the previous Update; keys are polled directly.
struct CameraInput {
    float mouseDx = 0;
    float mouseDy = 0;
    float wheel = 0;
    bool leftMouse = false;
    bool rightMouse = false;
};

// Angles in radians; yaw 0 looks down +Z, pitch is up from the horizon, roll turns the up vector toward
// the right one.
struct CameraPose {
    float position[3];
    float yaw;
    float pitch;
    float fov;
    float roll = 0;
};

class Camera {
public:
    void Frame(const float center[3], float radius);
    void FocusOn(const float center[3], float radius, float aspect);
    void Update(const CameraInput& input, float dt);

    void GetEye(float out[3]) const;
    void GetTarget(float out[3]) const;
    void GetUp(float out[3]) const;
    void GetPivot(float out[3]) const;
    void Translate(const float delta[3]);
    float GetFov() const { return fov; }
    void SetFov(float value) { fov = value; }
    // Fly speed in meters per second; the wheel scales it while flying.
    float GetSpeed() const { return speed; }
    void SetSpeed(float value) { speed = value; }
    CameraPose GetPose() const;
    void SetPose(const CameraPose& pose);

private:
    void Forward(float out[3]) const;

    float position[3]{};
    float pivot[3]{};
    float yaw = 0;
    float pitch = 0;
    float roll = 0;
    float speed = 1;
    float fov = 1.0472f;

    bool focusing = false;
    float focusTime = 0;
    float focusFrom[3]{};
    float focusTo[3]{};
};

#endif
