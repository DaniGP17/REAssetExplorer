#include "Renderer/Camera.h"

#include <cmath>

#include <windows.h>

namespace {

constexpr float LOOK_SENSITIVITY = 0.005f;
constexpr float PITCH_LIMIT = 1.55f;
constexpr float FOCUS_DURATION = 0.35f;

bool KeyDown(int key) {
    return (GetAsyncKeyState(key) & 0x8000) != 0;
}

}

void Camera::Frame(const float center[3], float radius) {
    position[0] = center[0];
    position[1] = center[1] + radius * 0.3f;
    position[2] = center[2] - radius * 2.0f;

    for (int i = 0; i < 3; i++) pivot[i] = center[i];

    float dir[3] = { center[0] - position[0], center[1] - position[1], center[2] - position[2] };
    float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    dir[0] /= len; dir[1] /= len; dir[2] /= len;
    pitch = std::asin(dir[1]);
    yaw = std::atan2(dir[0], dir[2]);

    speed = radius;
}

void Camera::FocusOn(const float center[3], float radius, float aspect) {
    float halfY = fov * 0.5f;
    float halfX = std::atan(std::tan(halfY) * aspect);
    float half = halfX < halfY ? halfX : halfY;
    float distance = (radius > 0.001f ? radius : 0.001f) / std::sin(half);
    float forward[3];
    Forward(forward);
    for (int i = 0; i < 3; i++) {
        pivot[i] = center[i];
        focusFrom[i] = position[i];
        focusTo[i] = center[i] - forward[i] * distance;
    }
    focusTime = 0;
    focusing = true;
}

void Camera::Forward(float out[3]) const {
    out[0] = std::cos(pitch) * std::sin(yaw);
    out[1] = std::sin(pitch);
    out[2] = std::cos(pitch) * std::cos(yaw);
}

void Camera::Update(const CameraInput& input, float dt) {
    float dx = input.mouseDx;
    float dy = input.mouseDy;
    float wheel = input.wheel;
    bool alt = KeyDown(VK_MENU);

    if (focusing && (input.rightMouse || (input.leftMouse && alt) || wheel != 0)) focusing = false;
    if (focusing) {
        focusTime += dt;
        float t = focusTime >= FOCUS_DURATION ? 1.0f : focusTime / FOCUS_DURATION;
        float ease = 1 - (1 - t) * (1 - t) * (1 - t);
        for (int i = 0; i < 3; i++) position[i] = focusFrom[i] + (focusTo[i] - focusFrom[i]) * ease;
        if (t >= 1) focusing = false;
    }

    if (wheel != 0 && KeyDown(VK_SHIFT)) {
        fov -= wheel * 0.08f;
        if (fov < 0.20f) fov = 0.20f;
        if (fov > 2.60f) fov = 2.60f;
        wheel = 0;
    }

    if (input.rightMouse) {
        yaw -= dx * LOOK_SENSITIVITY;
        pitch -= dy * LOOK_SENSITIVITY;
        if (pitch > PITCH_LIMIT) pitch = PITCH_LIMIT;
        if (pitch < -PITCH_LIMIT) pitch = -PITCH_LIMIT;

        if (wheel != 0) {
            speed *= std::pow(1.25f, wheel);
        }

        float forward[3];
        Forward(forward);
        float right[3] = { -forward[2], 0, forward[0] };
        float rightLen = std::sqrt(right[0] * right[0] + right[2] * right[2]);
        if (rightLen > 0) { right[0] /= rightLen; right[2] /= rightLen; }

        float move = speed * dt;
        if (KeyDown('W')) for (int i = 0; i < 3; i++) position[i] += forward[i] * move;
        if (KeyDown('S')) for (int i = 0; i < 3; i++) position[i] -= forward[i] * move;
        if (KeyDown('D')) for (int i = 0; i < 3; i++) position[i] += right[i] * move;
        if (KeyDown('A')) for (int i = 0; i < 3; i++) position[i] -= right[i] * move;
        if (KeyDown('E')) position[1] += move;
        if (KeyDown('Q')) position[1] -= move;
    } else if (input.leftMouse && alt) {
        float offset[3] = { position[0] - pivot[0], position[1] - pivot[1], position[2] - pivot[2] };
        float distance = std::sqrt(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
        if (distance < 0.001f) distance = 0.001f;

        yaw -= dx * LOOK_SENSITIVITY;
        pitch -= dy * LOOK_SENSITIVITY;
        if (pitch > PITCH_LIMIT) pitch = PITCH_LIMIT;
        if (pitch < -PITCH_LIMIT) pitch = -PITCH_LIMIT;

        float forward[3];
        Forward(forward);
        for (int i = 0; i < 3; i++) position[i] = pivot[i] - forward[i] * distance;
    } else if (wheel != 0) {
        float forward[3];
        Forward(forward);
        float move = wheel * speed * 0.3f;
        for (int i = 0; i < 3; i++) position[i] += forward[i] * move;
    }
}

void Camera::GetEye(float out[3]) const {
    for (int i = 0; i < 3; i++) out[i] = position[i];
}

void Camera::GetPivot(float out[3]) const {
    for (int i = 0; i < 3; i++) out[i] = pivot[i];
}

void Camera::Translate(const float delta[3]) {
    for (int i = 0; i < 3; i++) {
        position[i] += delta[i];
        pivot[i] += delta[i];
        focusFrom[i] += delta[i];
        focusTo[i] += delta[i];
    }
}

CameraPose Camera::GetPose() const {
    return { { position[0], position[1], position[2] }, yaw, pitch, fov, roll };
}

void Camera::SetPose(const CameraPose& pose) {
    float offset[3] = { position[0] - pivot[0], position[1] - pivot[1], position[2] - pivot[2] };
    float distance = std::sqrt(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2]);
    for (int i = 0; i < 3; i++) position[i] = pose.position[i];
    yaw = pose.yaw;
    pitch = pose.pitch < -PITCH_LIMIT ? -PITCH_LIMIT : pose.pitch > PITCH_LIMIT ? PITCH_LIMIT : pose.pitch;
    if (pose.fov > 0) fov = pose.fov;
    roll = pose.roll;
    focusing = false;
    float forward[3];
    Forward(forward);
    for (int i = 0; i < 3; i++) pivot[i] = position[i] + forward[i] * distance;
}

void Camera::GetUp(float out[3]) const {
    float forward[3];
    Forward(forward);
    float right[3] = { -forward[2], 0, forward[0] };
    float length = std::sqrt(right[0] * right[0] + right[2] * right[2]);
    if (length < 1e-6f) {
        out[0] = 0;
        out[1] = 1;
        out[2] = 0;
        return;
    }
    right[0] /= length;
    right[2] /= length;
    float up[3] = { right[1] * forward[2] - right[2] * forward[1], right[2] * forward[0] - right[0] * forward[2],
                    right[0] * forward[1] - right[1] * forward[0] };
    for (int i = 0; i < 3; i++) out[i] = up[i] * std::cos(roll) + right[i] * std::sin(roll);
}

void Camera::GetTarget(float out[3]) const {
    float forward[3];
    Forward(forward);
    for (int i = 0; i < 3; i++) out[i] = position[i] + forward[i];
}
