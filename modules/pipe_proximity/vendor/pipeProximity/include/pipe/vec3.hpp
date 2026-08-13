#pragma once
// 3D ベクトルと基本演算（ヘッダオンリー・全 inline）
#include <cmath>

namespace pipe {

struct Vec3 { double x=0, y=0, z=0; };

inline Vec3 operator+(Vec3 a, Vec3 b){ return {a.x+b.x, a.y+b.y, a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b){ return {a.x-b.x, a.y-b.y, a.z-b.z}; }
inline Vec3 operator*(Vec3 a, double s){ return {a.x*s, a.y*s, a.z*s}; }
inline Vec3 operator*(double s, Vec3 a){ return a*s; }

inline double dot(Vec3 a, Vec3 b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b){
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
inline double norm(Vec3 a){ return std::sqrt(dot(a,a)); }
inline Vec3 normalize(Vec3 a){ double n=norm(a); return n>1e-300 ? a*(1.0/n) : a; }

// T に直交する任意の単位ベクトル（円は軸対称なので基準角はどこでも可）
inline Vec3 anyPerp(Vec3 t){
    Vec3 ax = (std::abs(t.x)<=std::abs(t.y) && std::abs(t.x)<=std::abs(t.z)) ? Vec3{1,0,0}
            : (std::abs(t.y)<=std::abs(t.z)) ? Vec3{0,1,0} : Vec3{0,0,1};
    return normalize(cross(t, ax));
}
// v を「法線 T の平面」へ射影
inline Vec3 projPlane(Vec3 v, Vec3 T){ return v - dot(v,T)*T; }

// 軸アクセス（i=0,1,2 → x,y,z）。有限差分で各成分を摂動するのに使う。
inline double& comp(Vec3& v, int i){ return i==0 ? v.x : (i==1 ? v.y : v.z); }
inline double  comp(const Vec3& v, int i){ return i==0 ? v.x : (i==1 ? v.y : v.z); }

} // namespace pipe
