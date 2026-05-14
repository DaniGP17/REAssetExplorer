using System;
using System.Numerics;

namespace REAssetExplorer.Rendering;

/// <summary>
/// Cámara básica con transformación de vista y proyección
/// </summary>
public class Camera
{
    public Vector3 Position { get; set; }
    public Vector3 Target { get; set; }
    public Vector3 Up { get; set; } = Vector3.UnitY;

    public float FieldOfView { get; set; } = 60f;
    public float AspectRatio { get; set; } = 16f / 9f;
    public float NearPlane { get; set; } = 0.1f;
    public float FarPlane { get; set; } = 1000f;
    
    // Previous frame data for motion vectors
    public Vector3 PreviousPosition { get; private set; }
    public Vector3 PreviousTarget { get; private set; }
    public Matrix4x4 PreviousViewMatrix { get; private set; }
    public Matrix4x4 PreviousProjectionMatrix { get; private set; }
    public Matrix4x4 PreviousViewProjectionMatrix { get; private set; }
    
    // Camera control
    public float Yaw { get; set; } = 0f;  // Rotación horizontal
    public float Pitch { get; set; } = 0f;  // Rotación vertical
    public float RotationSpeed { get; set; } = 0.005f;  // Sensibilidad de rotación
    public float PanSpeed { get; set; } = 0.01f;  // Sensibilidad de paneo
    public float MoveSpeed { get; set; } = 5.0f;  // Velocidad de movimiento WASD
    public float MoveSpeedMin { get; set; } = 0.1f;  // Velocidad mínima
    public float MoveSpeedMax { get; set; } = 50.0f;  // Velocidad máxima
    
    // Orbital pivot point (usually origin or selected object)
    public Vector3 OrbitPivot { get; set; } = Vector3.Zero;

    public Camera()
    {
        Position = new Vector3(0, 0, 5);
        Target = Vector3.Zero;
        
        // Calcular yaw y pitch iniciales desde la dirección
        var direction = Vector3.Normalize(Target - Position);
        Pitch = MathF.Asin(direction.Y);
        Yaw = MathF.Atan2(direction.X, direction.Z);
        
        PreviousPosition = Position;
        PreviousTarget = Target;
        PreviousViewMatrix = GetViewMatrix();
        PreviousProjectionMatrix = GetProjectionMatrix();
        PreviousViewProjectionMatrix = PreviousViewMatrix * PreviousProjectionMatrix;
    }

    public Vector3 GetDirection()
    {
        return Vector3.Normalize(Target - Position);
    }

    public Matrix4x4 GetViewMatrix()
    {
        return Matrix4x4.CreateLookAt(Position, Target, Up);
    }

    public Matrix4x4 GetProjectionMatrix()
    {
        return Matrix4x4.CreatePerspectiveFieldOfView(
            FieldOfView * (MathF.PI / 180f),
            AspectRatio,
            NearPlane,
            FarPlane);
    }
    
    public Matrix4x4 GetViewProjectionMatrix()
    {
        return GetViewMatrix() * GetProjectionMatrix();
    }
    
    /// <summary>
    /// Extracts the 6 frustum planes from the view-projection matrix
    /// </summary>
    public Vector4[] GetFrustumPlanes()
    {
        var viewProj = GetViewProjectionMatrix();
        var planes = new Vector4[6];
        
        // Left plane
        planes[0] = new Vector4(
            viewProj.M14 + viewProj.M11,
            viewProj.M24 + viewProj.M21,
            viewProj.M34 + viewProj.M31,
            viewProj.M44 + viewProj.M41);
        
        // Right plane
        planes[1] = new Vector4(
            viewProj.M14 - viewProj.M11,
            viewProj.M24 - viewProj.M21,
            viewProj.M34 - viewProj.M31,
            viewProj.M44 - viewProj.M41);
        
        // Top plane
        planes[2] = new Vector4(
            viewProj.M14 - viewProj.M12,
            viewProj.M24 - viewProj.M22,
            viewProj.M34 - viewProj.M32,
            viewProj.M44 - viewProj.M42);
        
        // Bottom plane
        planes[3] = new Vector4(
            viewProj.M14 + viewProj.M12,
            viewProj.M24 + viewProj.M22,
            viewProj.M34 + viewProj.M32,
            viewProj.M44 + viewProj.M42);
        
        // Near plane
        planes[4] = new Vector4(
            viewProj.M13,
            viewProj.M23,
            viewProj.M33,
            viewProj.M43);
        
        // Far plane
        planes[5] = new Vector4(
            viewProj.M14 - viewProj.M13,
            viewProj.M24 - viewProj.M23,
            viewProj.M34 - viewProj.M33,
            viewProj.M44 - viewProj.M43);
        
        // Normalize planes
        for (int i = 0; i < 6; i++)
        {
            float length = MathF.Sqrt(planes[i].X * planes[i].X + 
                                     planes[i].Y * planes[i].Y + 
                                     planes[i].Z * planes[i].Z);
            if (length > 0)
                planes[i] /= length;
        }
        
        return planes;
    }
    
    /// <summary>
    /// Rotate camera around target (right mouse button drag)
    /// </summary>
    public void Rotate(float deltaX, float deltaY, bool orbitalMode = false)
    {
        Yaw -= deltaX * RotationSpeed;
        
        if (orbitalMode)
        {
            Pitch += deltaY * RotationSpeed;
        }
        else
        {
            Pitch -= deltaY * RotationSpeed;
        }
        
        // Limitar pitch para evitar gimbal lock
        Pitch = Math.Clamp(Pitch, -MathF.PI / 2 + 0.1f, MathF.PI / 2 - 0.1f);
        
        if (orbitalMode)
        {
            UpdatePositionFromAnglesOrbital();
        }
        else
        {
            UpdateTargetFromAngles();
        }
    }
    
    /// <summary>
    /// Update orbital pivot to current target (call when entering orbital mode)
    /// </summary>
    public void UpdateOrbitPivot()
    {
        OrbitPivot = Target;
        
        // Recalcular los ángulos basándose en la posición actual relativa al pivot
        var direction = Vector3.Normalize(Position - OrbitPivot);
        
        // Pitch es el ángulo vertical
        Pitch = MathF.Asin(direction.Y);
        
        // Yaw es el ángulo horizontal en el plano XZ
        Yaw = MathF.Atan2(direction.X, direction.Z);
    }
    
    /// <summary>
    /// Update angles for free look mode based on current orientation (call when exiting orbital mode)
    /// </summary>
    public void UpdateFreeLookAngles()
    {
        // Recalcular los ángulos basándose en la dirección actual de vista (Target - Position)
        var viewDirection = Vector3.Normalize(Target - Position);
        
        // Pitch es el ángulo vertical
        Pitch = MathF.Asin(viewDirection.Y);
        
        // Yaw es el ángulo horizontal en el plano XZ
        Yaw = MathF.Atan2(viewDirection.X, viewDirection.Z);
    }
    
    /// <summary>
    /// Pan camera (middle mouse button drag or Alt+Right mouse)
    /// </summary>
    public void Pan(float deltaX, float deltaY)
    {
        var forward = GetDirection();
        var right = Vector3.Normalize(Vector3.Cross(forward, Up));
        var actualUp = Vector3.Normalize(Vector3.Cross(right, forward));
        
        var offset = right * (-deltaX * PanSpeed) + 
                     actualUp * (deltaY * PanSpeed);
        
        Target += offset;
        Position += offset;
    }
    
    /// <summary>
    /// Adjust camera movement speed (mouse wheel while right-clicking)
    /// </summary>
    public void AdjustMoveSpeed(float delta)
    {
        MoveSpeed += delta * 0.5f;
        MoveSpeed = Math.Clamp(MoveSpeed, MoveSpeedMin, MoveSpeedMax);
    }
    
    /// <summary>
    /// Zoom camera (move forward/backward along view direction)
    /// </summary>
    public void Zoom(float delta)
    {
        var direction = GetDirection();
        float zoomSpeed = 0.5f;
        
        // Move camera position along the view direction
        Position += direction * delta * zoomSpeed;
    }
    
    /// <summary>
    /// Move camera in local space (WASD movement)
    /// </summary>
    public void Move(Vector3 localMovement, float deltaTime)
    {
        var forward = GetDirection();
        var right = Vector3.Normalize(Vector3.Cross(forward, Up));
        var actualUp = Vector3.Normalize(Vector3.Cross(right, forward));
        
        // Calcular el movimiento en el espacio mundial usando MoveSpeed configurable
        var movement = (right * localMovement.X + 
                       actualUp * localMovement.Y + 
                       forward * localMovement.Z) * MoveSpeed * deltaTime;
        
        // Mover tanto la posición como el target para mantener la orientación
        Position += movement;
        Target += movement;
    }
    
    private void UpdateTargetFromAngles()
    {
        // Calcular target basado en yaw y pitch (cámara FPS libre)
        // La cámara rota sobre sí misma, no alrededor de un punto
        float x = MathF.Cos(Pitch) * MathF.Sin(Yaw);
        float y = MathF.Sin(Pitch);
        float z = MathF.Cos(Pitch) * MathF.Cos(Yaw);
        
        var direction = new Vector3(x, y, z);
        Target = Position + direction;
    }
    
    private void UpdatePositionFromAnglesOrbital()
    {
        // Calcular posición basada en yaw, pitch girando alrededor del pivot (modo orbital)
        float distance = Vector3.Distance(Position, OrbitPivot);
        
        float x = distance * MathF.Cos(Pitch) * MathF.Sin(Yaw);
        float y = distance * MathF.Sin(Pitch);
        float z = distance * MathF.Cos(Pitch) * MathF.Cos(Yaw);
        
        Position = OrbitPivot + new Vector3(x, y, z);
        Target = OrbitPivot;
    }
    
    /// <summary>
    /// Focus camera on a target point (like pressing F in Blender/Unity)
    /// </summary>
    public void FocusOn(Vector3 target, float distance = 5f)
    {
        Target = target;
        OrbitPivot = target;
        
        // Mantener la dirección de vista actual pero ajustar la distancia
        var currentDirection = Vector3.Normalize(Position - target);
        Position = target + currentDirection * distance;
        
        // Recalcular yaw y pitch desde la dirección de vista (desde cámara hacia target)
        var viewDirection = Vector3.Normalize(target - Position);
        
        // Pitch es el ángulo vertical
        Pitch = MathF.Asin(viewDirection.Y);
        
        // Yaw es el ángulo horizontal en el plano XZ
        Yaw = MathF.Atan2(viewDirection.X, viewDirection.Z);
    }

    public void Update(float deltaTime)
    {
        // Store previous frame data
        PreviousPosition = Position;
        PreviousTarget = Target;
        PreviousViewMatrix = GetViewMatrix();
        PreviousProjectionMatrix = GetProjectionMatrix();
        PreviousViewProjectionMatrix = PreviousViewMatrix * PreviousProjectionMatrix;
    }
}
