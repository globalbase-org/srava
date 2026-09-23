#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""OPS 表の sig に現れる **型名の綴り** を、登録済みの型名と突き合わせる (#3527 段 6)。

# なぜ要るか

sig は **ただの文字列**なので、型名を綴り間違えてもコンパイルは通る。通ったうえで
ルータがその行に一度もヒットしないか、**別の型へ黙って routing される**。

    gg-mesh3d (geogram)  対  gu-mesh3d (geomutils)     ← 1 文字違い
    nf-mesh3d (SNC)      対  nfb-mesh3d (境界形式)
    vd-grid3d            対  vd-grid                   ← 後者は存在しない

⚠⚠ **この検査の守備範囲を取り違えないこと**。見ているのは「その名前が *登録されているか*」
  だけで、「*意図した型か*」ではない:

    gu-mesh3d  →  guu-mesh3d   ★ 捕まる (登録されていない名前)
    gu-mesh3d  →  gg-mesh3d    ✗ **捕まらない** (どちらも実在する名前)

  ⇒ 1 文字違いの取り違えそのものは検出できない。それは *routing の結果* を見る検査
    (test/routing_dependency.txt / 各カーネルの値の検定) の担当。
  ★ ここが捕まえるのは「**誰も名乗っていない型を sig が指している**」= その行が死んでいる
    (一度もヒットしない) 状態。実測した守備範囲は test/srava_sig_typenames.sh の陽性対照に書いてある。

# 材料

`srava --module-info` の出力。**ソースではなく実物**を読む — 型名はマクロで組み立てられる
(GU_TYPE_3D など) ので、ソースを正規表現で追うと展開前の名前を見ることになる。
⚠ 逆に「ビルドされていないモジュールの型」は出てこない。⇒ 台帳 (test/sig_typenames.txt) に
  *構成によって欠けうる型* を並べておき、登録済みの集合に足して突き合わせる。

# 使い方

    python3 tools/audit_sig_typenames.py <srava>            # 一覧を出す
    python3 tools/audit_sig_typenames.py <srava> --check    # 未登録の型名があれば終了コード 1
"""
import os, re, subprocess, sys

ROOT   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEDGER = os.path.join(ROOT, 'test', 'sig_typenames.txt')

# sig の中で型名になりうる字面。⚠ "->value" のようなハイフン無しの語は型ではないので拾わない。
TYPE_RE = re.compile(r'[a-z][a-z0-9_]*-[a-z0-9_]+')


def module_info(srava):
    out = subprocess.run([srava, '--module-info'], stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, text=True)
    if out.returncode != 0:
        sys.stderr.write("audit_sig_typenames: %s --module-info が失敗した\n" % srava)
        sys.exit(2)
    return out.stdout


def collect(text):
    """(宣言された型名, sig に現れた型名 -> [(モジュール, op)]) を返す。"""
    declared = set()
    used     = {}
    module   = '?'
    op       = '?'
    for line in text.splitlines():
        m = re.match(r'^(\S+)\s+\(abi=', line)
        if m:
            module = m.group(1)
            continue
        m = re.search(r'types = (\S*)', line)
        if m:
            for t in m.group(1).split(','):
                if t:
                    declared.add(t)
            continue
        m = re.match(r'^\s+([a-z_0-9]+)\s+nin=', line)
        if m:
            op = m.group(1)
            continue
        m = re.match(r'^\s+sig = (.*)$', line)
        if m:
            for t in TYPE_RE.findall(m.group(1)):
                used.setdefault(t, []).append((module, op))
    return declared, used


def ledger():
    """構成によって欠けうる型名 (ビルドされていないモジュールのもの)。"""
    names = set()
    if not os.path.exists(LEDGER):
        return names
    with open(LEDGER, encoding='utf-8') as f:
        for line in f:
            line = line.split('#', 1)[0].strip()
            if line:
                names.add(line)
    return names


def main():
    args  = [a for a in sys.argv[1:] if not a.startswith('--')]
    check = '--check' in sys.argv
    if not args:
        sys.stderr.write("usage: audit_sig_typenames.py <srava> [--check]\n")
        return 2
    declared, used = collect(module_info(args[0]))
    known = declared | ledger()

    unknown = sorted(t for t in used if t not in known)
    if not check:
        print("登録済みの型名 %d 個:" % len(declared))
        for t in sorted(declared):
            print("   ", t)
        extra = sorted(ledger() - declared)
        if extra:
            print("台帳にしか無い (この構成では建っていない) %d 個:" % len(extra))
            for t in extra:
                print("   ", t)
        print("sig に現れた型名 %d 個 / 未登録 %d 個" % (len(used), len(unknown)))
    for t in unknown:
        where = ', '.join('%s::%s' % (m, o) for m, o in used[t][:4])
        print("UNKNOWN %-16s <- %s" % (t, where))
    # ★ 逆向きも見る: **誰も使っていない型**。死んでいる登録 (名乗ったが sig に無い) の合図。
    #   ⚠ これは *即座に誤り* ではない (import/export 専用の型など) ので check では落とさない。
    idle = sorted(t for t in declared if t not in used)
    if idle and not check:
        print("⚠ 名乗っているが sig に現れない型 %d 個: %s" % (len(idle), ', '.join(idle)))
    if unknown:
        print("FAIL: sig が **登録されていない型名** を指している (%d 個)" % len(unknown))
        return 1
    if check:
        print("SIGTYPES-OK sig の型名 %d 個はすべて登録済み" % len(used))
    return 0


if __name__ == '__main__':
    sys.exit(main())
