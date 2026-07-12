# VSCODE_MEMO — lsparrot-vscode-extension 側で対応が必要な項目

対象: [LSParrot/lsparrot-vscode-extension](https://github.com/LSParrot/lsparrot-vscode-extension)
サーバー側: ext-lsparrot(このリポジトリ)の現行ブランチ時点の仕様に基づく。

---

## 1. サーバー独自プロトコル(クライアント実装が必要)

### 1.1 通知 `lsparrot.php/analyzerStatus`(server → client)

アナライザの状態表示(ステータスバー推奨)に使う。ペイロード:

```jsonc
{
  "analyzer": "phpstan" | "psalm" | "psalm-ls" | "index" | "driver",
  "state": "idle" | "running" | "error",
  "message": "human readable message",
  // 任意フィールド:
  "projectRoot": "/abs/path",        // プロジェクト単位の状態通知のとき
  "driver": "lsparrot+phpstan",      // analyzer=="driver" のとき
  "label": "LSParrot Engine + PHPStan",
  "missingAnalyzer": "phpstan"       // アナライザ未検出の警告のとき
}
```

- アナライザ未導入時は同時に `window/showMessage`(type=1)も送られる。両方を重複表示しないようにする。
- psalm-language-server がクラッシュした場合、サーバーは自動で再起動を試みる(最大3回、5秒バックオフ)。`state: "error"` → `"running"`(message: "Restarting Psalm language server.")の遷移が起こり得るので、error 状態を恒久表示にしない。

### 1.2 通知 `lsparrot.php/completionReady`(server → client)

psalm-ls の遅延補完/ホバーの結果がキャッシュに載ったことを示す。ペイロードは `{ "uri": "file://..." }`。
**推奨実装**: 該当 uri がアクティブエディタなら `vscode.commands.executeCommand('editor.action.triggerSuggest')` 等で補完を再要求すると、2回目の要求がキャッシュヒットして解析結果込みの補完が出る。無視しても機能はするが、アナライザ由来の候補が1テンポ遅れる。

### 1.3 リクエスト `lsparrot.php/status`(client → server)

ヘルスチェック/ステータスパネル用。レスポンス:

```jsonc
{
  "memory": { "current": 0, "peak": 0, "max": 0 },
  "symbolIndex": { "available": true, "indexing": false, "used": 0, "max": 0, "symbols": 0 },
  "processes": { "active": 0, "configured": 0, "phpstanRunning": false, "psalmRunning": false },
  "analyzers": { "phpstan": { "enabled": true, "running": false, "projects": { "/root": "ready" } }, ... },
  "runner": { ... },
  "performance": { "textDocument/completion": { "count": 1, "totalMs": 1.0, "maxMs": 1.0, "avgMs": 1.0 }, ... }
}
```

- **標準の `$/progress` は送られない**。初期インデックス構築中のスピナーは、`lsparrot.php/analyzerStatus`(analyzer=="index")の running/idle 遷移か、この status のポーリング(`symbolIndex.indexing`)で実装すること。

---

## 2. 起動オプション(`start_lsp()` に渡す配列)

サーバー設定はプロセス起動時に PHP スクリプト経由で固定される。拡張は VSCode 設定 → この配列への変換を担う。全キー:

```php
LSParrot\start_lsp([
    'analyzer' => 'auto',            // 'auto'|'lsparrot'|'phpstan'|'psalm'|'psalm-ls'|['phpstan','psalm-ls']
    'memoryLimit' => '-1',           // アナライザに渡す --memory-limit
    'phpstanLevel' => 6,
    'psalmLevel' => 6,
    'workerPhpArgs' => [],           // ワーカー PHP 起動引数の追加
    'symbolIndex' => ['size' => '64M'],
    'workers' => [
        'count' => 0,                // 0 = CPU数。初期インデックス並列化・アナライザ並列度に使用
        'analyzerDiagnosticsTimeout' => 60.0,
        'analyzerTypeQueryTimeout' => 5.0,
    ],
    'phpstan' => ['level' => 6],
    'psalm' => [
        'level' => 6,
        'transport' => 'auto',       // 'auto'|'cli'|'language-server'
        'onChange' => true,
        'enableAutocomplete' => true, 'enableDiagnostics' => true,
        'enableHover' => true, 'enableDefinition' => true, 'enableSignatureHelp' => true,
        'showInfo' => false, 'liveDeadCodeDiagnostics' => false, 'inMemory' => false,
        'onChangeDebounceMs' => 500, 'maxResponseWaitMs' => 200,
    ],
    'formatting' => [                // ★ 新規
        'enabled' => true,           // false で documentFormattingProvider 自体が false になる
        'reindent' => true,          // false なら末尾空白除去+最終改行のみ
        'indentStyle' => 'client',   // 'client'|'space'|'tab'(client = エディタの insertSpaces に従う)
        'indentSize' => 0,           // 0 = エディタの tabSize に従う。1-16 で強制
        'trimTrailingWhitespace' => true,
        'insertFinalNewline' => true,
    ],
]);
```

**拡張側 TODO**: 上記(特に `formatting.*`, `workers.count`, `analyzer`)を `contributes.configuration` に追加し、設定変更時の挙動を分ける:

- **ライブ反映可能**(`workspace/didChangeConfiguration` を送る。`settings.lsparrot.*` または `settings.*` 直下を読む):
  `formatting.*`, `phpstanLevel`, `psalmLevel`, `memoryLimit`, `analyzerDiagnosticsTimeout`, `analyzerTypeQueryTimeout`
- **サーバー再起動が必要**(プロセス構造に関わる): `analyzer`, `symbolIndex.size`, `workers.count`, `workerPhpArgs`, `psalm.transport` ほか psalm.* 全般
- 従来のプロジェクト単位ファイル `<project>/.lsparrot/vscode_config.json`(`phpstanLevel`/`psalmLevel`)も引き続き読まれる。didChangeConfiguration と併用時の優先順位に注意。

---

## 3. 標準 LSP の注意点(クライアント設定)

1. **同期モードは Full のみ**(`textDocumentSync.change = 1`)。`vscode-languageclient` はサーバー広告に従うので通常問題ないが、ミドルウェアで incremental を強制しないこと。range 付き didChange は**黙って破棄**される。
2. **didSave は `includeText: true`**。保存時に全文が送られる前提。
3. **`positionEncoding: "utf-16"`** を広告(LSP 3.17)。クライアント既定と同一なので特別な対応は不要。
4. **`$/cancelRequest` は数値/文字列 id とも有効**。タイピング中の stale な completion / workspace/symbol は積極的にキャンセルしてよい。
5. **ファイルウォッチャはサーバーから動的登録されない**(`client/registerCapability` 送信経路なし)。拡張側で `FileSystemWatcher` を静的に構成して `workspace/didChangeWatchedFiles` を送ること。推奨 glob:
   - `**/*.php`(インデックス増分更新・キャッシュ無効化)
   - `**/composer.json`, `**/composer.lock`, `vendor/composer/autoload_*.php`(オートロードマップ変更)
6. **マルチルート非対応**。`rootUri`(なければ `rootPath`)の1つ目のみ有効。`workspaceFolders` は読まれない。マルチルートワークスペースではフォルダ毎にサーバーインスタンスを起動する構成を推奨。
7. **診断は push のみ**(`textDocument/publishDiagnostics`)。didClose 時に空配列が来て Problems がクリアされる。pull 型(`textDocument/diagnostic`)は未実装。
8. 未知のメソッドには `-32601` が返る。shutdown 後のリクエストは `-32600`、`exit` で終了(正常時 exit code 0)。

## 4. サーバーが提供する機能一覧(capability 登録の確認用)

hover / definition / **declaration** / **typeDefinition** / references / documentHighlight / implementation / **foldingRange** / completion(resolve なし、trigger: `$ > : [ ( , SPACE @ - < { \`)/ signatureHelp(`(` `,`)/ documentSymbol(階層)/ workspaceSymbol(上限256件、プロジェクト優先)/ rename(prepare あり、型追跡でクラスファミリー限定)/ codeAction(quickfix, source.organizeImports)/ codeLens(resolve なし)/ inlayHint(パラメータ名)/ semanticTokens(full のみ、modifier なし)/ formatting・rangeFormatting(`formatting.enabled` 時)

**declaration / typeDefinition / foldingRange / didChangeConfiguration は今回追加**。古いサーバーとの互換が必要なら capability の有無で分岐すること。

## 5. 既知の制限・拡張側で吸収すべきギャップ

| 項目 | 状態 | 拡張側の推奨対応 |
|---|---|---|
| フォーマッタはインデント整形+空白整理(PSR-12 の空白/改行規則までは適用しない) | 仕様 | README に明記。フル整形が必要なユーザーには php-cs-fixer / phpcbf の併用を案内(`editor.defaultFormatter` の切替) |
| `$/progress` なし | 未実装 | `lsparrot.php/analyzerStatus` でスピナー実装(§1.1) |
| semanticTokens は full のみ(range/delta なし)、modifier 空 | 未実装 | クライアント側対応不要(vscode-languageclient が吸収)。大ファイルで気になる場合は `semanticTokens` を無効化する設定を用意してもよい |
| callHierarchy / typeHierarchy / selectionRange / documentLink / linkedEditingRange | 未実装 | capability に無いので VSCode は自動的に無効化する。対応不要 |
| completionItem/resolve なし(詳細は最初から inline) | 仕様 | 対応不要 |
| インレイの型ヒント(変数型)は未実装(パラメータ名のみ) | 未実装 | サーバー側の将来課題 |
| PHPStan/Psalm は didSave/didOpen 契機(didChange では走らない。psalm-ls の onChange を除く) | 仕様 | 「保存時に解析」であることを README に明記。`files.autoSave` との相性は良い |
| PHPStan 診断は行単位レンジ(列情報が JSON にない)。Psalm は列付き | 仕様 | 対応不要(表示上の差のみ) |

## 6. アナライザ挙動の変更点(拡張の表示・ドキュメントに影響)

- **auto モードでは psalm-language-server と Psalm CLI を同時に有効化しない**(両方入っている場合 psalm-ls 優先)。両方を意図的に使う場合は `analyzer` に配列で明示指定。
- 診断の severity: PHPStan → **Error(1)**、Psalm は `error`→1 / `warning`→2 / `info`→3。Psalm CLI 診断は `column_from/column_to` による**正確なレンジ**になった。
- 診断 `code` に PHPStan identifier / Psalm issue type が入る。`Deprecated*` / `*deprecated*` は `tags: [Deprecated]`、`Unused*` / `deadCode*` は `tags: [Unnecessary]` が付く(VSCode が取り消し線/淡色表示)。
- psalm-ls クラッシュ時は自動再起動(§1.1)。再起動後はサーバーが開いているドキュメントの didOpen を psalm-ls に再送するので拡張側の対応は不要。

## 7. 起動シーケンスの推奨

```
serverOptions = {
  command: phpPath,
  args: ['-n', '-d', 'extension=' + lsparrotSoPath, runnerScriptPath],
  options: { cwd: workspaceRoot }
}
```

- runnerScriptPath は §2 のオプション配列を埋め込んで拡張が生成する(現行方式の踏襲)。
- `.lsparrot/` ディレクトリ(インデックスキャッシュ `lsparrot-index.bin`、生成コンフィグ、並列インデックスの一時 part ファイル)がワークスペース直下に作られる。拡張の `.gitignore` 案内、および `files.watcherExclude` / `search.exclude` への `**/.lsparrot/**` 追加を推奨。
- 終了は LSP 準拠(shutdown → exit)。プロセス kill 前に必ず shutdown/exit を送ると、インデックスの永続化(`lsparrot-index.bin` 書き出し)が走り次回起動が高速化する。
