#!/usr/bin/env python3
# agentwatch — srava/プラグイン agent の並列実行を「群れ」としてライブ可視化するモニタ。
#
#   別ターミナルで実行: python3 tools/agentwatch.py
#     → comm が "_agent" で終わる全 agent(srava_agent + プラグイン agent: pipe_proximity_agent /
#       echo_agent 等)を捕捉。argv[1] に名前(カンマ区切りで複数可)を渡すとその名前だけに絞る。
#   そのまま srava を走らせると、起動中のエージェント 1 匹 = 1 ブロックでリアルタイム表示。
#
# 各 agent は `<bin> <op> <file> <line>` で起動される(planner/pigfPluginAgent が表示用に付与)。
# 負荷(CPU%)を計算し、
#   ・active (>50%)  … 実際に計算しているワーカー
#   ・waiting (≤50%) … 入力キャッシュの完成待ち等で 0% 近いワーカー(ts2Parallel 並列化で増えた)
# の 2 群に分けて表示する。各行: 演算名 ファイル名 行番号 ×個数 負荷(%)。
#
# TTY ならその場で再描画(ライブアニメ)、パイプ/リダイレクトなら 1 行スナップショット(ログ/デモ)。
# Linux は /proc 直読み(stat の utime+stime 差分で負荷算出)、macOS/BSD は pgrep+ps(%cpu)フォールバック。
import os, sys, time, collections, subprocess

# 監視対象プロセス名。argv[1] にカンマ区切りで明示指定可。省略時(TARGETS=None)は comm が
# "_agent" で終わる全プロセス(srava_agent + 各プラグイン agent)にマッチ。
_targ     = sys.argv[1] if len(sys.argv) > 1 else ""
TARGETS   = [t for t in _targ.split(",") if t] or None
HAVE_PROC = os.path.isdir('/proc')
HZ        = 60 if HAVE_PROC else 30
SPARK     = " ▁▂▃▄▅▆▇█"
HIST_N    = 60
CLK_TCK   = (os.sysconf('SC_CLK_TCK') if (HAVE_PROC and hasattr(os, 'sysconf')) else 100) or 100
LOAD_WIN  = 0.4    # 負荷を平均する窓(秒)。これ未満の間隔では前回値を据え置く(安定化)。
ACTIVE_TH = 50     # この % 超を active(計算中)、以下を waiting(待ち)とする。

def name_match(comm):
    """監視対象か。明示 TARGETS があればその集合、無ければ comm が "_agent" で終わる全 agent。"""
    base = comm.rsplit('/', 1)[-1]
    return (base in TARGETS) if TARGETS else base.endswith('_agent')

def _info(toks):
    """`/path/<bin> op file line` の末尾から (op, file, line) を取る。
    旧 `op line` 形式(file 無し)は file='?'。判別不能は ('?','?','?')。"""
    if len(toks) >= 4 and toks[-1].lstrip('-').isdigit():
        return (toks[-3], toks[-2], toks[-1])
    if len(toks) >= 3 and toks[-1].lstrip('-').isdigit():
        return (toks[-2], '?', toks[-1])
    return ('?', '?', '?')

def cpu_jiffies(pid):
    """Linux: /proc/pid/stat の utime+stime(jiffies)。comm が括弧/空白を含むので最後の ')' 以降を split。"""
    try:
        s = open('/proc/%d/stat' % pid).read()
        f = s[s.rfind(')') + 2:].split()
        return int(f[11]) + int(f[12])    # 14,15 番目フィールド(')' 以降の 12,13 番目)
    except (OSError, IndexError, ValueError):
        return None

def live_map_proc():
    """Linux: ({pid:(op,file,line)}, {})。負荷はループ側で stat 差分から算出。"""
    m = {}
    for d in os.listdir('/proc'):
        if not d.isdigit():
            continue
        try:
            # ★ /proc/comm は 15 文字に切り詰められる(TASK_COMM_LEN=16)ので長い agent 名
            #   (pipe_proximity_agent=20 文字 → "pipe_proximity_")が判定不能になる。代わりに
            #   cmdline の実行体名(argv0 の basename・切り詰めなし)で判定する。
            toks = open('/proc/%s/cmdline' % d).read().replace('\0', ' ').split()
            if not toks or not name_match(toks[0]):
                continue
            info = _info(toks)
            if info == ('?', '?', '?'):
                continue   # op/file/line 無し(終了直前/exec 前の過渡プロセス) → 除外
            m[int(d)] = info
        except OSError:
            pass
    return m, {}

def live_map_ps():
    """macOS/BSD: ({pid:(op,file,line)}, {pid:load%})。load は ps の %cpu。"""
    pids = set()
    try:
        if TARGETS:
            for nm in TARGETS:                       # 明示名: 名前ごとに完全一致 pgrep
                o = subprocess.run(['pgrep', '-x', nm],
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
                pids |= set(o.stdout.split())
        else:                                        # 既定: cmdline に "_agent" を含むものを拾い
            o = subprocess.run(['pgrep', '-f', '_agent'],  # comm で最終判定(下の name_match)
                               stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            pids |= set(o.stdout.split())
    except OSError:
        return {}, {}
    if not pids:
        return {}, {}
    m, load = {}, {}
    try:
        # ★ -ww: 幅無制限で command を出す。macOS の ps はパイプ時に既定幅(~80桁)で切り詰めるため、
        #   agent のパスが長いと末尾の "op file line" が落ちて _info が ('?','?','?') を返す
        #   (Linux は /proc/cmdline 直読みで切れないので発生しない)。
        #   実行体名(command の argv0=f[2] basename)で name_match して pgrep -f の巻き込みを除く。
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
            if not name_match(f[2]):
                continue   # 実行体名が対象でない(pgrep -f の巻き込み)→ 除外
            info = _info(f[2:])
            if info == ('?', '?', '?'):
                continue   # command が取れない過渡プロセス(ゾンビ/exec 前) → 除外
            m[pid] = info
            load[pid] = cpu
    except OSError:
        pass
    return m, load

live_map = live_map_proc if HAVE_PROC else live_map_ps

def color(s, c):
    return "\033[%dm%s\033[0m" % (c, s)

def group_rows(buckets):
    """buckets: {(op,file,line): [loads]} → [(n, op, file, line, avg_load)] 個数降順。"""
    rows = []
    for (op, fn, ln), loads in buckets.items():
        n = len(loads)
        rows.append((n, op, fn, ln, (sum(loads) / n if n else 0.0)))
    rows.sort(key=lambda r: -r[0])
    return rows

def emit_section(label, buckets, col, maxrows=8):
    rows = group_rows(buckets)
    if not rows:
        return "  %s  %s\n" % (label, color("(none)", 90))
    s = "  %s\n" % label
    # ★ 桁揃えは **プレーン文字列を %-Ns で詰めてから** 色付け(色コードは可視幅 0)。
    #   `%-14s % color(...)` だと色コードが幅に数えられて端末で桁ずれするため。
    for (n, op, fn, ln, avg) in rows[:maxrows]:
        s += "    %s %-22s %5s  %s  %s\n" % (
            color("%-14s" % op, col),               # 演算名(14)
            fn[:22],                                # ファイル名(22)
            ln,                                     # 行番号(右5)
            color("%-5s" % ("×%d" % n), 90),        # 個数
            color("%4.0f%%" % avg, col))            # 負荷(%)
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
                    j = cpu_jiffies(p)
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
            na = sum(len(v) for v in active.values())     # active 数(バー塗り分け/表示で使う)
            nw = sum(len(v) for v in waiting.values())     # waiting 数
            peak_active = max(peak_active, na)
            oldest_tag, oldest_age = "", 0.0
            if first:
                op = min(first, key=lambda p: first[p])
                oldest_tag = "%s %s %s" % m.get(op, ('?', '?', '?'))
                oldest_age = t - first[op]

            if tty:
                # now バーを active(緑) / waiting(黄) で塗り分ける。比率 na:nw を live ブロックに配分。
                live   = min(now_n, barw)
                a_len  = int(round(na * live / now_n)) if now_n > 0 else 0
                a_len  = min(a_len, live)
                w_len  = live - a_len
                blocks = color("█" * a_len, 92) + color("█" * w_len, 93)   # 緑=active / 黄=waiting
                guide  = color("·" * max(0, min(peak, barw) - live), 90)
                pad    = " " * max(0, barw - live - max(0, min(peak, barw) - live))
                spark  = "".join(SPARK[min(8, int(v * 8 / hi))] for v in hist)
                el     = t - t0
                rate   = len(seen) / el if el > 0 else 0
                out = ["\033[H\033[J"]
                _scope = ",".join(TARGETS) if TARGETS else "*_agent"
                out.append(color("  agent live monitor", 96) + color("  [%s]" % _scope, 90)
                           + color("   (Ctrl-C で終了)", 90) + "\n\n")
                # now/peak は **active/全体** で表示(active=緑, 全体=シアン)。
                out.append("  now %s%s%s  [%s%s%s]   %s%s\n\n" % (
                    color("%d" % na, 92), color("/", 90), color("%d" % now_n, 96),
                    blocks, guide, pad,
                    color("█ active", 92), color("  █ waiting", 93)))   # 凡例
                out.append("  peak %s%s%s   spawned %s   ~%s/s\n\n" %
                           (color("%d" % peak_active, 92), color("/", 90), color("%d" % peak, 91),
                            color("%d" % len(seen), 94), color("%.0f" % rate, 94)))
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
                    sys.stdout.write("t=%5.2f now=%2d peak=%2d spawned=%3d active=%2d waiting=%2d\n"
                                     % (t - t0, now_n, peak, len(seen), na, nw))
                    sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        if tty:
            sys.stdout.write("\033[?25h\n")
        sys.stdout.write("\n[agentwatch] peak=%d  total spawned=%d\n" % (peak, len(seen)))

if __name__ == "__main__":
    main()
