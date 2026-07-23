#!/usr/bin/env python3
"""meshserve.py — メッシュ(STL)をブラウザでライブ表示する小さな HTTP サーバ。

Linux 側でファイルを上書きすると、ブラウザ側が mtime ポーリングで検知し、
**視点(OrbitControls の回転/ズーム)を保ったまま形状だけ差し替える**。
SMB も f3d の watch も介さないので、属性キャッシュや「再起動で視点が飛ぶ」問題が出ない。

  python3 tools/meshserve.py out.stl                 # http://<this-ip>:8088/ をブラウザで
  python3 tools/meshserve.py out.3mf --port 9000 --host 10.131.0.53

依存: Python 標準ライブラリのみ(サーバ)。three.js は CDN(unpkg)。
対応形式: STL / 3MF / PLY / AMF(拡張子で自動判別。OFF/OBJ は未対応 → STL で出すと確実)。
LAN(10.131)前提。ブラウザは Mac の Safari/Chrome で http://10.131.0.53:8088/ を開く。
"""
import argparse, os, sys, errno, socketserver, http.server

PAGE = r"""<!doctype html><html><head><meta charset="utf-8">
<title>meshserve</title>
<style>
 html,body{margin:0;height:100%;background:#1c1e22;overflow:hidden;font:12px monospace;color:#aab}
 #hud{position:fixed;left:10px;top:8px;white-space:pre;text-shadow:0 1px 2px #000;pointer-events:none}
 #help{position:fixed;left:10px;bottom:8px;white-space:pre;line-height:1.5;color:#7f8896;text-shadow:0 1px 2px #000;pointer-events:none}
 #c{display:block}
</style>
<script type="importmap">
{ "imports": {
  "three": "https://unpkg.com/three@0.160.0/build/three.module.js",
  "three/addons/": "https://unpkg.com/three@0.160.0/examples/jsm/"
}}
</script></head>
<body>
<div id="hud">meshserve: loading…</div>
<div id="help">ドラッグ=回転   右ドラッグ / 矢印キー=平行移動   スクロール・ピンチ=拡大縮小
Q / E=画面ロール   R=視点リセット   (横ドラッグ=Z軸まわりに回転)</div>
<canvas id="c"></canvas>
<script type="module">
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { STLLoader } from 'three/addons/loaders/STLLoader.js';
import { PLYLoader } from 'three/addons/loaders/PLYLoader.js';
import { ThreeMFLoader } from 'three/addons/loaders/3MFLoader.js';
import { AMFLoader } from 'three/addons/loaders/AMFLoader.js';

const hud = document.getElementById('hud');
const renderer = new THREE.WebGLRenderer({canvas:document.getElementById('c'), antialias:true});
renderer.setPixelRatio(devicePixelRatio);
const scene = new THREE.Scene();
scene.background = new THREE.Color(0x1c1e22);
const camera = new THREE.PerspectiveCamera(40, innerWidth/innerHeight, 0.01, 1e6);
camera.up.set(0, 0, 1);                       // srava は Z-up。横ドラッグ=データZ軸まわりの周回(ターンテーブル)
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.screenSpacePanning = true;          // 平行移動を画面平面で(直感的)
// 矢印キーの平行移動は「物体が押した向きに動く」(=右ドラッグと同じ向き)。OrbitControls の
// listenToKeyEvents は逆(視点が動く)なので使わず、自前で物体追従パンする。
function panObject(dx, dy){   // dx,dy = 視野高に対する割合。物体が +dx=右 / +dy=上 へ動く
  const dist = camera.position.distanceTo(controls.target);
  const scale = 2 * dist * Math.tan(camera.fov * Math.PI / 360);
  const right = new THREE.Vector3(1,0,0).applyQuaternion(camera.quaternion);
  const up    = new THREE.Vector3(0,1,0).applyQuaternion(camera.quaternion);
  const move  = new THREE.Vector3().addScaledVector(right, -dx*scale).addScaledVector(up, -dy*scale);
  camera.position.add(move); controls.target.add(move); controls.update();
}

// Q/E = 画面ロール(視線軸まわり)。R = 視点リセット。矢印 = 物体追従パン。
// 向きはドラッグ(物体が手に追従)に合わせる。preventDefault しないと Firefox の find-as-you-type が発火。
addEventListener('keydown', e => {
  const k = e.key.toLowerCase();
  const P = 0.05;
  if(k === 'q' || k === 'e'){
    e.preventDefault();
    const dir = new THREE.Vector3().subVectors(controls.target, camera.position).normalize();
    camera.up.applyAxisAngle(dir, (k === 'q' ? -1 : 1) * 0.06);   // 物体が押した向きへ回るよう反転
    controls.update();
  } else if(k === 'r' && current){
    e.preventDefault();
    camera.up.set(0, 0, 1); frameObj(current);   // Z-up に戻す
  }
  else if(e.key === 'ArrowUp')    { e.preventDefault(); panObject(0,  P); }
  else if(e.key === 'ArrowDown')  { e.preventDefault(); panObject(0, -P); }
  else if(e.key === 'ArrowLeft')  { e.preventDefault(); panObject(-P, 0); }
  else if(e.key === 'ArrowRight') { e.preventDefault(); panObject( P, 0); }
});

scene.add(new THREE.HemisphereLight(0xffffff, 0x404048, 1.1));
const key = new THREE.DirectionalLight(0xffffff, 1.4);
scene.add(key);
const mat = new THREE.MeshStandardMaterial({color:0x9aa3ad, metalness:0.15, roughness:0.55});
let current = null, firstLoad = true, lastM = null, ext = 'stl';

function resize(){ renderer.setSize(innerWidth, innerHeight); camera.aspect = innerWidth/innerHeight; camera.updateProjectionMatrix(); }
addEventListener('resize', resize); resize();

function frameObj(obj){
  const box = new THREE.Box3().setFromObject(obj);
  if(box.isEmpty()) return;
  const s = box.getBoundingSphere(new THREE.Sphere()), r = s.radius || 1;
  controls.target.copy(s.center);
  const dist = r / Math.sin(camera.fov*Math.PI/360) * 1.25;
  camera.position.copy(s.center).add(new THREE.Vector3(1,1,0.7).normalize().multiplyScalar(dist));
  camera.near = r/200; camera.far = r*200; camera.updateProjectionMatrix();
  controls.update();
}

function disposeObj(o){ o.traverse(n => { if(n.geometry) n.geometry.dispose(); }); }

function triCount(o){ let n=0; o.traverse(c => { if(c.geometry && c.geometry.getAttribute('position')) n += c.geometry.getAttribute('position').count/3; }); return n|0; }

async function loadMesh(){
  const buf = await (await fetch('/mesh?t='+Date.now())).arrayBuffer();
  let obj;
  if(ext === '3mf')      obj = new ThreeMFLoader().parse(buf);
  else if(ext === 'amf') obj = new AMFLoader().parse(buf);
  else {                                              // stl / ply → BufferGeometry
    const geo = (ext === 'ply' ? new PLYLoader() : new STLLoader()).parse(buf);
    geo.computeVertexNormals();
    obj = new THREE.Mesh(geo, mat);
  }
  if(current){ scene.remove(current); disposeObj(current); }
  current = obj; scene.add(obj);
  if(firstLoad){ frameObj(obj); firstLoad = false; }
  hud.textContent = 'meshserve  ' + triCount(obj) + ' tris  (' + ext + ')  updated ' + new Date().toLocaleTimeString();
}

async function init(){
  try {
    const name = await (await fetch('/name')).text();
    const dot = name.lastIndexOf('.');
    if(dot >= 0) ext = name.slice(dot+1).toLowerCase();
  } catch(e){}
  setInterval(async () => {
    try {
      const m = await (await fetch('/mtime?t='+Date.now())).text();
      if(m !== lastM){ lastM = m; await loadMesh(); }
    } catch(e){ hud.textContent = 'meshserve: ' + e; }
  }, 1000);
}
init();

(function animate(){ requestAnimationFrame(animate); controls.update(); key.position.copy(camera.position); renderer.render(scene, camera); })();
</script>
</body></html>"""


class Handler(http.server.BaseHTTPRequestHandler):
    mesh_path = None
    protocol_version = "HTTP/1.1"   # keep-alive: 毎秒のポーリングで接続を乱立させない

    def _send(self, code, ctype, body):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        p = self.path.split("?")[0]
        if p == "/":
            self._send(200, "text/html; charset=utf-8", PAGE.encode())
        elif p == "/name":
            self._send(200, "text/plain", os.path.basename(self.mesh_path).encode())
        elif p == "/mtime":
            try:
                m = "%.3f" % os.path.getmtime(self.mesh_path)
            except OSError:
                m = "0"
            self._send(200, "text/plain", m.encode())
        elif p == "/mesh":
            try:
                with open(self.mesh_path, "rb") as f:
                    data = f.read()
            except OSError:
                data = b""
            self._send(200, "application/octet-stream", data)
        else:
            self._send(404, "text/plain", b"not found")

    def log_message(self, *a):
        pass


class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True   # SO_REUSEADDR は bind 前=クラス属性で立てる(TIME_WAIT 残でも再起動可)
    daemon_threads = True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mesh", help="表示する STL/3MF/PLY/AMF ファイル(上書きで自動リロード)")
    ap.add_argument("--host", default="0.0.0.0", help="bind アドレス(既定 0.0.0.0=このマシンの全IF)")
    ap.add_argument("--port", type=int, default=8088)
    a = ap.parse_args()
    Handler.mesh_path = os.path.abspath(a.mesh)
    try:
        httpd = Server((a.host, a.port), Handler)
    except OSError as e:
        if getattr(e, "errno", None) == errno.EADDRINUSE:
            sys.stderr.write(
                "meshserve: ポート %d は使用中です。\n"
                "  既存を止める:  pkill -f meshserve.py\n"
                "  別ポートで:     python3 %s '%s' --port %d\n"
                % (a.port, sys.argv[0], a.mesh, a.port + 1))
            raise SystemExit(1)
        raise
    shown = a.host if a.host != "0.0.0.0" else "10.131.0.53"
    print("meshserve: %s" % Handler.mesh_path)
    print("  Mac のブラウザで  http://%s:%d/  を開く" % (shown, a.port))
    print("  Linux 側で上書きすると視点を保ったまま自動リロード。Ctrl-C で停止。")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
