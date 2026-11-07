// One-off client: node tests/ask.mjs <cmd> [json-args]
// Example: node tests/ask.mjs teleport "{\"pos\":[0,0,0]}"
import { createConnection } from 'node:net';

const [cmd = 'ping', args = '{}'] = process.argv.slice(2);
const port = Number(process.env.KENSHI_BRIDGE_PORT || 47801);
const s = createConnection({ host: '127.0.0.1', port }, () => {
  s.write(JSON.stringify({ id: 1, cmd, args: JSON.parse(args) }) + '\n');
});
let buf = '';
s.on('data', (d) => {
  buf += d;
  const i = buf.indexOf('\n');
  if (i >= 0) {
    console.log(JSON.stringify(JSON.parse(buf.slice(0, i)), null, 2));
    s.end();
  }
});
s.on('error', (e) => {
  console.error('NO_CONNECTION', e.code);
  process.exitCode = 1;
});
s.setTimeout(5000, () => {
  console.error('TIMEOUT');
  s.destroy();
  process.exitCode = 1;
});
