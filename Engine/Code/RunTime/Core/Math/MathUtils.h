#pragma once

#define GLM_ENABLE_EXPERIMENTAL

#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix3X3.h"
#include "Matrix4x4.h"
#include "Quaternion.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/common.hpp>

#include <cmath>

namespace Spark::Math
{
    // ---- Angle Conversion ----

    inline float Radians(float degrees)
    {
        return glm::radians(degrees);
    }

    inline float Degrees(float radians)
    {
        return glm::degrees(radians);
    }

    // ---- Vector Operations ----

    inline float Dot(const Vector2& a, const Vector2& b)
    {
        return glm::dot(a, b);
    }

    inline float Dot(const Vector3& a, const Vector3& b)
    {
        return glm::dot(a, b);
    }

    inline float Dot(const Vector4& a, const Vector4& b)
    {
        return glm::dot(a, b);
    }

    inline Vector3 Cross(const Vector3& a, const Vector3& b)
    {
        return glm::cross(a, b);
    }

    inline Vector2 Normalize(const Vector2& v)
    {
        return glm::normalize(v);
    }

    inline Vector3 Normalize(const Vector3& v)
    {
        return glm::normalize(v);
    }

    inline Vector4 Normalize(const Vector4& v)
    {
        return glm::normalize(v);
    }

    inline float Length(const Vector2& v)
    {
        return glm::length(v);
    }

    inline float Length(const Vector3& v)
    {
        return glm::length(v);
    }

    inline float Length(const Vector4& v)
    {
        return glm::length(v);
    }

    inline float Distance(const Vector2& a, const Vector2& b)
    {
        return glm::distance(a, b);
    }

    inline float Distance(const Vector3& a, const Vector3& b)
    {
        return glm::distance(a, b);
    }

    inline Vector3 Reflect(const Vector3& incident, const Vector3& normal)
    {
        return glm::reflect(incident, normal);
    }

    inline Vector3 Refract(const Vector3& incident, const Vector3& normal, float eta)
    {
        return glm::refract(incident, normal, eta);
    }

    inline Vector2 Lerp(const Vector2& a, const Vector2& b, float t)
    {
        return glm::mix(a, b, t);
    }

    inline Vector3 Lerp(const Vector3& a, const Vector3& b, float t)
    {
        return glm::mix(a, b, t);
    }

    inline Vector4 Lerp(const Vector4& a, const Vector4& b, float t)
    {
        return glm::mix(a, b, t);
    }

    inline float Lerp(float a, float b, float t)
    {
        return glm::mix(a, b, t);
    }

    inline Vector3 Min(const Vector3& a, const Vector3& b)
    {
        return glm::min(a, b);
    }

    inline Vector3 Max(const Vector3& a, const Vector3& b)
    {
        return glm::max(a, b);
    }

    inline Vector3 Clamp(const Vector3& v, const Vector3& minVal, const Vector3& maxVal)
    {
        return glm::clamp(v, minVal, maxVal);
    }

    inline float Clamp(float v, float minVal, float maxVal)
    {
        return glm::clamp(v, minVal, maxVal);
    }

    inline Vector3 Abs(const Vector3& v)
    {
        return glm::abs(v);
    }

    inline Vector3 Floor(const Vector3& v)
    {
        return glm::floor(v);
    }

    inline Vector3 Ceil(const Vector3& v)
    {
        return glm::ceil(v);
    }

    // ---- Matrix Construction (LH_ZO convention) ----

    inline Matrix4X4 PerspectiveFov(float fovY, float aspect, float zNear, float zFar)
    {
        return glm::perspectiveLH_ZO(fovY, aspect, zNear, zFar);
    }

    inline Matrix4X4 OrthographicProjection(float left, float right, float bottom, float top, float zNear, float zFar)
    {
        return glm::orthoLH_ZO(left, right, bottom, top, zNear, zFar);
    }

    inline Matrix4X4 LookAt(const Vector3& eye, const Vector3& center, const Vector3& up)
    {
        return glm::lookAtLH(eye, center, up);
    }

    // ---- Matrix Transform ----

    inline Matrix4X4 Translate(const Matrix4X4& m, const Vector3& v)
    {
        return glm::translate(m, v);
    }

    inline Matrix4X4 Rotate(const Matrix4X4& m, float angle, const Vector3& axis)
    {
        return glm::rotate(m, angle, axis);
    }

    inline Matrix4X4 Scale(const Matrix4X4& m, const Vector3& v)
    {
        return glm::scale(m, v);
    }

    inline Matrix4X4 Inverse(const Matrix4X4& m)
    {
        return glm::inverse(m);
    }

    inline Matrix4X4 Transpose(const Matrix4X4& m)
    {
        return glm::transpose(m);
    }

    inline Matrix3X3 Inverse(const Matrix3X3& m)
    {
        return glm::inverse(m);
    }

    inline Matrix3X3 Transpose(const Matrix3X3& m)
    {
        return glm::transpose(m);
    }

    // ---- Quaternion Operations ----

    inline Quaternion QuaternionFromAxisAngle(const Vector3& axis, float angle)
    {
        return glm::angleAxis(angle, axis);
    }

    inline Quaternion QuaternionFromEuler(const Vector3& eulerAngles)
    {
        return glm::quat(eulerAngles);
    }

    inline Vector3 QuaternionToEuler(const Quaternion& q)
    {
        return glm::eulerAngles(q);
    }

    inline Matrix4X4 QuaternionToMatrix4X4(const Quaternion& q)
    {
        return glm::mat4_cast(q);
    }

    inline Matrix3X3 QuaternionToMatrix3X3(const Quaternion& q)
    {
        return glm::mat3_cast(q);
    }

    inline Quaternion NormalizeQuat(const Quaternion& q)
    {
        return glm::normalize(q);
    }

    inline Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t)
    {
        return glm::slerp(a, b, t);
    }

    inline Quaternion QuaternionLookAt(const Vector3& direction, const Vector3& up)
    {
        return glm::quatLookAtLH(direction, up);
    }

    // ---- Data Access ----

    inline const float* ValuePtr(const Matrix4X4& m)
    {
        return glm::value_ptr(m);
    }

    inline const float* ValuePtr(const Vector3& v)
    {
        return glm::value_ptr(v);
    }

    inline const float* ValuePtr(const Vector4& v)
    {
        return glm::value_ptr(v);
    }
}
