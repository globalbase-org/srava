#!/bin/sh
# ★★ #3564: **sPtr::__get() を木に置かない**ことを数える。$1 = ソース木。
#
# ---- なぜ ----
# @__get()@ は sPtr から **生ポインタを剥がす**唯一の口。ひさ 2026-09-20 に **利用禁止**。
# 剥がした先は参照カウントに載らないので、
#   ・保持すると「借りたポインタ越しの操作」になる (#3560 で撤収中に SIGSEGV を踏んだ家系)
#   ・null 検査が「一度も入っていない」しか拾えず、**「入ったが畳まれた」は素通り**する
#     (#3562 で入れた flush() / error() の panic が、#3564 で sPtr にして初めて本物になった)
# ⇒ 置き換え方は 2 通りしかない:
#     ① 保持する用途 … **メンバを sPtr にする** (pigEnvironment::tryPtr が先例)
#     ② 参照を渡す用途 (@*p.__get()@) … **受け側を sPtr 受けにする**。sPtr に @operator*@ は
#        無いので、これをやらない限り @__get()@ は消えない (cg_refine_3d / op_proximity /
#        nf_to_mesh などを 2026-09-20 に sPtr 受けへ直した)
#
# ⚠ 対象は **この木のソースだけ**。ts2 の実装 (/usr/local/include/ts2/c++/sPtr.h) は
#   sPtr 自身が内部で使うので当然出てくる — そこは触らないし数えない。
D="${1:?source dir not given}"

# ★★ #3522: ハングの番犬 (共通)。詳細は test/srava_hangwatch.sh。
. "$(dirname "$0")/srava_hangwatch.sh"

command -v grep >/dev/null 2>&1 || { echo "RAWGET-SKIP grep が無い"; exit 77; }

SRC="$D/src $D/modules"
for d in $SRC; do
	[ -d "$d" ] || { echo "RAWGET-SKIP $d が無い"; exit 77; }
done

# ---- ★★ 陽性対照: 探し方が **当たることを見てから** 0 を報告する ----
# ⚠ 「0 件でした」は何も見せない — ファイル一覧や grep の書き方が壊れていても 0 になる
#   (2026-09-15 に mac で @grep -P@ が無く「検出 0 件」で緑になった実例がある)。
# ★ @sPtr<@ は木じゅうに在るので、同じ探し方で **必ず当たる**。
ctl=$(grep -rl "sPtr<" --include=*.cpp --include=*.h $SRC 2>/dev/null | grep -c '')
echo "--- 陽性対照 (同じ探し方で sPtr< を持つファイル): $ctl 本"
if [ "$ctl" = "0" ]; then
	echo "FAIL: 陽性対照が当たらない — **探し方の方が壊れている**"
	echo "  ⇒ この木の .cpp/.h に sPtr< が 1 つも無いことはありえない"
	exit 1
fi

hits=$(grep -rn "__get()" --include=*.cpp --include=*.h $SRC 2>/dev/null |
       grep -v '利用禁止' | grep -c '')
echo "--- __get() の出現: $hits 件 (許容 0)"
if [ "$hits" != "0" ]; then
	echo "FAIL: sPtr::__get() が $hits 件ある (ひさ 2026-09-20: **利用禁止**)"
	grep -rn "__get()" --include=*.cpp --include=*.h $SRC 2>/dev/null |
	  grep -v '利用禁止' | sed 's/^/       /' | head -20
	echo "  ⇒ 保持する用途なら **メンバを sPtr に** (先例: pigEnvironment::tryPtr・#3564)"
	echo "  ⇒ *p.__get() で参照を渡しているなら **受け側を sPtr 受けに** する"
	echo "     (sPtr に operator* は無いので、受け側を直さない限り消えない)"
	exit 1
fi
echo "RAWGET-OK 木に sPtr::__get() は無い"
