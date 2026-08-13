/*
 * pipe_proximity_compute — pipe_proximity の計算本体 + marshaling(op ディスパッチ)。
 *   pipe_proximity(別 repo・MIT・CGAL 非依存)の自己接近検出を srava から呼べるようにする。
 *
 * この TU は pigData(値の型)だけを include し、pipe ヘッダ(namespace pipe)は触らない
 * (namespace pipe ↔ POSIX ::pipe 衝突回避。pipe 側は pipe_proximity_adapter.cpp に隔離)。
 * 依存方向は一方向(cgal-processor → pipe_proximity)。アダプタ + 登録は host 側。
 *
 * ★ .so 化 Phase 5: 旧 pipe_proximity_agent.cpp から計算本体を分離。純 pigData 境界
 *   (pp_compute) を公開し、process 版 (serve) と in-proc 版 (ptsAgent 派生) の双方から共有する。
 *
 * srava での呼び出し:
 *   pipe_proximity(ctrl_pts, radius, report_gap)
 *     ctrl_pts  = [[x,y,z], ...]  先頭=始点 S / 末尾=終点 E / 中間=off-curve 制御点 C
 *     radius    = スカラ r(一定) または [r0, m](r(s)=r0*exp(m*s))
 *     report_gap= スカラ(この gap 以下の接近のみ返す)
 *   返り = [[gap, [pax,pay,paz], [pbx,pby,pbz], [nx,ny,nz], sA, sB, rA, rB], ...]  (無ければ [])
 */
#include "pipe_proximity_compute.h"
#include "pipe_proximity_adapter.h"

#include <vector>
#include <cstring>
#include <cstdio>   /* snprintf (エラー文言) */

static double num(sPtr<pigData> v) { return ( v != thNULL ) ? v->get_flt() : 0.0; }
static double idx_num(sPtr<pigData> arr, int i) {
	return num(arr->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
}

static sPtr<pigData> vec_arr(const double v[3]) {
	sPtr<pigDataArray> a = thNEW(pigDataArray,());
	a->push(thNEW(pigDataFloat,(v[0])));
	a->push(thNEW(pigDataFloat,(v[1])));
	a->push(thNEW(pigDataFloat,(v[2])));
	return a;
}

/* ctrl_pts(配列 [[x,y,z],...]) → 平坦 [x,y,z,...]。array でない/2 点未満なら npts=0。 */
static std::vector<double> parse_ctrl(sPtr<pigData> a0, int *npts) {
	*npts = 0;
	std::vector<double> flat;
	if ( a0 == thNULL ) return flat;                 /* 引数省略 / hget が空 */
	sPtr<pigDataArray> pts = a0->obt_array();
	if ( ! pts.is_notNull() || pts->length() < 2 ) return flat;
	int n = pts->length();
	flat.reserve((size_t)n * 3);
	for ( int i = 0 ; i < n ; ++i ) {
		sPtr<pigData> p = pts->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));
		flat.push_back(idx_num(p, 0));
		flat.push_back(idx_num(p, 1));
		flat.push_back(idx_num(p, 2));
	}
	*npts = n;
	return flat;
}

/* radius 引数 → r0,m(指数) と sr([s,r] キーポイント平坦列 [s0,r0,s1,r1,...]・線形補間)。
 * 引数の形で自動判別する:
 *   スカラ r              → r0=r, m=0, sr=[]                (一定半径)
 *   フラット [r0, m]      → 指数 r0*exp(m*s)、sr=[]
 *   フラット [r0,m,p1,r1]   → 指数を s>=p1 で r1 に固定(p1 で不連続)。cS0=cS1=p1, cR=r1
 *   フラット [r0,m,p1,p2,r2]→ p1..p2 で指数値から r2 へ線形、s>=p2 で r2。cS0=p1, cS1=p2, cR=r2
 *   ネスト [[s,r],...]     → sr=[s0,r0,s1,r1,...](線形補間)。r0,m は無視。
 * 判別は「先頭要素が配列かどうか」(srava の flat=ベクトル / nest=コンテナ慣習と同じ)。
 * cS0/cS1/cR は exp 弧長クランプ(cS0<0=無効)。 */
static void parse_radius(sPtr<pigData> a, double *r0, double *m, std::vector<double> *sr,
                         double *cS0, double *cS1, double *cR) {
	*r0 = 0.5; *m = 0.0; sr->clear(); *cS0 = -1; *cS1 = -1; *cR = 0;
	if ( a == thNULL ) return;
	sPtr<pigDataArray> ra = a->obt_array();
	if ( ! ra.is_notNull() ) { *r0 = num(a); return; }          /* スカラ = 一定半径 */
	if ( ra->length() < 1 ) return;
	/* 要素は obt_array() で取る (遅延ノードは compact ゲートウェイが解決)。素の d_cast だと
	 * in-proc で nest/flat の判別を誤る。 */
	sPtr<pigData> e0 = ra->get_ix(thNEW(pigDataInteger,((INTEGER64)0)));
	sPtr<pigDataArray> e0a = e0->obt_array();
	if ( e0a.is_notNull() ) {                                   /* [[s,r],...] = 線形補間 */
		int n = ra->length();
		for ( int i = 0 ; i < n ; ++i ) {
			sPtr<pigData> p = ra->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));
			sr->push_back(idx_num(p, 0));                       /* s */
			sr->push_back(idx_num(p, 1));                       /* r */
		}
	} else {                                                    /* [r0, m, ...] = 指数(+クランプ) */
		int L = ra->length();
		*r0 = idx_num(ra, 0);
		*m  = ( L >= 2 ) ? idx_num(ra, 1) : 0.0;
		if ( L == 4 ) {                 /* [r,m,p1,r1]: p1 で r1 に固定(不連続) */
			*cS0 = idx_num(ra, 2); *cS1 = *cS0; *cR = idx_num(ra, 3);
		} else if ( L >= 5 ) {          /* [r,m,p1,p2,r2]: p1..p2 線形→r2、以降 r2 */
			*cS0 = idx_num(ra, 2); *cS1 = idx_num(ra, 3); *cR = idx_num(ra, 4);
		}
	}
}

static sPtr<pigData> err(const char *msg) {
	return thNEW(pigDataError,(thNEW(stdString,(msg))));
}

/* ハッシュ h のキー k を返す。無ければ thNULL(get_ix は欠落でエラーを返すので吸収)。 */
static sPtr<pigData> hget(sPtr<pigDataHash> h, const char *k) {
	sPtr<pigData> v = h->get_ix(thNEW(pigDataString,(k)));
	if ( v != thNULL && v->is_error() ) return sPtr<pigData>();
	return v;
}

static PPAdjustParams default_params() {
	PPAdjustParams p;
	p.dMin = 0.5; p.maxIter = 200; p.fixEnds = 1; p.wBend = 0.1;
	p.wLen = 1.0; p.wPenalty = 1e3;   /* 上流 CtrlParams 既定に一致(張力/罰則の重み) */
	p.wSpace = 0.0;                   /* 間隔均一化は opt-in (0=従来と完全一致) */
	p.stepMax = 0.05; p.alOuter = 4;
	p.fZ = 0.0; p.fAxis = 0.0; p.fOrigin = 0.0;
	p.sepEnable = 1; p.sepGain = 0.25; p.sepIter = 1000; p.sepLambda = 0.15;
	p.sepTension = 0.0;   /* 実験用。>0 は今のところ曲線短縮で形状が縮むので既定 OFF */
	p.polishEnable = 1;   /* 接触フリー区間の後段ポリッシュ(終端アーチ等の脱出) */
	p.solver = 0; p.cdPitch0 = 8.0; p.cdPitchMin = 0.01;   /* 0=勾配降下, 1=座標降下 */
	p.cdReverse = 0;                                       /* 0=前方(根 c0 側から), 1=後方(尾側から) */
	p.cdParallel = 0; p.cdThreads = 0;                     /* 0=直列 GS / 1=ブロック Jacobi、スレッド 0=自動 */
	return p;
}

/* params ハッシュ {dMin,maxIter,fixEnds,wBend,fZ,fAxis,fOrigin,fixed[],pins[]} → PPAdjustParams。
 * ハッシュでなければ既定値を返す(各 op 側で位置引数フォールバックを処理)。 */
static PPAdjustParams parse_params(sPtr<pigData> a) {
	PPAdjustParams p = default_params();
	if ( a == thNULL ) return p;                     /* 引数省略 */
	sPtr<pigDataHash> h = a->obt_hash();
	if ( ! h.is_notNull() ) return p;
	sPtr<pigData> v;
	if ( (v = hget(h,"dMin"))    != thNULL ) p.dMin    = v->get_flt();
	if ( (v = hget(h,"maxIter")) != thNULL ) p.maxIter = (int)v->get_int();
	if ( (v = hget(h,"fixEnds")) != thNULL ) p.fixEnds = v->get_bool() ? 1 : 0;
	if ( (v = hget(h,"wLen"))    != thNULL ) p.wLen    = v->get_flt();
	if ( (v = hget(h,"wBend"))   != thNULL ) p.wBend   = v->get_flt();
	if ( (v = hget(h,"wPenalty"))!= thNULL ) p.wPenalty= v->get_flt();
	if ( (v = hget(h,"wSpace"))  != thNULL ) p.wSpace  = v->get_flt();
	if ( (v = hget(h,"stepMax")) != thNULL ) p.stepMax = v->get_flt();
	if ( (v = hget(h,"alOuter")) != thNULL ) p.alOuter = (int)v->get_int();
	if ( (v = hget(h,"fZ"))      != thNULL ) p.fZ      = v->get_flt();
	if ( (v = hget(h,"fAxis"))   != thNULL ) p.fAxis   = v->get_flt();
	if ( (v = hget(h,"fOrigin")) != thNULL ) p.fOrigin = v->get_flt();
	if ( (v = hget(h,"separate"))  != thNULL ) p.sepEnable = v->get_bool() ? 1 : 0;
	if ( (v = hget(h,"sepGain"))   != thNULL ) p.sepGain   = v->get_flt();
	if ( (v = hget(h,"sepIter"))   != thNULL ) p.sepIter   = (int)v->get_int();
	if ( (v = hget(h,"sepLambda")) != thNULL ) p.sepLambda = v->get_flt();
	if ( (v = hget(h,"sepTension"))!= thNULL ) p.sepTension= v->get_flt();
	if ( (v = hget(h,"polish"))    != thNULL ) p.polishEnable = v->get_bool() ? 1 : 0;
	if ( (v = hget(h,"solver"))    != thNULL ) { sPtr<stdString> s = v->get_str();
		p.solver = ( s.is_notNull() && (s->cmp("cd")==0 || s->cmp("coord")==0) ) ? 1 : 0; }
	if ( (v = hget(h,"cd"))        != thNULL ) p.solver     = v->get_bool() ? 1 : 0;
	if ( (v = hget(h,"cdPitch0"))  != thNULL ) p.cdPitch0   = v->get_flt();
	if ( (v = hget(h,"cdPitchMin"))!= thNULL ) p.cdPitchMin = v->get_flt();
	/* 座標降下スイープ方向: sweep:"reverse"/"tail" か cdReverse:1 で尾側から。既定=前方(根 c0 側) */
	if ( (v = hget(h,"sweep"))     != thNULL ) { sPtr<stdString> s = v->get_str();
		p.cdReverse = ( s.is_notNull() && (s->cmp("reverse")==0 || s->cmp("tail")==0) ) ? 1 : 0; }
	if ( (v = hget(h,"cdReverse")) != thNULL ) p.cdReverse  = v->get_bool() ? 1 : 0;
	/* 並列座標降下: parallel:1 or parallel:"jacobi" でブロック Jacobi。threads でスレッド上限。
	 * ⚠ get_str() は数値 1 も "1" という有効文字列にするので、キーワード一致でなければ get_bool() に倒す。 */
	if ( (v = hget(h,"parallel"))  != thNULL ) { sPtr<stdString> s = v->get_str();
		if ( s.is_notNull() && (s->cmp("jacobi")==0 || s->cmp("block")==0) ) p.cdParallel = 1;
		else p.cdParallel = v->get_bool() ? 1 : 0; }
	if ( (v = hget(h,"cdParallel")) != thNULL ) p.cdParallel = v->get_bool() ? 1 : 0;
	if ( (v = hget(h,"threads"))    != thNULL ) p.cdThreads  = (int)v->get_int();
	if ( (v = hget(h,"cdThreads"))  != thNULL ) p.cdThreads  = (int)v->get_int();
	if ( (v = hget(h,"fixed"))   != thNULL ) {
		sPtr<pigDataArray> fa = v->obt_array();
		if ( fa.is_notNull() )
			for ( int i = 0 ; i < fa->length() ; ++i )
				p.fixed.push_back((int)fa->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->get_int());
	}
	if ( (v = hget(h,"pins"))    != thNULL ) {
		sPtr<pigDataArray> pa = v->obt_array();
		if ( pa.is_notNull() )
			for ( int i = 0 ; i < pa->length() ; ++i ) {
				sPtr<pigDataHash> ph = pa->get_ix(thNEW(pigDataInteger,((INTEGER64)i)))->obt_hash();
				if ( ! ph.is_notNull() ) continue;
				PPPin pin; pin.joint = 0; pin.hard = 0;
				pin.target[0] = pin.target[1] = pin.target[2] = 0.0;
				sPtr<pigData> jv = hget(ph,"joint"); if ( jv != thNULL ) pin.joint = (int)jv->get_int();
				sPtr<pigData> av = hget(ph,"at");
				if ( av != thNULL ) { pin.target[0]=idx_num(av,0); pin.target[1]=idx_num(av,1); pin.target[2]=idx_num(av,2); }
				sPtr<pigData> hv = hget(ph,"hard");  if ( hv != thNULL ) pin.hard = hv->get_bool() ? 1 : 0;
				p.pins.push_back(pin);
			}
	}
	return p;
}

/* 硬ピンの joint 範囲検査 → エラー or thNULL。
 * ★硬ピンは DOF j+1, j+2 に拘束行を張る (controller.cpp buildConstraints)。npts (= ChainDesign の
 *   numDOF) を越える joint を渡すと、以前は **範囲外へ書いてヒープを壊していた** (SIGSEGV /
 *   corrupted double-linked list・2026-08-13 に発覚)。ライブラリ側にも範囲検査を入れたが、
 *   そちらは黙って落とすだけなので、利用者に届く明示エラーはここで出す (黙るフォールバック禁止)。 */
static sPtr<pigData> check_pins(const PPAdjustParams& P, int npts, const char *opname) {
	for ( size_t i = 0 ; i < P.pins.size() ; ++i ) {
		if ( ! P.pins[i].hard ) continue;            /* 軟ピンは拘束行を張らない */
		if ( P.pins[i].joint < 0 || P.pins[i].joint + 2 >= npts ) {
			char b[192];
			::snprintf(b, sizeof b, "%s: pins[%d].joint=%d が範囲外 (hard ピンは 0..%d)",
			           opname, (int)i, P.pins[i].joint, npts - 3);
			return err(b);
		}
	}
	return sPtr<pigData>();
}

/* body ハッシュ {ctrl, radius, movable} → PPBody(npts<2 で失敗 = npts 0)。 */
static PPBody parse_body(sPtr<pigData> a) {
	PPBody b; b.npts = 0; b.r0 = 0.5; b.m = 0.0; b.movable = 0;
	if ( a == thNULL ) return b;                     /* hget が空 */
	sPtr<pigDataHash> h = a->obt_hash();
	if ( ! h.is_notNull() ) return b;
	int npts = 0;
	b.ctrl_xyz = parse_ctrl(hget(h,"ctrl"), &npts);
	b.npts = npts;
	parse_radius(hget(h,"radius"), &b.r0, &b.m, &b.radial_sr, &b.clampS0, &b.clampS1, &b.clampR);
	sPtr<pigData> mv = hget(h,"movable"); if ( mv != thNULL ) b.movable = mv->get_bool() ? 1 : 0;
	return b;
}

/* PPContact → 配列レコード [gap, pA, pB, normal, sA, sB, rA, rB (, bodyA, bodyB)]。 */
static sPtr<pigData> contact_record(const PPContact& c, bool withBody) {
	sPtr<pigDataArray> rec = thNEW(pigDataArray,());
	rec->push(thNEW(pigDataFloat,(c.gap)));
	rec->push(vec_arr(c.pA));
	rec->push(vec_arr(c.pB));
	rec->push(vec_arr(c.normal));
	rec->push(thNEW(pigDataFloat,(c.sA)));
	rec->push(thNEW(pigDataFloat,(c.sB)));
	rec->push(thNEW(pigDataFloat,(c.rA)));
	rec->push(thNEW(pigDataFloat,(c.rB)));
	if ( withBody ) {
		rec->push(thNEW(pigDataInteger,((INTEGER64)c.bodyA)));
		rec->push(thNEW(pigDataInteger,((INTEGER64)c.bodyB)));
	}
	return rec;
}

/* PPAdjustResult → {ctrl, iters, energy, clearViolation, feasible}。 */
static sPtr<pigData> adjust_result_hash(const PPAdjustResult& r) {
	sPtr<pigDataArray> ctrl = thNEW(pigDataArray,());
	for ( int i = 0 ; i < r.npts ; ++i ) {
		sPtr<pigDataArray> p = thNEW(pigDataArray,());
		p->push(thNEW(pigDataFloat,(r.ctrl_xyz[3*i+0])));
		p->push(thNEW(pigDataFloat,(r.ctrl_xyz[3*i+1])));
		p->push(thNEW(pigDataFloat,(r.ctrl_xyz[3*i+2])));
		ctrl->push(p);
	}
	sPtr<pigDataHash> out = thNEW(pigDataHash,());
	out->set_ix(thNEW(pigDataString,("ctrl")),           ctrl);
	out->set_ix(thNEW(pigDataString,("iters")),          thNEW(pigDataInteger,((INTEGER64)r.iters)));
	out->set_ix(thNEW(pigDataString,("energy")),         thNEW(pigDataFloat,(r.energy)));
	out->set_ix(thNEW(pigDataString,("clearViolation")), thNEW(pigDataFloat,(r.maxClearViolation)));
	out->set_ix(thNEW(pigDataString,("feasible")),       thNEW(pigDataInteger,((INTEGER64)r.feasible)));
	return out;
}

static sPtr<pigData>
compute_proximity(const char *op, sArray<sPtr<pigData> >& args)
{
	(void)op;
	if ( args.length() < 1 )
		return thNEW(pigDataError,(thNEW(stdString,("pipe_proximity: ctrl_pts(点列) が必要"))));

	/* 1) ctrl_pts → 平坦 [x,y,z,...] ---- */
	sPtr<pigDataArray> pts = args[0]->obt_array();
	if ( ! pts.is_notNull() || pts->length() < 2 )
		return thNEW(pigDataError,(thNEW(stdString,(
		    "pipe_proximity: ctrl_pts は 2 点以上の [[x,y,z],...] (先頭=S, 末尾=E, 中間=制御点)"))));
	int npts = pts->length();
	std::vector<double> flat;
	flat.reserve((size_t)npts * 3);
	for ( int i = 0 ; i < npts ; ++i ) {
		sPtr<pigData> p = pts->get_ix(thNEW(pigDataInteger,((INTEGER64)i)));
		flat.push_back(idx_num(p, 0));
		flat.push_back(idx_num(p, 1));
		flat.push_back(idx_num(p, 2));
	}

	/* 2) radius → r0,m(指数) または [s,r] キーポイント(線形補間) ---- */
	double r0, m, cS0, cS1, cR; std::vector<double> radial_sr;
	parse_radius(args.length() >= 2 ? args[1] : sPtr<pigData>(), &r0, &m, &radial_sr, &cS0, &cS1, &cR);

	/* 3) report_gap ---- */
	double reportGap = 1e9;
	if ( args.length() >= 3 && args[2] != thNULL )
		reportGap = num(args[2]);

	/* 4) 検出(アダプタ TU 経由) ---- */
	std::vector<PPContact> contacts = pipe_proximity_run(flat, npts, r0, m, radial_sr, reportGap, cS0, cS1, cR);

	/* 5) PPContact → 配列レコード(単一チェーンは body 番号なし) ---- */
	sPtr<pigDataArray> out = thNEW(pigDataArray,());
	for ( size_t i = 0 ; i < contacts.size() ; ++i )
		out->push(contact_record(contacts[i], /*withBody=*/false));
	return out;
}

/*
 * pipe_adjust(ctrl_pts, radius, params)
 *   自己接近する初期設計を、一様クリアランス gap >= dMin を満たすよう制御点を動かす
 *   (pipe::adjust・ペナルティ/拡張ラグランジュ法)。
 *     ctrl_pts = [[x,y,z],...]   radius = r / [r0, m] / [[s,r],...](線形補間)
 *     params   = ハッシュ {dMin, maxIter, fixEnds, wBend, fZ, fAxis, fOrigin, fixed[], pins[]}
 *                (後方互換: 位置引数 dMin, maxIter, fixEnds, wBend でも可)
 *   返り = {"ctrl":[[x,y,z],...], "iters", "energy", "clearViolation", "feasible"}
 *     ctrl = 調整後の制御点(入力と同じ並び)。そのまま tube / pipe_proximity に渡せる。
 */
static sPtr<pigData>
compute_adjust(sArray<sPtr<pigData> >& args)
{
	int npts = 0;
	std::vector<double> flat = parse_ctrl(args.length() >= 1 ? args[0] : sPtr<pigData>(), &npts);
	if ( npts < 2 )
		return err("pipe_adjust: ctrl_pts は 2 点以上の [[x,y,z],...]");

	double r0, m, cS0, cS1, cR; std::vector<double> radial_sr;
	parse_radius(args.length() >= 2 ? args[1] : sPtr<pigData>(), &r0, &m, &radial_sr, &cS0, &cS1, &cR);

	/* 第 3 引数がハッシュ → params ハッシュ。数値等 → 後方互換の位置引数。 */
	sPtr<pigData> a2 = ( args.length() >= 3 ) ? args[2] : sPtr<pigData>();
	sPtr<pigDataHash> h2 = a2.is_notNull() ? a2->obt_hash() : sPtr<pigDataHash>();
	PPAdjustParams P;
	if ( h2.is_notNull() ) {
		P = parse_params(a2);
	} else {
		P = default_params();
		if ( args.length() >= 3 && args[2] != thNULL ) P.dMin    = num(args[2]);
		if ( args.length() >= 4 && args[3] != thNULL ) P.maxIter = (int)args[3]->get_int();
		if ( args.length() >= 5 && args[4] != thNULL ) P.fixEnds = args[4]->get_bool() ? 1 : 0;
		if ( args.length() >= 6 && args[5] != thNULL ) P.wBend   = num(args[5]);
	}

	sPtr<pigData> pe = check_pins(P, npts, "pipe_adjust");
	if ( pe != thNULL ) return pe;

	PPAdjustResult r = pipe_adjust_run(flat, npts, r0, m, radial_sr, P, cS0, cS1, cR);
	return adjust_result_hash(r);
}

/* bodies 引数(配列 [{ctrl,radius,movable},...]) → vector<PPBody>。失敗時に *e にエラーを入れて空返し。 */
static std::vector<PPBody>
parse_bodies(sPtr<pigData> a, const char *opname, sPtr<pigData> *e) {
	std::vector<PPBody> bodies; *e = sPtr<pigData>();
	sPtr<pigDataArray> ba = a.is_notNull() ? a->obt_array() : sPtr<pigDataArray>();
	if ( ! ba.is_notNull() || ba->length() < 1 ) {
		*e = err("pipe_scene_*: bodies は [{ctrl,radius,movable},...] (1 体以上)"); (void)opname;
		return bodies;
	}
	for ( int i = 0 ; i < ba->length() ; ++i ) {
		PPBody b = parse_body(ba->get_ix(thNEW(pigDataInteger,((INTEGER64)i))));
		if ( b.npts < 2 ) { *e = err("pipe_scene_*: 各 body の ctrl は 2 点以上の [[x,y,z],...]"); bodies.clear(); return bodies; }
		bodies.push_back(b);
	}
	return bodies;
}

/*
 * pipe_scene_proximity(bodies, report_gap)
 *   N 体の近接(可動 body の自己接近 + 異 body 間の交差)を gap 昇順で返す。
 *     bodies = [{ctrl:[[x,y,z],...], radius:..., movable:1/0}, ...]
 *   返り = [[gap, pA, pB, normal, sA, sB, rA, rB, bodyA, bodyB], ...]  (body 番号つき)
 */
static sPtr<pigData>
compute_scene_proximity(sArray<sPtr<pigData> >& args)
{
	if ( args.length() < 1 ) return err("pipe_scene_proximity: bodies が必要");
	sPtr<pigData> e;
	std::vector<PPBody> bodies = parse_bodies(args[0], "pipe_scene_proximity", &e);
	if ( e != thNULL ) return e;

	double reportGap = ( args.length() >= 2 && args[1] != thNULL ) ? num(args[1]) : 1e9;
	std::vector<PPContact> contacts = pipe_scene_proximity_run(bodies, reportGap);

	sPtr<pigDataArray> out = thNEW(pigDataArray,());
	for ( size_t i = 0 ; i < contacts.size() ; ++i )
		out->push(contact_record(contacts[i], /*withBody=*/true));
	return out;
}

/*
 * pipe_scene_adjust(bodies, movableIdx, params)
 *   movableIdx の body を、他の固定 body 群を障害物として gap >= dMin へ調整する(adjustScene)。
 *   params は pipe_adjust と同じハッシュ。fixed/pins は可動 body の DOF に効く。
 *   返り = {"ctrl", "iters", "energy", "clearViolation", "feasible"}  (可動 body の調整後 ctrl)
 */
static sPtr<pigData>
compute_scene_adjust(sArray<sPtr<pigData> >& args)
{
	if ( args.length() < 2 ) return err("pipe_scene_adjust: bodies, movableIdx が必要");
	sPtr<pigData> e;
	std::vector<PPBody> bodies = parse_bodies(args[0], "pipe_scene_adjust", &e);
	if ( e != thNULL ) return e;

	int movableIdx = ( args[1] != thNULL ) ? (int)args[1]->get_int() : 0;
	if ( movableIdx < 0 || movableIdx >= (int)bodies.size() )
		return err("pipe_scene_adjust: movableIdx が範囲外");

	PPAdjustParams P = parse_params(args.length() >= 3 ? args[2] : sPtr<pigData>());
	sPtr<pigData> pe = check_pins(P, bodies[movableIdx].npts, "pipe_scene_adjust");
	if ( pe != thNULL ) return pe;
	PPAdjustResult r = pipe_scene_adjust_run(bodies, movableIdx, P);
	return adjust_result_hash(r);
}

/*
 * pipe_sample(ctrl_pts, radius, pitch)
 *   中心線を**弧長等間隔ピッチ** pitch でサンプルし、各点に半径 R(s) を付けて返す
 *   (可変太さ管の tube 化用・ライブラリの正確な弧長で R(s) を評価)。
 *     端点(s=0,Smax)と半径キーポイント([[s,r]] の s)は強制的に含める。pitch 省略/<=0 で Smax/64。
 *   返り = [[ [x,y,z], r ], ...]  → そのまま tube(res, segs) に渡せる。
 */
static sPtr<pigData>
compute_sample(sArray<sPtr<pigData> >& args)
{
	int npts = 0;
	std::vector<double> flat = parse_ctrl(args.length() >= 1 ? args[0] : sPtr<pigData>(), &npts);
	if ( npts < 2 )
		return err("pipe_sample: ctrl_pts は 2 点以上の [[x,y,z],...]");

	double r0, m, cS0, cS1, cR; std::vector<double> radial_sr;
	parse_radius(args.length() >= 2 ? args[1] : sPtr<pigData>(), &r0, &m, &radial_sr, &cS0, &cS1, &cR);
	double pitch = ( args.length() >= 3 && args[2] != thNULL ) ? num(args[2]) : 0.0;

	std::vector<PPSample> ss = pipe_sample_run(flat, npts, r0, m, radial_sr, pitch, cS0, cS1, cR);

	sPtr<pigDataArray> out = thNEW(pigDataArray,());
	for ( size_t i = 0 ; i < ss.size() ; ++i ) {
		sPtr<pigDataArray> rec = thNEW(pigDataArray,());
		rec->push(vec_arr(ss[i].pos));
		rec->push(thNEW(pigDataFloat,(ss[i].r)));
		out->push(rec);
	}
	return out;
}

/* op で分岐(同一計算本体が検出/調整/シーン/サンプルの各 op を serve)。 */
sPtr<pigData>
pp_compute(const char *op, sArray<sPtr<pigData> >& args)
{
	if ( op && ::strcmp(op, "pipe_adjust") == 0 )            return compute_adjust(args);
	if ( op && ::strcmp(op, "pipe_scene_proximity") == 0 )   return compute_scene_proximity(args);
	if ( op && ::strcmp(op, "pipe_scene_adjust") == 0 )      return compute_scene_adjust(args);
	if ( op && ::strcmp(op, "pipe_sample") == 0 )            return compute_sample(args);
	return compute_proximity(op, args);
}
