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
    string(REPLACE "/" "_" _nm "${_src}")
    set(_stamp ${_stampdir}/${_nm}.t)
    add_custom_command(
      OUTPUT ${_stamp}
      COMMAND ${STLCPP_CMD} file ${_src} --baseheader=${CMAKE_BINARY_DIR} --header=_ts2
      COMMAND ${CMAKE_COMMAND} -E touch ${_stamp}
      DEPENDS ${_src}
      COMMENT "tscpp2: codegen ${_src}"
    )
    list(APPEND _stamps ${_stamp})
  endforeach()
  add_custom_target(${CODEGEN_TARGET} DEPENDS ${_stamps})
endfunction()
