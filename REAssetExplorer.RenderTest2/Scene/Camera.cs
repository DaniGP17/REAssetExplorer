using System;
using System.Collections.Generic;
using System.Drawing;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Windows.Forms;
using REAssetExplorer.RenderTest2.Interop;
using REAssetExplorer.RenderTest2.Interop.Structures;
using Vector3 = System.Numerics.Vector3;

namespace REAssetExplorer.RenderTest2.Scene;

[StructLayout(LayoutKind.Sequential)]
public struct CameraData
{
    public Vector3 Position;
    public Vector3 Target;
    public Vector3 Up;
    public float Fov;
    public float AspectRatio;
    public float NearPlane;
    public float FarPlane;
}

public class Camera
{
    // Camera position and rotation
    private Vector3 _position = new (0, 5, 10);
    private float _yaw = MathF.PI;
    private float _pitch = 0.4f;
    
    // Camera settings
    private float _speed = 10.0f;
    private float _minSpeed = 1.0f;
    private float _maxSpeed = 500.0f;
    private float _mouseSensitivity = 0.003f;
    private float _fov = MathF.PI / 4.0f;
    private float _minFov = MathF.PI / 12.0f;  // 15 degrees
    private float _maxFov = MathF.PI / 2.0f;   // 90 degrees
    private float _zoomSensitivity = 0.1f;
    private float _zoomSpeed = 2.0f;
    private float _nearPlane = 0.1f;
    private float _farPlane = 1000.0f;
    
    // Mouse control
    private bool _isRotating = false;
    private Point _lastMousePos;
    
    // Input state
    private readonly HashSet<Keys> _pressedKeys;

    public Vector3 Position => _position;
    public float Yaw => _yaw;
    public float Pitch => _pitch;
    public bool IsRotating => _isRotating;
    public Point LastMousePos => _lastMousePos;
    
    public Camera(ref HashSet<Keys> pressedKeys)
    {
        _pressedKeys = pressedKeys;
    }
    
    public void Update(float deltaTime)
    {
        if (deltaTime <= 0 || deltaTime > 0.1f) 
            deltaTime = 0.016f;
        
        var forward = GetForwardVector();
        
        Vector3 right = Vector3.Normalize(Vector3.Cross(Vector3.UnitY, forward));
        
        float speed = _speed * deltaTime;
        
        if (_pressedKeys.Contains(Keys.W))
            _position += forward * speed;
        if (_pressedKeys.Contains(Keys.S))
            _position -= forward * speed;
        if (_pressedKeys.Contains(Keys.A))
            _position += right * speed;
        if (_pressedKeys.Contains(Keys.D))
            _position -= right * speed;
        if (_pressedKeys.Contains(Keys.E))
            _position += Vector3.UnitY * speed;
        if (_pressedKeys.Contains(Keys.Q))
            _position -= Vector3.UnitY * speed;
    }
    
    public void HandleMouseRotation(int deltaX, int deltaY)
    {
        _yaw -= deltaX * _mouseSensitivity;
        _pitch += deltaY * _mouseSensitivity;
        
        _pitch = Math.Clamp(_pitch, -MathF.PI / 2.0f + 0.1f, MathF.PI / 2.0f - 0.1f);
    }
    
    public void StartRotating(Point mousePos)
    {
        _isRotating = true;
        _lastMousePos = mousePos;
    }
    
    public void StopRotating()
    {
        _isRotating = false;
    }
    
    public void UpdateLastMousePos(Point pos)
    {
        _lastMousePos = pos;
    }
    
    public void HandleMouseWheel(int delta)
    {
        if (_isRotating)
        {
            _speed += delta * _zoomSensitivity * 0.2f;
            _speed = Math.Clamp(_speed, _minSpeed, _maxSpeed);
            return;
        }
        
        if (_pressedKeys.Contains(Keys.ShiftKey))
        {
            float zoomFactor = -delta * _zoomSensitivity * 0.001f;
            _fov += zoomFactor;
            _fov = Math.Clamp(_fov, _minFov, _maxFov);
            return;
        }
        
        Vector3 forward = GetForwardVector();
        float zoomAmount = delta * _zoomSpeed * 0.001f;
        _position += forward * zoomAmount;
    }
    
    public CameraData GetCameraData(float aspectRatio)
    {
        Vector3 forward = GetForwardVector();
        Vector3 target = _position + forward;
        
        return new CameraData
        {
            Position = _position,
            Target = target,
            Up = new Vector3(0, 1, 0),
            Fov = _fov,
            AspectRatio = aspectRatio,
            NearPlane = _nearPlane,
            FarPlane = _farPlane
        };
    }
    
    /// <summary>Gets FOV in radians</summary>
    public float GetFov()
    {
        return _fov;
    }
    
    /// <summary>Sets FOV in radians</summary>
    public void SetFov(float fov)
    {
        _fov = Math.Clamp(fov, _minFov, _maxFov);
    }
    
    /// <summary>Gets FOV in degrees</summary>
    public float GetFovDegrees()
    {
        return _fov * (180.0f / MathF.PI);
    }
    
    /// <summary>Sets FOV in degrees</summary>
    public void SetFovDegrees(float degrees)
    {
        float radians = degrees * (MathF.PI / 180.0f);
        SetFov(radians);
    }
    
    private Vector3 GetForwardVector()
    {
        float yawCos = MathF.Cos(_yaw);
        float yawSin = MathF.Sin(_yaw);
        float pitchCos = MathF.Cos(_pitch);
        float pitchSin = MathF.Sin(_pitch);
        
        Vector3 forward = new Vector3(
            yawSin * pitchCos,
            -pitchSin,
            yawCos * pitchCos
        );
        
        return Vector3.Normalize(forward);
    }
}