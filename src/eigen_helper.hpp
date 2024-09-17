//
// Created by Admin on 2022/6/22.
//
#pragma once
#ifndef VK_TUTORIAL_GEOMETRY_HELPER_HPP
#define VK_TUTORIAL_GEOMETRY_HELPER_HPP

#include <Eigen/Core>
#include <Eigen/Geometry>

namespace EigenHelper {

using Vec2f = Eigen::Vector2f;
using Vec3f = Eigen::Vector3f;
using Vec4f = Eigen::Vector4f;
using Mat3f = Eigen::Matrix3f;
using Mat4f = Eigen::Matrix4f;

inline float to_radian(float degree) { return degree / 180 * EIGEN_PI; }

inline float to_degree(float radian) { return radian * EIGEN_PI / 180; }

inline Mat4f translate(float x, float y, float z) {
  Eigen::Affine3f transform = Eigen::Affine3f::Identity();
  return transform.translate(Vec3f(x, y, z)).matrix();
}

inline Mat4f rotate(float rad, Vec3f const &axis) {
  Eigen::Affine3f transform = Eigen::Affine3f::Identity();
  transform.rotate(Eigen::AngleAxis<float>(rad, axis.normalized()));
  return transform.matrix();
}

inline Mat4f perspective(float fovy, float aspect, float z_near, float z_far) {
  auto const tan_half_fovy = tan(fovy / 2);
  Mat4f result = Eigen::Matrix4f::Zero();
  result(0, 0) = 1 / (aspect * tan_half_fovy);
  result(1, 1) = 1 / (tan_half_fovy);
  result(2, 2) = -(z_far + z_near) / (z_far - z_near);
  result(3, 2) = -1.0;
  result(2, 3) = -(2 * z_far * z_near) / (z_far - z_near);
  return result;
}

Eigen::Matrix4f ortho(float left, float right, float bottom, float top,
                      float zNear, float zFar) {
  Eigen::Matrix4f result = Eigen::Matrix4f::Identity();
  result(0, 0) = 2 / (right - left);
  result(1, 1) = 2 / (top - bottom);
  result(2, 2) = -2 / (zFar - zNear);
  result(0, 3) = -(right + left) / (right - left);
  result(1, 3) = -(top + bottom) / (top - bottom);
  result(2, 3) = -(zFar + zNear) / (zFar - zNear);
  return result;
}

inline Eigen::Matrix4f lookAt(Eigen::Vector3f const &eye,
                       Eigen::Vector3f const &center,
                       Eigen::Vector3f const &up) {
  Eigen::Vector3f f(center - eye);
  Eigen::Vector3f s(f.cross(up));
  Eigen::Vector3f u(s.cross(f));
  f.normalize();
  s.normalize();
  u.normalize();

  Eigen::Matrix4f result = Eigen::Matrix4f::Identity();
  result(0, 0) = s.x();
  result(0, 1) = s.y();
  result(0, 2) = s.z();
  result(1, 0) = u.x();
  result(1, 1) = u.y();
  result(1, 2) = u.z();
  result(2, 0) = -f.x();
  result(2, 1) = -f.y();
  result(2, 2) = -f.z();
  result(0, 3) = -s.dot(eye);
  result(1, 3) = -u.dot(eye);
  result(2, 3) = f.dot(eye);
  return result;
}
} // namespace EigenHelper
#endif // VK_TUTORIAL_GEOMETRY_HELPER_HPP
