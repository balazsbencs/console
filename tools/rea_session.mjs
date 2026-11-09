#!/usr/bin/env node
// Project-local REA MCP client. Supply REA_PACKAGE_ROOT, GHIDRA_INSTALL_DIR,
// JAVA_HOME and PATH in the launching shell; no global configuration changes.
// Each stdin line: {"name":"tool_name","arguments":{},"save":"file.json"}.
import {createRequire} from 'node:module';
import {mkdir, writeFile} from 'node:fs/promises';
import {resolve, dirname} from 'node:path';
import {pathToFileURL} from 'node:url';
import {createInterface} from 'node:readline';

const packageRoot = process.env.REA_PACKAGE_ROOT;
if (!packageRoot) throw new Error('Set REA_PACKAGE_ROOT to the installed rea-agents directory');
const require = createRequire(resolve(packageRoot, 'package.json'));
const {Client} = await import(pathToFileURL(require.resolve('@modelcontextprotocol/client')));
const {StdioClientTransport} = await import(pathToFileURL(require.resolve('@modelcontextprotocol/client/stdio')));
const transport = new StdioClientTransport({
  command: resolve(packageRoot, '../.bin/rea'), args: ['mcp'],
  env: {...process.env}, stderr: 'pipe',
});
transport.stderr?.on('data', chunk => process.stderr.write(chunk));
const client = new Client({name: 'rc20-research', version: '0.1.0'});
await client.connect(transport);
console.log('REA session ready');
try {
  for await (const line of createInterface({input: process.stdin, crlfDelay: Infinity})) {
    if (!line.trim()) continue;
    const request = JSON.parse(line);
    if (request.name === 'exit') break;
    try {
      const result = await client.callTool({name: request.name, arguments: request.arguments ?? {}}, {timeout: 900000});
      if (request.save) {
        await mkdir(dirname(request.save), {recursive: true});
        await writeFile(request.save, JSON.stringify(result, null, 2) + '\n');
      }
      const value = result.structuredContent ?? result.content?.filter(x => x.type === 'text').map(x => {
        try {return JSON.parse(x.text);} catch {return x.text;}
      });
      console.log(JSON.stringify({name: request.name, isError: result.isError ?? false,
        save: request.save, result: request.save && !result.isError ? 'Saved complete result' : value}));
    } catch (error) {
      console.error(JSON.stringify({name: request.name, error: String(error)}));
    }
  }
} finally {
  try {await client.callTool({name:'close_binary', arguments:{}}, {timeout:60000});}
  finally {
    await client.close();
    process.stdin.pause();
    process.stdin.unref?.();
  }
}
