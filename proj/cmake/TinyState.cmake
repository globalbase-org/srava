# tinyState codegen ヘルパ(tscpp2)
#
# tinyState クラス(.cpp に CLASS_TINYSTATE を持つもの)は、コンパイル前に tscpp2 で
# 状態機械コードを生成する必要がある:
#   tscpp2 file <src.cpp> --baseheader=${CMAKE_BINARY_DIR} --header=_ts2
#   => ${CMAKE_BINARY_DIR}/_ts2/c++/<Class>_.h, <Class>_pb.h を生成
#
# 使い方:
#   tinystate_codegen(<codegen_target> <abs_src1> <abs_src2> ...)
#   add_dependencies(<your_target> <codegen_target>)
#   target には include に ${CMAKE_BINARY_DIR}(生成ヘッダ)が要る。
#
# 注: tinyState .cpp は普通に target_sources に足してコンパイルする(生成は別経路)。
#     stdObject だけの純 C++(pigData 等)は codegen 不要。

# find_package(tinyState) が見つけた package の tscpp2 を優先(別 prefix でも正しい世代を使う)。
# 未設定なら従来どおり PATH から探す。
if(TINYSTATE_TSCPP2)
  set(STLCPP "${TINYSTATE_TSCPP2}")
else()
  find_program(STLCPP tscpp2 REQUIRED)
endif()

# Windows(MinGW/MSVC): custom command は cmd.exe 経由で実行され、拡張子無しの perl スクリプト
# tscpp2 を直接起動できない(tscpp2.bat を同梱しない install prefix もある)。perl を前置して起動する。
# Cygwin は WIN32=false なので shebang がそのまま効き、この分岐に入らない。
if(WIN32)
  find_package(Perl REQUIRED)
  set(STLCPP_CMD ${PERL_EXECUTABLE} ${STLCPP})
else()
  set(STLCPP_CMD ${STLCPP})
endif()

function(tinystate_codegen CODEGEN_TARGET)
  set(_stampdir ${CMAKE_BINARY_DIR}/stamps)
  file(MAKE_DIRECTORY ${_stampdir})
  set(_stamps)
  foreach(_src ${ARGN})
    # ★ コロンも潰す: MinGW/MSYS2 では絶対パスが `C:/Users/...` なので、スラッシュだけ置換すると
    #   ドライブレターのコロンが名前に残る。NTFS では `名前:ストリーム` が代替データストリーム (ADS)
    #   の構文なので、`stamps/C:_Users_….t` への touch は **`C` という名前のファイルの ADS** になり、
    #   ninja からは stamp が「存在しない」ままになる (`ninja explain: output ... doesn't exist`)。
    #   → codegen が毎回全件再実行され、生成ヘッダの mtime 更新で依存 TU も全部再コンパイルされる
    #     (= MinGW のビルドが永遠に差分にならない)。2026-08-30 実測: stamps/ の中身が Linux 256 個に
    #     対し MinGW は `C` ひとつだけ。Cygwin は `/cygdrive/c/...` でコロンが無いので無傷だった。
    string(REGEX REPLACE "[/:]" "_" _nm "${_src}")
    set(_stamp ${_stampdir}/${_nm}.t)
    # tscpp2 は各ソースにつき ${CMAKE_BINARY_DIR}/_ts2/c++/<basename>_.h (private) と
    #   <basename>_pb.h (public base) を生成する (genOutputName: <baseheader>/<header>/c++/<basename>)。
    # ★ これらの生成ヘッダを **stamp だけでなく本物の OUTPUT として宣言**する (tinyState 上流 #3393 の修正を移植)。
    #   stamp-only OUTPUT だと生成ヘッダはビルドグラフに「見えない生成物」になり:
    #     ① クリーン並列ビルドで、生成ヘッダを include する TU が codegen 完了前にコンパイルされて
    #        "No such file or directory" で落ちる (要 add_dependencies での順序付け・下記も併用)。
    #     ② 基底クラスのメンバ変更が派生 TU を再ビルドせず silent wrong-layout バイナリを生む (#3393)。
    #   OUTPUT として宣言すると、コンパイラ depfile が記録する「どの _.h を include したか」を通じて
    #   基底変更が 1 パスで全派生を再ビルドする。★basename は全 codegen 呼び出しで一意なので
    #   (共有 pts* は pigts_codegen のみ・各カーネルは自分のクラスのみ) 重複 OUTPUT 衝突は起きない。
    get_filename_component(_base ${_src} NAME_WLE)
    set(_gen_priv ${CMAKE_BINARY_DIR}/_ts2/c++/${_base}_.h)
    set(_gen_pub  ${CMAKE_BINARY_DIR}/_ts2/c++/${_base}_pb.h)
    add_custom_command(
      OUTPUT ${_stamp} ${_gen_priv} ${_gen_pub}
      COMMAND ${STLCPP_CMD} file ${_src} --baseheader=${CMAKE_BINARY_DIR} --header=_ts2
      COMMAND ${CMAKE_COMMAND} -E touch ${_stamp}
      DEPENDS ${_src}
      COMMENT "tscpp2: codegen ${_src}"
    )
    list(APPEND _stamps ${_stamp})
  endforeach()
  add_custom_target(${CODEGEN_TARGET} DEPENDS ${_stamps})
endfunction()
