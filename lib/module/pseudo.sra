// module/pseudo.sra — 擬似モジュール集: **粒度の既定値を練り込む** (#3574)
//
// 使い方:
//
//     module("cgal.so", {});           // ← .so のロードは **module() の仕事**。
//     module("openvdb.so", {});         //   ★ 使うものだけを名指すのが原則
//     include "module/pseudo.sra";
//     use [ pm_cgal({seg:64}), pm_openvdb({seg:64, dx:0.01}) ];
//
//     sphere(1.5);                     // → "cgal"::sphere(1.5, 64)
//     tube(path, 24);                  // → "cgal"::tube_ruled(path, 24)   ← 名前の橋渡し
//
// ★ 何のためにあるか
//   分割数 `seg` / ボクセルサイズ `dx` は **近似の細かさ** なので、op ごとに引数で渡す。
//   ところが「この計算は全部 seg=64 で」と決めたい場面では、同じ数を **全部の呼び出しに
//   書いて回る** ことになり、書き漏らしは *静かに既定値 (32) で通る*。
//   ⇒ 擬似モジュール (#3555 段3) を候補列の先頭に置いて、**呼び出しの手前で埋める**。
//
// ★ pm_* は **ラムダ (工場)** で、返り値が擬似モジュールの定義ハッシュ。
//   ⚠ srava のラムダは **引数の数が厳密** なので、既定のままでも `pm_cgal({})` と書く
//     (`pm_cgal()` は "apply: argument count mismatch")。
//
// ★ 札 (name 欄) も `pm_*` に揃えてある。エラー文に出るのはこの名前
//   (⚠ 札では指名できない — 擬似モジュールは値なので `"pm_cgal"::op` とは引けない)。
//
// ⚠⚠ **このファイルは `use` を書かない**。候補列を書くこと自体は原則だが、*書くのは利用者*で
//   あって、読み込まれた側ではない。既定として候補列を敷くと `module(so,{priority:N})` という
//   別軸の指定が黙って効かなくなる (#3555 の教訓)。候補列を決めるのは常に利用者。
//
// ⚠ **.so はロードしない**。ロードは module() の仕事 (一式が要るなら all.sra) で、
//   ここは値を定義するだけ。
//   ロードされていないモジュールを指した擬似は、その op を使ったときに初めて落ちる。
//
// ★ 覆う範囲 = **粒度の引数を持つ生成 op** だけ。
//
//     A 群 (seg)   cgal / manifold / geogram / cherchi / nef_hybrid
//                  sphere ・ cylinder ・ cone ・ torus ・ tube_ruled ・ tube
//                  + 2D を持つ cgal / manifold だけ: circle ・ revolve
//     B 群 (dx)    openvdb だけ。⚠ **一律に dx を足した形ではない** —
//                  sphere / box / boxa / prism / pyramid / tetrahedron / empty3d は
//                  **seg を持たず dx だけ** (距離場なので粒度は dx が決める)。
//     C 群 (無し)  occt。⇒ **pm_occt は置かない**。occt は解析曲面なので seg が無く
//                  (#3570 段3)、擬似が受けて捨てると「書いた分割数が黙って消える」形に
//                  なる。⇒ occt で seg を書きたい場面は無い、が正しい状態。
//
// ⚠ `seg` を渡さなければ **各カーネルの既定 (32)** のまま。内部では `0` を渡していて、
//   `0` は「省略と同じ」という既存の規約 (docs の「分割数 segs と辺数 n の規約」)。
//
// ⚠⚠ `dx` には「既定」と言えるものが無い (長さなのでモデルの寸法に依る)。ここでは
//   **0.05** を置いてあるが、⇒ **openvdb を使うときは dx を明示すること**。
//   桁が外れると、細かすぎれば返ってこず、粗すぎれば塊になる。どちらも緑のまま起きる。

// ═══════════════════════════════════════════════════════════════════════
//   内部ヘルパ — 末尾 _ 付き。★ 名前を全部 pm_ で始めるのは、利用者の変数と
//   衝突しないため (#3574 ひさ 2026-09-22)。
// ═══════════════════════════════════════════════════════════════════════

// キーが在るか。⚠ ハッシュの **無いキーはエラー** (null ではない) ので try で拾う。
//   ⇒ `h.seg == null` では書けない (エラーは比較まで届かず、そのまま上へ抜ける)。
var pm_has_ = \(h, k){
	var f = 0;
	try { h[k]; f = 1; } catch { f = 0; }
	return f;
};

// 省略された要素を既定で埋める。★ **明示的な null も省略と同じ**に倒す
//   (⇒ null は比較不能 = #3567。`v == null` は null リテラルとだけ成立する)。
var pm_opt_ = \(h, k, d){
	var v = d;
	try { v = h[k]; } catch { v = d; }
	if (v == null) { v = d; }
	return v;
};

// 引数の検査。★ **知らないキーはエラーにする** — `{segs:64}` (seg の綴り違い) を
//   黙って無視すると「指定したのに効かない」= 既定値で通る形になり、緑と同じ顔をする。
//   ⚠ keys() が無いので **在る数を数えて length(h) と突き合わせる**。
var pm_check_ = \(who, h, allow){
	if (kind_of(h) != "hash") {
		throw { message: who + ": 引数はハッシュで書く (既定のままなら " + who + "({}) )",
		        class: "fatal" };
	}
	var n = 0;
	for (var i = 0; i < length(allow); i = i + 1) {
		if (pm_has_(h, allow[i])) { n = n + 1; }
	}
	if (n != length(h)) {
		var m = "";
		for (var j = 0; j < length(allow); j = j + 1) {
			if (j > 0) { m = m + " / "; }
			m = m + allow[j];
		}
		throw { message: who + ": 知らないキーがある (使えるのは " + m + ")", class: "fatal" };
	}
	return 1;
};

// ═══════════════════════════════════════════════════════════════════════
//   A 群 — seg を取るメッシュ系
// ═══════════════════════════════════════════════════════════════════════
//
// ★ どの行も `nreq` で **省略形と明示形の両方**を受ける。省略された seg は null で
//   届くので、そこだけ埋めて実モジュールへ素通しする。
//   ⇒ 擬似 1 つだけを候補列に書いても (実モジュール名を並べなくても) 両方書ける。
// ⚠ 本体は **必ず `mod::` で指名する**。指名を省くと候補列の先頭 = この擬似自身へ
//   戻ってきて無限に回る。
var pm_mesh_ops_ = \(mod, seg, with2d){
	var ops = [
		{ name:"sphere",     in:["value","value"], nreq:1,
		  body:\(r, s){ if (s == null) { s = seg; } mod::sphere(r, s); } },
		{ name:"cylinder",   in:["value","value","value"], nreq:2,
		  body:\(r, ht, s){ if (s == null) { s = seg; } mod::cylinder(r, ht, s); } },
		{ name:"cone",       in:["value","value","value"], nreq:2,
		  body:\(r, ht, s){ if (s == null) { s = seg; } mod::cone(r, ht, s); } },
		{ name:"torus",      in:["value","value","value"], nreq:2,
		  body:\(rr, r, s){ if (s == null) { s = seg; } mod::torus(rr, r, s); } },
		{ name:"tube_ruled", in:["value","value"], nreq:1,
		  body:\(p, s){ if (s == null) { s = seg; } mod::tube_ruled(p, s); } },
		// ★★ 名前の橋渡し (#3555 段4a で tube / tube_ruled に分けた分の戻し)。
		//   `tube` は occt だけの op になったが、occt は第 2 引数がハッシュのときだけ
		//   成立する (#3570 段3) ので、`tube(path, 24)` は occt が候補から外れる。
		//   ⇒ 擬似が居れば、そこへ降りてメッシュ系の tube_ruled が答える。
		//   ⚠ 第 2 引数が **ハッシュなら受けない** (述語スロット)。`tube(path, {closed:1})`
		//     は occt の形なので、ここで掴むと occt へ降りる道を塞ぐ。
		{ name:"tube",       in:["value", \(v){ kind_of(v) != "hash"; }], nreq:1,
		  body:\(p, s){ if (s == null) { s = seg; } mod::tube_ruled(p, s); } }
	];
	// ⚠ 2D を持つのは cgal / manifold だけ。持たないカーネルにこの行を足すと、
	//   擬似が受けてから「そんな op は無い」で落ちる = **隣へ降りる道を塞ぐ**。
	if (with2d) {
		ops[length(ops)] = { name:"circle",  in:["value","value"], nreq:1,
		  body:\(r, s){ if (s == null) { s = seg; } mod::circle(r, s); } };
		ops[length(ops)] = { name:"revolve", in:["cache","value","value"], nreq:1,
		  body:\(g, a, s){ if (a == null) { a = 360; }
		                   if (s == null) { s = seg; } mod::revolve(g, a, s); } };
	}
	return ops;
};

var pm_mesh_ = \(who, mod, h, with2d){
	pm_check_(who, h, ["seg"]);
	return [ { type:"pseudo_module", name:who,
	           ops: pm_mesh_ops_(mod, pm_opt_(h, "seg", 0), with2d) },
	         mod ];
};

var pm_cgal     = \(h){ return pm_mesh_("pm_cgal",     "cgal",       h, 1); };
var pm_manifold = \(h){ return pm_mesh_("pm_manifold", "manifold",   h, 1); };
var pm_geogram  = \(h){ return pm_mesh_("pm_geogram",  "geogram",    h, 0); };
var pm_cherchi  = \(h){ return pm_mesh_("pm_cherchi",  "cherchi",    h, 0); };
var pm_nef      = \(h){ return pm_mesh_("pm_nef",      "nef_hybrid", h, 0); };

// ═══════════════════════════════════════════════════════════════════════
//   B 群 — openvdb (dx が必須。seg を持つ op と持たない op が混ざる)
// ═══════════════════════════════════════════════════════════════════════
//
// ⚠⚠ **seg を持たない op に seg を渡す行は置かない**。例えば `sphere` は距離場を
//   直に書くので分割数という概念が無く、粒度は dx だけが決める。ここで
//   `sphere(r, seg)` を受けて seg を捨てる行を足すと「書いた分割数が黙って消える」
//   ので、受けない = 候補から外れる (隣のカーネルへ降りる) ままにしてある。
var pm_openvdb = \(h){
	pm_check_("pm_openvdb", h, ["seg", "dx"]);
	var seg = pm_opt_(h, "seg", 0);
	var dx  = pm_opt_(h, "dx",  0.05);
	var mod = "openvdb";
	return [ { type:"pseudo_module", name:"pm_openvdb",
	  ops:[
		// ---- dx だけ (seg を持たない) ----
		{ name:"sphere",      in:["value"], nreq:1,
		  body:\(r){ mod::sphere(r, dx); } },
		{ name:"tetrahedron", in:["value"], nreq:1,
		  body:\(a){ mod::tetrahedron(a, dx); } },
		{ name:"boxa",        in:["value"], nreq:1,
		  body:\(a){ mod::boxa(a, dx); } },
		{ name:"box",         in:["value","value","value"], nreq:3,
		  body:\(x, y, z){ mod::box(x, y, z, dx); } },
		{ name:"prism",       in:["value","value","value"], nreq:3,
		  body:\(a, b, c){ mod::prism(a, b, c, dx); } },
		{ name:"pyramid",     in:["value","value","value"], nreq:3,
		  body:\(a, b, c){ mod::pyramid(a, b, c, dx); } },
		{ name:"empty3d",     in:[], nreq:0,
		  body:\(){ mod::empty3d(dx); } },
		// ---- 細分回数 (seg ではない) ----
		{ name:"icosphere",   in:["value","value"], nreq:1,
		  body:\(r, sd){ if (sd == null) { sd = 0; } mod::icosphere(r, sd, dx); } },
		// ---- seg + dx ----
		{ name:"cylinder",    in:["value","value","value"], nreq:2,
		  body:\(r, ht, s){ if (s == null) { s = seg; } mod::cylinder(r, ht, s, dx); } },
		{ name:"cone",        in:["value","value","value"], nreq:2,
		  body:\(r, ht, s){ if (s == null) { s = seg; } mod::cone(r, ht, s, dx); } },
		{ name:"torus",       in:["value","value","value"], nreq:2,
		  body:\(rr, r, s){ if (s == null) { s = seg; } mod::torus(rr, r, s, dx); } },
		{ name:"tube_ruled",  in:["value","value"], nreq:1,
		  body:\(p, s){ if (s == null) { s = seg; } mod::tube_ruled(p, s, dx); } },
		//   ⚠ A 群と同じく、ハッシュの第 2 引数 (occt の形) は受けない。
		{ name:"tube",        in:["value", \(v){ kind_of(v) != "hash"; }], nreq:1,
		  body:\(p, s){ if (s == null) { s = seg; } mod::tube_ruled(p, s, dx); } }
	  ] },
	  mod ];
};

// ═══════════════════════════════════════════════════════════════════════
//   C 群 — 点群 (#3582 の次 ・ ひさ判断 2026-09-22)
// ═══════════════════════════════════════════════════════════════════════
//
// ★★ ここだけ役割が違う。A/B 群は **既定値を練り込む**が、こちらは
//   **引数の少ない形を足す** (2 引数の intersection)。
//
//     intersection(点群, 形)  →  [境界ちょうど, 内側 (開), 外側]   ★ 3 要素配列
//     intersection(点群, 形, mode)  →  1 つ   (mode 0 / -1 / +1)   ← モジュールが持つ
//     difference(点群, 形)          →  外側と同じもの              ← モジュールが持つ (2 引数)
//
//   ★ 不変条件: nverts(s[0]) + nverts(s[1]) + nverts(s[2]) == nverts(点群)
//   ★ 境界ちょうどの点を「内側」「外側」のどちらに入れるかは **一意に決まらない**ので、
//     判定器に答えさせず **第 3 の集合**として切り出す (#3575 ・ section の共面と同じ形)。
//
// ═══ ⚠⚠ なぜ **パーサの糖衣にしなかったか** (実測して決めた 2026-09-22) ═══
//
//   @section(m,P,N)@ の 3 要素配列は **パーサの糖衣**で作っているが、同じ手が使えない:
//
//       section       op は nin=4 のみ ・ 糖衣は na==3   ⇒ **競合しない**
//       intersection  op は nin=2 (メッシュのブール積)   ⇒ 2 引数で糖衣を焚くと **覆い隠す**
//
//   パーサは **型を知らない** (routing は eval 時) ので「点群か」を判別できない。
//   ⚠ @union@ の糖衣を 2 引数へ広げて測ったら、@union(a,b)@ が第 2 引数を **黙って捨てて**
//     @a@ を返した (エラーにならない)。
//   ⚠⚠ しかも @a &&& b@ は @mk_meshop@ に落ちて **@mk_call@ を通らない** ので、糖衣だと
//     *綴りで意味が割れる* (関数形だけ 3 要素配列になる)。
//
//   ⇒ **擬似モジュールなら eval 時に値を見られる**ので「点群のときだけ受ける」が書け、
//     しかも @&&&@ にも効く (routing の候補列は演算子経由のノードも通るため)。
//
// ⚠ 実装 (内外の判定) は **geomutils / openvdb / occt** に分かれている。⇒ 本体は
//   @mod::@ で指名せず **素の 3 引数呼び**にしてある。利用者がロードしている実装が答える。
//   ★ 3 引数は擬似の行 (nreq:2) に **arity で当たらない**ので、擬似自身へは戻らない (実測)。
//
// ⚠ @difference@ の行は **置かない**。あちらは元から 2 引数なので、関数形も @---@ も
//   そのまま通る (擬似が受けて捨てると「隣へ降りる道を塞ぐ」だけになる)。

// 値が点群か。⚠ 幾何でない値に @type_of@ を当てても落ちないことを確かめてある。
var pm_is_pts_ = \(v){
	var t = type_of(v);
	return t == "pt-cloud2d" || t == "pt-cloud3d";
};

var pm_points = \(h){
	pm_check_("pm_points", h, []);
	return [ { type:"pseudo_module", name:"pm_points",
	  ops:[
		// ★ 第 1 引数が点群のときだけ受ける。メッシュどうしの intersection / &&& は
		//   ここで **外れて**、隣の実カーネルへ降りる。
		{ name:"intersection", in:[ \(v){ pm_is_pts_(v); }, "cache" ], nreq:2,
		  body:\(a, m){ [ intersection(a, m, 0),
		                  intersection(a, m, -1),
		                  intersection(a, m, 1) ]; } }
	  ] },
	  "points" ];
};
