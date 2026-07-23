


pigData.h



typedef INTEGER64 pHashKeyType;

class pigInfo : public stdObject {
public:
  pigInfo(sPtr<stdString> _filename,int _lineno);
protected:
  sPtr<stdString> filename;
  int lineno;
};

// 実行シーケンス用のローカル変数を保持する環境
// データの取得は、parent に伝播する。
// 定義されていない変数に対するアクセスはエラーとなる。
class pigEnvironment : public stdObject {
public:
  pigEnvironment(sPtr<pigEnvironment> p) {
    parent = p;
  }
  // 変数定義
  sPtr<pigData> def_var(const char *);
  sPtr<pigData> def_var(sPtr<stdString>);
  sPtr<pigData> def_var(sPtr<pigData>);

  // 変数取得
  sPtr<pigData> get_var(const char *);
  sPtr<pigData> get_var(sPtr<stdString>);
  sPtr<pigData> get_var(sPtr<pigData>);

  // 変数更新
  sPtr<pigData> set_var(const char *,sPtr<pigData>);
  sPtr<pigData> set_var(sPtr<stdString>,sPtr<pigData>);
  sPtr<pigData> set_var(sPtr<pigData>,sPtr<pigData>);
protected:
  sPtr<pigEnvironment> parent;
  // ハッシュ構造
};

class pigData : public stdObject {
public:
  pigData(sPtr<pigInfo> _info) {
    info = _info;
  }
  virtual sPtr<pigData> copy();

  virtual sPtr<pigData> add(sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (add)",info));
  }
  virtual sPtr<pigData> sub(sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (sub)",info));
  }
  virtual sPtr<pigData> mul(sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (mul)",info));
  }

  virtual sPtr<pigData> div(sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (div)",info));
  }

  virtual sPtr<pigData> rem(sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (div)",info));
  }

  // 算術論理演算系
  virtual sPtr<pigData> minus() {
    return thNEW(pigDataError,("unsupport function (minus)",info));
  }

  virtual sPtr<pigData> aand(sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (aand)",info));
  }

  virtual sPtr<pigData> aor(sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (aor)",info));
  }

  virtual sPtr<pigData> axor(sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (axor)",info));
  }

  virtual sPtr<pigData> anot() {
    return thNEW(pigDataError,("unsupport function (anot)",info));
  }

  // boolean論理演算系
  virtual sPtr<pigData> b_and(sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (and)",info));
  }

  virtual sPtr<pigData> b_or(sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (or)",info));
  }

  virtual sPtr<pigData> b_xor(sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (xor)",info));
  }

  virtual sPtr<pigData> b_not() {
    return thNEW(pigDataError,("unsupport function (not)",info));
  }

  virtual INTEGER64 get_int();
  virtual double get_flt();

  // flag = 0 pigDataDelay による遅延を展開する。
  // flag = 1 pigDataDelay による遅延を展開しない。
  // env はget_str の書式の定義、当面はthNULL;
  virtual sPtr<stdString> get_str(int flag=0,sPtr<stdObject> env=thNULL);

  virtual sPtr<pigData> get_ix(int) {
    return thNEW(pigDataError,("unsupport function (index)",info));
  }

  virtual sPtr<pigData> get_ix(char *) {
    return thNEW(pigDataError,("unsupport function (index)",info));
  }

  virtual sPtr<pigData> get_ix(sPtr<stdString>) {
    return thNEW(pigDataError,("unsupport function (index)",info));
  }
  virtual sPtr<pigData> get_ix(sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (index)",info));
  }



  // インデックスに値をセットする。
  virtual sPtr<pigData> set_ix(int,sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (index)",info));
  }
  virtual sPtr<pigData> set_ix(char *,sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (index)",info));
  }
  virtual sPtr<pigData> set_ix(sPtr<stdString>,sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (index)",info));
  }
  virtual sPtr<pigData> set_ix(sPtr<pigData>,sPtr<pigData>) {
    return thNEW(pigDataError,("unsupport function (index)",info));
  }

  // pigDataDelay を展開する
  virtual sPtr<pigData> compact() {
    return thThis;
  }

  virtual pHashKeyType get_hashkey() {
    sPtr<stdString> d = get_str();
    return ハッシュ計算(d->get_str());
  }

  virtual sPtr<pigData> car() {
    return thNULL;
  }
  virtual sPtr<pigData> cdr() {
    return thNULL;
  }
		 
  sPtr<pigData> p_add(INTEGER64) {
    return thNEW(pigDataError,("unsupport function",info));
  }
  sPtr<pigData> p_add(double) {
    return thNEW(pigDataError,("unsupport function",info));
  }
  sPtr<pigData> p_add(sPtr<stdString>) {
    return thNEW(pigDataError,("unsupport function",info));
  }
  // 配列演算
  sPtr<pigData> p_add(sArray<pigData>&) {
    return thNEW(pigDataError,("unsupport function",info));
  }
  
  //  ...

  virtual int is_compact() {
    return 1;
  }
  sPtr<pigInfo> get_info() {
    return info;
  }
protected:
  
  sPtr<pigInfo> info;
};


class pigDataInteger : public pigData {
protected:
  INTEGER64 d;
};

class pigDataFloat : public pigData {
protected:
  double d;
};

class pigDataString : public pigData {
 protected:
  sPtr<stdString> d;
};

class pigDataNull : public pigData {
 protected:
};

class pigDataError : public pigData {
  pigDataError(sPtr<stdString> msg,sPtr<pigInfo> info);
 protected:
  sPtr<stdString> msg;
};

class pigDataControl : public pigData {
public:
  pigDataDataControl(int t,sPtr<pigData> v) {
    type = t;
    value = v;
  }
 protected:
  int type;
#define PIG_CONTROL_TYPE_RETURN 0
#define PIG_CONTROL_TYPE_BREAK 1
#define PIG_CONTROL_TYPE_CONTINUE 2
#define PIG_CONTROL_TYPE_CACHE 3
  sPtr<pigDatA> value;
};

class pigDataArray : public pigData {
 protected:
  sArray<pigDdata> d;
};

class pigDataHash : public pigData {
protected:
};

class pigDataLambda : public pigData {
public:
  pigDataLambda(sArray<stdString>& args,sPtr<pigData> body);
protected:
  sArray<stdString> args;
  sPtr<pigData> body;
};

class pigDataCache : public pigData {
 public:
  pigDataCache(sPtr<ptsObject> _creator,pHashKeyType hk) {
    hashkey = hk;
    creator = _creator;
  }
  sPtr<pigData> copy() {
    return thThis; // pigDataCache はハッシュの重複する複数オブジェクトを作らないようにする。
  }
  virtual pHashKeyType get_hashkey() {
    if ( err != thNULL )
      return 0;
    return hashkey;
  }
  void errorFile(sPtr<pigDataError> _err) {
    err = _err;
    // errorFile をした後は、 invoke_listen をする必要がある。
    // creator->invoke_listen([this](int*tp){
    //   return tp ? *tp=TSE_UPDATED,thNULL : thNEW(stdEvent,(TSE_UPDATED,ifThis,thNULL));
    // });
  }
  void listen(sPtr<tinyState> listener) {
    _creator->listen(listener,TSE_UPDATED);
  }
 protected:
  pHashKeyType hashkey;
  sPtr<pigDataCache> list_next; // 同一キャッシュを作らないようにグローバル領域ptsApplication 上のリストで管理
  sPtr<ptsObject> creator;
};

class pigDataPair : public pigData {
public:
  pigDataPair(sPtr<pigData> __car,sPtr<pigData> __cdr) {
    _car = __car;
    _cdr = __cdr;
  }
  virtual sPtr<pigData> car() {
    return _car;
  }
  virtual sPtr<pigData> cdr() {
    return _cdr;
  }
		 
protected:
  sPtr<pigData> _car;
  sPtr<pigData> _cdr;
};

class pigDataDelay : public pigData {
 public:
  pigDataDelay() {
  }
  pigDataDelay(sPtr<ptsObject> _helper) {
    helper = _helper;
    start();
  }
  virtual sPtr<pigData> add(sPtr<pigData> inp) {
    preprocess();
    return result->add(inp);
  }
  virtual sPtr<pigData> p_add(INTEGER64 d) {
    preprocess();
    return result->p_add(d);
  }
  virtual sPtr<pigData> compact() {
    preprocess();
    return result;
  }
  void set_result(sPtr<pigData> r,int flag=0) {
    if ( flag && result != thNULL )
	return;
    result = r;
    helper->invoke_listen([this](int*tp){
      return tp ? *tp=TSE_UPDATED,thNULL : thNEW(stdEvent,(TSE_UPDATED,ifThis,thNULL));
    });
  }
  virtual int is_compact() {
    if ( result == thNULL ) {
      start();
      return 0;
    }
    return result->is_compact();
  }
 protected:
  void start() {
    if ( start_flag == 0 ) {
      start_flag = 1;
      _start(); // launch up helper
    }
  }
  void preprocess() {
    start();
    if ( result == thNULL ) {
      helper->listen(sCallSection::key->caller(),TSE_UPDATED);
      throw sException([this](sPtr<tinyState> caller) {
	  return 1;
	});
    }
  }
  virtual void _start();

  
  unsigned start_flag:1;
  sPtr<pigData> result;
  sPtr<ptsObject> helper;
};


class pigDataOperator : public pigDataDelay {
 public:
  void pushArg(sPtr<pigData> a) {
    args[args.length()] = a;
  }
  sPtr<pigData> get_arg(int ix) {
    if ( ix < 0 || ix >= args.length() )
      return thNEW(pigDataError,("index over run",info));
    return args[ix];
  }
  int get_argsSize() {
    return args.length();
  }
 protected:
  sArray<sPtr<pigData> > args;
};


// 状態を持たない関数　Add Sub 等　は、pigDataOperator の_start を上書きするだけで
// 機能を実現できる。

class pigDataOperatorAdd : public pigDataOperator {
 public:
 protected:
  virtual void _start() {
    int i;
    sPtr<pigData> acc;
    acc = args[0];
    for ( i = 1 ; i < args.length() ; i ++ )
      acc = acc->add(args[i]);
    result = acc; // pigDataDelay だが、四則演算の場合は実際はdelay はおこらない。
  }
};


class pigDataOperatorReturn : public pigDataOperator {
 public:
 protected:
  virtual void _start() {
    if ( args.length() == 0 )
      result = thNEW(pigDataControl,(PIG_CONTROL_RETURN,thNEW(pigDataNull,())));
    else result = thNEW(pigDataControl,(PIG_CONTROL_RETURN,args[0]));
  }
};

class pigDataOperatorVariable : public pigDataOperator {
 public:
 protected:
  virtual void _start() {
    if ( args.length() == 0 )
      result = thNEW(pigDataError,("1 argument is required",get_info()));
    else result = sPtr<pigfFunction>::d_cast(sCallSection::key->caller())->get_env()
	   ->get_var(args[0]->get_str());
  }
};

// pigDataOperatorSub, pigDataOperatorMul, pigDataOperatorDiv, ...

// 状態遷移を持つ関数　Sq(シーケンス) For, While あるいは、cgal agent
// これらは、状態を制御する pigfFunction = tinyState helper クラスを与えるテンプレート。
// pigfFunction は、pigDataFunction::_start() により、生成起動される。
// 全ての状態遷移が終わった時点で、result へ終了コードをセットし、TSE_UPDATED 処理する。
// pigfAgent は例外で、ハッシュ計算が終了した時点で、result = pigDataCache
// その後もagentの処理が続く。ディレイが、result へ持ち越される形。
//    以下、pigfAgent の項参照

// pigDataFunction<pigfSequence>, pigDataFunction<pigfIf>, ....

// pigfFunction の派生クラスの例

// pigfSequence
    // pigfSequence は、tinyState class のシーケンサー args を順番に評価していく。
    // args[args.length()-1] の評価結果をresult へセット。TSE_UPDATED処理。

// pigIf, pigFor, pigWhile
//     条件処理を伴うシーケンスに関しては、条件値を得るまでブロックする。
//     ループを伴うシーケンスは、大量のワーカー(pigfAgent)を生成する可能性がある。
//     いまのところ制御しないが、将来的にはワーカー数制御のため、一時的にループを
//     ブロックする必要がでてくるかもしれない。

// pigfAgent
    // CGAL mesh surface の計算のためのagent を起動するクラス
    // 演算子＋args のハッシュ(args[i]->get_hashkey() )　全体を連結したバイナリのハッシュを結果ハッシュとする。
    //    キャッシュのハッシュは、キャッシュの内容ではなく、なんの演算の結果かを示す。
    //    同一演算、同一引数の演算結果は常に同じである前提。
    // 結果ハッシュのキャッシュファイルがすでに存在する場合は、
    //    このファイルがデータキャッシュの場合、中身を読み出し、それを戻り値として、終了
    //    ファイルがCGAL mesh バイナリの場合は、結果ハッシュのpigDataCache を生成、戻り値とする。
    // キャッシュファイルがない場合は計算を実行する。結果は、結果ハッシュ値のキャッシュファイルに保存。
    // agentから、ファイル生成のメッセージが来た段階で、結果ハッシュのpigDataCache を戻り値とする。
    // pigfAgent は戻り値を返した後も、agent が完全に終了するまでは、終了しない。
    // 演算子ごとに、pigfAgent を継承したクラスを作る。
    // 演算子：　bool AND,OR,SUB,XOR, 標準図形生成, 移動回転（マトリックス適用）
    //         2次元図形・パスからの3次元図形生成
    //         重心、堆積、表面積　計算

// pigfApply
//     pigDataLambda を実行する。
//     args[0] == pigDataLambda を与える。
//     args[1....] = 引数とする。


class pigDataFunction_b : public pigDataOperator {
public:
protected:
};

template<class __TYPE>
class pigDataFunction : public pigDataFunction_b {
public:
protected:
  virtual void _start() {
    helper = thNEW(__TYPE,(sPtr<pigfFunction>::d_cast(sCallSection::key->caller()),thThis));
  }
};

// pigDataFunctionFor, pigDataFunctionWhile, pigDataFunctionIf, pigDataFunctionSwitch

/**********************************************************************/
// pig　系のtinyState の共通祖先
class ptsObject_ : tinyState_ {
};


/**********************************************************************/
// pigDataFunction 用 状態遷移機械例
class pigfFunction_ : ptsObject_ {
  pigfFunction_(sPtr<pigFunction_> parent,sPtr<pigDataFunction_b> front);
  pigfFunction_(sPtr<ptsObject_> parent);  // global function
public:
  // シーケンス実行のための環境
  sPtr<pigEnvironment> env;
protected:
  sArray<sPtr<pigData> > args;
};



TS_STATE(INI_START)
{
  // args 内容をコピー
  int i;
  args.length(front->argsSize());
  for ( i = 0 ; i < front->argsSize() ; i ++ ) {
    args[i] = front->get_arg(i);
  }
  // 親の環境がそのまま見える設定
  env = parent->env;
  // 自分のローカル環境を設置するやりかた。上位のクラスで実行
  // env = thNEW(pigEnvironment,(env));
  return rDO|INI_pigfFunction_START;
}
TS_STATE(INI_pigfFunction_START)
{
  return rDO|ACT_START;
}


TS_STATE(FIN_START)
{
  return rDO|FIN_pigFunction_START;
}
TS_STATE(FIN_pigFunction_START)
{
  front->set_result(thNEW(pigDataError,("return value is required for function",front->get_info())),1);
  return rDO|FIN_TINYSTATE_START;
}


/**********************************************************************/
// pigfAgent
class pigfAgent_ : public pigfFunction_ {
public:
  pigfAgent(sPtr<pigfFunction_> parent,
	    sPtr<pigDataFunction<pigfAgent> front,
	    const * operator_str);
protected:
  sPtr<ts2System> agent;
  sPtr<pigDataError> err;
  sArray<sPtr<pigData> > alreadySend;
  int alreadySend_count;
  sPtr<pigDataDelay> delayed;
};



TS_STATE(INI_pigfFunction_START)
{
  return rDO|INI_pigfAgent_START;
}
TS_STATE(INI_pigfAgent_START)
{
  return rDO|ACT_START;
}

TS_STATE(ACT_START)
{
for ( i = 0 ; i < args.length() ; i ++ )
   if ( is_pigDataType(pigDataError,args[i]) ) {
      // エラー発見
     front->set_result(args[i]);
     return rDO|FIN_START;
   }
// すべての引数にエラーがないことが分かった時点で、compact は確定している。

 for  ( i = 0 ; i < args.length() ; i ++ )
    if ( sPtr<pigDataPair>::d_cast(args[i]) != thNULL )
      break;
  if ( i == args.length() ) {
    // delay がない場合
    // 結果ハッシュ値の計算 = 演算子名 引数のハッシュ値 の列のハッシュ値
    //  結果ハッシュ値のファイルが存在するかチェック。
    if ( 存在する ) {
      if ( 中身はデータキャッシュか )
	front->set_result(中身);
      else
	front->set_result(thNEW(pigDdataCache,(ifThis,結果ハッシュ値)));
      return rDO|FIN_START;
    }
  }
  // 戻り値として、delayed 通知をする。　コンティニュエーションチック
  delayed = thNEW(pigDataDelay,(ifThis));
  front->set_result(thNEW(pigDataPair,(thNEW(pigDataString,("delayed")),delayed)));
  // ts2System にて、srava-agent を起動。
  return rDO|ACT_1;
}
TS_STATE(ACT_1)
{
  // ts2Parallel を活用して書き換えよう　2026.04.02 ひさ
  if ( /*処理中に、srava-agent が死亡、あるいは、*/is_destroyed() == true ) {
    err = thNEW(pigDataError,("aborted",front->get_info()));
    return rDO|ACT_ERROR;
  }
  for ( i = 0 ; i < args.length() ; i ++ ) {
    if ( alreadySend[i] != thNULL )
      continue;
    if ( !is_pigDataType(pigDataPair,args[i]) )
      alreadySend[i] = args[i];
    else if ( !args[i]->cdr()->is_compact() )
      continue;
    else alreadySend[i] = args[i]->cdr(); // is_compact() = true なのでこれでいい
    if ( is_pigDataType(pigDataError,alreadySend[i]) ) {
      err = alreadySend[i];
      return rDO|ACT_ERROR;
    }
    // alreadySend[i] をsrava-agent へ送信
    alreadySend_count ++;
  }
  if ( alreadySend_count < args.length() )
    return 0;
  // srava-agent へ引数送信完了通知
  // 全ての引数がそろう。
  return rDO|ACT_2;
}

TS_STATE(ACT_2)
{
  // 計算終了待ち
  if ( 処理中に、srava-agent が死亡、あるいは、is_destroyed() == true ) {
    err = thNEW(pigDataError,("aborted",front->get_info()));
    return rDO|ACT_ERROR;
  }
  if ( !(agent 計算終了、データ(キャッシュ)保存開始) )
    return 0;
  if ( 計算結果はデータキャッシュか？ )
    delayed->set_result(キャッシュファイルの中身);
  else delayed->set_result(thNEW(pigDdataCache,(ifThis,結果ハッシュ値)));
  return rDO|ACT_3;
}
TS_STATE(ACT_3)
{
  // agent 終了待ち
  if ( agent 正常or異常終了 )
    return rDO|ACT_FINISH;
  return 0;
}


TS_STATE(ACT_ERROR)
{
  // 結果キャッシュファイルが残っていたら、削除処理
  delayed->set_result(thNEW(pigDataError,("aborted",front->get_info())));
  return rDO|FIN_START;
}


TS_STATE(ACT_FINISH)
{
  return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
  return rDO|FIN_pigfAgent_START;
}
TS_STATE(FIN_pigfAgent_START)
{
  agent->destroy();
  return rDO|FIN_pigfFunction_START;
}



/**********************************************************************/
// pigfSequence

class pigfSequence_ : public pigfFunction_ {
public:
  pigfSequence(sPtr<pigfFunction_> parent,
            sPtr<pigDataFunction<pigfAgent> front,
            const * operator_str);
protected:
  int i;
};



TS_STATE(INI_pigfFunction_START)
{
  i = 0;
  return rDO|INI_pigfSequence_START;
}
TS_STATE(INI_pigfSequence_START)
{
  return rDO|ACT_START;
}

TS_STATE(ACT_START)
{
  if ( i >= args.length() )
    return rDO|FIN_START;
  return rDO|ACT_WAIT;
}
TS_STATE(ACT_WAIT)
{
  if ( args[i]->is_error() ) {
    front->set_result(args[i]);
    return rDO|FIN_START;
  }
  i ++;
  return rDO|ACT_START;
}

TS_STATE(FIN_START)
{
  return rDO|FIN_pigfSequence_START;
}
TS_STATE(FIN_pigfSequence_START)
{
  front->set_result(args[args.length()-1]);
  return rDO|FIN_pigfFunction_START;
}


/**********************************************************************/
// pigfAssign
// 引数は最大２つ。代入先変数名（テキスト）と、代入値
// 代入値を省略すると、def_var 変数定義の意味になる。
// 代入先変数名は、compact に解決されなければならない。
// 一方で、代入値は、compact化はしない。＝＞実際に必要になるまで遅延される。

class pigfAssign_ : public pigfFunction_ {
public:
  pigfAssign(sPtr<pigfFunction_> parent,
            sPtr<pigDataFunction<pigfAgent> front,
            const * operator_str);
protected:
  int i;
};



TS_STATE(INI_pigfFunction_START)
{
  return rDO|INI_pigfSequence_START;
}
TS_STATE(INI_pigfSequence_START)
{
  return rDO|ACT_START;
}

TS_STATE(ACT_START)
{
  if ( args[0]->is_error() )
    return rDO|FIN_START;
  if ( args.length() == 1 )
    env->def_var(args[0]->get_str());
  else env->set_var(args[0]->get_str(),args[1]);
  return rDO|FIN_START;
}

TS_STATE(FIN_START)
{
  return rDO|FIN_pigfSequence_START;
}
TS_STATE(FIN_pigfSequence_START)
{
  front->set_result(args[0]);
  return rDO|FIN_pigfFunction_START;
}


