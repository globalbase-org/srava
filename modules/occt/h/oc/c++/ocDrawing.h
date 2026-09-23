#ifndef ___ocDrawing_H___
#define ___ocDrawing_H___
/*
 * ocDrawing — occt の **2D 図面の入出力** (DXF / SVG)。#3544 段 3。
 *
 * ★★ なぜ occt が自前で書くのか (案 甲' ・ #3544 の計画):
 *   いまの DXF / SVG の語彙は **折れ線だけ** (書き手 LWPOLYLINE のみ・読み手 LWPOLYLINE /
 *   POLYLINE のみ)。そこへ occt を乗せると **円が 360 角形になる** — occt を使う理由が
 *   ファイルの境界でちょうど失われる。⇒ 中立化して cgal と共有する案は成立しない
 *   (共有 = occt を折れ線へ落とすこと)。重複ではなく *別の仕事* なので別に書く。
 *
 * ★ 実測 (#3544 段 0 / 段 2) で HLR と 2D op から出てくる曲線は 4 種:
 *     Line ・ Circle ・ Ellipse ・ BSpline
 *   ⇒ この 4 つを **DXF の対応する実体へそのまま**書く (LINE / CIRCLE・ARC / ELLIPSE / SPLINE)。
 *   ⚠ 分類は @BRepAdaptor_Curve@ で行う。@BRep_Tool::Curve@ は HLR の稜では **null** なので、
 *     素直にそちらで書くと「曲線なし」と読んで全部が折れ線へ落ちる (段 0 で実際に踏んだ)。
 *
 * ⚠⚠ **書ける語彙と読める語彙は必ず揃える**。片方だけ広げると往復で黙って欠ける。
 *   ⇒ この 1 ファイルに書き手と読み手を **並べて**置いてある。
 *
 * ⚠⚠⚠ ただし **カーネルをまたぐと揃っていない** (2026-09-17 に実測 ⇒ **Redmine #3551**):
 *   cgal の DXF 読み手 (cgaImport.cpp の parse_dxf) は LWPOLYLINE / POLYLINE しか見ないので、
 *   occt が書いた CIRCLE / ARC / ELLIPSE / SPLINE を **黙って落とす**。
 *
 *     実測 (当たる例): LWPOLYLINE (10x10 の正方形) と CIRCLE (中心 (30,5)・r=2) を 1 つの
 *     .dxf に並べて cgal で import すると、area = 100 ・ bbox = [[0,0,0],[10,10,0]] が返る。
 *     ⇒ 円は **在ったことすら報告されない**。
 *     ★ 逆に *折れ線を 1 つも含まない* occt の図面なら「failed to read DXF」と明示エラーに
 *       なる (読めたリングが 0 本だから)。⇒ 危ないのは **混ざったとき**だけ。
 *
 *   ⚠ SVG も同じ — cgal の parse_svg は path の M / L しか見ないので、こちらが書いた
 *     円弧 (A) が同じように落ちる (当たる例は #3551 の②)。
 *   ⚠ これは occt 側で塞げない — 塞ぐには cgal の読み手が円弧や B-spline を *折れ線へ落として*
 *     読むことになり、**分割数をいくつにするか**という約束を新しく決める話になる (曲線を
 *     持てないカーネルなので落とすこと自体は正しい)。⇒ **#3551 で扱う** (段 3 の範囲外)。
 *   ★ 元からある穴でもある (第三者の .dxf の円は以前から落ちていた)。occt が書けるように
 *     なったことで **srava の中から到達できるようになった**のが今回の変化。
 */
#include <TopoDS_Shape.hxx>

/* いずれも成功で true。失敗は false + err に理由 (呼び手が明示エラーにする)。
 * ⚠ どれも **z=0 平面の図面**だけを扱う (oc-cross2d)。平面の外にある 2D は
 *   err に「project_flatten で落とすか STEP で出せ」と書いて断る — 黙って潰さない。 */
bool oc_write_dxf(const TopoDS_Shape &s, const char *path, const char *unit, char *err, int errsz);
bool oc_write_svg(const TopoDS_Shape &s, const char *path, const char *unit, char *err, int errsz);
/* DXF を読む。⚠ 語彙は書き手と 1:1 (LINE / CIRCLE / ARC / ELLIPSE / SPLINE /
 *   LWPOLYLINE / POLYLINE)。読めた稜の compound を out に入れる。 */
bool oc_read_dxf(const char *path, TopoDS_Shape &out, char *err, int errsz);

#endif
