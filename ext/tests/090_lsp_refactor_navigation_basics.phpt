--TEST--
LSP supports references, document highlights, rename, and formatting basics
--EXTENSIONS--
lsparrot
--STDIN--
Content-Length: 106

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"rootUri":"file:///tmp/lsp-refactor-basics-test"}}Content-Length: 356

{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/lsp-refactor-basics-test/file.php","languageId":"php","version":1,"text":"<?php\nnamespace RefactorFixture;\n\nfinal class Sample\n{\n    public function run(): void\n    {\n        $value = 1;    \n        $value++;\n        $other = new Sample();\n    }\n}"}}}Content-Length: 185

{"jsonrpc":"2.0","id":2,"method":"textDocument/documentHighlight","params":{"textDocument":{"uri":"file:///tmp/lsp-refactor-basics-test/file.php"},"position":{"line":7,"character":10}}}Content-Length: 178

{"jsonrpc":"2.0","id":3,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///tmp/lsp-refactor-basics-test/file.php"},"position":{"line":7,"character":10}}}Content-Length: 181

{"jsonrpc":"2.0","id":4,"method":"textDocument/prepareRename","params":{"textDocument":{"uri":"file:///tmp/lsp-refactor-basics-test/file.php"},"position":{"line":7,"character":10}}}Content-Length: 194

{"jsonrpc":"2.0","id":5,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///tmp/lsp-refactor-basics-test/file.php"},"position":{"line":7,"character":10},"newName":"renamed"}}Content-Length: 185

{"jsonrpc":"2.0","id":6,"method":"textDocument/formatting","params":{"textDocument":{"uri":"file:///tmp/lsp-refactor-basics-test/file.php"},"options":{"tabSize":4,"insertSpaces":true}}}Content-Length: 56

{"jsonrpc":"2.0","id":7,"method":"shutdown","params":[]}
--FILE--
<?php
LSParrot\start_lsp(['analyzer' => 'lsparrot']);
?>
--EXPECTF--
%A"jsonrpc":"2.0","id":1,"result"%A"referencesProvider":true%A"documentHighlightProvider":true%A"implementationProvider":true%A"renameProvider":{"prepareProvider":true}%A"documentFormattingProvider":true%AContent-Length: %d

%A"textDocument/publishDiagnostics"%AContent-Length: %d

%A"jsonrpc":"2.0","id":2,"result":[%A"line":7,"character":8%A"line":8,"character":8%AContent-Length: %d

%A"jsonrpc":"2.0","id":3,"result":[%A"uri":"file:///tmp/lsp-refactor-basics-test/file.php"%A"line":7,"character":8%A"line":8,"character":8%AContent-Length: %d

%A"jsonrpc":"2.0","id":4,"result":{"start":{"line":7,"character":8},"end":{"line":7,"character":14}}%AContent-Length: %d

%A"jsonrpc":"2.0","id":5,"result"%A"$renamed"%A"$renamed"%AContent-Length: %d

%A"jsonrpc":"2.0","id":6,"result"%A$value = 1;\n        $value++;%AContent-Length: %d

{"jsonrpc":"2.0","id":7,"result":null}
