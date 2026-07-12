# NEXT_PLAN — ext-lsparrot 開発引き継ぎメモ

このドキュメントは、これまでの大規模改修(バグ修正・機能追加・性能改善)で得られた
**アーキテクチャ上の知識・設計判断・落とし穴・残タスク** を、
セッションやモデルが変わっても失われないようにまとめたものです。
コードを触る前に必ず一読してください。

---

## 1. アーキテクチャ全体像(ファイル → 責務)

| ファイル | 責務 |
|---|---|
| `php_lsparrot.c` | トークナイザ(`lsp_lsparrot_tokens_to_zval`: **テキストポインタ同一性で8スロットキャッシュ**)、AST パース、token name テーブル |
| `lsp_server.c` | サーバーライフサイクル(init → loop → teardown)。**teardown で各キャッシュ clear を呼ぶ**(brace / resolve / composer / semanticTokens) |
| `lsp_server_protocol.c` | initialize(capabilities)、ディスパッチテーブル `lsp_server_handle`、documentSymbol(outline キャッシュ付き)、workspace/symbol(上限256・プロジェクト優先)、foldingRange、selectionRange、documentLink |
| `lsp_server_documents.c` | 非ブロッキングトランスポート(poll + pending queue、`$/cancelRequest`、didChange 合流)、キャッシュ退避、`lsp_reap_analyzer_jobs`(全バックグラウンド処理のポンプ)、`lsparrot.php/status` |
| `lsp_server_core.c` | オプション解析(`lsp_options_from_zval` / **ライブ反映は `lsp_options_apply_runtime`**)、UTF-16⇔バイト変換、document 破棄 |
| `lsp_server_index.c` | シンボルインデックス構築(**マルチルート・並列ワーカー・.lsparrot キャッシュ+署名検証**)、増分更新、アナライザ検出 |
| `lsp_server_symbols.c` | インデックスキー、import 収集(AST+token fallback、**'R' プレフィクスの逆引きキー**)、`lsp_document_namespace_at`(**オフセット対応名前空間**) |
| `lsp_server_phpdoc.c` | `lsp_resolve_class_name(_at)`(**メモ化キャッシュはファイル単位。_at は複数 namespace ファイルのみ非キャッシュ経路**) |
| `lsp_server_inference.c` | 型推論(パラメータ型 → phpdoc → 配列アクセス → メソッド呼び出し代入 → **プロパティ fetch 代入** → new → 外部呼び出し → foreach)。`lsp_infer_receiver_class` は **extern** |
| `lsp_server_member_cache.c` | クラスメンバーキャッシュ(path+mtime+documentVersion で鮮度判定)、トレイト収集(**`_in_bounds` でクラス本体スコープ**) |
| `lsp_server_definition.c` | definition / typeDefinition(**マルチクラス対応: `lsp_find_class_header_for_name` 優先**) |
| `lsp_server_hierarchy.c` | callHierarchy / typeHierarchy(名前+呼び出し形マッチ、`lsp_hier_enclosing_decl` の1パス フレームスタック) |
| `lsp_server_workspace.c` | マルチルート管理(`lsp_workspace_roots_each`)、willRenameFiles(クラス名リネーム+PSR-4 namespace 書き換え) |
| `lsp_server_refactor.c` | rename(**型追跡ファミリー限定**: `lsp_refactor_add_method_rename_changes`)、references/highlight(スコープ対応)、inlayHint(パラメータ名+**変数型、range 限定・上限64**) |
| `lsp_server_formatting.c` | トークンベース整形(行プラン方式: LOCKED/CODE/COMMENT/BLANK。**文字列・heredoc・inline HTML は不変**。`endif` 系は trim-only fallback) |
| `lsp_server_semantic_tokens.c` | full / range / **full/delta**(uri キーの静的キャッシュ、didClose/didSave で evict)、modifier 4種 |
| `lsp_server_analyzers.c` | composer.json デコードキャッシュ(stat 署名)、**プロジェクト別ジョブテーブル** `lsp_analyzer_job_slot`、診断組み立て(severity / column / DiagnosticTag) |
| `lsp_server_phpstan.c` / `lsp_server_psalm.c` | CLI アナライザ起動(pending starter が **worker 数を上限に並列起動**) |
| `lsp_server_psalm_ls.c` | psalm-language-server プロキシ(**クラッシュ時 5 秒バックオフ・最大3回再起動、didOpen 再送**) |
| `lsp_server_runner.c` | 常駐ランナー(型クエリの遅延実行、**アイドル300秒で回収**、SIGTERM→SIGKILL エスカレーション) |

## 2. 絶対に守るべき不変条件・落とし穴

1. **シェル cwd**: テスト実行は必ず `cd /home/user/ext-lsparrot/ext && make test NO_INTERACTION=1 TESTS="tests"`。cwd はコマンド間でリセットされることがある。
2. **config.m4 / config.w32 に .c を追加したら必ず `phpize && ./configure --enable-lsparrot`**。make だけでは新ファイルがリンクされず、`.so` は**undefined symbol で実行時に死ぬ**(ビルドは通る)。
3. **トークンキャッシュはテキストの「ポインタ同一性」でヒット**する。`document->text` を差し替えると自然に無効化される。この規約に合わせ、outline キャッシュ・semanticTokens delta キャッシュもテキスト世代 / uri キーで無効化している。
4. **シンボルインデックスの検索はワーカー join が前提**。インデックスを引く新ハンドラは必ず最初に `lsp_index_join_worker(server)` を呼ぶこと(過去に hover が NULL を返す原因になった)。
5. **`lsp_resolve_class_name` のメモ化キー(text, base)は名前空間を含まない**。オフセット依存の解決が必要な場面(複数 namespace ファイル)は必ず `_at` を使う。`_at` は単一 namespace ならキャッシュ経路へ委譲する。
6. **宣言名の解決(`lsp_class_declared_name`)は import を先に見る**。PHP では import と同短名のクラス宣言は fatal なので実害はないが、挙動として知っておくこと。
7. **fork ワーカー**(インデックス並列化・アナライザ)は `_exit()` で終了し Zend の shutdown を走らせない。valgrind で子プロセスに 21,728B の "definitely lost" が出るのは**設計どおり**(親プロセスが 0 であることだけ確認する)。
8. **part ファイル名は fork 前に親の pid で確定させる**(子で `getpid()` すると親のマージが見つけられず全滅→直列フォールバックで倍遅くなる。実際に踏んだバグ)。
9. **`textDocumentSync.change=1`(Full)**: range 付き didChange は黙って破棄する仕様。incremental 対応するならこの防御と同時に変えること。
10. **PHPT の EXPECTF は capabilities の形状に敏感**(test 100 は semanticTokensProvider の形、117 は「未知メソッド」プローブに実在メソッドを使わないこと)。capability を追加・変更したら 100 / 117 / 125 / 127 を確認。
11. **`lsp_process_wait` は無タイムアウトでブロックする**。プロセス回収は必ず `lsp_process_wait_timeout` + `lsp_process_terminate_force` のエスカレーションを使う(サーバーは単一スレッド。1個の wedged プロセスで全機能が止まる)。
12. **診断 severity のテスト**(053/099 など)はクライアント入力側の値。サーバー出力の severity を変えても壊れないが、grep で確認してから触ること。

## 3. 検証インフラ(再利用すべき資産)

- **PHPT**: `ext/tests/` 001–130(130 = hierarchy/multi-root/willRename)。ハーネスパターンは 122 以降のテストからコピーする(proc_open `php -n -d extension=...`、Content-Length フレーミング)。
- **QA スクリプト群**(scratchpad、コンテナ消滅で失われる — パターンだけ覚えておく): `harness.inc` + fixture 生成 + `lsp_position_after`。アナライザ統合の検証は**偽の phpstan/psalm バイナリ(JSON を吐くシェルスクリプト)**を vendor/bin に置く方式が有効だった。
- **ベンチマーク手法**: 1セッション内で連続リクエストし、リクエスト毎に microtime 計測(cold/warm 区別)。合成コーパスは classmap+psr4 混在・継承3段・~900クラス+3000行ホットファイル。実測値: 補完 warm 2–4ms、hover 1–4ms、documentSymbol 23ms(現在はキャッシュで2回目以降ほぼ0)、リネーム82ファイル 23ms、初期インデックス 2000ファイル 0.37s(4コア並列)。
- **valgrind**: サーバープロセスに直接パイプする(`valgrind php -n -d extension=... run.php < input.bin`)。`--trace-children=yes` は fork ワーカーのノイズを含むので注意。

## 4. 残タスク(優先度順・実装ポインタ付き)

### P1 — タイピング体験のスケーリング(最大の構造的リスク)
- **毎キーストロークの全文再トークナイズ+全文 zend_compile**(`lsp_document_analyze` → `lsp_lsparrot_parse_to_zval_ex`, `php_lsparrot.c:443-504`)。現状 3000 行で ~5ms と余裕だが O(ファイルサイズ)。
  - 案A(小): 直前テキストとの差分が空白/コメントのみなら AST 再コンパイルをスキップ。
  - 案B(中): incremental sync(change=2)対応でペイロードとコピーを削減(§2-9 の防御と同時に)。
  - 案C(大): 差分領域だけ再レックスして token 配列をスプライス。
- **didChange 毎の completion_cache 全走査 strstr 退避**(`lsp_server_documents.c:22-43`)。URI 毎のサブテーブル化で O(1) に。

### P2 — LSP 機能の残り
- **pull 診断**(`textDocument/diagnostic` / `workspace/diagnostic`): 診断は既にキャッシュされているので配線は小さい。プロジェクト全体の Problems パネル(PhpStorm 相当)は workspace/diagnostic が本命。
- **`$/progress`**: サーバーに**アウトバウンドリクエスト経路が無い**(`lsp_protocol_notify` のみ、`lsp_internal.h:901` 付近)。`window/workDoneProgress/create` を送るには request-id 管理と応答ルーティングが必要。それまでは `lsparrot.php/analyzerStatus` で代替(VSCODE_MEMO §1.1)。
- **completionItem/resolve**: 巨大プロジェクトで補完ペイロード削減。documentation を resolve 側に遅延するだけで効果あり。
- **use-import 補完のサーバー側プレフィルタ**(`lsp_server_symbols.c` ~2402-2506): 現在は全679件返してクライアント任せ。数万クラス級で支配的コストになる。segment 配列は既にソート済みなので binary search で絞れる。
- **linkedEditingRange**: 価値が低い。着手しない判断済み。

### P3 — アナライザ統合の深化
- **常駐ランナーの世代管理**: composer.lock 変更時にランナーを再起動する(autoload が古いまま型クエリに答え続ける)。`lsp_did_change_watched_files` にフックポイントあり。
- **phpstan/psalm の結果キャッシュディレクトリ**(tmpDir/cacheDir)を `.lsparrot/` 配下に明示指定して再解析を高速化。
- **baseline ファイル**(phpstan-baseline.neon / psalm-baseline.xml)はプロジェクト config 経由で暗黙尊重されるが、生成コンフィグ使用時に引き継がれるか未検証。テスト追加推奨。
- psalm-ls の `completionReady` フローを VSCode 拡張側と結合テストする(拡張側実装は VSCODE_MEMO §1.2)。

### P4 — 品質・保守
- **subtypes の全ファイル走査**(`lsp_server_hierarchy.c`)は短名 pre-filter 付きだが、巨大プロジェクトでは逆参照インデックス(extends/implements テーブルをシンボルインデックスに追加)が本筋。インデックス payload version を上げてスキーマ拡張する(`LSP_SYMBOL_INDEX_PAYLOAD_VERSION`)。
- **incomingCalls も同様**に呼び出し元インデックス化の余地(現状は 4096 ファイル/256 結果でキャップ)。
- `lsp_server_protocol.c` が肥大化(selectionRange/documentLink/folding を含む)。`lsp_server_navigation.c` 等への分割を検討。
- Windows ビルド(config.w32)は形だけ維持。`LSP_HAVE_POSIX_PROCESS=0` 経路(並列インデックス・fork ワーカー無し)は未テスト。
- テストの共通ハーネス関数が各 .phpt に複製されている。`lsp_test_helper.inc` に集約するリファクタ(EXPECTF に影響しないよう注意)。
- **レビューで確認済み・意図的に見送った低優先度項目**(着手時はこのリストから):
  - psalm-ls 再起動時の didOpen リプレイは「開いている全ドキュメント」を新セッションへ送るが、送信対象の判定(`lsp_psalm_ls_document_open` のスコープ判定)と再スキャン時の判定に使う述語が完全一致ではない。ルート境界ぎりぎりのファイルで片方だけ送られる可能性。述語を 1 関数に統一する。
  - `textDocument/didClose` は psalm-ls 全セッションへブロードキャストしている。該当プロジェクトのセッションだけに送るのが正しいが、余分な didClose は psalm 側で無害(未知 URI は無視される)ため見送り。
  - `semanticTokens/range` は行単位でトークンを切っており、開始/終了行の文字位置(character)までは絞らない。クライアントは範囲外トークンを無視するため実害なし。
  - `lsp_server_hierarchy.c` はリクエスト内で同一ファイルを複数回読み直す経路がある(incomingCalls で候補ファイル→確認の 2 段階)。リクエスト単位のファイル内容キャッシュを入れると大規模プロジェクトで効く。
  - `lsp_hier_enclosing_decl` のフレームに `name_offset` フィールドがあるが現在未使用(dead field)。次に触るとき削除してよい。

### P5 — 既知の意図的な非対応(再検討条件付き)
- `namespace Foo;` 宣言自体の hover(価値低)。
- フォーマッタの PSR-12 空白規則(php-cs-fixer 併用を案内。実装するなら formatting.c の行プラン方式の上にトークン間スペース規則を足す)。
- インメモリ限定リネーム後の stale 定義(実エディタは保存時にディスクへ書くため実害なし)。

## 5. 設計判断の記録(なぜそうしたか)

- **リネームのファミリー限定**は「編集は破壊的なので保守的に絞る」、**hierarchy は情報表示なので保守的に広げる**(同名候補を含める)— 意図的な非対称。
- **auto モードで psalm-ls を CLI より優先**: composer install は両方のバイナリを入れるため、両方有効化すると全診断が二重になる(実測)。明示配列指定なら併用可能。
- **workspace/symbol の上限256・プロジェクト優先**: エディタはキー入力毎に投げるため。ソート済み segment 配列があるので、更に絞るならプレフィクス binary search。
- **formatting は「行の再配置をしない」**: トークン列を保ったまま行頭空白と行末だけ触る。これにより文字列/heredoc 破壊の可能性を構造的に排除している。全面整形をやるなら別レイヤで。
- **インデックスキャッシュの署名**は全ルートのファイル(パス+size+mtime+nsec)の FNV 混合。ルート追加は署名に入るので didChangeWorkspaceFolders 後の再構築で自然に不一致→再スキャンになる。

## 6. リリース前チェックリスト

1. `cd ext && phpize && ./configure --enable-lsparrot && make -j$(nproc)`(config.m4 を触った場合は必須)
2. `make test NO_INTERACTION=1 TESTS="tests"` → 全 PASS(1 環境スキップは既知: 071 FTP)
3. valgrind: 代表セッション(補完/hover/リネーム/hierarchy/formatting)でメインプロセス 0 エラー
4. `workers.count 0/1/4` の3構成で初期インデックスとシンボル一致を確認(test 126 が自動化済み)
5. VSCODE_MEMO.md と本ファイルの「残タスク」を更新
