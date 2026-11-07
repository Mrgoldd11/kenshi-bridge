// Protocol test against build/bridge-harness.exe (the plugin's real network code without the game).
// Run: scripts\test.cmd   (builds the harness, then `node --test tests/`)
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createConnection } from 'node:net';
import { fileURLToPath } from 'node:url';
import { join, dirname } from 'node:path';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const PORT = 47899;

function startHarness() {
  return new Promise((resolve, reject) => {
    const p = spawn(join(root, 'build', 'bridge-harness.exe'), [String(PORT)]);
    p.stdout.once('data', (d) => (String(d).startsWith('LISTENING') ? resolve(p) : reject(new Error(String(d)))));
    p.once('error', reject);
  });
}

function connect() {
  return new Promise((resolve, reject) => {
    const s = createConnection({ host: '127.0.0.1', port: PORT }, () => resolve(s));
    s.once('error', reject);
    let buf = '';
    const waiters = [];
    s.on('data', (d) => {
      buf += d;
      let i;
      while ((i = buf.indexOf('\n')) >= 0) {
        const line = buf.slice(0, i);
        buf = buf.slice(i + 1);
        waiters.shift()?.(line);
      }
    });
    s.next = () => new Promise((res) => waiters.push(res));
    s.ask = (obj) => {
      const p = s.next();
      s.write((typeof obj === 'string' ? obj : JSON.stringify(obj)) + '\n');
      return p.then((l) => JSON.parse(l));
    };
  });
}

test('protocol v1', async (t) => {
  const harness = await startHarness();
  t.after(() => harness.kill());
  const c = await connect();
  t.after(() => c.destroy());

  await t.test('ping answers with plugin info', async () => {
    const r = await c.ask({ id: 1, cmd: 'ping' });
    assert.equal(r.id, 1);
    assert.equal(r.ok, true);
    assert.equal(r.result.plugin, 'kenshi-bridge');
    assert.equal(r.result.protocol, 1);
    assert.equal(r.result.inGame, true);
  });

  await t.test('commands go through the main loop, id is echoed as is', async () => {
    const r = await c.ask({ id: 'a-1', cmd: 'echo', args: { text: 'Привет "мир"\n' } });
    assert.deepEqual(r, { id: 'a-1', ok: true, result: { text: 'Привет "мир"\n' } });
  });

  await t.test('two requests in one packet', async () => {
    const a = c.next();
    const b = c.next();
    c.write('{"id":2,"cmd":"echo","args":{"text":"x"}}\n{"id":3,"cmd":"ping"}\n');
    const [ra, rb] = (await Promise.all([a, b])).map((l) => JSON.parse(l));
    assert.deepEqual(new Set([ra.id, rb.id]), new Set([2, 3]));
  });

  await t.test('errors: bad json, missing cmd, unknown command', async () => {
    const bad = await c.ask('{not json');
    assert.equal(bad.ok, false);
    assert.equal(bad.error.code, 'bad_json');
    const noCmd = await c.ask({ id: 4 });
    assert.equal(noCmd.ok, false);
    assert.equal(noCmd.error.code, 'bad_request');
    const unk = await c.ask({ id: 5, cmd: 'nope' });
    assert.equal(unk.error.code, 'unknown_cmd');
  });

  await t.test('second client works at the same time', async () => {
    const c2 = await connect();
    const r = await c2.ask({ id: 9, cmd: 'echo', args: { text: 'two' } });
    assert.equal(r.result.text, 'two');
    c2.destroy();
  });

  await t.test('not_in_game when the main loop stops', async () => {
    await c.ask({ id: 6, cmd: 'leave_game' });
    await new Promise((r) => setTimeout(r, 3300));
    const ping = await c.ask({ id: 7, cmd: 'ping' });
    assert.equal(ping.result.inGame, false);
    const r = await c.ask({ id: 8, cmd: 'echo', args: { text: 'x' } });
    assert.equal(r.ok, false);
    assert.equal(r.error.code, 'not_in_game');
  });
});
