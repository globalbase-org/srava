#!/usr/bin/env python3
# agentwatch — srava の agent 並列実行を「群れ」としてライブ可視化するモニタ。
#
#   別ターミナルで実行: python3 tools/agentwatch.py
#     → process 実行の agent (srava_agent + 旧プラグイン agent) を全部捕捉。
#       引数に **モジュール名** (cgal,manifold,d4 …) を渡すとそのカーネルだけに絞る。
#       旧来どおり **実行体名** (pipe_proximity_agent 等) でも絞れる ("_agent" で終わる語は実行体名扱い)。
#   そのまま srava を走らせると、起動中のエージェント 1 匹 = 1 ブロックでリアルタイム表示。
#
# ★ .so 化 (rev4) 対応で変わったこと
#   - agent の起動 argv は **`<srava_agent> <…/kernel.so> <op> <file> <line>`**
#     (pigfModuleAgent.cpp「引数の並び: <so> op file line」)。カーネルごとに実行体が分かれていた
#     旧世界と違い、**実行体名は全カーネル共通 `srava_agent`**。識別子は argv[1] の .so に移った。
#     → 表示・集計・フィルタの主キーを **モジュール名** (so の basename から ".so" を落としたもの) にした。
#   - **in-proc カーネルはプロセスを作らない**。descriptor の exec_default=THREAD のモジュール
#     (manifold・d2〜d5・pipe_proximity 等) は planner 内の worker thread で走るため、
#     プロセスを数える従来の見方では **丸ごと視界から消える** (CGAL だけが見える偏った絵になる)。
#     → planner (`srava`) を /proc から読み、**in-proc の仕事を「群」として別建て表示**する。
#     負荷 = planner **プロセス合計** の jiffies 差分・本数 = いま R (running) 状態のスレッド数。
#     (★2026-08-12: 旧「tid ごとの utime 差分」は pipe_proximity 等の parallel_for が**呼び出しごとに
#      std::thread を生成→join** する (寿命 ~ms・tid churn 秒間数千) ため一度も負荷が算出されず
#      in-proc が丸ごと見えなかった。プロセス合計は join 済みスレッド分も積算されるので churn 耐性。)
#     スレッドには名前が付いていない (comm は全部 "srava") ので op 名までは出せない。個数と負荷のみ。
#     op 名まで出したければ in-proc 実行の起点で pthread_setname_np(op) する C++ 側の改修が要る (未実施)。
#
# 負荷(CPU%)を計算し、
#   ・active (>50%)  … 実際に計算しているワーカー
#   ・waiting (≤50%) … 入力キャッシュの完成待ち等で 0% 近いワーカー(ts2Parallel 並列化で増えた)
# の 2 群に分けて表示する。各行: モジュール 演算名 ファイル名 行番号 ×個数 負荷(%)。
#
# TTY ならその場で再描画(ライブアニメ)、パイプ/リダイレクトなら 1 行スナップショット(ログ/デモ)。
# Linux は /proc 直読み(stat の utime+stime 差分で負荷算出)、macOS/BSD は pgrep+ps(%cpu)フォールバック。
# (in-proc スレッドの可視化は /proc/<pid>/task に依存するので Linux のみ。)
import os, sys, time, collections, subprocess

# 監視対象。argv[1] にカンマ区切りで指定。"_agent" で終わる語は**実行体名**、それ以外は**モジュール名**。
#   例: agentwatch.py cgal          → cgal.so の agent だけ
#       agentwatch.py cgal,manifold → 2 カーネル
#       agentwatch.py echo_agent    → 旧来の実行体名指定 (後方互換)
_targ       = sys.argv[1] if len(sys.argv) > 1 else ""
_toks       = [t for t in _targ.split(",") if t]
TARGET_BINS = [t for t in _toks if t.endswith("_agent")] or None
TARGET_MODS = [t for t in _toks if not t.endswith("_agent")] or None
HAVE_PROC   = os.path.isdir('/proc')
HZ          = 60 if HAVE_PROC else 30
SPARK       = " ▁▂▃▄▅▆▇█"
HIST_N      = 60
CLK_TCK     = (os.sysconf('SC_CLK_TCK') if (HAVE_PROC and hasattr(os, 'sysconf')) else 100) or 100
LOAD_WIN    = 0.4    # 負荷を平均する窓(秒)。これ未満の間隔では前回値を据え置く(安定化)。
ACTIVE_TH   = 50     # この % 超を active(計算中)、以下を waiting(待ち)とする。
PLANNER     = "srava"   # in-proc thread を数える親プロセスの実行体名。
INPROC_MOD  = "(in-proc)"   # in-proc worker thread の擬似モジュール名 (so が特定できないため)。

def bin_match(comm):
    """実行体が監視対象か。TARGET_BINS 明示があればその集合、無ければ "_agent" で終わる全 agent。
    .so 化後は実質 srava_agent のみだが、旧プラグイン agent の残骸も拾えるよう条件は据え置き。"""
    base = comm.rsplit('/', 1)[-1]
    return (base in TARGET_BINS) if TARGET_BINS else base.endswith('_agent')

def mod_match(mod):
    """モジュール名が監視対象か。TARGET_MODS 明示が無ければ全通し。"""
    return (mod in TARGET_MODS) if TARGET_MODS else True

def _info(toks):
    """argv トークン列から (module, op, file, line) を取る。
      ★ .so 形式  `<bin> <…/kernel.so> op file line` → module=kernel        (現行)
        旧 3 引数  `<bin> op file line`              → module=実行体名由来   (旧プラグイン agent)
        旧 2 引数  `<bin> op line`                   → file='?'
      判別不能は ('?','?','?','?')。"""
    mod = '?'
    # argv[1] が .so なら .so 化後の形式。module 名は basename から ".so" を落としたもの。
    if len(toks) >= 2 and toks[1].endswith('.so'):
        mod  = toks[1].rsplit('/', 1)[-1][:-3]
        rest = toks[2:]
    else:
        # 旧世界: 実行体名がカーネルを表していた (pipe_proximity_agent → pipe_proximity)。
        base = toks[0].rsplit('/', 1)[-1]
        mod  = base[:-6] if base.endswith('_agent') else base
        rest = toks[1:]
    if len(rest) >= 3 and rest[-1].lstrip('-').isdigit():
        return (mod, rest[-3], rest[-2], rest[-1])
    if len(rest) >= 2 and rest[-1].lstrip('-').isdigit():
        return (mod, rest[-2], '?', rest[-1])
    return ('?', '?', '?', '?')

def cpu_jiffies(path):
    """Linux: <path>/stat の utime+stime(jiffies)。<path> は /proc/<pid> でも /proc/<pid>/task/<tid> でも可。
    comm が括弧/空白を含むので最後の ')' 以降を split。"""
    try:
        s = open('%s/stat' % path).read()
        f = s[s.rfind(')') + 2:].split()
        return int(f[11]) + int(f[12])    # 14,15 番目フィールド(')' 以降の 12,13 番目)
    except (OSError, IndexError, ValueError):
        return None

def planner_pids():
    """planner (srava 本体) の pid 集合。in-proc worker thread を数えるために使う。"""
    out = []
    for d in os.listdir('/proc'):
        if not d.isdigit():
            continue
        try:
            toks = open('/proc/%s/cmdline' % d).read().replace('\0', ' ').split()
        except OSError:
            continue
        if toks and toks[0].rsplit('/', 1)[-1] == PLANNER:
            out.append(int(d))
    return out

def inproc_threads():
    """planner 内スレッドの {(pid,tid): '/proc/<pid>/task/<tid>'}。
    ★ スレッド名は付いていない (comm は全部 "srava") ので op までは分からない。"""
    m = {}
    for p in planner_pids():
        try:
            for t in os.listdir('/proc/%d/task' % p):
                m[(p, int(t))] = '/proc/%d/task/%s' % (p, t)
        except OSError:
            pass
    return m

def inproc_running(th):
    """瞬間スナップショット: R (running) 状態の planner スレッド数。
    ★ pipe_proximity 等の parallel_for は呼び出しごとに std::thread を生成して join する
      (寿命 ~ms) ため、tid を跨ぐ差分計測は成立しない。状態 R の本数は差分不要で churn に強い。
      main/event ループは大抵 S (sleep) なので自然に落ちる。"""
    n = 0
    for path in th.values():
        try:
            with open(path + '/stat') as f:
                s = f.read()
            if s[s.rfind(')') + 2] == 'R':
                n += 1
        except (OSError, IndexError):
            pass
    return n

def live_map_proc():
    """Linux: ({pid:(module,op,file,line)}, {})。負荷はループ側で stat 差分から算出。"""
    m = {}
    for d in os.listdir('/proc'):
        if not d.isdigit():
            continue
        try:
            # ★ /proc/comm は 15 文字に切り詰められる(TASK_COMM_LEN=16)ので長い agent 名
            #   (pipe_proximity_agent=20 文字 → "pipe_proximity_")が判定不能になる。代わりに
            #   cmdline の実行体名(argv0 の basename・切り詰めなし)で判定する。
            #   NB: agent は sh -c 経由で起動されるので `sh` の親も居るが、argv0 が sh なので弾かれる。
            toks = open('/proc/%s/cmdline' % d).read().replace('\0', ' ').split()
            if not toks or not bin_match(toks[0]):
                continue
            info = _info(toks)
            if info == ('?', '?', '?', '?') or not mod_match(info[0]):
                continue   # op/file/line 無し(終了直前/exec 前の過渡プロセス)or 対象外モジュール
            m[int(d)] = info
        except OSError:
            pass
    return m, {}

def live_map_ps():
    """macOS/BSD: ({pid:(module,op,file,line)}, {pid:load%})。load は ps の %cpu。"""
    pids = set()
    try:
        if TARGET_BINS:
            for nm in TARGET_BINS:                   # 明示名: 名前ごとに完全一致 pgrep
                o = subprocess.run(['pgrep', '-x', nm],
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                pids |= set(o.stdout.split())
        else:                                        # 既定: cmdline に "_agent" を含むものを拾い
            o = subprocess.run(['pgrep', '-f', '_agent'],  # 実行体名で最終判定(下の bin_match)
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            pids |= set(o.stdout.split())
    except OSError:
        return {}, {}
    if not pids:
        return {}, {}
    m, load = {}, {}
    try:
        # ★ -ww: 幅無制限で command を出す。macOS の ps はパイプ時に既定幅(~80桁)で切り詰めるため、
        #   agent のパスが長いと末尾の "op file line" が落ちて _info が '?' を返す
        #   (Linux は /proc/cmdline 直読みで切れないので発生しない)。
        #   .so 化後は argv が 1 個増えている (so パス) ので、なおさら -ww が要る。
        ps = subprocess.run(['ps', '-ww', '-o', 'pid=', '-o', '%cpu=', '-o', 'command=',
                             '-p', b','.join(pids).decode()],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        for ln in ps.stdout.decode().splitlines():
            f = ln.split()
            if len(f) < 3:
                continue
            try:
                pid = int(f[0]); cpu = float(f[1])
            except ValueError:
                continue
            if not bin_match(f[2]):
                continue   # 実行体名が対象でない(pgrep -f の巻き込み)→ 除外
            info = _info(f[2:])
            if info == ('?', '?', '?', '?') or not mod_match(info[0]):
                continue   # command が取れない過渡プロセス(ゾンビ/exec 前) or 対象外モジュール
            m[pid] = info
            load[pid] = cpu
    except OSError:
        pass
    return m, load

live_map = live_map_proc if HAVE_PROC else live_map_ps

def color(s, c):
    return "\033[%dm%s\033[0m" % (c, s)

def group_rows(buckets):
    """buckets: {(module,op,file,line): [loads]} → [(n, module, op, file, line, avg_load)] 個数降順。"""
    rows = []
    for (mod, op, fn, ln), loads in buckets.items():
        n = len(loads)
        rows.append((n, mod, op, fn, ln, (sum(loads) / n if n else 0.0)))
    rows.sort(key=lambda r: -r[0])
    return rows

def emit_section(label, buckets, col, maxrows=8):
    rows = group_rows(buckets)
    if not rows:
        return "  %s  %s\n" % (label, color("(none)", 90))
    s = "  %s\n" % label
    # ★ 桁揃えは **プレーン文字列を %-Ns で詰めてから** 色付け(色コードは可視幅 0)。
    #   `%-14s % color(...)` だと色コードが幅に数えられて端末で桁ずれするため。
    for (n, mod, op, fn, ln, avg) in rows[:maxrows]:
        s += "    %s %s %-18s %5s  %s  %s\n" % (
            color("%-10s" % mod[:10], 95),           # モジュール(10・マゼンタ)= .so 化の主キー
            color("%-14s" % op, col),                # 演算名(14)
            fn[:18],                                 # ファイル名(18)
            ln,                                      # 行番号(右5)
            color("%-5s" % ("×%d" % n), 90),         # 個数
            color("%4.0f%%" % avg, col))             # 負荷(%)
    extra = len(rows) - maxrows
    if extra > 0:
        s += "    %s\n" % color("… +%d more" % extra, 90)
    return s

def main():
    tty   = sys.stdout.isatty()
    width = (os.get_terminal_size().columns if tty else 80)
    barw  = max(10, width - 14)
    seen  = set()
    peak  = 0
    peak_active = 0   # active(>50%) のピーク
    hist  = collections.deque([0] * HIST_N, maxlen=HIST_N)
    first = {}                  # pid -> 初観測時刻
    cpu_prev = {}               # pid -> (jiffies, t)
    cpu_load = {}               # pid -> pct
    pl_prev  = {}               # planner pid -> (jiffies, t)   in-proc 負荷 (プロセス合計) 用
    pl_load  = {}               # planner pid -> pct (プロセス合計。>100% あり)
    t0    = time.time()
    last_print = 0.0
    if tty:
        sys.stdout.write("\033[?25l")
    try:
        while True:
            m, ps_load = live_map()
            pids = set(m)
            now_n = len(pids)
            t = time.time()
            for p in pids:
                first.setdefault(p, t)
            for p in list(first):
                if p not in pids:
                    del first[p]
            # --- 負荷(%) ---
            if HAVE_PROC:
                for p in pids:
                    j = cpu_jiffies('/proc/%d' % p)
                    if j is None:
                        continue
                    if p in cpu_prev:
                        pj, pt = cpu_prev[p]
                        dt = t - pt
                        if dt > 0.05:   # ~0.05s たまったら毎フレーム算出(初回も即出る)。窓内の平均。
                            cpu_load[p] = max(0.0, (j - pj) / (dt * CLK_TCK) * 100.0)
                        if dt >= LOAD_WIN:   # 窓を更新(直近 LOAD_WIN の平均に保つ)
                            cpu_prev[p] = (j, t)
                    else:
                        cpu_prev[p] = (j, t); cpu_load.setdefault(p, 0.0)
                for p in list(cpu_prev):
                    if p not in pids:
                        cpu_prev.pop(p, None); cpu_load.pop(p, None)
                load = cpu_load    # 上限キャップ無し(マルチスレッド agent は >100% もあり)
            else:
                load = ps_load

            # --- in-proc worker thread (planner 内・プロセスを作らないカーネル) ---
            # ★ これを出さないと exec_default=THREAD のモジュール (manifold 等) の仕事が
            #   画面から丸ごと消え、「CGAL しか動いていない」ように見える。
            n_inproc = 0
            inproc_avg = 0.0
            # モジュール絞り込みがあっても in-proc 群は出す (スレッドは so に紐付けられないため、
            # 絞ると「その仕事が無い」と誤読させる。個数と負荷だけの参考値として常時表示)。
            # ★ 2026-08-12: 「同じ tid の utime 差分」方式を廃止。pipe_proximity 等の parallel_for は
            #   **呼び出しごとに std::thread を生成して join** する (寿命 ~ms・tid が秒間数千個流れる)
            #   ため、どの tid も 2 回サンプルされず負荷ゼロ扱い = in-proc が丸ごと見えなかった。
            #   代わりに: 負荷 = **planner プロセス合計** の jiffies 差分 (/proc/<pid>/stat は join 済み
            #   スレッド分もプロセス合計へ積算される = churn 耐性)。本数 = いま R (running) 状態の
            #   スレッド数 (瞬間値・差分不要)。
            if HAVE_PROC:
                pls = planner_pids()
                for p in pls:
                    j = cpu_jiffies('/proc/%d' % p)
                    if j is None:
                        continue
                    if p in pl_prev:
                        pj, pt = pl_prev[p]
                        dt = t - pt
                        if dt > 0.05:
                            pl_load[p] = max(0.0, (j - pj) / (dt * CLK_TCK) * 100.0)
                        if dt >= LOAD_WIN:
                            pl_prev[p] = (j, t)
                    else:
                        pl_prev[p] = (j, t); pl_load.setdefault(p, 0.0)
                for p in list(pl_prev):
                    if p not in pls:
                        pl_prev.pop(p, None); pl_load.pop(p, None)
                tot = sum(pl_load.values())
                n_inproc = inproc_running(inproc_threads())
                if tot <= ACTIVE_TH:
                    n_inproc = 0        # プロセス合計が閾値以下 = idle (R を偶然踏んだだけ) は出さない
                inproc_avg = (tot / n_inproc) if n_inproc else 0.0

            seen |= pids
            peak = max(peak, now_n)
            hist.append(now_n)
            hi = max(1, peak)
            # --- active / waiting 振り分け ---
            active  = collections.defaultdict(list)
            waiting = collections.defaultdict(list)
            for p in pids:
                ld = load.get(p, 0.0)
                (active if ld > ACTIVE_TH else waiting)[m[p]].append(ld)
            # in-proc 群は op が分からないので 1 行にまとめて active 側へ載せる。
            if n_inproc:
                active[(INPROC_MOD, 'thread', 'planner', '-')] = [inproc_avg] * n_inproc
            na = sum(len(v) for v in active.values())     # active 数(バー塗り分け/表示で使う)
            nw = sum(len(v) for v in waiting.values())     # waiting 数
            peak_active = max(peak_active, na)
            oldest_tag, oldest_age = "", 0.0
            if first:
                op = min(first, key=lambda p: first[p])
                oldest_tag = "%s %s %s %s" % m.get(op, ('?', '?', '?', '?'))
                oldest_age = t - first[op]

            if tty:
                # now バーを active(緑) / waiting(黄) で塗り分ける。比率 na:nw を live ブロックに配分。
                total  = now_n + n_inproc
                live   = min(total, barw)
                a_len  = int(round(na * live / total)) if total > 0 else 0
                a_len  = min(a_len, live)
                w_len  = live - a_len
                blocks = color("█" * a_len, 92) + color("█" * w_len, 93)   # 緑=active / 黄=waiting
                guide  = color("·" * max(0, min(peak, barw) - live), 90)
                pad    = " " * max(0, barw - live - max(0, min(peak, barw) - live))
                spark  = "".join(SPARK[min(8, int(v * 8 / hi))] for v in hist)
                el     = t - t0
                rate   = len(seen) / el if el > 0 else 0
                out = ["\033[H\033[J"]
                _scope = ",".join(_toks) if _toks else "全モジュール"
                out.append(color("  agent live monitor", 96) + color("  [%s]" % _scope, 90)
                           + color("   (Ctrl-C で終了)", 90) + "\n\n")
                # now/peak は **active/全体** で表示(active=緑, 全体=シアン)。
                out.append("  now %s%s%s  [%s%s%s]   %s%s\n\n" % (
                    color("%d" % na, 92), color("/", 90), color("%d" % total, 96),
                    blocks, guide, pad,
                    color("█ active", 92), color("  █ waiting", 93)))   # 凡例
                out.append("  peak %s%s%s   spawned %s   ~%s/s   %s\n\n" %
                           (color("%d" % peak_active, 92), color("/", 90), color("%d" % peak, 91),
                            color("%d" % len(seen), 94), color("%.0f" % rate, 94),
                            color("in-proc %d" % n_inproc, 95)))   # process を作らない仕事の可視化
                out.append(emit_section(color("active  (>50%%)  %d" % na, 92), active, 92))
                out.append(emit_section(color("waiting (≤50%%) %d" % nw, 93), waiting, 90))
                if oldest_tag:
                    flag = 91 if oldest_age > 3 else 90
                    out.append("\n  oldest: %s  %s\n" % (color(oldest_tag, 93), color("%.1fs" % oldest_age, flag)))
                out.append("\n  " + color(spark, 96) + "\n")
                sys.stdout.write("".join(out)); sys.stdout.flush()
                time.sleep(1.0 / HZ)
            else:
                time.sleep(1.0 / HZ)
                if t - last_print >= 0.5:
                    last_print = t
                    sys.stdout.write("t=%5.2f now=%2d peak=%2d spawned=%3d active=%2d waiting=%2d inproc=%2d\n"
                                     % (t - t0, now_n, peak, len(seen), na, nw, n_inproc))
                    sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        if tty:
            sys.stdout.write("\033[?25h\n")
        sys.stdout.write("\n[agentwatch] peak=%d  total spawned=%d\n" % (peak, len(seen)))

if __name__ == "__main__":
    main()
