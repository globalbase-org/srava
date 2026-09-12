#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""routing への **暗黙依存** を洗い出す (#3485)。

# なぜ要るか
テストが `module("X.so",{priority:99})` で routing を操作しているとき、式の中に
**X が持たない op** があると、その op は別カーネルへ落ちる。つまりそのテストは
「**X はこの op を持たない**」に暗黙に依存している。

★ あとで X にその op を配ると、実行者が静かに入れ替わる。値が一致してしまう場合は
  **テストは落ちず、見たかったものだけが失われる**。実際に #3474 で tube を配ったとき、
  4 箇所のうち落ちたのは 1 箇所だけで、残り 3 箇所は空振りになっていた。

# 使い方
    python3 tools/audit_routing_dependency.py            # 一覧を出す
    python3 tools/audit_routing_dependency.py --check    # 承認済みリストと突き合わせる (差分で終了コード 1)

承認済みリストは test/routing_dependency.txt。**依存そのものを禁止するのではなく、
「意図的か否かを人が判断した記録」を突き合わせる**のが目的。

  意図的  … 「カーネル固有 op が持ち主へ落ちること」を見るテスト (solidify / minkowski / voxels 等)。
            これは routing の検査そのものなので、名指しに直すと **テストの意味が消える**
  事故    … 汎用 op (tube / box …) を「別カーネルが作る」前提で使っている。名指しに直すべき
"""
import io, os, re, sys, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AGENTS = {
    'cgal':       'modules/cgal/c++/cgatsAgent.cpp',
    'manifold':   'modules/manifold/c++/mfatsAgent.cpp',
    'nef_snc':    'modules/nef/c++/nftsAgent.cpp',
    'nef_hybrid': 'modules/nef/c++/nftsAgent.cpp',
    'geogram':    'modules/geogram/c++/ggtsAgent.cpp',
    'cherchi':    'modules/cherchi/c++/chtsAgent.cpp',
    'occt':       'modules/occt/c++/octsAgent.cpp',
    'openvdb':    'modules/openvdb/c++/vdtsAgent.cpp',
}
OPROW = re.compile(r'^\s*\{\s*"([a-z_0-9]+)"\s*,')
QUAL  = re.compile(r'\\?"([a-z_0-9]+)\\?"\s*::\s*([a-z_0-9]+)\s*\(')
CALL  = re.compile(r'(?<![\w:"])([a-z_][a-z_0-9]*)\s*\(')
BOOST = re.compile(r'module\(\\?"([a-z_0-9]+)\.so\\?"\s*,\s*\{[^}]*priority')
SHELL = {'echo','grep','sed','awk','printf','rm','cat','head','tail','cut','test','exit','tr','sh',
         'expr','wc','mkdir','sort','uniq','date','seq','basename','dirname','command','eval','read'}
LIST  = 'test/routing_dependency.txt'


def ops_by_module():
    have = collections.defaultdict(set)
    for name, rel in AGENTS.items():
        p = os.path.join(ROOT, rel)
        if not os.path.exists(p):
            continue
        for ln in io.open(p, encoding='utf-8'):
            m = OPROW.match(ln)
            if m:
                have[name].add(m.group(1))
    return have


def scan():
    have = ops_by_module()
    known = set().union(*have.values()) if have else set()
    out = []
    tdir = os.path.join(ROOT, 'test')
    for name in sorted(os.listdir(tdir)):
        if not name.endswith('.sh'):
            continue
        raw = io.open(os.path.join(tdir, name), encoding='utf-8').read().split('\n')
        # ⚠ コメント行は **落としてから**窓を取る。落とさないと、注記を 1 行足しただけで
        #   窓の範囲が変わり結果が動く (道具が自分の出力を不安定にする)。
        lines = [l for l in raw if not l.lstrip().startswith('#')]
        for i, ln in enumerate(lines):
            m = BOOST.search(ln)
            if not m or m.group(1) not in have:
                continue
            X = m.group(1)
            # ⚠ 窓は **case 分岐 (`;;`) で区切る**。固定行数の窓だと隣のモードへはみ出し、
            #   そのモードの op を誤検出する (実際に `valid` を拾った)。
            lo = i
            while lo > 0 and ';;' not in lines[lo - 1]:
                lo -= 1
            hi = i
            while hi < len(lines) - 1 and ';;' not in lines[hi]:
                hi += 1
            blk = '\n'.join(lines[lo:hi + 1])
            qual = {x.group(2) for x in QUAL.finditer(blk)}
            used = {x.group(1) for x in CALL.finditer(blk)} - SHELL - qual
            missing = sorted(o for o in used if o in known and o not in have[X])
            if missing:
                out.append((name, X, tuple(missing)))
    # 同じ (ファイル, 上げたモジュール, op 集合) は 1 件に畳む
    seen, uniq = set(), []
    for r in out:
        if r in seen:
            continue
        seen.add(r)
        uniq.append(r)
    return sorted(uniq)


def load_approved():
    p = os.path.join(ROOT, LIST)
    if not os.path.exists(p):
        return None
    got = set()
    for ln in io.open(p, encoding='utf-8'):
        ln = ln.split('#')[0].strip()
        if not ln:
            continue
        f, x, ops = ln.split(None, 2)
        got.add((f, x, tuple(sorted(ops.split(',')))))
    return got


def main():
    found = scan()
    if '--check' not in sys.argv:
        for f, x, ops in found:
            print('%-24s boosted=%-11s missing-from-boosted: %s' % (f, x, ','.join(ops)))
        print('\n%d dependency site(s).' % len(found))
        return 0
    approved = load_approved()
    if approved is None:
        print('FAIL: %s not found' % LIST)
        return 1
    cur = {(f, x, tuple(sorted(o))) for f, x, o in found}
    new, gone = sorted(cur - approved), sorted(approved - cur)
    for f, x, ops in new:
        print('FAIL: new implicit routing dependency: %s boosted=%s missing=%s' % (f, x, ','.join(ops)))
    for f, x, ops in gone:
        print('FAIL: approved dependency disappeared (update %s): %s boosted=%s missing=%s'
              % (LIST, f, x, ','.join(ops)))
    if new or gone:
        print('\nA dependency changed. Decide for each site whether it is')
        print('  (a) deliberate  - the test checks that a kernel-specific op reaches its owner')
        print('                    -> keep it and add a line to %s' % LIST)
        print('  (b) accidental  - a generic op was assumed to be produced by another kernel')
        print('                    -> name the producer explicitly, e.g. "manifold"::tube(...)')
        return 1
    print('ROUTING-DEP-OK %d approved site(s)' % len(cur))
    return 0


if __name__ == '__main__':
    sys.exit(main())
